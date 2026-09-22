#include "serial.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/* Defined in serial_tcp.c — struct layout must match */
SerialIO *serial_open_tcp(const char *spec, int baud);

struct SerialIO {
    int fd;
    int is_tcp;
    int baud;
};

static speed_t baud_to_flag(int baud) {
    switch (baud) {
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
    default: return B19200;
    }
}

static SerialIO *serial_open_posix(const char *path, int baud) {
    SerialIO *s;
    struct termios tio;
    int fd;

    fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        perror(path);
        return NULL;
    }
    if (tcgetattr(fd, &tio) != 0) {
        perror("tcgetattr");
        close(fd);
        return NULL;
    }
    cfmakeraw(&tio);
    cfsetispeed(&tio, baud_to_flag(baud));
    cfsetospeed(&tio, baud_to_flag(baud));
    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= ~(PARENB | CSTOPB | CSIZE);
    tio.c_cflag |= CS8;
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;
    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        perror("tcsetattr");
        close(fd);
        return NULL;
    }
#ifdef TIOCEXCL
    /* One host process at a time — NSE and rh cannot share the bridge. */
    (void)ioctl(fd, TIOCEXCL);
#endif
    tcflush(fd, TCIOFLUSH);

    s = calloc(1, sizeof(*s));
    if (!s) {
        close(fd);
        return NULL;
    }
    s->fd = fd;
    s->is_tcp = 0;
    s->baud = baud > 0 ? baud : 19200;
    return s;
}

SerialIO *serial_open(const char *spec, int baud) {
    if (!spec) {
        return NULL;
    }
    if (strncmp(spec, "tcp://", 6) == 0) {
        return serial_open_tcp(spec, baud);
    }
    return serial_open_posix(spec, baud);
}

void serial_close(SerialIO *s) {
    if (!s) {
        return;
    }
    if (s->fd >= 0) {
        close(s->fd);
    }
    free(s);
}

int serial_fd(SerialIO *s) {
    return s ? s->fd : -1;
}

int serial_read(SerialIO *s, void *buf, size_t len, int timeout_ms) {
    struct pollfd pfd;
    ssize_t n;

    if (!s || !buf || len == 0) {
        return -1;
    }
    pfd.fd = s->fd;
    pfd.events = POLLIN;
    if (poll(&pfd, 1, timeout_ms) <= 0) {
        return 0;
    }
    n = read(s->fd, buf, len);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        return -1;
    }
    return (int)n;
}

int serial_write(SerialIO *s, const void *buf, size_t len) {
    const uint8_t *p;
    size_t left;
    int baud;

    if (!s || (!buf && len) || len == 0) {
        return len == 0 ? 0 : -1;
    }
    p = buf;
    left = len;
    baud = s->baud > 0 ? s->baud : 19200;

    while (left > 0) {
        size_t chunk = left;
        ssize_t n;

        /* Pace USB CDC → 328P UART → AltSoftSerial @ baud.
         * 16-byte / 10ms still overruns the Uno 64-byte CDC buffer on PUT. */
        if (chunk > 8) {
            chunk = 8;
        }

        n = write(s->fd, p, chunk);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                struct pollfd pfd;
                pfd.fd = s->fd;
                pfd.events = POLLOUT;
                if (poll(&pfd, 1, 2000) <= 0) {
                    return -1;
                }
                continue;
            }
            return -1;
        }
        if (n == 0) {
            return -1;
        }
        p += (size_t)n;
        left -= (size_t)n;

        {
            useconds_t us = (useconds_t)(((size_t)n * 15u * 1000000u) / (unsigned)baud);
            if (us < 2500) {
                us = 2500;
            }
            usleep(us);
        }
    }
    return (int)len;
}
