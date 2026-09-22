#ifndef SLACK_BRIDGE_H
#define SLACK_BRIDGE_H

#include "http_client.h"

#include <stddef.h>
#include <stdint.h>

/* Host-side Slack cache + tiny display API for RetroSlack.
 *
 * Mac talks to pseudo-host http://rs/... over RHTTP. The NSE fetches real
 * Slack Web API traffic, keeps a refreshed message cache for the open
 * channel/thread, and returns only the compact payloads the Mac will render.
 */

#define RS_HOST "rs"
#define RS_URL_PREFIX "http://rs/"

/* True if URL targets the RetroSlack display API (not passthrough Slack). */
int slack_bridge_is_rs_url(const char *url);

void slack_bridge_init(const char *token);
void slack_bridge_shutdown(void);

/* Handle GET/POST to http://rs/...  Fills out like http_request().
 * Returns 0 on success (including HTTP 4xx bodies), -1 on hard failure. */
int slack_bridge_handle(const char *method, const char *url,
                        const void *body, size_t body_len,
                        HttpResponse *out);

#endif
