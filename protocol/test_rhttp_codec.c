#include "rhttp_codec.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void expect_ok(int rc, const char *what) {
    if (rc != RHTTP_OK) {
        fprintf(stderr, "FAIL %s: rc=%d\n", what, rc);
        fails++;
    }
}

static void expect_eq_int(int a, int b, const char *what) {
    if (a != b) {
        fprintf(stderr, "FAIL %s: %d != %d\n", what, a, b);
        fails++;
    }
}

static void expect_eq_u32(uint32_t a, uint32_t b, const char *what) {
    if (a != b) {
        fprintf(stderr, "FAIL %s: %u != %u\n", what, (unsigned)a, (unsigned)b);
        fails++;
    }
}

int main(void) {
    uint8_t buf[256];
    RHTTPFrameHeader hdr;
    int n;
    const char *meta = "METHOD GET\nURL https://example.com/\n";
    const char *body = "hi";

    n = rhttp_write_pingpong(buf, sizeof(buf), RHTTP_TYPE_PING, 42);
    expect_eq_int(n, (int)RHTTP_HEADER_SIZE, "ping size");
    expect_ok(rhttp_decode_header(buf, &hdr), "decode ping");
    expect_eq_int((int)hdr.type, RHTTP_TYPE_PING, "ping type");
    expect_eq_u32(hdr.id, 42, "ping id");

    n = rhttp_write_frame(buf, sizeof(buf), RHTTP_TYPE_REQ, 0, 7,
                          meta, (uint32_t)strlen(meta),
                          body, (uint32_t)strlen(body));
    if (n < 0) {
        fprintf(stderr, "FAIL write_frame: %d\n", n);
        fails++;
    } else {
        expect_ok(rhttp_decode_header(buf, &hdr), "decode req");
        expect_eq_int((int)hdr.type, RHTTP_TYPE_REQ, "req type");
        expect_eq_u32(hdr.header_len, (uint32_t)strlen(meta), "hdr len");
        expect_eq_u32(hdr.body_len, (uint32_t)strlen(body), "body len");
        if (memcmp(buf + RHTTP_HEADER_SIZE, meta, strlen(meta)) != 0) {
            fprintf(stderr, "FAIL meta mismatch\n");
            fails++;
        }
        if (memcmp(buf + RHTTP_HEADER_SIZE + strlen(meta), body, strlen(body)) != 0) {
            fprintf(stderr, "FAIL body mismatch\n");
            fails++;
        }
    }

    {
        uint8_t bad[RHTTP_HEADER_SIZE];
        memset(bad, 0, sizeof(bad));
        if (rhttp_decode_header(bad, &hdr) != RHTTP_ERR_BADMAGIC) {
            fprintf(stderr, "FAIL expected BADMAGIC\n");
            fails++;
        }
    }

    if (fails) {
        fprintf(stderr, "%d test(s) failed\n", fails);
        return 1;
    }
    printf("rhttp_codec tests ok\n");
    return 0;
}
