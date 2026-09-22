#ifndef RHTTP_CODEC_H
#define RHTTP_CODEC_H

#include "rhttp.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    RHTTP_OK = 0,
    RHTTP_ERR_ARG = -1,
    RHTTP_ERR_NOSPC = -2,
    RHTTP_ERR_BADMAGIC = -3,
    RHTTP_ERR_BADVER = -4,
    RHTTP_ERR_TOOBIG = -5,
    RHTTP_ERR_SHORT = -6
};

/* Encode fixed 20-byte header into out[0..19]. */
int rhttp_encode_header(const RHTTPFrameHeader *hdr, uint8_t out[RHTTP_HEADER_SIZE]);

/* Decode fixed 20-byte header from in[0..19]. */
int rhttp_decode_header(const uint8_t in[RHTTP_HEADER_SIZE], RHTTPFrameHeader *hdr);

/* Total frame size including header + payload, or error if too large. */
int rhttp_frame_total_size(const RHTTPFrameHeader *hdr, size_t *out_total);

/* Write a complete frame into buf. Returns bytes written or negative error. */
int rhttp_write_frame(uint8_t *buf, size_t buf_cap,
                      uint8_t type, uint16_t flags, uint32_t id,
                      const void *header, uint32_t header_len,
                      const void *body, uint32_t body_len);

/* Fill a PING or PONG frame with empty payloads. */
int rhttp_write_pingpong(uint8_t *buf, size_t buf_cap,
                         uint8_t type, uint32_t id);

#ifdef __cplusplus
}
#endif

#endif /* RHTTP_CODEC_H */
