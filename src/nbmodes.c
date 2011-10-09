#include <X11/Intrinsic.h>

#include "microadsb.h"
#include "util.h"

#include <fcntl.h>
#include <errno.h>

typedef struct {
	XtAppContext	app;
	char		buf[BUFSIZ];
	int		offset;
	XtInputId	rio;
	int		retry;
	XtIntervalId	timer;
	int		count[2];
	int		size[2];

	/* XXX config */
	int		fd;
	char*		device;
	int		modeBits;
} _NBModeS, *NBModeS;

void _HandleRead(XtPointer baton, int* source, XtInputId* id);

void _TryReconnect(XtPointer baton, XtIntervalId* id) {
	NBModeS	nbm = (NBModeS)baton;

	logmsg("Reconnecting ...\n");
	if (-1 == (nbm->fd = ma_init(nbm->device, nbm->modeBits))) {
		logmsg("[%d] open(%s): %s\n", nbm->retry++,
		    nbm->device, strerror(errno));
		if (10 < nbm->retry++) {
			logmsg("... giving up\n");
			exit(0);
		}
		nbm->timer = XtAppAddTimeOut(nbm->app,
		    1000, _TryReconnect, baton);
		return;
	}
	logmsg("Reconnected!\n");
	if (-1 == fcntl(nbm->fd, F_SETFL, O_NONBLOCK))
		logmsg("fcntl(%s,O_NONBLOCK): %s\n",
		    nbm->device, strerror(errno));
	nbm->rio = XtAppAddInput(nbm->app, nbm->fd,
	    (XtPointer)XtInputReadMask, _HandleRead, (XtPointer)nbm);
}

static void _BadPacket(NBModeS nbm, char* why) {
	/* XXX maybe recover last N? */
	/* pretty print */
	int i;
	for (i = 0; i < nbm->offset; i++)
		switch (nbm->buf[i]) {
		case '\r': nbm->buf[i] = '.'; break;
		case '\n': nbm->buf[i] = ','; break;
		case '\0': nbm->buf[i] = '!'; break;
		default:
			if (! isprint(nbm->buf[i]))
				nbm->buf[i] = '?';
			break;
		}
	logmsg("%02d> '%.*s' [%d:%d, %d:%d, why=%s]\n",
	  nbm->offset, nbm->offset, nbm->buf,
	  nbm->size[0], nbm->count[0],
	  nbm->size[1], nbm->count[1], why);
	/* reset the counters */
	nbm->count[0] = nbm->count[1] = 0;
}

static void _MaybeSendIt(NBModeS nbm) {
	/* sanity */
	if ('\n' != nbm->buf[nbm->offset - 1]
	    || '\r' != nbm->buf[0]) {
		_BadPacket(nbm, "CRLF botch");
		return;
	}

	int fc = 2;			/* first char */
	int lc = nbm->offset - 2;	/* last char */
	if (nbm->modeBits & MADSB_MODE_FRAMENUMBER)
		lc -= 10;
	if (nbm->modeBits & MADSB_MODE_TIMECODE) {
		if ('@' != nbm->buf[1]) {
			_BadPacket(nbm, "@ botch");
			return;
		}
		fc += 12;
	} else if ('*' != nbm->buf[1]) {
		_BadPacket(nbm, "* botch");
		return;
	}
	if (';' != nbm->buf[lc]) {
		_BadPacket(nbm, "; botch");
		return;
	}

	/* last chance */
	int i;
	for (i = fc; i < lc; i++)
		if (! isxdigit(nbm->buf[i])) {
			_BadPacket(nbm, "hex botch");
			return;
		}

	/* clear to leave the ship */
	nbm->buf[lc] = '\0';
	udp_send(nbm->buf + fc);
	nbm->buf[lc] = ';';
}

void _HandleRead(XtPointer baton, int* source, XtInputId* id) {
	static char _func[] = "_HandleRead";
	int i;

	NBModeS nbm = (NBModeS)baton;
	int cc = read(nbm->fd, nbm->buf + nbm->offset,
	    sizeof(nbm->buf) - nbm->offset);
	switch (cc) {
	case -1:
		logmsg("%s:read(%s): %s\n",
		    _func, nbm->device, strerror(errno));
		close(nbm->fd);
		XtRemoveInput(nbm->rio);
		/* XXX
		 * See https://bugs.freedesktop.org/show_bug.cgi?id=34715
		 * */
#define XT_STILL_BUGGY
#ifdef	XT_STILL_BUGGY
		exit(0);
#else
		XtAppSetExitFlag(nbm->app);
#endif
	case 0:
		logmsg("read: 0 ... reopening\n");
		close(nbm->fd);
		XtRemoveInput(nbm->rio);
		nbm->retry = 1;
		nbm->timer = XtAppAddTimeOut(nbm->app,
		    1000, _TryReconnect, baton);
		break;
	default:
		/* XXX parse, etc. */
		nbm->offset += cc;

		if (nbm->size[0] == cc) {
			nbm->count[0]++;
			_MaybeSendIt(nbm);
		} else if (nbm->size[1] == cc) {
			nbm->count[1]++;
			_MaybeSendIt(nbm);
		} else
			_BadPacket(nbm, "wrong size");

		nbm->offset = 0;
		break;
	}
}

int main(int argc, char** argv) {
	static _NBModeS nbm;

	setappname(argv[0]);

	if (2 == argc && udp_parsearg(argv[1]) == -1)
		return -1;

	/* event anchor */
	nbm.app = XtCreateApplicationContext();

	/* XXX */
	nbm.device = "/dev/ttyACM0";
	nbm.modeBits = (
		MADSB_MODE_ALL
		|MADSB_MODE_TIMECODE
		|MADSB_MODE_FRAMENUMBER
		);

	/* after possibly changing modeBits */
	nbm.size[0] = 18;	/* 14 + 2 + 2 */
	nbm.size[1] = 32;	/* 28 + 2 + 2 */
	if (nbm.modeBits & MADSB_MODE_TIMECODE) {
		nbm.size[0] += 12;
		nbm.size[1] += 12;
	}
	if (nbm.modeBits & MADSB_MODE_FRAMENUMBER) {
		nbm.size[0] += 10;
		nbm.size[1] += 10;
	}

	nbm.fd = ma_init(nbm.device, nbm.modeBits);
	if (-1 == nbm.fd)
		return -1;
	if (-1 == fcntl(nbm.fd, F_SETFL, O_NONBLOCK)) {
		logmsg("fcntl(%s,O_NONBLOCK): %s\n",
		    nbm.device, strerror(errno));
		return -1;
	}
	nbm.rio = XtAppAddInput(nbm.app, nbm.fd, (XtPointer)XtInputReadMask,
	    _HandleRead, (XtPointer)&nbm);

	XtAppMainLoop(nbm.app);

	return 0;
}
