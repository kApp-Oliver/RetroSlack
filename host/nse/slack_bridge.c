#include "slack_bridge.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

enum {
    kNameLen = 48,
    kReactNameLen = 24,
    kTsLen = 24,
    kTextLen = 240,
    kMaxChannels = 32,
    kMaxMessages = 128,
    kMaxUsers = 64,
    kMaxReactions = 6,
    kDefaultView = 14,
    kCacheRefreshSec = 8,
    kHistoryLimit = 100
};

typedef struct Reaction {
    char name[kReactNameLen];
    int count;
    int mine;
} Reaction;

typedef struct Message {
    char user_id[kNameLen];
    char user[kNameLen];
    char text[kTextLen];
    char ts[kTsLen];
    char thread_ts[kTsLen];
    unsigned long unix_ts;
    int reply_count;
    Reaction reactions[kMaxReactions];
    int reaction_count;
} Message;

typedef struct Channel {
    char id[kNameLen];
    char name[kNameLen];
} Channel;

typedef struct User {
    char id[kNameLen];
    char name[kNameLen];
} User;

typedef struct Bridge {
    char token[256];
    char self_id[kNameLen];
    char self_name[kNameLen];

    Channel channels[kMaxChannels];
    int channel_count;

    char channel_id[kNameLen];
    char channel_name[kNameLen];
    char thread_ts[kTsLen]; /* empty = channel view */
    int in_thread;

    Message messages[kMaxMessages];
    int message_count;
    int view_off; /* newest=0; higher = older */
    time_t cache_time;

    User users[kMaxUsers];
    int user_count;
} Bridge;

static Bridge g;

/* --- tiny JSON helpers (same spirit as mac/common/mini_json) --- */

static const char *find_key(const char *json, const char *key) {
    char pat[96];
    const char *p;
    int klen = (int)strlen(key);
    if (klen > 80 || !json) {
        return NULL;
    }
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    p = json;
    while ((p = strstr(p, pat)) != NULL) {
        const char *q = p + strlen(pat);
        while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') {
            q++;
        }
        if (*q == ':') {
            q++;
            while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') {
                q++;
            }
            return q;
        }
        p++;
    }
    return NULL;
}

static int json_find_string(const char *json, const char *key, char *out, int out_sz) {
    const char *v = find_key(json, key);
    int i = 0;
    if (!v || !out || out_sz < 2 || *v != '"') {
        return 0;
    }
    v++;
    while (*v && *v != '"' && i < out_sz - 1) {
        if (*v == '\\' && v[1]) {
            v++;
            if (*v == 'n') {
                out[i++] = '\n';
            } else if (*v == 't') {
                out[i++] = '\t';
            } else {
                out[i++] = *v;
            }
            v++;
            continue;
        }
        out[i++] = *v++;
    }
    out[i] = '\0';
    return 1;
}

static int json_find_raw(const char *json, const char *key, char *out, int out_sz) {
    const char *v = find_key(json, key);
    int i = 0;
    if (!v || !out || out_sz < 2) {
        return 0;
    }
    if (*v == '"') {
        return json_find_string(json, key, out, out_sz);
    }
    while (*v && *v != ',' && *v != '}' && *v != ']' && i < out_sz - 1) {
        if (*v != ' ' && *v != '\n' && *v != '\r' && *v != '\t') {
            out[i++] = *v;
        }
        v++;
    }
    out[i] = '\0';
    return i > 0;
}

typedef void (*json_object_cb)(const char *obj, int obj_len, void *userdata);

static int json_foreach_object_in_array(const char *json, const char *array_key,
                                        json_object_cb cb, void *userdata) {
    const char *v = find_key(json, array_key);
    const char *p;
    int count = 0;
    if (!v || *v != '[' || !cb) {
        return 0;
    }
    p = v + 1;
    while (*p) {
        while (*p == ' ' || *p == '\n' || *p == ',' || *p == '\r' || *p == '\t') {
            p++;
        }
        if (*p == ']') {
            break;
        }
        if (*p == '{') {
            const char *start = p;
            int depth = 0;
            do {
                if (*p == '{') {
                    depth++;
                } else if (*p == '}') {
                    depth--;
                } else if (*p == '"') {
                    p++;
                    while (*p && *p != '"') {
                        if (*p == '\\' && p[1]) {
                            p += 2;
                            continue;
                        }
                        p++;
                    }
                }
                if (*p) {
                    p++;
                }
            } while (*p && depth > 0);
            cb(start, (int)(p - start), userdata);
            count++;
            continue;
        }
        while (*p && *p != ',' && *p != ']') {
            p++;
        }
    }
    return count;
}

/* --- URL / query helpers --- */

int slack_bridge_is_rs_url(const char *url) {
    if (!url) {
        return 0;
    }
    return strncasecmp(url, RS_URL_PREFIX, strlen(RS_URL_PREFIX)) == 0
        || strncasecmp(url, "http://rs?", 10) == 0
        || strcasecmp(url, "http://rs") == 0;
}

