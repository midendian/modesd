#ifndef __MODES_UTIL_H__
#define __MODES_UTIL_H__

#define APPNAME "mode-s-avr"
void logmsg(const char *format, ...);

extern int readln(int fd, char *buf, int buflen);

#endif /* ndef __MODES_UTIL_H__ */
