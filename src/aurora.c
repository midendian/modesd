
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <string.h>

#include "util.h"
#include "aurora.h"
#include "frame.h"

static int
aurora_setbaud(int fd)
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

#define ADXP_HEARTBEAT "$!MSRAHB"
#define ADXP_MODERAW "$!MSRARAW,*00"

static int
aurora_init(const char *devname)
{
	int fd;
	int flags;

	logmsg("resetting device... \n");
	if ((fd = open(devname, O_RDWR | O_NOCTTY | O_NDELAY)) == -1) {
		logmsg("unable to open %s for writing: %s\n", devname, strerror(errno));
		return -1;
	}
	if (fcntl(fd, F_SETFL, O_APPEND) == -1) {
		logmsg("unable to restore blocking flag on %s\n", devname);
		close(fd);
		return -1;
	}
	if (aurora_setbaud(fd) != 0) {
		logmsg("unable to set baud rate on %s\n", devname);
		close(fd);
		return -1;
	}

	/*
	 * device is reset by raising DTR for at least 50ms. it will always
	 * come up in NMEA mode.
	 */
	flags = TIOCM_DTR;
	if (ioctl(fd, TIOCMBIS, &flags) != 0) {
		logmsg("failed to set DTR: %s\n", strerror(errno));
		close(fd);
		return -1;
	}
	sleep(1);
	flags = TIOCM_DTR;
	if (ioctl(fd, TIOCMBIC, &flags) != 0) {
		logmsg("failed to clear DTR: %s\n", strerror(errno));
		close(fd);
		return -1;
	}

	/* should send its first heartbeat within 15 sec */
	for (;;) {
		char buf[255];
		if (readln(fd, buf, sizeof(buf)) < 0) {
			logmsg("read error waiting for first heartbeat: %s\n", strerror(errno));
			return -1;
		}
		if (strlen(buf) == 0) continue;

		if (strncmp(buf, ADXP_HEARTBEAT, strlen(ADXP_HEARTBEAT)) == 0)
			break;
		if (buf[0] == '#') { /* device ID, version, etc */
			logmsg("device info: %s\n", buf);
			continue;
		}

		logmsg("unrecognized line after reset: '%s'\n", buf);
	}

	if (write(fd, ADXP_MODERAW, strlen(ADXP_MODERAW)) < strlen(ADXP_MODERAW)) {
		logmsg("failed to write entire RAW mode string\n");
		close(fd);
		return -1;
	}

	/* there will still be some ADXP messages in the queue, but aurora_read
	 * should suck them up.
	 */

	return fd;
}

int
aurora_open(const char *devname, int init)
{
	int fd;

	if (init)
		return aurora_init(devname);

	if ((fd = open(devname, O_RDONLY | O_NOCTTY | O_NDELAY)) == -1) {
		logmsg("unable to open %s for reading: %s\n", devname, strerror(errno));
		return -1;
	}
	if (aurora_setbaud(fd) != 0) {
		if (errno == ENOTTY) {
			logmsg("WARNING: %s is not a terminal, skipping serial port config\n", devname);
		} else {
			logmsg("unable to set baud rate on %s\n", devname);
			close(fd);
			return -1;
		}
	}
	if (fcntl(fd, F_SETFL, 0) == -1) {
		logmsg("unable to restore blocking flag on %s\n", devname);
		close(fd);
		return -1;
	}

	return fd;
}

/* assumes dest is 28+1 bytes */
static int
sprintmodes(char *dest, const unsigned char *src)
{
	int i, j, len;
	unsigned char df;

	/* apparently the aurora pads standard 1090 with random bytes, so only
	 * copy all of them if it's obviously 1090ES
	 */
	df = (src[0] >> 3) & 0x1f;
	len = (df >= 16) ? 14 : 7;

	for (i = 0, j = 0; i < len; i++)
		j += sprintf(dest + j, "%02X", src[i]);
	return 0;
}

#define DLE 0x10
#define STX 0x02
#define ETX 0x03

#define AURORAW_FRAMETYPE_STANDARD 0x02
#define AURORAW_FRAMESUBTYPE_MODES 0x00
static int
parseframe(const unsigned char *buf, int buflen, struct frame *frame)
{
	unsigned char subtype;
	unsigned char timestatus;

	int i;

	i = 0;
	if (buf[i] != 0x00)
		logmsg("reserved byte 0 is unexpected value 0x%02x\n", buf[i]);
	i++;
	if (buf[i] != AURORAW_FRAMETYPE_STANDARD) {
		logmsg("unknown frame type 0x%02x\n", buf[i]);
		return 0;
	}
	i++;

	subtype = buf[i++];
	if (subtype != AURORAW_FRAMESUBTYPE_MODES) {
		logmsg("unsupported frame subtype 0x%02x\n", subtype);
		return 0;
	}
	if (buflen != 36) {
		logmsg("invalid length for ModeS frame (%d)\n", buflen);
		return 0;
	}

	/* values don't make sense */
	timestatus = buf[i++];

	/* unknown */
	i += 2;

	/* time code (64bits, in ns?) XXX */
	i += 8;

	/* unknown */
	i += 7;

	/* Mode-S data (padded with zeros?) */
	sprintmodes(frame->data, buf + i);
	i += 14;

	/* unknown */
	i += 2;

	return 1;
}

/*
 * Aurora RAW mode frames are enclosed in STX/ETX escaped with DLE, frame
 * length variable.
 */
int
aurora_read(int fd, struct frame *frame, int timeout)
{
	unsigned char buf[255];
	int n;
	int state;
	int ret;

	gettimeofday(&frame->rxstart, NULL);

#define OUTOFFRAME 0
#define INDLE 1
#define INFRAME 2
#define INDLEINFRAME 3
#define ENDOFFRAME 4
	for (state = OUTOFFRAME, n = 0; n < sizeof(buf); n++) {
		if (readn(fd, buf + n, 1) < 1)
			return -1;
		if (state == OUTOFFRAME) {
			if (buf[n] != DLE) {
				frame->skipped += n;
				gettimeofday(&frame->rxend, NULL);
				return 0;
			}
			state = INDLE;

		} else if (state == INDLE) {
			if (buf[n] != STX) {
				frame->skipped += n;
				gettimeofday(&frame->rxend, NULL);
				return 0;
			}
			state = INFRAME;

		} else if (state == INFRAME) {
			if (buf[n] == DLE) {
				state = INDLEINFRAME;
				continue;
			}
			/* normal case. */

		} else if (state == INDLEINFRAME) {
			/* only assuming that DLE is strictly stuffed, docs
			 * aren't clear
			 */
			if (buf[n] == DLE) {
				state = INFRAME;
				n--; /* remove a DLE */

			} else if (buf[n] == ETX) {
				state = ENDOFFRAME;
				break;
			} else {
				logmsg("unknown DLE-escaped sequenece 0x%02x\n", buf[n]);
				frame->skipped += n;
				gettimeofday(&frame->rxend, NULL);
				return 0;
			}
		}
	}
	gettimeofday(&frame->rxend, NULL);
	if (state != ENDOFFRAME) {
		logmsg("frame exceeded expected length (%d)", n);
		frame->skipped += n;
		return 0;
	}

	/* pass along without <DLE><STX><DLE><ETX> */
	ret = parseframe(buf + 2, n - 4, frame);
	if (ret < 0)
		return -1;
	else if (ret == 0)
		return 0; /* no error, but no Mode-S frame either */
	return 1;
}


