#include <Arduino.h>
#include <SPI.h>
#include <Ethernet.h>

#include "rhttp.h"
#include "rhttp_codec.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#warning "arduino/nse/secrets.h missing — using empty token"
#define SLACK_BOT_TOKEN ""
#define USE_DHCP 1
#endif

/* W5500 CS pin — change for your wiring */
#ifndef W5500_CS
#define W5500_CS 5
#endif

static byte macaddr[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};
static EthernetClient eth;

static const char *allowlist[] = {
  "slack.com",
  "www.slack.com",
  "example.com",
  NULL
};

static int host_allowed(const char *url) {
  const char *p = strstr(url, "://");
  char host[64];
  size_t n = 0;
  const char *slash;
  int i;
  if (!p) return 0;
  p += 3;
  slash = strchr(p, '/');
  while (p[n] && p[n] != ':' && p[n] != '/' && n < sizeof(host) - 1) {
    if (slash && p + n >= slash) break;
    host[n] = p[n];
    n++;
  }
  host[n] = 0;
  for (i = 0; allowlist[i]; i++) {
    if (strcasecmp(host, allowlist[i]) == 0) return 1;
  }
  return 0;
}

static void uart_write(const uint8_t *data, size_t len) {
  Serial1.write(data, len);
}

static int uart_read_byte(uint8_t *b, uint32_t timeout_ms) {
  uint32_t start = millis();
  while (millis() - start < timeout_ms) {
    if (Serial1.available()) {
      *b = (uint8_t)Serial1.read();
      return 1;
    }
  }
  return 0;
}

static int uart_read_exact(uint8_t *buf, size_t len, uint32_t timeout_ms) {
  size_t got = 0;
  while (got < len) {
    if (!uart_read_byte(buf + got, timeout_ms)) return 0;
    got++;
  }
  return 1;
}

static void send_pong(uint32_t id) {
  uint8_t buf[RHTTP_HEADER_SIZE];
  int n = rhttp_write_pingpong(buf, sizeof(buf), RHTTP_TYPE_PONG, id);
  if (n > 0) uart_write(buf, (size_t)n);
}

static void send_err(uint32_t id, const char *reason) {
  char hdr[128];
  uint8_t frame[256];
  int hlen = snprintf(hdr, sizeof(hdr), "REASON %s\n", reason);
  int n = rhttp_write_frame(frame, sizeof(frame), RHTTP_TYPE_ERR, 0, id,
                            hdr, (uint32_t)hlen, NULL, 0);
  if (n > 0) uart_write(frame, (size_t)n);
}

static int parse_meta(const char *meta, size_t len, char *method, char *url) {
  char copy[512];
  char *line;
  char *save = NULL;
  if (len >= sizeof(copy)) len = sizeof(copy) - 1;
  memcpy(copy, meta, len);
  copy[len] = 0;
  method[0] = url[0] = 0;
  for (line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
    if (strncmp(line, "METHOD ", 7) == 0) strncpy(method, line + 7, 15);
    else if (strncmp(line, "URL ", 4) == 0) strncpy(url, line + 4, 255);
  }
  return method[0] && url[0];
}

/* Minimal HTTP GET/POST over EthernetClient. HTTPS requires board TLS stack;
 * for ESP32 you may swap in WiFiClientSecure + HTTPClient later. */
static int http_do(const char *method, const char *url,
                   const uint8_t *req_body, uint32_t req_len,
                   char *resp_meta, size_t resp_meta_sz,
                   uint8_t *resp_body, size_t resp_body_cap, size_t *resp_body_len) {
  char host[64];
  char path[192];
  const char *p = strstr(url, "://");
  int port = 80;
  int is_tls = 0;
  size_t hi = 0;
  const char *pathstart;

  if (!p) return -1;
  if (strncmp(url, "https://", 8) == 0) {
    is_tls = 1;
    port = 443;
  }
  p += 3;
  while (*p && *p != '/' && *p != ':' && hi < sizeof(host) - 1) {
    host[hi++] = *p++;
  }
  host[hi] = 0;
  if (*p == ':') {
    port = atoi(p + 1);
    while (*p && *p != '/') p++;
  }
  pathstart = (*p == '/') ? p : "/";
  strncpy(path, pathstart, sizeof(path) - 1);

#if defined(RHTTP_CLEARTEXT_ONLY)
  if (is_tls) {
    snprintf(resp_meta, resp_meta_sz, "REASON TLS not supported on this build\n");
    return -1;
  }
#else
  (void)is_tls;
  /* Production boards should use a TLS client here. This stub uses cleartext
   * EthernetClient for bring-up; replace with WiFiClientSecure/HTTPClient for Slack. */
  if (is_tls) {
    snprintf(resp_meta, resp_meta_sz, "REASON wire TLS client not linked — use desktop NSE or add Secure client\n");
    return -1;
  }
#endif

  if (!eth.connect(host, port)) {
    snprintf(resp_meta, resp_meta_sz, "REASON connect failed\n");
    return -1;
  }

  eth.print(method);
  eth.print(" ");
  eth.print(path);
  eth.println(" HTTP/1.0");
  eth.print("Host: ");
  eth.println(host);
  eth.println("User-Agent: RetroSlack-Arduino-NSE/1.0");
  eth.println("Connection: close");
  if (strstr(host, "slack.com") && SLACK_BOT_TOKEN[0]) {
    eth.print("Authorization: Bearer ");
    eth.println(SLACK_BOT_TOKEN);
  }
  if (req_body && req_len) {
    eth.print("Content-Length: ");
    eth.println(req_len);
    eth.println("Content-Type: application/json");
    eth.println();
    eth.write(req_body, req_len);
  } else {
    eth.println();
  }

  /* Read status line */
  {
    String line = eth.readStringUntil('\n');
    int code = 0;
    if (sscanf(line.c_str(), "HTTP/%*s %d", &code) == 1) {
      snprintf(resp_meta, resp_meta_sz, "STATUS %d\n", code);
    } else {
      snprintf(resp_meta, resp_meta_sz, "STATUS 0\n");
    }
  }
  /* Skip headers */
  while (eth.connected()) {
    String line = eth.readStringUntil('\n');
    if (line == "\r" || line.length() == 0) break;
  }

  *resp_body_len = 0;
  while (eth.available() && *resp_body_len < resp_body_cap) {
    resp_body[(*resp_body_len)++] = (uint8_t)eth.read();
  }
  eth.stop();
  return 0;
}