static const char *rs_path(const char *url) {
    static char pathbuf[128];
    const char *p;
    size_t n = 0;

    if (!url) {
        return "/";
    }
    p = strstr(url, "://");
    if (!p) {
        return "/";
    }
    p += 3;
    while (*p && *p != '/' && *p != '?') {
        p++;
    }
    if (*p != '/') {
        return "/";
    }
    /* Copy path only — strip ?query so /nav?dir=older matches /nav. */
    while (*p && *p != '?' && n + 1 < sizeof(pathbuf)) {
        pathbuf[n++] = *p++;
    }
    pathbuf[n] = '\0';
    return pathbuf[0] ? pathbuf : "/";
}

static const char *rs_query(const char *url) {
    const char *q = url ? strchr(url, '?') : NULL;
    return q ? q + 1 : "";
}

static int query_get(const char *query, const char *key, char *out, int out_sz) {
    size_t klen = strlen(key);
    const char *p = query;
    if (!query || !key || !out || out_sz < 2) {
        return 0;
    }
    while (*p) {
        if ((p == query || p[-1] == '&')
            && strncmp(p, key, klen) == 0 && p[klen] == '=') {
            const char *v = p + klen + 1;
            int i = 0;
            while (*v && *v != '&' && i < out_sz - 1) {
                if (*v == '%' && isxdigit((unsigned char)v[1])
                    && isxdigit((unsigned char)v[2])) {
                    char hex[3] = {v[1], v[2], 0};
                    out[i++] = (char)strtol(hex, NULL, 16);
                    v += 3;
                } else if (*v == '+') {
                    out[i++] = ' ';
                    v++;
                } else {
                    out[i++] = *v++;
                }
            }
            out[i] = '\0';
            return 1;
        }
        p++;
    }
    out[0] = '\0';
    return 0;
}

static int query_get_int(const char *query, const char *key, int def) {
    char buf[32];
    if (!query_get(query, key, buf, sizeof(buf)) || !buf[0]) {
        return def;
    }
    return atoi(buf);
}

/* --- response builders --- */

static int fill_http_ok(HttpResponse *out, const char *body) {
    char hdr[32];
    size_t blen = body ? strlen(body) : 0;
    int n;

    memset(out, 0, sizeof(*out));
    n = snprintf(hdr, sizeof(hdr), "STATUS 200\n");
    out->status = 200;
    out->headers = strdup(hdr);
    out->headers_len = (size_t)n;
    out->body = (uint8_t *)malloc(blen + 1);
    if (!out->headers || !out->body) {
        http_response_free(out);
        out->error = strdup("oom");
        return -1;
    }
    if (body) {
        memcpy(out->body, body, blen);
    }
    out->body[blen] = '\0';
    out->body_len = blen;
    return 0;
}

static int fill_http_err(HttpResponse *out, long status, const char *msg) {
    char hdr[32];
    char body[256];
    int n;
    size_t blen;

    memset(out, 0, sizeof(*out));
    n = snprintf(hdr, sizeof(hdr), "STATUS %ld\n", status);
    snprintf(body, sizeof(body), "{\"ok\":false,\"error\":\"%.80s\"}",
             msg ? msg : "error");
    blen = strlen(body);
    out->status = status;
    out->headers = strdup(hdr);
    out->headers_len = (size_t)n;
    out->body = (uint8_t *)malloc(blen + 1);
    if (!out->headers || !out->body) {
        http_response_free(out);
        out->error = strdup("oom");
        return -1;
    }
    memcpy(out->body, body, blen + 1);
    out->body_len = blen;
    return 0;
}

struct Grow {
    char *data;
    size_t len;
    size_t cap;
};

static int grow_printf(struct Grow *g, const char *fmt, ...) {
    va_list ap;
    va_list ap2;
    int need;
    char *nd;

    va_start(ap, fmt);
    va_copy(ap2, ap);
    need = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (need < 0) {
        va_end(ap2);
        return -1;
    }
    if (g->len + (size_t)need + 1 > g->cap) {
        size_t ncap = g->cap ? g->cap * 2 : 1024;
        while (ncap < g->len + (size_t)need + 1) {
            ncap *= 2;
        }
        nd = realloc(g->data, ncap);
        if (!nd) {
            va_end(ap2);
            return -1;
        }
        g->data = nd;
        g->cap = ncap;
    }
    vsnprintf(g->data + g->len, g->cap - g->len, fmt, ap2);
    va_end(ap2);
    g->len += (size_t)need;
    return 0;
}

static void json_escape_append(struct Grow *g, const char *s) {
    if (!s) {
        return;
    }
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') {
            grow_printf(g, "\\%c", *s);
        } else if (*s == '\n') {
            grow_printf(g, "\\n");
        } else if (*s == '\r') {
            /* skip */
        } else {
            grow_printf(g, "%c", *s);
        }
    }
}

