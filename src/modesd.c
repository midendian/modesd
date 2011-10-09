/*
 * Skeletal program for running Mode-S/ADS-B receivers with the SPRUT firmware.
 *
 * Adam Fritzler <mid@zigamorph.net>
 */

#include "util.h"
#include "udp.h"
#include "microadsb.h"

#include <sys/time.h>
#include <signal.h>

/* serial devices are not quite entirely unlike files. */
static int
readn(int fd, char *buf, int i)
{
	int c = i;
	while (i > 0) {
		int n = read(fd, buf, i);
		if (n <= 0)
			return n;
		i -= n, buf += n;
	}
	return c;
}

/* read until just after the next ";\r\n" (ie, the next byte should be @) */
static int
resync(int fd)
{
	int i = 0;
	for (;;) {
		char c;
		if (readn(fd, &c, 1) != 1)
			return -1;
		i++;
		if (c != ';')
			continue;
		/* note that ';' also occurs between the squitter and the frame number! */

		char b[2];
		if (readn(fd, b, 2) != 2)
			return -1;
		i += 2;
		/* again, note the backwards terminator... */
		if ((b[0] == '\n') && (b[1] == '\r'))
			break;
	}
	return i;
}

static void
usage(const char *arg0)
{
	printf("\n");
	printf("%s [-I] [-v] -d /dev/device [-U host:port[:protocol]]\n", arg0);
	printf("\n");
	printf("\t-d /dev/device\t\tfilename of AVR-format-speaking Mode-S decoder (required)\n");
	printf("\t-I\t\t\tskip microADS-B init process\n");
	printf("\t-T secs\t\t\texit if no data for n seconds\n");
	printf("\t-U host:port[:protocol]\tSend UDP messages to host:port. Protocol may be:\n");
	printf("\t\t\t\t\t*XXXXXXXXXXXXXX;\traw (default)\n");
	printf("\t\t\t\t\tAV*XXXXXXXXXXXXXX;\tplaneplotter\n");
	printf("\t-v\t\t\tprint Mode-S messages to stdout\n");
	printf("\n");
	exit(2);
}

static int
parseudparg(const char *optarg)
{
	/* -u host:port[:variant] */
	char *hstr = NULL, *pstr = NULL, *vstr = NULL;
	udp_variant_t variant = UDP_RAW;
	int port = 0;
	int err = 0;

	if (!optarg ||
	    (strlen(optarg) <= 0))
		return -1;

	if (!(hstr = strdup(optarg)) ||
	    !(pstr = index(hstr, ':'))) {
		err = -1; goto out;
	}
	*pstr = '\0'; pstr++;
	if (index(pstr, ':')) {
		vstr = index(pstr, ':');
		*vstr = '\0'; vstr++;
	}
	if ((strlen(hstr) <= 0) ||
	    (strlen(pstr) <= 0) ||
	    ((port = atoi(pstr)) <= 0)) {
		err = -1; goto out;
	}
	if (vstr) {
		if (strcmp(vstr, "raw") == 0)
			variant = UDP_RAW;
		else if (strcmp(vstr, "planeplotter") == 0)
			variant = UDP_PLANEPLOTTER;
		else {
			fprintf(stderr, "invalid protocol '%s' for %s:%d\n", vstr, hstr, port);
			err = -1; goto out;
		}
	}
	if (udp_addport(hstr, (unsigned short)port, variant) == -1) {
		fprintf(stderr, "failed to add UDP output port for %s:%d\n", hstr, port);
		err = -1; goto out;
	}
out:
	if (hstr) free(hstr);
	return err;
}

