#ifndef RHTTP_H
#define RHTTP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RHTTP_MAGIC_0 'R'
#define RHTTP_MAGIC_1 'H'
#define RHTTP_MAGIC_2 'T'
#define RHTTP_MAGIC_3 'P'

#define RHTTP_VERSION 1

#define RHTTP_TYPE_REQ  1
#define RHTTP_TYPE_RES  2
#define RHTTP_TYPE_ERR  3
#define RHTTP_TYPE_PING 4
#define RHTTP_TYPE_PONG 5

#define RHTTP_FLAG_MORE 0x0001u

/* Shared link defaults (Mac, desktop NSE, Arduino) */
#define RHTTP_BAUD 19200

/* Guest-oriented caps (Plus RAM). NSE may refuse larger bodies. */
#define RHTTP_MAX_HEADER 2048u
#define RHTTP_MAX_BODY   65536u

#define RHTTP_HEADER_SIZE 20u

typedef struct RHTTPFrameHeader {
    uint8_t magic[4];
    uint8_t version;
    uint8_t type;
    uint16_t flags;
    uint32_t id;
    uint32_t header_len;
    uint32_t body_len;
} RHTTPFrameHeader;

#ifdef __cplusplus
}
#endif

#endif /* RHTTP_H */