static void append_msg_json(struct Grow *g, const Message *m) {
    int i;
    grow_printf(g, "{\"u\":\"");
    json_escape_append(g, m->user);
    grow_printf(g, "\",\"uid\":\"");
    json_escape_append(g, m->user_id);
    grow_printf(g, "\",\"t\":\"");
    json_escape_append(g, m->text);
    grow_printf(g, "\",\"ts\":\"");
    json_escape_append(g, m->ts);
    grow_printf(g, "\",\"tt\":\"");
    json_escape_append(g, m->thread_ts);
    grow_printf(g, "\",\"n\":%lu,\"rc\":%d,\"r\":[",
                m->unix_ts, m->reply_count);
    for (i = 0; i < m->reaction_count; i++) {
        if (i) {
            grow_printf(g, ",");
        }
        grow_printf(g, "{\"n\":\"");
        json_escape_append(g, m->reactions[i].name);
        grow_printf(g, "\",\"c\":%d,\"m\":%d}",
                    m->reactions[i].count, m->reactions[i].mine ? 1 : 0);
    }
    grow_printf(g, "]}");
}

/* --- Slack HTTP --- */

static int slack_http(const char *method, const char *url,
                      const char *json_body, HttpResponse *out) {
    const char *extra = "Header: Content-Type: application/json; charset=utf-8\n";
    if (!g.token[0]) {
        out->error = strdup("no Slack token on NSE");
        return -1;
    }
    return http_request(method, url, extra,
                        json_body, json_body ? strlen(json_body) : 0,
                        g.token, out);
}

static int slack_ok(const HttpResponse *resp) {
    char ok[16];
    if (!resp || !resp->body || resp->body_len == 0) {
        return 0;
    }
    if (!json_find_raw((const char *)resp->body, "ok", ok, sizeof(ok))) {
        return 0;
    }
    return strcmp(ok, "true") == 0;
}

static unsigned long parse_slack_ts(const char *s) {
    unsigned long v = 0;
    if (!s) {
        return 0;
    }
    while (*s >= '0' && *s <= '9') {
        v = v * 10UL + (unsigned long)(*s - '0');
        s++;
    }
    return v;
}

static void cache_user(const char *id, const char *name) {
    int i;
    if (!id || !id[0] || !name || !name[0]) {
        return;
    }
    for (i = 0; i < g.user_count; i++) {
        if (strcmp(g.users[i].id, id) == 0) {
            strncpy(g.users[i].name, name, kNameLen - 1);
            g.users[i].name[kNameLen - 1] = '\0';
            return;
        }
    }
    if (g.user_count >= kMaxUsers) {
        return;
    }
    strncpy(g.users[g.user_count].id, id, kNameLen - 1);
    strncpy(g.users[g.user_count].name, name, kNameLen - 1);
    g.users[g.user_count].id[kNameLen - 1] = '\0';
    g.users[g.user_count].name[kNameLen - 1] = '\0';
    g.user_count++;
}

static const char *cached_user(const char *id) {
    int i;
    for (i = 0; i < g.user_count; i++) {
        if (strcmp(g.users[i].id, id) == 0) {
            return g.users[i].name;
        }
    }
    return NULL;
}

static int pick_display_name(const char *json, char *out, int out_sz) {
    if (json_find_string(json, "display_name", out, out_sz) && out[0]) {
        return 1;
    }
    if (json_find_string(json, "real_name", out, out_sz) && out[0]) {
        return 1;
    }
    if (json_find_string(json, "name", out, out_sz) && out[0]) {
        return 1;
    }
    return 0;
}

static void resolve_user(const char *id, char *out, int out_sz) {
    const char *hit;
    HttpResponse resp;
    char url[160];
    char name[kNameLen];

    if (!id || !id[0] || !out || out_sz < 2) {
        return;
    }
    hit = cached_user(id);
    if (hit) {
        strncpy(out, hit, out_sz - 1);
        out[out_sz - 1] = '\0';
        return;
    }
    strncpy(out, id, out_sz - 1);
    out[out_sz - 1] = '\0';

    snprintf(url, sizeof(url), "https://slack.com/api/users.info?user=%s", id);
    if (slack_http("GET", url, NULL, &resp) != 0) {
        http_response_free(&resp);
        return;
    }
    if (slack_ok(&resp) && pick_display_name((const char *)resp.body, name, sizeof(name))) {
        cache_user(id, name);
        strncpy(out, name, out_sz - 1);
        out[out_sz - 1] = '\0';
    }
    http_response_free(&resp);
}