int
main(int argc, char *argv[])
{
	char *devname = NULL;
	int init = 1; // default to re-initializing the device
	int verbose = 0;
	int readto = 2; /* seconds */

	int c;
	opterr = 0;
	while ((c = getopt(argc, argv, "Id:T:U:v")) != -1) {
		switch (c) {
			case 'I': init = 0; break;
			case 'd': devname = optarg; break;
			case 'U':
				if (parseudparg(optarg) == -1) {
					usage(argv[0]);
					exit(2);
				}
				break;
			case 'T':
				readto = atoi(optarg);
				if (readto <= 0) {
					fprintf(stderr, "invalid value for read timeout (%d)\n", readto);
					exit(2);
				}
				break;
			case 'v': verbose++; break;
			case '?':
				if (optopt == 'd')
					fprintf(stderr, "-d requires argument\n");
				else if (isprint(optopt))
					fprintf(stderr, "unknown option -%c\n", optopt);
				usage(argv[0]); exit(2);
		}
	}
	if (!devname)
		usage(argv[0]);

	logmsg("using device on %s\n", devname);
	int devfd;
	if (init)
		devfd = ma_init(devname);
	else
		devfd = ma_open(devname);
	if (devfd == -1)
		exit(2);

	/* used only for generating EINTR */
	signal(SIGALRM, SIG_IGN);

#define TC_LEN 12
#define SQ_LEN 14
#define ES_LEN 28
#define FC_LEN 8
#define SQ_LEN_TOTAL (1 + TC_LEN + SQ_LEN + 2 + FC_LEN + 3)
#define ES_LEN_TOTAL (1 + TC_LEN + ES_LEN + 2 + FC_LEN + 3)
	/*
	 * @00001BA972C05DA7717DBBB591;#000000EC;\n\r
	 * @00001BB20C208D896114583BC127EC054587426E;#000000ED;\n\r
	 * @<48b><56b/112b>;#<64b>;\n\r aka @<12c><14c/28c>;#<8c>;\n\r (40/54bytes)
	 *
	 * Note that the line terminator is \n\r (0x0a, 0x0d), not the rather
	 * more common \r\n!
	 */

	long nFrames = 0;
	long nSkipped = 0;
	time_t nTime = time(NULL);

	/* XXX exit loop if no valid data received in N seconds (should use a timer--many places can block) */
	logmsg("starting...\n");
	for (;;) {
		char buf[ES_LEN_TOTAL + 1];
		int len = SQ_LEN_TOTAL;
		int n;
		struct timeval tv_start;
		gettimeofday(&tv_start, NULL);
		alarm(readto);
		n = readn(devfd, buf, len);
		if (n == -1) {
			logmsg("alarm fired, exiting\n");
			break;
		} else if (n < len) {
			logmsg("EOF, exiting\n");
			break;
		}
		alarm(0);
		buf[len] = '\0';
		if (buf[0] != '@') {
			int n = resync(devfd);
			if (n == -1)
				break;
			nSkipped += n;
			continue;
		}
		if (buf[SQ_LEN_TOTAL - 1] != '\r') {
			len = ES_LEN_TOTAL;
			if (readn(devfd, buf + SQ_LEN_TOTAL, len - SQ_LEN_TOTAL) < (len - SQ_LEN_TOTAL))
				break;
			buf[len] = '\0';
		}
		if ( (buf[len - 2] != '\n') || (buf[len - 1] != '\r') )
			continue; /* it'll start resync again at the top */
		buf[len - 2] = '\0'; len -= 2;
		unsigned long long tc = extractTC(buf + 1);
		unsigned long fc = extractFC(buf + len - FC_LEN - 1);
		char es[ES_LEN + 1];
		memcpy(es, buf + 1 + TC_LEN, len - TC_LEN - 1 - 2 - FC_LEN - 1);
		es[len - TC_LEN - 1 - 2 - FC_LEN - 1] = '\0';
		/* XXX correct time vs PIC clock */
		if (verbose)
			printf("%ld.%06ld *%s;\n", tv_start.tv_sec, (long)tv_start.tv_usec, es);
		/* XXX support ASTERIX here as well? */
		if (udp_send(es) < 0)
			logmsg("failed to send message to one or more UDP hosts\n");
		nFrames++;

		if ((time(NULL) - nTime) > 2) {
			logmsg("%g frames/sec, %g skipped bytes/sec\n", nFrames / (double)(time(NULL) - nTime), nSkipped / (double)(time(NULL) - nTime));
			nFrames = 0; nSkipped = 0; nTime = time(NULL);
			fflush(stdout);
		}
	}
	logmsg("ending...\n");

	udp_clearports();
	return 0;
}
