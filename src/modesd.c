/*
 * Skeletal program for running Mode-S/ADS-B receivers with the SPRUT firmware.
 *
 * Adam Fritzler <mid@zigamorph.net>
 */

#include <sys/time.h>
#include <signal.h>

#include "util.h"
#include "udp.h"
#include "frame.h"

#include "microadsb.h"
#include "aurora.h"

static void
usage(const char *arg0)
{
	printf("\n");
	printf("%s [-I] [-v] -d /dev/device -t type [-U host:port[:protocol]]\n", arg0);
	printf("\n");
	printf("\t-d /dev/device\t\tfilename of AVR-format-speaking Mode-S decoder (required)\n");
	printf("\t-I\t\t\tassume device already in correct mode (TC+FC for microADS-B, RAW mode for Aurora)\n");
	printf("\t-t type\t\t\tdevice type (know: microadsb, aurora)\n");
	printf("\t-T secs\t\t\texit if no data for n seconds\n");
	printf("\t-U host:port[:protocol]\tSend UDP messages to host:port. Protocol may be:\n");
	printf("\t\t\t\t\t*XXXXXXXXXXXXXX;\traw (default)\n");
	printf("\t\t\t\t\tAV*XXXXXXXXXXXXXX;\tplaneplotter\n");
	printf("\t-v\t\t\tprint Mode-S messages to stdout\n");
	printf("\n");
	exit(2);
}

struct devtype {
	const char name[32];
	int (*open)(const char *devname, int init);
	int (*read)(int fd, struct frame *frame, int timeout);
};
static const struct devtype devtypes[] = {
	{ "microadsb", ma_open, ma_read },
	{ "aurora", aurora_open, aurora_read },
};
#define DEVTYPESLEN (sizeof(devtypes) / sizeof(devtype))

int
main(int argc, char *argv[])
{
	setappname(argv[0]);
	char *devname = NULL;
	const struct devtype *devtype = NULL;
	int init = 1; // default to re-initializing the device
	int verbose = 0;
	int readto = 2; /* seconds */

	int c;
	opterr = 0;
	while ((c = getopt(argc, argv, "Id:t:T:U:v")) != -1) {
		switch (c) {
			case 'I': init = 0; break;
			case 'd': devname = optarg; break;
			case 'U':
				if (udp_parsearg(optarg) == -1) {
					usage(argv[0]);
					exit(2);
				}
				break;
			case 't':
				devtype = NULL;
				{
					int i;
					for (i = 0; i < DEVTYPESLEN; i++) {
					if (strcmp(optarg, devtypes[i].name) == 0)
						devtype = &devtypes[i];
					}
				}
				if (devtype == NULL) {
					fprintf(stderr, "unknown device type '%s'\n", devtype);
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
	if (!devname || !devtype)
		usage(argv[0]);

	logmsg("using device on %s, type %s\n", devname, devtype->name);
	int devfd = devtype->open(devname, init);
	if (devfd == -1)
		exit(2);

	/* used only for generating EINTR */
	signal(SIGALRM, SIG_IGN);

	long nFrames = 0;
	long nSkipped = 0;
	time_t nTime = time(NULL);

	/* XXX exit loop if no valid data received in N seconds (should use a timer--many places can block) */
	logmsg("starting...\n");
	for (;;) {
		struct frame f;
		int ret;

		memset(&f, '\0', sizeof(struct frame));
		ret = devtype->read(devfd, &f, readto);
		if (ret == -1) {
			logmsg("device error, exiting\n");
			break;
		}
		if (ret == 0) {
			nSkipped += f.skipped;
			continue;
		}

		if (verbose)
			printf("%ld.%06ld *%s;\n", f.rxstart.tv_sec, (long)f.rxstart.tv_usec, f.data);
		/* XXX support ASTERIX here as well? */
		if (udp_send(f.data) < 0)
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
