
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>

#include "util.h"
#include "udp.h"

struct udp_target {
	char *host;
	unsigned short port;
	udp_variant_t variant;
	struct sockaddr_in sin;
	int fd;

	struct udp_target *next;
};
static struct udp_target *udp_targets = NULL;

static struct udp_target *
udp_target_alloc(const char *host, unsigned short port, udp_variant_t variant)
{
	struct udp_target *ut;

	if (!host ||
	    (strlen(host) <= 0) ||
	    (port <= 0))
		return NULL;

	if (!(ut = (struct udp_target *)malloc(sizeof(struct udp_target))))
		return NULL;
	memset(ut, 0, sizeof(struct udp_target));
	if (!(ut->host = strdup(host))) {
		free(ut);
		return NULL;
	}
	ut->port = port;
	ut->variant = variant;
	ut->fd = -1;

	return ut;
}

static void
udp_target_free(struct udp_target *ut)
{
	if (!ut) return;

	if (ut->host) free(ut->host);
	free(ut);

	return;
}

int
udp_addport(const char *host, unsigned short port, udp_variant_t variant)
{
	struct udp_target *ut;
	struct hostent *hp;

	if (!host ||
	    (strlen(host) <= 0) ||
	    (port <= 0))
		return -1;

	if (!(ut = udp_target_alloc(host, port, variant)))
		return -1;

	if ((ut->fd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		udp_target_free(ut);
		return -1;
	}
	if (!(hp = gethostbyname(ut->host))) {
		logmsg("unknown host '%s'\n", ut->host);
		udp_target_free(ut);
		return -1;
	}
	ut->sin.sin_family = AF_INET;
	memcpy(&ut->sin.sin_addr, hp->h_addr, hp->h_length);
	ut->sin.sin_port = htons(ut->port);
	/* we use a separate fd for each and connect it so we can get errors easier... */
	if (connect(ut->fd, (struct sockaddr *)&ut->sin, sizeof(ut->sin)) == -1) {
		logmsg("unable to connect socket: %s\n", strerror(errno));
		udp_target_free(ut);
		return -1;
	}

	ut->next = udp_targets;
	udp_targets = ut;

	return 0;
}

void
udp_clearports(void)
{
	struct udp_target *ut = udp_targets;
	while (ut) {
		struct udp_target *nut = ut->next;
		udp_target_free(ut);
		ut = nut;
	}
	return;
}

int
udp_send(const char *raw)
{
	struct udp_target *ut;
	int err = 0;

	if (!raw ||
	    ((strlen(raw) != 14) && (strlen(raw) != 28)))
		return -1;

	for (ut = udp_targets; ut; ut = ut->next) {
		char *buf;
		int buflen;
		int i = 0;

		buflen = 1 + strlen(raw) + 1;
		if (ut->variant == UDP_PLANEPLOTTER)
			buflen += strlen("AV");

		if (!(buf = malloc(buflen + 1))) {
			err++;
			continue;
		}
		if (ut->variant == UDP_PLANEPLOTTER)
			i += snprintf(buf+i, buflen-i, "AV");
		i += snprintf(buf+i, buflen-i, "*%s;", raw);

		if (send(ut->fd, buf, i, 0) < 0)
			err++;
		free(buf);
	}

	return -err;
}


