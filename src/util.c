/*
 * Some utility functions
 *
 * Adam Fritzler <mid@zigamorph.net>
 */

#include <stdarg.h>
#include <time.h>

#include "util.h"

static char* _appname = "SET-APPNAME";

void setappname(char* name) {
	char* cp = rindex(name, '/');
	if (NULL != cp)
		_appname = cp+1;
	else
		_appname = name;
}

void
logmsg(const char *format, ...)
{
	time_t nowT = time(NULL);
	struct tm *now = localtime(&nowT);
	char buf[128];
	strftime(buf, sizeof(buf), "%F %T %z", now);
	fprintf(stderr, "%s %s: ", buf, _appname);

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
