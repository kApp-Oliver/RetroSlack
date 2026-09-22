#include "../../protocol/rhttp_codec.h"
#include "http_client.h"
#include "serial.h"
#include "slack_bridge.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>

static int g_verbose;
static const char *g_slack_token;

static const char *default_allowlist[] = {
    "slack.com",
    "www.slack.com",
    "example.com",
    "www.example.com",
    "httpbin.org",
    "rs",
    NULL
};

static void logv(const char *fmt, ...) {
    va_list ap;
    if (!g_verbose) {
        return;
    }
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

static int read_exact(SerialIO *s, uint8_t *buf, size_t need, int timeout_ms) {
    size_t got = 0;
    while (got < need) {
        int n = serial_read(s, buf + got, need - got, timeout_ms);
        if (n < 0) {
            return -1;
        }
        if (n == 0) {
            return -1; /* timeout */
        }
        got += (size_t)n;
    }
    return 0;
}

static int write_all(SerialIO *s, const void *buf, size_t len) {
    return serial_write(s, buf, len) == (int)len ? 0 : -1;
}

static int parse_req_meta(const char *meta, size_t meta_len,
                          char *method, size_t method_sz,
                          char *url, size_t url_sz,
                          char *extra, size_t extra_sz) {
    char *copy;
    char *line;
    char *save = NULL;

    method[0] = url[0] = extra[0] = '\0';
    copy = malloc(meta_len + 1);
    if (!copy) {
        return -1;
    }
    memcpy(copy, meta, meta_len);
    copy[meta_len] = '\0';

    for (line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        if (strncmp(line, "METHOD ", 7) == 0) {
            strncpy(method, line + 7, method_sz - 1);
            method[method_sz - 1] = '\0';
        } else if (strncmp(line, "URL ", 4) == 0) {
            strncpy(url, line + 4, url_sz - 1);
            url[url_sz - 1] = '\0';
        } else if (strncmp(line, "Header: ", 8) == 0) {
            size_t cur = strlen(extra);
            size_t add = strlen(line);
            if (cur + add + 2 < extra_sz) {
                memcpy(extra + cur, line, add);
                extra[cur + add] = '\n';
                extra[cur + add + 1] = '\0';
            }
        }
    }
    free(copy);
    return (method[0] && url[0]) ? 0 : -1;
}

static int send_err(SerialIO *s, uint32_t id, const char *reason) {
    char hdr[512];
    uint8_t frame[1024];
    int n;
    int hlen = snprintf(hdr, sizeof(hdr), "REASON %s\n", reason ? reason : "error");
    if (hlen < 0) {
        return -1;
    }
    n = rhttp_write_frame(frame, sizeof(frame), RHTTP_TYPE_ERR, 0, id,
                          hdr, (uint32_t)hlen, NULL, 0);
    if (n < 0) {
        return -1;
    }
    return write_all(s, frame, (size_t)n);
}

static int handle_req(SerialIO *s, uint32_t id,
                      const uint8_t *header, uint32_t header_len,
                      const uint8_t *body, uint32_t body_len) {
    char method[16];
    char url[1024];
    char extra[2048];
    HttpResponse resp;
    uint8_t *frame;
    size_t frame_cap;
    int n;
    const char *inject = NULL;

    if (parse_req_meta((const char *)header, header_len,
                       method, sizeof(method), url, sizeof(url),
                       extra, sizeof(extra)) != 0) {
        return send_err(s, id, "bad REQ meta");
    }

    logv("REQ id=%u %s %s\n", (unsigned)id, method, url);

    /* RetroSlack display API: Slack work happens on the host; Mac gets tiny bodies. */
    if (slack_bridge_is_rs_url(url)) {
        if (slack_bridge_handle(method, url, body, body_len, &resp) != 0) {
            int rc = send_err(s, id, resp.error ? resp.error : "rs bridge failed");
            http_response_free(&resp);
            return rc;
        }
    } else {
        if (!http_host_allowed(url, default_allowlist)) {
            return send_err(s, id, "host not allowlisted");
        }

        if (g_slack_token && g_slack_token[0]
            && (strstr(url, "slack.com") != NULL)) {
            inject = g_slack_token;
        }

        if (http_request(method, url, extra, body, body_len, inject, &resp) != 0) {
            int rc = send_err(s, id, resp.error ? resp.error : "http failed");
            http_response_free(&resp);
            return rc;
        }
    }

    frame_cap = RHTTP_HEADER_SIZE + resp.headers_len + resp.body_len + 64;
    frame = malloc(frame_cap);
    if (!frame) {
        http_response_free(&resp);
        return send_err(s, id, "oom");
    }

    if (resp.body_len > RHTTP_MAX_BODY) {
        free(frame);
        http_response_free(&resp);
        return send_err(s, id, "response too large");
    }

    n = rhttp_write_frame(frame, frame_cap, RHTTP_TYPE_RES, 0, id,
                          resp.headers, (uint32_t)resp.headers_len,
                          resp.body, (uint32_t)resp.body_len);
    http_response_free(&resp);
    if (n < 0) {
        free(frame);
        return send_err(s, id, "encode RES failed");
    }
    logv("RES id=%u bytes=%d\n", (unsigned)id, n);
    {
        int rc = write_all(s, frame, (size_t)n);
        free(frame);
        return rc;
    }
}

static int process_one_frame(SerialIO *s) {
    uint8_t hdrbuf[RHTTP_HEADER_SIZE];
    RHTTPFrameHeader hdr;
    uint8_t *payload = NULL;
    size_t payload_len;
    int rc;

    /* Resync: wait for magic */
    for (;;) {
        uint8_t b;
        int n = serial_read(s, &b, 1, 1000);
        if (n < 0) {
            return -1;
        }
        if (n == 0) {
            return 0; /* idle */
        }
        if (b != RHTTP_MAGIC_0) {
            continue;
        }
        hdrbuf[0] = b;
        if (read_exact(s, hdrbuf + 1, RHTTP_HEADER_SIZE - 1, 2000) != 0) {
            return -1;
        }
        break;
    }

    rc = rhttp_decode_header(hdrbuf, &hdr);
    if (rc != RHTTP_OK) {
        logv("bad header rc=%d\n", rc);
        return 0;
    }

    payload_len = (size_t)hdr.header_len + (size_t)hdr.body_len;
    if (payload_len > 0) {
        payload = malloc(payload_len);
        if (!payload) {
            return send_err(s, hdr.id, "oom");
        }
        if (read_exact(s, payload, payload_len, 10000) != 0) {
            free(payload);
            return -1;
        }
    }

    switch (hdr.type) {
    case RHTTP_TYPE_PING: {
        uint8_t out[RHTTP_HEADER_SIZE];
        int n = rhttp_write_pingpong(out, sizeof(out), RHTTP_TYPE_PONG, hdr.id);
        logv("PING -> PONG id=%u\n", (unsigned)hdr.id);
        rc = (n > 0) ? write_all(s, out, (size_t)n) : -1;
        break;
    }
    case RHTTP_TYPE_PONG:
        logv("PONG id=%u\n", (unsigned)hdr.id);
        rc = 0;
        break;
    case RHTTP_TYPE_REQ:
        rc = handle_req(s, hdr.id,
                        payload, hdr.header_len,
                        payload ? payload + hdr.header_len : NULL, hdr.body_len);
        break;
    default:
        rc = send_err(s, hdr.id, "unsupported type");
        break;
    }

    free(payload);
    return rc;
}

static void usage(const char *argv0) {
    fprintf(stderr,
            "Usage: %s --serial tcp://127.0.0.1:1984| /dev/cu.xxx [--baud 19200] [--verbose]\n"
            "Env: SLACK_USER_TOKEN (or SLACK_TOKEN / SLACK_BOT_TOKEN) for Slack on the host\n"
            "RetroSlack uses http://rs/... (auth,channels,open,view,nav,msg,post,react);\n"
            "passthrough allowlisted HTTPS still works for RHTTPTest.\n",
            argv0);
}

int main(int argc, char **argv) {
    const char *serial_spec = "tcp://127.0.0.1:1984";
    int baud = RHTTP_BAUD;
    SerialIO *sio;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--serial") == 0 && i + 1 < argc) {
            serial_spec = argv[++i];
        } else if (strcmp(argv[i], "--baud") == 0 && i + 1 < argc) {
            baud = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            g_verbose = 1;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    g_slack_token = getenv("SLACK_USER_TOKEN");
    if (!g_slack_token || !g_slack_token[0]) {
        g_slack_token = getenv("SLACK_TOKEN");
    }
    if (!g_slack_token || !g_slack_token[0]) {
        g_slack_token = getenv("SLACK_BOT_TOKEN");
    }
    curl_global_init(CURL_GLOBAL_DEFAULT);
    slack_bridge_init(g_slack_token);

    fprintf(stderr, "nse: opening %s baud=%d\n", serial_spec, baud);
    sio = serial_open(serial_spec, baud);
    if (!sio) {
        slack_bridge_shutdown();
        curl_global_cleanup();
        return 1;
    }
    fprintf(stderr, "nse: ready (verbose=%d token=%s) rs=http://rs/\n",
            g_verbose, (g_slack_token && g_slack_token[0]) ? "yes" : "no");

    for (;;) {
        int rc = process_one_frame(sio);
        if (rc < 0) {
            fprintf(stderr, "nse: link error, exiting\n");
            break;
        }
    }

    serial_close(sio);
    slack_bridge_shutdown();
    curl_global_cleanup();
    return 1;
}