static int reaction_includes_me(const char *obj) {
    const char *p;
    char id[kNameLen];
    int n;

    if (!obj || !g.self_id[0]) {
        return 0;
    }
    p = strstr(obj, "\"users\"");
    if (!p) {
        return 0;
    }
    while (*p && *p != '[') {
        p++;
    }
    if (*p != '[') {
        return 0;
    }
    p++;
    while (*p && *p != ']') {
        while (*p == ' ' || *p == ',' || *p == '\n' || *p == '\r' || *p == '\t') {
            p++;
        }
        if (*p == ']') {
            break;
        }
        if (*p != '"') {
            p++;
            continue;
        }
        p++;
        n = 0;
        while (*p && *p != '"' && n < kNameLen - 1) {
            id[n++] = *p++;
        }
        id[n] = '\0';
        if (*p == '"') {
            p++;
        }
        if (strcmp(id, g.self_id) == 0) {
            return 1;
        }
    }
    return 0;
}

static void on_reaction(const char *obj, int obj_len, void *userdata) {
    Message *m = userdata;
    char tmp[384];
    char name[kReactNameLen];
    char countbuf[16];
    int count = 0;

    if (!m || m->reaction_count >= kMaxReactions) {
        return;
    }
    if (obj_len >= (int)sizeof(tmp)) {
        obj_len = (int)sizeof(tmp) - 1;
    }
    memcpy(tmp, obj, (size_t)obj_len);
    tmp[obj_len] = '\0';
    if (!json_find_string(tmp, "name", name, sizeof(name)) || !name[0]) {
        return;
    }
    if (json_find_raw(tmp, "count", countbuf, sizeof(countbuf))) {
        count = atoi(countbuf);
    }
    if (count <= 0) {
        count = 1;
    }
    strncpy(m->reactions[m->reaction_count].name, name, kReactNameLen - 1);
    m->reactions[m->reaction_count].name[kReactNameLen - 1] = '\0';
    m->reactions[m->reaction_count].count = count;
    m->reactions[m->reaction_count].mine = reaction_includes_me(tmp);
    m->reaction_count++;
}

static void on_message(const char *obj, int obj_len, void *userdata) {
    Message *m;
    char tmp[4096];
    char replybuf[16];
    (void)userdata;

    if (g.message_count >= kMaxMessages) {
        return;
    }
    if (obj_len >= (int)sizeof(tmp)) {
        obj_len = (int)sizeof(tmp) - 1;
    }
    memcpy(tmp, obj, (size_t)obj_len);
    tmp[obj_len] = '\0';

    m = &g.messages[g.message_count];
    memset(m, 0, sizeof(*m));

    if (!json_find_string(tmp, "text", m->text, kTextLen)) {
        m->text[0] = '\0';
    }
    if (json_find_string(tmp, "ts", m->ts, kTsLen)) {
        m->unix_ts = parse_slack_ts(m->ts);
    }
    json_find_string(tmp, "thread_ts", m->thread_ts, kTsLen);
    if (json_find_raw(tmp, "reply_count", replybuf, sizeof(replybuf))) {
        m->reply_count = atoi(replybuf);
    }
    if (!json_find_string(tmp, "user", m->user_id, kNameLen)) {
        m->user_id[0] = '\0';
        if (!json_find_string(tmp, "username", m->user, kNameLen)) {
            strcpy(m->user, "?");
        }
    } else {
        resolve_user(m->user_id, m->user, kNameLen);
    }
    json_foreach_object_in_array(tmp, "reactions", on_reaction, m);
    g.message_count++;
}

static void on_channel(const char *obj, int obj_len, void *userdata) {
    char tmp[512];
    char id[kNameLen];
    char name[kNameLen];
    (void)userdata;
    if (g.channel_count >= kMaxChannels) {
        return;
    }
    if (obj_len >= (int)sizeof(tmp)) {
        obj_len = (int)sizeof(tmp) - 1;
    }
    memcpy(tmp, obj, (size_t)obj_len);
    tmp[obj_len] = '\0';
    if (!json_find_string(tmp, "id", id, sizeof(id))) {
        return;
    }
    if (!json_find_string(tmp, "name", name, sizeof(name))) {
        return;
    }
    strncpy(g.channels[g.channel_count].id, id, kNameLen - 1);
    strncpy(g.channels[g.channel_count].name, name, kNameLen - 1);
    g.channels[g.channel_count].id[kNameLen - 1] = '\0';
    g.channels[g.channel_count].name[kNameLen - 1] = '\0';
    g.channel_count++;
}

static int clamp_off(int off) {
    int max_off;
    if (g.message_count <= 0) {
        return 0;
    }
    max_off = g.message_count - 1;
    if (off < 0) {
        return 0;
    }
    if (off > max_off) {
        return max_off;
    }
    return off;
}

static int max_view_off(int count) {
    if (g.message_count <= count) {
        return 0;
    }
    return g.message_count - count;
}

