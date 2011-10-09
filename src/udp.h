#ifndef __MODES_UDP_H__
#define __MODES_UDP_H__

typedef enum udp_variant {
	UDP_RAW = 0,
	UDP_PLANEPLOTTER = 1,
} udp_variant_t;

int udp_addport(const char *host, unsigned short port, udp_variant_t variant);
void udp_clearports(void);
int udp_send(const char *avrraw);
int udp_parsearg(const char *optarg);

#endif /* ndef __MODES_UDP_H__ */
