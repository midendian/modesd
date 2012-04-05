/*
 * Utilities for microADSB gizmo
 *
 * Adam Fritzler <mid@zigamorph.net>
 * Jordan Hayes <jmhayes@j-o-r-d-a-n.com>
 */

#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <termios.h>
#include <sys/time.h>

#include "microadsb.h"
#include "frame.h"

static int
ma_setbaud(int fd)
{
	struct termios tios;

	if (tcgetattr(fd, &tios) != 0)
		return -1;
	cfsetispeed(&tios, B115200);
	cfsetospeed(&tios, B115200);
	tios.c_cflag |= (CLOCAL | CREAD);
	tios.c_iflag &= ~INLCR;
	tios.c_iflag &= ~ICRNL;
	tios.c_oflag &= ~OPOST;
	if (tcsetattr(fd, TCSANOW, &tios) != 0)
		return -1;
	return 0;
}

int
ma_init(const char *devname, int bits)
{
	int fd;

	/*
	 * it's best to always reset the device so we can ensure it's in the
	 * right mode. even if it was just plugged into the current box, it
	 * could be connected through a USB hub, which might have kept it
	 * powered on, so we'd still get whatever state it was last in.
	 */
	fprintf(stderr, "resetting... ");
	if ((fd = open(devname, O_WRONLY | O_NOCTTY | O_NDELAY)) == -1) {
		fprintf(stderr, "\nunable to open %s for writing: %s\n", devname, strerror(errno));
		return -1;
	}
	if (fcntl(fd, F_SETFL, O_APPEND) == -1) {
		fprintf(stderr, "\nunable to restore blocking flag on %s\n", devname);
		close(fd);
		return -1;
	}
	if (ma_setbaud(fd) != 0) {
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
		if ((fd = open(devname, O_RDWR | O_NOCTTY | O_NDELAY)) != -1)
			break;
	}
	if (fd == -1) {
		fprintf(stderr, "\nfailed to re-open device after reset: %s\n", strerror(errno));
		return -1;
	}
	fprintf(stderr, "done\n");
	if (fcntl(fd, F_SETFL, 0) == -1) {
		fprintf(stderr, "\nunable to restore blocking flag on %s\n", devname);
		close(fd);
		return -1;
	}
	if (ma_setbaud(fd) != 0) {
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
	if ((strncmp(verstr, MADSB_KNOWNVERSION5, strlen(MADSB_KNOWNVERSION5)) != 0) &&
	    (strncmp(verstr, MADSB_KNOWNVERSION6, strlen(MADSB_KNOWNVERSION6)) != 0) &&
	    (strncmp(verstr, MADSB_KNOWNVERSION8, strlen(MADSB_KNOWNVERSION8)) != 0)) {
		fprintf(stderr, "unknown version string: %s\n", verstr);
		close(fd);
		return -1;
	}
	logmsg("device version: %s\n", verstr);

	/* set output mode */
	char modestr[128];
	snprintf(modestr, sizeof(modestr), "#%02X-%02X\n",
	    MADSB_CMD_SET_MODE, bits);
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
ma_open(const char *devname, int init)
{
	int fd;

	if (init) {
		return ma_init(devname,
				MADSB_MODE_ALL|
				MADSB_MODE_TIMECODE|
				MADSB_MODE_FRAMENUMBER);
	}

	if ((fd = open(devname, O_RDONLY | O_NOCTTY | O_NDELAY)) == -1) {
		fprintf(stderr, "unable to open %s for reading: %s\n", devname, strerror(errno));
		return -1;
	}
	if (ma_setbaud(fd) != 0) {
		if (errno == ENOTTY) {
			logmsg("WARNING: %s is not a terminal, skipping serial port config\n", devname);
		} else {
			fprintf(stderr, "unable to set baud rate on %s\n", devname);
			close(fd);
			return -1;
		}
	}
	if (fcntl(fd, F_SETFL, 0) == -1) {
		fprintf(stderr, "\nunable to restore blocking flag on %s\n", devname);
		close(fd);
		return -1;
	}

	return fd;
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

static unsigned long long
extractTC(const char *buf)
{
	char tcstr[12 + 1];
	memcpy(tcstr, buf, 12);
	tcstr[12] = '\0';
	unsigned long long tc = 0;
	sscanf(tcstr, "%llX", &tc);
	return tc;
}

static unsigned long
extractFC(const char *buf)
{
	char fcstr[8 + 1];
	memcpy(fcstr, buf, 8);
	fcstr[8] = '\0';
	unsigned long fc = 0;
	sscanf(fcstr, "%lX", &fc);
	return fc;
}

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
int
ma_read(int fd, struct frame *frame, int timeout)
{
	char buf[ES_LEN_TOTAL + 1];
	int len = SQ_LEN_TOTAL;
	int n;

	gettimeofday(&frame->rxstart, NULL);

	alarm(timeout);
	n = readn(fd, buf, len);
	if (n == -1) {
		logmsg("timeout\n");
		return -1;
	} else if (n < len) {
		logmsg("EOF\n");
		return -1;
	}
	gettimeofday(&frame->rxend, NULL);
	alarm(0);
	buf[len] = '\0';
	if (buf[0] != '@') {
		int n = resync(fd);
		if (n == -1)
			return -1;
		frame->skipped += n;
		return 0;
	}
	if (buf[SQ_LEN_TOTAL - 1] != '\r') {
		len = ES_LEN_TOTAL;
		if (readn(fd, buf + SQ_LEN_TOTAL, len - SQ_LEN_TOTAL) < (len - SQ_LEN_TOTAL))
			return -1;
		buf[len] = '\0';
		gettimeofday(&frame->rxend, NULL);
	}
	if ( (buf[len - 2] != '\n') || (buf[len - 1] != '\r') )
		return 0; /* it'll start resync when called again */
	buf[len - 2] = '\0'; len -= 2;

	frame->ticks = extractTC(buf + 1);
	frame->seqnum = extractFC(buf + len - FC_LEN - 1);
	memcpy(frame->data, buf + 1 + TC_LEN, len - TC_LEN - 1 - 2 - FC_LEN - 1);
	frame->data[len - TC_LEN - 1 - 2 - FC_LEN - 1] = '\0';
	/* XXX correct time vs PIC clock */
	return 1;
}


