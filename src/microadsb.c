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

#include "microadsb.h"

int
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
ma_init(const char *devname)
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
ma_open(const char *devname)
{
	int fd;

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
