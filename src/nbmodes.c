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

	/* XXX config */
	int		fd;
	char*		device;
} _NBModeS, *NBModeS;

void _HandleRead(XtPointer baton, int* source, XtInputId* id);

void _TryReconnect(XtPointer baton, XtIntervalId* id) {
	NBModeS	nbm = (NBModeS)baton;

	logmsg("Reconnecting ...\n");
	if (-1 != nbm->fd) {
		close(nbm->fd);
		XtRemoveInput(nbm->rio);
		nbm->retry = 1;
	}
	if (-1 == (nbm->fd = ma_open(nbm->device))) {
		logmsg("[%d] open(%s): %s\n", nbm->retry++,
		    nbm->device, strerror(errno));
		if (11 == nbm->retry) {
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
		nbm->timer = XtAppAddTimeOut(nbm->app,
		    1000, _TryReconnect, baton);
		break;
	default:
		/* XXX parse, etc. */
		nbm->offset += cc;
		for (i = 0; i < nbm->offset; i++)
			switch (nbm->buf[i]) {
			case '\r':	nbm->buf[i] = '.'; break;
			case '\n':	nbm->buf[i] = ','; break;
			}
		logmsg("%02d> '%*s'\n", cc, cc, nbm->buf);
		nbm->offset = 0;
		break;
	}
}

int main(int argc, char** argv) {
	static _NBModeS nbm;

	/* event anchor */
	nbm.app = XtCreateApplicationContext();

	/* XXX */
	nbm.device = "/dev/ttyACM0";

	nbm.fd = ma_init(nbm.device);
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