static int refresh_messages(int force) {
    HttpResponse resp;
    char url[320];
    time_t now = time(NULL);

    if (!g.channel_id[0]) {
        return -1;
    }
    if (!force && g.cache_time != 0
        && (now - g.cache_time) < kCacheRefreshSec) {
        return 0;
    }

    g.message_count = 0;
    if (g.in_thread && g.thread_ts[0]) {
        snprintf(url, sizeof(url),
                 "https://slack.com/api/conversations.replies?channel=%s&ts=%s&limit=%d",
                 g.channel_id, g.thread_ts, kHistoryLimit);
    } else {
        snprintf(url, sizeof(url),
                 "https://slack.com/api/conversations.history?channel=%s&limit=%d",
                 g.channel_id, kHistoryLimit);
    }

    if (slack_http("GET", url, NULL, &resp) != 0) {
        http_response_free(&resp);
        return -1;
    }
    if (!slack_ok(&resp)) {
        http_response_free(&resp);
        return -1;
    }
    json_foreach_object_in_array((const char *)resp.body, "messages",
                                 on_message, NULL);
    http_response_free(&resp);
    g.cache_time = now;
    g.view_off = clamp_off(g.view_off);
    if (g.view_off > max_view_off(kDefaultView)) {
        g.view_off = max_view_off(kDefaultView);
    }
    return 0;
}

static int ensure_auth(void) {
    HttpResponse resp;
    char name[kNameLen];

    if (g.self_id[0]) {
        return 0;
    }
    if (slack_http("GET", "https://slack.com/api/auth.test", NULL, &resp) != 0) {
        http_response_free(&resp);
        return -1;
    }
    if (!slack_ok(&resp)) {
        http_response_free(&resp);
        return -1;
    }
    json_find_string((const char *)resp.body, "user_id", g.self_id, sizeof(g.self_id));
    if (json_find_string((const char *)resp.body, "user", name, sizeof(name)) && name[0]) {
        strncpy(g.self_name, name, kNameLen - 1);
        g.self_name[kNameLen - 1] = '\0';
    } else {
        strcpy(g.self_name, "me");
    }
    if (g.self_id[0]) {
        cache_user(g.self_id, g.self_name);
    }
    http_response_free(&resp);
    return 0;
}

static int load_channels(void) {
    HttpResponse resp;
    const char *url =
        "https://slack.com/api/conversations.list?limit=32&exclude_archived=true"
        "&types=public_channel,private_channel";

    g.channel_count = 0;
    if (slack_http("GET", url, NULL, &resp) != 0) {
        http_response_free(&resp);
        return -1;
    }
    if (!slack_ok(&resp)) {
        http_response_free(&resp);
        return -1;
    }
    json_foreach_object_in_array((const char *)resp.body, "channels",
                                 on_channel, NULL);
    http_response_free(&resp);
    return 0;
}

static void find_channel_name(const char *id, char *out, int out_sz) {
    int i;
    out[0] = '\0';
    for (i = 0; i < g.channel_count; i++) {
        if (strcmp(g.channels[i].id, id) == 0) {
            strncpy(out, g.channels[i].name, out_sz - 1);
            out[out_sz - 1] = '\0';
            return;
        }
    }
    strncpy(out, id, out_sz - 1);
    out[out_sz - 1] = '\0';
}

static int build_view_body(struct Grow *gbuf, int off, int count) {
    int i;
    int n = 0;

    if (count < 1) {
        count = kDefaultView;
    }
    if (count > kDefaultView) {
        count = kDefaultView;
    }
    off = clamp_off(off);
    if (off > max_view_off(count)) {
        off = max_view_off(count);
    }
    g.view_off = off;

    grow_printf(gbuf,
                "{\"ok\":true,\"ch\":\"%s\",\"th\":%d,\"total\":%d,\"off\":%d,\"n\":",
                g.channel_name[0] ? g.channel_name : g.channel_id,
                g.in_thread ? 1 : 0, g.message_count, off);
    /* count msgs we'll emit */
    for (i = off; i < g.message_count && n < count; i++) {
        n++;
    }
    grow_printf(gbuf, "%d,\"msgs\":[", n);
    n = 0;
    for (i = off; i < g.message_count && n < count; i++) {
        if (n) {
            grow_printf(gbuf, ",");
        }
        append_msg_json(gbuf, &g.messages[i]);
        n++;
    }
    grow_printf(gbuf, "]}");
    return 0;
}

/* --- route handlers --- */

static int handle_auth(HttpResponse *out) {
    struct Grow gbuf = {0};
    if (ensure_auth() != 0) {
        return fill_http_err(out, 502, "auth.test failed");
    }
    grow_printf(&gbuf, "{\"ok\":true,\"uid\":\"%s\",\"user\":\"%s\"}",
                g.self_id, g.self_name);
    if (fill_http_ok(out, gbuf.data) != 0) {
        free(gbuf.data);
        return -1;
    }
    free(gbuf.data);
    return 0;
}

