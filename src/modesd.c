/*
 * Skeletal program for running Mode-S/ADS-B receivers with the SPRUT firmware.
 *
 * Adam Fritzler <mid@zigamorph.net>
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <termios.h>
#include <sys/time.h>
#include <time.h>
#include <stdarg.h>

#define APPNAME "mode-s-avr"

/* for the microADS-B v1 device with SPRUT firmware 6 */
#define MADSB_KNOWNVERSION "#00-00-06-04"
/* see user.[ch] in SPRUT firmware source for commands info */
#define MADSB_CMD_READ_VERSION   0x00 /* no args */
#define MADSB_CMD_SET_MODE       0x43 /* takes one byte, see MADSB_MODE_* */
#define MADSB_CMD_RESET          0xff /* no args */

#define MADSB_MODE_ALL           0x02 /* send all demodulated squitters, in *...; format */
#define MADSB_MODE_ADSB          0x03 /* send only ADS-B (DF=17/18/19) squitters, in *...; format */
#define MADSB_MODE_ADSB_CRC      0x04 /* send only ADS-B squitters with CRC */
#define MADSB_MODE_TIMECODE      0x10
#define MADSB_MODE_FRAMENUMBER   0x20

int
readln(int fd, char *buf, int buflen)
{
	int i = 0;

	while (i < (buflen -  1)) {
		if (read(fd, buf, 1) != 1)
			return -1;
		if (*buf == '\r') continue;
		if (*buf == '\n') break;
		buf++, i++;
	}
	*buf = '\0';

	return i;
}

int
setbaud(int fd)
{
	struct termios tios;

	if (tcgetattr(fd, &tios) != 0)
		return -1;
	cfsetispeed(&tios, B115200);
	cfsetospeed(&tios, B115200);
	tios.c_cflag |= (CLOCAL | CREAD);
	if (tcsetattr(fd, TCSANOW, &tios) != 0)
		return -1;
	return 0;
}

int
openmicro_init(const char *devname)
{
	int fd;

	/*
	 * it's best to always reset the device so we can ensure it's in the
	 * right mode. even if it was just plugged into the current box, it
	 * could be connected through a USB hub, which might have kept it
	 * powered on, so we'd still get whatever state it was last in.
	 */
	fprintf(stderr, "resetting... ");
	if ((fd = open(devname, O_WRONLY)) == -1) {
		fprintf(stderr, "\nunable to open %s for writing: %s\n", devname, strerror(errno));
		return -1;
	}
	if (setbaud(fd) != 0) {
		fprintf(stderr, "\nunable to set baud rate on %s\n", devname);
		close(fd);
		return -1;
	}
	if (write(fd, "#FF\n", 4) != 4) {
		fprintf(stderr, "\nunable to write reset string to %s: %s\n", devname, strerror(errno));
		close(fd);
		return -1;
	}
	close(fd);

	/*
	 * the reset will drop it off the bus for a bit; wait up to 10s for it
	 * to come back, though something is screwy if it takes more than
	 * three...
	 */
	int i;
	for (i = 0; i < 10; i++) {
		sleep(1);
		fprintf(stderr, "%d ", i);
		if ((fd = open(devname, O_RDWR)) != -1)
			break;
	}
	if (fd == -1) {
		fprintf(stderr, "\nfailed to re-open device after reset: %s\n", strerror(errno));
		return -1;
	}
	fprintf(stderr, "done\n");
	if (setbaud(fd) != 0) {
		fprintf(stderr, "unable to set baud rate on %s\n", devname);
		close(fd);
		return -1;
	}

	/* fetch the version string, just to make sure we haven't gone off the deep end. */
	char verstr[128];
	snprintf(verstr, sizeof(verstr), "#%02X\n", MADSB_CMD_READ_VERSION);
	if (write(fd, verstr, strlen(verstr)) != strlen(verstr)) {
		fprintf(stderr, "failed to write READ_VERSION command to device: %s\n", strerror(errno));
		close(fd);
		return -1;
	}
	if (readln(fd, verstr, sizeof(verstr)) == -1) {
		fprintf(stderr, "failed to read version string from device\n");
		close(fd);
		return -1;
	}
	if (strncmp(verstr, MADSB_KNOWNVERSION, strlen(MADSB_KNOWNVERSION)) != 0) {
		fprintf(stderr, "unknown version string: %s\n", verstr);
		close(fd);
		return -1;
	}

	/* set output mode */
	char modestr[128];
	snprintf(modestr, sizeof(modestr), "#%02X-%02X\n",
			MADSB_CMD_SET_MODE,
			MADSB_MODE_ALL | MADSB_MODE_TIMECODE | MADSB_MODE_FRAMENUMBER);
	if (write(fd, modestr, strlen(modestr)) != strlen(modestr)) {
		fprintf(stderr, "failed to write SET_MODE command to device: %s\n", strerror(errno));
		close(fd);
		return -1;
	}
	if (readln(fd, modestr, sizeof(modestr)) == -1) {
		fprintf(stderr, "failed to read mode string from device\n");
		close(fd);
		return -1;
	}
	if (strncmp(modestr, "#43", strlen("#43")) != 0) {
		fprintf(stderr, "invalid response to SET_MODE string: %s\n", modestr);
		close(fd);
		return -1;
	}

	return fd;
}

