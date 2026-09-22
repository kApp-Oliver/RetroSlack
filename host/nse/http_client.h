#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <stddef.h>
#include <stdint.h>

typedef struct HttpResponse {
    long status;
    char *headers;   /* malloc'd STATUS/Header lines for RHTTP */
    size_t headers_len;
    uint8_t *body;
    size_t body_len;
    char *error;     /* malloc'd on failure */
} HttpResponse;

void http_response_free(HttpResponse *r);

/* Perform HTTP(S). extra_headers are raw "Header: value\n" lines from Mac.
 * If inject_auth is non-NULL, adds Authorization Bearer. */
int http_request(const char *method, const char *url,
                 const char *extra_headers,
                 const void *body, size_t body_len,
                 const char *inject_auth,
                 HttpResponse *out);

int http_host_allowed(const char *url, const char *const *allowlist);

#endif