static int handle_channels(HttpResponse *out) {
    struct Grow gbuf = {0};
    int i;
    if (ensure_auth() != 0) {
        return fill_http_err(out, 502, "auth failed");
    }
    if (load_channels() != 0) {
        return fill_http_err(out, 502, "channels.list failed");
    }
    grow_printf(&gbuf, "{\"ok\":true,\"channels\":[");
    for (i = 0; i < g.channel_count; i++) {
        if (i) {
            grow_printf(&gbuf, ",");
        }
        grow_printf(&gbuf, "{\"id\":\"%s\",\"name\":\"%s\"}",
                    g.channels[i].id, g.channels[i].name);
    }
    grow_printf(&gbuf, "]}");
    if (fill_http_ok(out, gbuf.data) != 0) {
        free(gbuf.data);
        return -1;
    }
    free(gbuf.data);
    return 0;
}

static int handle_open(const void *body, size_t body_len, HttpResponse *out) {
    char tmp[512];
    char channel[kNameLen];
    char thread[kTsLen];
    struct Grow gbuf = {0};
    size_t n = body_len;

    if (ensure_auth() != 0) {
        return fill_http_err(out, 502, "auth failed");
    }
    if (n >= sizeof(tmp)) {
        n = sizeof(tmp) - 1;
    }
    if (body && n > 0) {
        memcpy(tmp, body, n);
        tmp[n] = '\0';
    } else {
        tmp[0] = '\0';
    }
    if (!json_find_string(tmp, "channel", channel, sizeof(channel)) || !channel[0]) {
        return fill_http_err(out, 400, "channel required");
    }
    thread[0] = '\0';
    json_find_string(tmp, "thread", thread, sizeof(thread));

    strncpy(g.channel_id, channel, kNameLen - 1);
    g.channel_id[kNameLen - 1] = '\0';
    find_channel_name(channel, g.channel_name, sizeof(g.channel_name));
    if (thread[0]) {
        g.in_thread = 1;
        strncpy(g.thread_ts, thread, kTsLen - 1);
        g.thread_ts[kTsLen - 1] = '\0';
    } else {
        g.in_thread = 0;
        g.thread_ts[0] = '\0';
    }
    g.view_off = 0;
    g.cache_time = 0;
    if (refresh_messages(1) != 0) {
        return fill_http_err(out, 502, "history failed");
    }
    build_view_body(&gbuf, 0, kDefaultView);
    if (fill_http_ok(out, gbuf.data) != 0) {
        free(gbuf.data);
        return -1;
    }
    free(gbuf.data);
    return 0;
}

static int handle_view(const char *query, HttpResponse *out) {
    int off = query_get_int(query, "off", g.view_off);
    int count = query_get_int(query, "n", kDefaultView);
    struct Grow gbuf = {0};

    if (!g.channel_id[0]) {
        return fill_http_err(out, 400, "no channel open");
    }
    if (refresh_messages(0) != 0) {
        return fill_http_err(out, 502, "refresh failed");
    }
    build_view_body(&gbuf, off, count);
    if (fill_http_ok(out, gbuf.data) != 0) {
        free(gbuf.data);
        return -1;
    }
    free(gbuf.data);
    return 0;
}

/* Shift viewport by one message; return only the newly visible edge message. */
static int handle_nav(const char *query, HttpResponse *out) {
    char dir[16];
    int count = query_get_int(query, "n", kDefaultView);
    int new_off;
    int edge;
    struct Grow gbuf = {0};

    if (!g.channel_id[0]) {
        return fill_http_err(out, 400, "no channel open");
    }
    if (count < 1) {
        count = kDefaultView;
    }
    if (refresh_messages(0) != 0) {
        return fill_http_err(out, 502, "refresh failed");
    }

    query_get(query, "dir", dir, sizeof(dir));
    new_off = g.view_off;
    if (strcmp(dir, "older") == 0 || strcmp(dir, "next") == 0) {
        if (g.view_off >= max_view_off(count)) {
            grow_printf(&gbuf,
                        "{\"ok\":true,\"moved\":0,\"off\":%d,\"total\":%d,\"at_end\":1}",
                        g.view_off, g.message_count);
            if (fill_http_ok(out, gbuf.data) != 0) {
                free(gbuf.data);
                return -1;
            }
            free(gbuf.data);
            return 0;
        }
        new_off = g.view_off + 1;
        /* New message entering at the older end of the viewport. */
        edge = new_off + count - 1;
    } else if (strcmp(dir, "newer") == 0 || strcmp(dir, "prev") == 0) {
        if (g.view_off <= 0) {
            grow_printf(&gbuf,
                        "{\"ok\":true,\"moved\":0,\"off\":%d,\"total\":%d,\"at_end\":1}",
                        g.view_off, g.message_count);
            if (fill_http_ok(out, gbuf.data) != 0) {
                free(gbuf.data);
                return -1;
            }
            free(gbuf.data);
            return 0;
        }
        new_off = g.view_off - 1;
        edge = new_off; /* new message at newer end */
    } else {
        return fill_http_err(out, 400, "dir=older|newer required");
    }

    if (edge < 0 || edge >= g.message_count) {
        return fill_http_err(out, 404, "no message");
    }
    g.view_off = new_off;
    grow_printf(&gbuf,
                "{\"ok\":true,\"moved\":1,\"off\":%d,\"total\":%d,\"at_end\":0,\"edge\":%d,\"msg\":",
                g.view_off, g.message_count,
                (strcmp(dir, "older") == 0 || strcmp(dir, "next") == 0) ? 1 : 0);
    append_msg_json(&gbuf, &g.messages[edge]);
    grow_printf(&gbuf, "}");
    if (fill_http_ok(out, gbuf.data) != 0) {
        free(gbuf.data);
        return -1;
    }
    free(gbuf.data);
    return 0;
}

