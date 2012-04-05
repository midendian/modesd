#ifndef __FRAME_H__
#define __FRAME_H__

#include <sys/time.h>

struct frame {
	struct timeval rxstart; /* wall-clock at first byte */
	struct timeval rxend; /* wall-clock at last byte */
	unsigned long long seqnum; /* sequence number (from device) */
	unsigned long long ticks; /* clock ticks since device boot (from device) */
	int skipped; /* bytes skipped to resynch */
	char data[28+1]; /* data as ascii hex string */
};

#endif

