#ifndef NSE_SERIAL_H
#define NSE_SERIAL_H

#include <stddef.h>
#include <stdint.h>

typedef struct SerialIO SerialIO;

SerialIO *serial_open(const char *spec, int baud);
void serial_close(SerialIO *s);
int serial_read(SerialIO *s, void *buf, size_t len, int timeout_ms);
/* Writes all bytes. TCP bridges are paced to ~baud so the Mac SCC RX
 * buffer is not flooded (Snow feeds guest serial as fast as TCP allows). */
int serial_write(SerialIO *s, const void *buf, size_t len);
int serial_fd(SerialIO *s);

#endif