static int handle_msg(const char *query, HttpResponse *out) {
    int i = query_get_int(query, "i", -1);
    struct Grow gbuf = {0};

    if (!g.channel_id[0]) {
        return fill_http_err(out, 400, "no channel open");
    }
    if (refresh_messages(0) != 0) {
        return fill_http_err(out, 502, "refresh failed");
    }
    if (i < 0 || i >= g.message_count) {
        return fill_http_err(out, 404, "index out of range");
    }
    grow_printf(&gbuf, "{\"ok\":true,\"i\":%d,\"total\":%d,\"msg\":",
                i, g.message_count);
    append_msg_json(&gbuf, &g.messages[i]);
    grow_printf(&gbuf, "}");
    if (fill_http_ok(out, gbuf.data) != 0) {
        free(gbuf.data);
        return -1;
    }
    free(gbuf.data);
    return 0;
}

static void escape_json_text(const char *in, char *out, size_t out_sz) {
    size_t j = 0;
    size_t i;
    for (i = 0; in && in[i] && j + 2 < out_sz; i++) {
        if (in[i] == '"' || in[i] == '\\') {
            out[j++] = '\\';
            out[j++] = in[i];
        } else if (in[i] == '\n') {
            out[j++] = '\\';
            out[j++] = 'n';
        } else {
            out[j++] = in[i];
        }
    }
    out[j] = '\0';
}

static int handle_post(const void *body, size_t body_len, HttpResponse *out) {
    char tmp[512];
    char text[kTextLen];
    char esc[kTextLen * 2];
    char json[768];
    HttpResponse resp;
    struct Grow gbuf = {0};
    size_t n = body_len;
    char tsbuf[kTsLen];

    if (!g.channel_id[0]) {
        return fill_http_err(out, 400, "no channel open");
    }
    if (ensure_auth() != 0) {
        return fill_http_err(out, 502, "auth failed");
    }
    if (n >= sizeof(tmp)) {
        n = sizeof(tmp) - 1;
    }
    if (body && n > 0) {
        memcpy(tmp, body, n);
        tmp[n] = '\0';
    } else {
        tmp[0] = '\0';
    }
    if (!json_find_string(tmp, "text", text, sizeof(text)) || !text[0]) {
        return fill_http_err(out, 400, "text required");
    }
    escape_json_text(text, esc, sizeof(esc));
    if (g.in_thread && g.thread_ts[0]) {
        snprintf(json, sizeof(json),
                 "{\"channel\":\"%s\",\"text\":\"%s\",\"thread_ts\":\"%s\"}",
                 g.channel_id, esc, g.thread_ts);
    } else {
        snprintf(json, sizeof(json),
                 "{\"channel\":\"%s\",\"text\":\"%s\"}",
                 g.channel_id, esc);
    }
    if (slack_http("POST", "https://slack.com/api/chat.postMessage", json, &resp) != 0) {
        http_response_free(&resp);
        return fill_http_err(out, 502, "post failed");
    }
    if (!slack_ok(&resp)) {
        char err[64];
        if (!json_find_string((const char *)resp.body, "error", err, sizeof(err))) {
            strcpy(err, "rejected");
        }
        http_response_free(&resp);
        return fill_http_err(out, 502, err);
    }
    tsbuf[0] = '\0';
    json_find_string((const char *)resp.body, "ts", tsbuf, sizeof(tsbuf));
    http_response_free(&resp);

    /* Force cache refresh so the new message is available. */
    g.cache_time = 0;
    refresh_messages(1);
    if (!g.in_thread) {
        g.view_off = 0;
    } else {
        g.view_off = max_view_off(kDefaultView);
    }
    build_view_body(&gbuf, g.view_off, kDefaultView);
    /* Annotate with posted ts for Mac if needed — already in msgs. */
    (void)tsbuf;
    if (fill_http_ok(out, gbuf.data) != 0) {
        free(gbuf.data);
        return -1;
    }
    free(gbuf.data);
    return 0;
}

static void normalize_emoji_name(char *name) {
    char *s = name;
    char *d = name;
    int len;
    if (!name) {
        return;
    }
    while (*s == ':' || *s == ' ' || *s == '\t') {
        s++;
    }
    while (*s) {
        *d++ = *s++;
    }
    *d = '\0';
    len = (int)strlen(name);
    while (len > 0 && (name[len - 1] == ':' || name[len - 1] == ' '
                       || name[len - 1] == '\t')) {
        name[--len] = '\0';
    }
}

