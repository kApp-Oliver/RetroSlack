#include "http_client.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <curl/curl.h>

struct GrowBuf {
    uint8_t *data;
    size_t len;
    size_t cap;
};

static int grow_append(struct GrowBuf *g, const void *p, size_t n) {
    if (n == 0) {
        return 0;
    }
    if (g->len + n > g->cap) {
        size_t ncap = g->cap ? g->cap * 2 : 4096;
        uint8_t *nd;
        while (ncap < g->len + n) {
            ncap *= 2;
        }
        nd = realloc(g->data, ncap);
        if (!nd) {
            return -1;
        }
        g->data = nd;
        g->cap = ncap;
    }
    memcpy(g->data + g->len, p, n);
    g->len += n;
    return 0;
}

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    struct GrowBuf *g = userdata;
    size_t n = size * nmemb;
    if (grow_append(g, ptr, n) != 0) {
        return 0;
    }
    return n;
}

void http_response_free(HttpResponse *r) {
    if (!r) {
        return;
    }
    free(r->headers);
    free(r->body);
    free(r->error);
    memset(r, 0, sizeof(*r));
}

static int extract_host(const char *url, char *host, size_t host_sz) {
    const char *p;
    const char *slash;
    const char *colon;
    size_t n;

    if (!url || !host || host_sz == 0) {
        return -1;
    }
    p = strstr(url, "://");
    if (!p) {
        return -1;
    }
    p += 3;
    slash = strchr(p, '/');
    colon = strchr(p, ':');
    if (colon && (!slash || colon < slash)) {
        n = (size_t)(colon - p);
    } else if (slash) {
        n = (size_t)(slash - p);
    } else {
        n = strlen(p);
    }
    if (n == 0 || n >= host_sz) {
        return -1;
    }
    memcpy(host, p, n);
    host[n] = '\0';
    return 0;
}

int http_host_allowed(const char *url, const char *const *allowlist) {
    char host[256];
    size_t i;

    if (extract_host(url, host, sizeof(host)) != 0) {
        return 0;
    }
    for (i = 0; allowlist && allowlist[i]; i++) {
        const char *a = allowlist[i];
        size_t al = strlen(a);
        size_t hl = strlen(host);
        if (strcasecmp(host, a) == 0) {
            return 1;
        }
        /* allow subdomains: foo.slack.com matches suffix .slack.com if allow is slack.com */
        if (hl > al + 1 && host[hl - al - 1] == '.'
            && strcasecmp(host + (hl - al), a) == 0) {
            return 1;
        }
    }
    return 0;
}

static int build_rhttp_headers(long status, struct GrowBuf *hdr_out) {
    char line[64];
    int n = snprintf(line, sizeof(line), "STATUS %ld\n", status);
    if (n < 0) {
        return -1;
    }
    return grow_append(hdr_out, line, (size_t)n);
}

int http_request(const char *method, const char *url,
                 const char *extra_headers,
                 const void *body, size_t body_len,
                 const char *inject_auth,
                 HttpResponse *out) {
    CURL *curl;
    CURLcode cres;
    struct GrowBuf body_buf = {0};
    struct GrowBuf hdr_meta = {0};
    struct curl_slist *hdrs = NULL;
    char errbuf[CURL_ERROR_SIZE];
    long status = 0;

    memset(out, 0, sizeof(*out));
    errbuf[0] = '\0';

    curl = curl_easy_init();
    if (!curl) {
        out->error = strdup("curl_easy_init failed");
        return -1;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method ? method : "GET");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body_buf);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "RetroSlack-NSE/1.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

    if (body && body_len > 0) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body_len);
    }

    if (extra_headers && extra_headers[0]) {
        const char *line = extra_headers;
        while (*line) {
            const char *nl = strchr(line, '\n');
            size_t len = nl ? (size_t)(nl - line) : strlen(line);
            if (len > 8 && strncmp(line, "Header: ", 8) == 0) {
                char tmp[1024];
                size_t copy = len - 8;
                if (copy >= sizeof(tmp)) {
                    copy = sizeof(tmp) - 1;
                }
                memcpy(tmp, line + 8, copy);
                tmp[copy] = '\0';
                while (copy > 0 && (tmp[copy - 1] == '\r' || tmp[copy - 1] == ' ')) {
                    tmp[--copy] = '\0';
                }
                hdrs = curl_slist_append(hdrs, tmp);
            }
            if (!nl) {
                break;
            }
            line = nl + 1;
        }
    }

    if (inject_auth && inject_auth[0]) {
        char auth[512];
        snprintf(auth, sizeof(auth), "Authorization: Bearer %s", inject_auth);
        hdrs = curl_slist_append(hdrs, auth);
    }

    if (hdrs) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    }

    cres = curl_easy_perform(curl);
    if (cres != CURLE_OK) {
        char msg[CURL_ERROR_SIZE + 64];
        snprintf(msg, sizeof(msg), "curl failed: %s",
                 errbuf[0] ? errbuf : curl_easy_strerror(cres));
        out->error = strdup(msg);
        curl_slist_free_all(hdrs);
        curl_easy_cleanup(curl);
        free(body_buf.data);
        return -1;
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);

    if (build_rhttp_headers(status, &hdr_meta) != 0) {
        out->error = strdup("oom building headers");
        free(body_buf.data);
        free(hdr_meta.data);
        return -1;
    }

    out->status = status;
    out->headers = (char *)hdr_meta.data;
    out->headers_len = hdr_meta.len;
    out->body = body_buf.data;
    out->body_len = body_buf.len;
    return 0;
}
