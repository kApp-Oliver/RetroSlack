#include "rhttp_codec.h"

#include <string.h>

static void wr_u16_le(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)((v >> 8) & 0xffu);
}

static void wr_u32_le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)((v >> 8) & 0xffu);
    p[2] = (uint8_t)((v >> 16) & 0xffu);
    p[3] = (uint8_t)((v >> 24) & 0xffu);
}

static uint16_t rd_u16_le(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd_u32_le(const uint8_t *p) {
    return (uint32_t)p[0]
        | ((uint32_t)p[1] << 8)
        | ((uint32_t)p[2] << 16)
        | ((uint32_t)p[3] << 24);
}

int rhttp_encode_header(const RHTTPFrameHeader *hdr, uint8_t out[RHTTP_HEADER_SIZE]) {
    if (!hdr || !out) {
        return RHTTP_ERR_ARG;
    }
    out[0] = hdr->magic[0];
    out[1] = hdr->magic[1];
    out[2] = hdr->magic[2];
    out[3] = hdr->magic[3];
    out[4] = hdr->version;
    out[5] = hdr->type;
    wr_u16_le(out + 6, hdr->flags);
    wr_u32_le(out + 8, hdr->id);
    wr_u32_le(out + 12, hdr->header_len);
    wr_u32_le(out + 16, hdr->body_len);
    return RHTTP_OK;
}

int rhttp_decode_header(const uint8_t in[RHTTP_HEADER_SIZE], RHTTPFrameHeader *hdr) {
    if (!in || !hdr) {
        return RHTTP_ERR_ARG;
    }
    if (in[0] != RHTTP_MAGIC_0 || in[1] != RHTTP_MAGIC_1
        || in[2] != RHTTP_MAGIC_2 || in[3] != RHTTP_MAGIC_3) {
        return RHTTP_ERR_BADMAGIC;
    }
    if (in[4] != RHTTP_VERSION) {
        return RHTTP_ERR_BADVER;
    }
    hdr->magic[0] = in[0];
    hdr->magic[1] = in[1];
    hdr->magic[2] = in[2];
    hdr->magic[3] = in[3];
    hdr->version = in[4];
    hdr->type = in[5];
    hdr->flags = rd_u16_le(in + 6);
    hdr->id = rd_u32_le(in + 8);
    hdr->header_len = rd_u32_le(in + 12);
    hdr->body_len = rd_u32_le(in + 16);
    if (hdr->header_len > RHTTP_MAX_HEADER || hdr->body_len > RHTTP_MAX_BODY) {
        return RHTTP_ERR_TOOBIG;
    }
    return RHTTP_OK;
}

int rhttp_frame_total_size(const RHTTPFrameHeader *hdr, size_t *out_total) {
    size_t total;
    if (!hdr || !out_total) {
        return RHTTP_ERR_ARG;
    }
    if (hdr->header_len > RHTTP_MAX_HEADER || hdr->body_len > RHTTP_MAX_BODY) {
        return RHTTP_ERR_TOOBIG;
    }
    total = (size_t)RHTTP_HEADER_SIZE + (size_t)hdr->header_len + (size_t)hdr->body_len;
    *out_total = total;
    return RHTTP_OK;
}

int rhttp_write_frame(uint8_t *buf, size_t buf_cap,
                      uint8_t type, uint16_t flags, uint32_t id,
                      const void *header, uint32_t header_len,
                      const void *body, uint32_t body_len) {
    RHTTPFrameHeader hdr;
    size_t total;
    int rc;

    if (!buf) {
        return RHTTP_ERR_ARG;
    }
    if (header_len > 0 && !header) {
        return RHTTP_ERR_ARG;
    }
    if (body_len > 0 && !body) {
        return RHTTP_ERR_ARG;
    }
    if (header_len > RHTTP_MAX_HEADER || body_len > RHTTP_MAX_BODY) {
        return RHTTP_ERR_TOOBIG;
    }

    hdr.magic[0] = RHTTP_MAGIC_0;
    hdr.magic[1] = RHTTP_MAGIC_1;
    hdr.magic[2] = RHTTP_MAGIC_2;
    hdr.magic[3] = RHTTP_MAGIC_3;
    hdr.version = RHTTP_VERSION;
    hdr.type = type;
    hdr.flags = flags;
    hdr.id = id;
    hdr.header_len = header_len;
    hdr.body_len = body_len;

    rc = rhttp_frame_total_size(&hdr, &total);
    if (rc != RHTTP_OK) {
        return rc;
    }
    if (total > buf_cap) {
        return RHTTP_ERR_NOSPC;
    }

    rc = rhttp_encode_header(&hdr, buf);
    if (rc != RHTTP_OK) {
        return rc;
    }
    if (header_len) {
        memcpy(buf + RHTTP_HEADER_SIZE, header, header_len);
    }
    if (body_len) {
        memcpy(buf + RHTTP_HEADER_SIZE + header_len, body, body_len);
    }
    return (int)total;
}

int rhttp_write_pingpong(uint8_t *buf, size_t buf_cap, uint8_t type, uint32_t id) {
    if (type != RHTTP_TYPE_PING && type != RHTTP_TYPE_PONG) {
        return RHTTP_ERR_ARG;
    }
    return rhttp_write_frame(buf, buf_cap, type, 0, id, NULL, 0, NULL, 0);
}
