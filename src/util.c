/*
 * Some utility functions
 *
 * Adam Fritzler <mid@zigamorph.net>
 */

#include <stdio.h>
#include <stdarg.h>
#include <time.h>

#include "util.h"

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
