#ifndef __MODES_UTIL_H__
#define __MODES_UTIL_H__

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

extern void setappname(char* name);
extern void logmsg(const char *format, ...);

extern int readn(int fd, unsigned char *buf, int i);
extern int readln(int fd, char *buf, int buflen);

#endif /* ndef __MODES_UTIL_H__ */