static void handle_req(uint32_t id, const uint8_t *header, uint32_t header_len,
                       const uint8_t *body, uint32_t body_len) {
  char method[16];
  char url[256];
  char resp_meta[64];
  static uint8_t resp_body[4096];
  size_t resp_len = 0;
  uint8_t *frame;
  size_t frame_cap;
  int n;

  if (!parse_meta((const char *)header, header_len, method, url)) {
    send_err(id, "bad REQ meta");
    return;
  }
  if (!host_allowed(url)) {
    send_err(id, "host not allowlisted");
    return;
  }
  if (http_do(method, url, body, body_len, resp_meta, sizeof(resp_meta),
              resp_body, sizeof(resp_body), &resp_len) != 0) {
    send_err(id, resp_meta[0] ? resp_meta : "http failed");
    return;
  }
  frame_cap = RHTTP_HEADER_SIZE + strlen(resp_meta) + resp_len + 8;
  frame = (uint8_t *)malloc(frame_cap);
  if (!frame) {
    send_err(id, "oom");
    return;
  }
  n = rhttp_write_frame(frame, frame_cap, RHTTP_TYPE_RES, 0, id,
                        resp_meta, (uint32_t)strlen(resp_meta),
                        resp_body, (uint32_t)resp_len);
  if (n > 0) uart_write(frame, (size_t)n);
  free(frame);
}

static void process_frames(void) {
  uint8_t b;
  uint8_t hdrbuf[RHTTP_HEADER_SIZE];
  RHTTPFrameHeader hdr;
  uint8_t *payload;

  if (!Serial1.available()) return;
  if (!uart_read_byte(&b, 50) || b != RHTTP_MAGIC_0) return;
  hdrbuf[0] = b;
  if (!uart_read_exact(hdrbuf + 1, RHTTP_HEADER_SIZE - 1, 2000)) return;
  if (rhttp_decode_header(hdrbuf, &hdr) != RHTTP_OK) return;

  payload = NULL;
  if (hdr.header_len + hdr.body_len > 0) {
    payload = (uint8_t *)malloc(hdr.header_len + hdr.body_len);
    if (!payload) {
      send_err(hdr.id, "oom");
      return;
    }
    if (!uart_read_exact(payload, hdr.header_len + hdr.body_len, 10000)) {
      free(payload);
      return;
    }
  }

  if (hdr.type == RHTTP_TYPE_PING) {
    send_pong(hdr.id);
  } else if (hdr.type == RHTTP_TYPE_REQ) {
    handle_req(hdr.id, payload, hdr.header_len,
               payload ? payload + hdr.header_len : NULL, hdr.body_len);
  }
  free(payload);
}

void setup() {
  Serial.begin(115200);
  Serial1.begin(RHTTP_BAUD);
  Ethernet.init(W5500_CS);
#if USE_DHCP
  if (Ethernet.begin(macaddr) == 0) {
    Serial.println("DHCP failed");
  } else {
    Serial.print("IP ");
    Serial.println(Ethernet.localIP());
  }
#else
  {
    IPAddress ip(STATIC_IP), dns(STATIC_DNS), gw(STATIC_GW), sn(STATIC_SN);
    Ethernet.begin(macaddr, ip, dns, gw, sn);
  }
#endif
  Serial.println("Arduino NSE ready (RHTTP on Serial1)");
}

void loop() {
  Ethernet.maintain();
  process_frames();
}