static int handle_react(const void *body, size_t body_len, HttpResponse *out) {
    char tmp[512];
    char ts[kTsLen];
    char name[kReactNameLen];
    char addbuf[16];
    char json[256];
    HttpResponse resp;
    struct Grow gbuf = {0};
    size_t n = body_len;
    int want_add = 1;
    const char *url;

    if (!g.channel_id[0]) {
        return fill_http_err(out, 400, "no channel open");
    }
    if (ensure_auth() != 0) {
        return fill_http_err(out, 502, "auth failed");
    }
    if (n >= sizeof(tmp)) {
        n = sizeof(tmp) - 1;
    }
    if (body && n > 0) {
        memcpy(tmp, body, n);
        tmp[n] = '\0';
    } else {
        tmp[0] = '\0';
    }
    if (!json_find_string(tmp, "ts", ts, sizeof(ts)) || !ts[0]) {
        return fill_http_err(out, 400, "ts required");
    }
    if (!json_find_string(tmp, "name", name, sizeof(name)) || !name[0]) {
        return fill_http_err(out, 400, "name required");
    }
    normalize_emoji_name(name);
    if (json_find_raw(tmp, "add", addbuf, sizeof(addbuf))) {
        want_add = (strcmp(addbuf, "true") == 0 || strcmp(addbuf, "1") == 0);
    }
    snprintf(json, sizeof(json),
             "{\"channel\":\"%s\",\"timestamp\":\"%s\",\"name\":\"%s\"}",
             g.channel_id, ts, name);
    url = want_add ? "https://slack.com/api/reactions.add"
                   : "https://slack.com/api/reactions.remove";
    if (slack_http("POST", url, json, &resp) != 0) {
        http_response_free(&resp);
        return fill_http_err(out, 502, "react failed");
    }
    if (!slack_ok(&resp)) {
        char err[64];
        if (!json_find_string((const char *)resp.body, "error", err, sizeof(err))) {
            strcpy(err, "rejected");
        }
        /* already_reacted / no_reaction are soft — still refresh */
        if (strcmp(err, "already_reacted") != 0 && strcmp(err, "no_reaction") != 0) {
            http_response_free(&resp);
            return fill_http_err(out, 502, err);
        }
    }
    http_response_free(&resp);
    g.cache_time = 0;
    refresh_messages(1);
    build_view_body(&gbuf, g.view_off, kDefaultView);
    if (fill_http_ok(out, gbuf.data) != 0) {
        free(gbuf.data);
        return -1;
    }
    free(gbuf.data);
    return 0;
}

void slack_bridge_init(const char *token) {
    memset(&g, 0, sizeof(g));
    if (token && token[0]) {
        strncpy(g.token, token, sizeof(g.token) - 1);
    }
    strcpy(g.self_name, "me");
}

void slack_bridge_shutdown(void) {
    memset(&g, 0, sizeof(g));
}

int slack_bridge_handle(const char *method, const char *url,
                        const void *body, size_t body_len,
                        HttpResponse *out) {
    const char *path = rs_path(url);
    const char *query = rs_query(url);
    char m[16];
    size_t i;

    memset(out, 0, sizeof(*out));
    if (!method) {
        method = "GET";
    }
    for (i = 0; method[i] && i + 1 < sizeof(m); i++) {
        m[i] = (char)toupper((unsigned char)method[i]);
    }
    m[i] = '\0';

    /* Strip trailing slash for matching: /auth vs /auth/ */
    if (strcmp(path, "/auth") == 0 || strcmp(path, "/auth/") == 0) {
        return handle_auth(out);
    }
    if (strcmp(path, "/channels") == 0 || strcmp(path, "/channels/") == 0) {
        return handle_channels(out);
    }
    if (strcmp(path, "/open") == 0 || strcmp(path, "/open/") == 0) {
        if (strcmp(m, "POST") != 0) {
            return fill_http_err(out, 405, "POST required");
        }
        return handle_open(body, body_len, out);
    }
    if (strcmp(path, "/view") == 0 || strcmp(path, "/view/") == 0) {
        return handle_view(query, out);
    }
    if (strcmp(path, "/nav") == 0 || strcmp(path, "/nav/") == 0) {
        return handle_nav(query, out);
    }
    if (strcmp(path, "/msg") == 0 || strcmp(path, "/msg/") == 0) {
        return handle_msg(query, out);
    }
    if (strcmp(path, "/post") == 0 || strcmp(path, "/post/") == 0) {
        if (strcmp(m, "POST") != 0) {
            return fill_http_err(out, 405, "POST required");
        }
        return handle_post(body, body_len, out);
    }
    if (strcmp(path, "/react") == 0 || strcmp(path, "/react/") == 0) {
        if (strcmp(m, "POST") != 0) {
            return fill_http_err(out, 405, "POST required");
        }
        return handle_react(body, body_len, out);
    }
    return fill_http_err(out, 404, "unknown rs path");
}
