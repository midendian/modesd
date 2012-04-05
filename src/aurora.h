#ifndef __AURORA_H__
#define __AURORA_H__

#include "frame.h"

extern int aurora_open(const char *devname, int init);
extern int aurora_read(int fd, struct frame *frame, int timeout);

#endif