int
openmicro(const char *devname)
{
	int fd;

	if ((fd = open(devname, O_RDONLY)) == -1) {
		fprintf(stderr, "unable to open %s for reading: %s\n", devname, strerror(errno));
		return -1;
	}
	if (setbaud(fd) != 0) {
		fprintf(stderr, "unable to set baud rate on %s\n", devname);
		close(fd);
		return -1;
	}

	return fd;
}

/* serial devices are not quite entirely unlike files. */
int
readn(int fd, unsigned char *buf, int i)
{
	int c = i;
	while (i > 0) {
		int n = read(fd, buf, i);
		if (n < 0)
			return -1;
		i -= n, buf += n;
	}
	return c;
}

/* read until just after the next ";\r\n" (ie, the next byte should be @) */
int
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

unsigned long long
extractTC(const char *buf)
{
	char tcstr[12 + 1];
	memcpy(tcstr, buf, 12);
	tcstr[12] = '\0';
	unsigned long long tc = 0;
	sscanf(tcstr, "%llX", &tc);
	return tc;
}

unsigned long
extractFC(const char *buf)
{
	char fcstr[8 + 1];
	memcpy(fcstr, buf, 8);
	fcstr[8] = '\0';
	unsigned long fc = 0;
	sscanf(fcstr, "%lX", &fc);
	return fc;
}

void
usage(const char *arg0)
{
	printf("\n");
	printf("%s [-I] -d /dev/device\n", arg0);
	printf("\n");
	printf("\t-d /dev/device\tfilename of AVR-format-speaking Mode-S decoder (required)\n");
	printf("\t-I\t\tskip microADS-B init process\n");
	printf("\n");
	exit(2);
}

void
logmsg(const char *format, ...)
{
	time_t nowT = time(NULL);
	struct tm *now = localtime(&nowT);
	char buf[128];
	strftime(buf, sizeof(buf), "%F %T %z", now);
	fprintf(stderr, "%s %s: ", buf, APPNAME);

	va_list ap;
	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);
	return;
}

int
main(int argc, char *argv[])
{
	char *devname = NULL;
	int init = 1; // default to re-initializing the device

	int c;
	opterr = 0;
	while ((c = getopt(argc, argv, "Id:")) != -1) {
		switch (c) {
			case 'I': init = 0; break;
			case 'd': devname = optarg; break;
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

	fprintf(stderr, "using device on %s\n", devname);
	int devfd;
	if (init)
		devfd = openmicro_init(devname);
	else
		devfd = openmicro(devname);
	if (devfd == -1)
		exit(2);

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
		struct timeval tv_start;
		gettimeofday(&tv_start, NULL);
		if (readn(devfd, buf, len) < len)
			break;
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
		/* XXX use planeplotter UDP protocol, ASTERIX, etc */
		printf("%ld.%06ld *%s;\n", tv_start.tv_sec, (long)tv_start.tv_usec, es);
		nFrames++;

		if ((time(NULL) - nTime) > 30) {
			logmsg("%g frames/sec, %g skipped bytes/sec\n", nFrames / (double)(time(NULL) - nTime), nSkipped / (double)(time(NULL) - nTime));
			nFrames = 0; nSkipped = 0; nTime = time(NULL);
			fflush(stdout);
		}
	}
	logmsg("ending...\n");

	return 0;
}


