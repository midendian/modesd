/*
 * Some utility functions
 *
 * Adam Fritzler <mid@zigamorph.net>
 */

#include <stdarg.h>
#include <time.h>

#include "util.h"

static char* _appname = "SET-APPNAME";

void
setappname(char* name) {
	char* cp = rindex(name, '/');
	if (NULL != cp)
		_appname = cp+1;
	else
		_appname = name;
}

void
logmsg(const char *format, ...)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	time_t now = (time_t)tv.tv_sec;
	struct tm *tm = localtime(&now);
	char buf[128];
	strftime(buf, sizeof(buf), "%F %T", tm);
	fprintf(stderr, "%s.%06d %s: ", buf, tv.tv_usec, _appname);

	va_list ap;
	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);
	return;
}

/* serial devices are not quite entirely unlike files. */
int
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

