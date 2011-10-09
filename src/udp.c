
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
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
udp_send(char *raw)
{
	struct udp_target *ut;
	int err = 0;

	/* sanity */
	if (NULL == raw)
		return -1;
	int rLen = strlen(raw);
	if (14 != rLen || 28 != rLen)
		return -1;

	for (ut = udp_targets; ut; ut = ut->next) {
		struct iovec iov[4];
		int n = 0;

		if (UDP_PLANEPLOTTER == ut->variant) {
			iov[n].iov_base = "AV"; iov[n++].iov_len = 2;
		}
		iov[n].iov_base = "*"; iov[n++].iov_len = 1;
		iov[n].iov_base = raw; iov[n++].iov_len = rLen;
		iov[n].iov_base = ";"; iov[n++].iov_len = 1;

		if (writev(ut->fd, iov, n) < 0)
			err++;
	}

	return -err;
}

int
udp_parsearg(const char *optarg)
{
	/* -u host:port[:variant] */
	char *hstr = NULL, *pstr = NULL, *vstr = NULL;
	udp_variant_t variant = UDP_RAW;
	int port = 0;
	int err = 0;

	if (!optarg ||
	    (strlen(optarg) <= 0))
		return -1;

	if (!(hstr = strdup(optarg)) ||
	    !(pstr = index(hstr, ':'))) {
		err = -1; goto out;
	}
	*pstr = '\0'; pstr++;
	if (index(pstr, ':')) {
		vstr = index(pstr, ':');
		*vstr = '\0'; vstr++;
	}
	if ((strlen(hstr) <= 0) ||
	    (strlen(pstr) <= 0) ||
	    ((port = atoi(pstr)) <= 0)) {
		err = -1; goto out;
	}
	if (vstr) {
		if (strcmp(vstr, "raw") == 0)
			variant = UDP_RAW;
		else if (strcmp(vstr, "planeplotter") == 0)
			variant = UDP_PLANEPLOTTER;
		else {
			fprintf(stderr, "invalid protocol '%s' for %s:%d\n", vstr, hstr, port);
			err = -1; goto out;
		}
	}
	if (udp_addport(hstr, (unsigned short)port, variant) == -1) {
		fprintf(stderr, "failed to add UDP output port for %s:%d\n", hstr, port);
		err = -1; goto out;
	}
out:
	if (hstr) free(hstr);
	return err;
}
