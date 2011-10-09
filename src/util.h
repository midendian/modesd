#ifndef __MODES_UTIL_H__
#define __MODES_UTIL_H__

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define APPNAME "mode-s-avr"
void logmsg(const char *format, ...);

extern unsigned long long extractTC(const char *buf);
extern unsigned long extractFC(const char *buf);

extern int readln(int fd, char *buf, int buflen);

#endif /* ndef __MODES_UTIL_H__ */
