#include "serial.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

/* Must match SerialIO in serial_posix.c */
struct SerialIO {
    int fd;
    int is_tcp;
    int baud;
};

SerialIO *serial_open_tcp(const char *spec, int baud) {
    SerialIO *s;
    char host[256];
    int port = 0;
    const char *p;
    const char *colon;
    size_t hlen;
    struct sockaddr_in addr;
    int fd = -1;
    int attempt;

    if (strncmp(spec, "tcp://", 6) != 0) {
        return NULL;
    }
    p = spec + 6;
    colon = strrchr(p, ':');
    if (!colon || colon == p) {
        fprintf(stderr, "nse: bad tcp spec '%s'\n", spec);
        return NULL;
    }
    hlen = (size_t)(colon - p);
    if (hlen >= sizeof(host)) {
        return NULL;
    }
    memcpy(host, p, hlen);
    host[hlen] = '\0';
    port = atoi(colon + 1);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "nse: bad port in '%s'\n", spec);
        return NULL;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        fprintf(stderr, "nse: bad host '%s' (use IPv4)\n", host);
        return NULL;
    }

    for (attempt = 0; attempt < 50; attempt++) {
        fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            perror("socket");
            return NULL;
        }
        if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
            break;
        }
        close(fd);
        fd = -1;
        usleep(100000);
    }
    if (fd < 0) {
        fprintf(stderr, "nse: could not connect to %s:%d (is Snow TCP bridge up?)\n",
                host, port);
        return NULL;
    }

    {
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags >= 0) {
            fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        }
    }

    s = calloc(1, sizeof(*s));
    if (!s) {
        close(fd);
        return NULL;
    }
    s->fd = fd;
    s->is_tcp = 1;
    s->baud = baud > 0 ? baud : 19200;
    return s;
}
