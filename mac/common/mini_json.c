#include "mini_json.h"

#include <stdio.h>
#include <string.h>

static const char *find_key(const char *json, const char *key) {
    char pat[96];
    const char *p;
    int klen = (int)strlen(key);
    if (klen > 80) {
        return NULL;
    }
    sprintf(pat, "\"%s\"", key);
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

int json_find_string(const char *json, const char *key, char *out, int out_sz) {
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

int json_find_raw(const char *json, const char *key, char *out, int out_sz) {
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

static const char *find_array(const char *json, const char *array_key) {
    const char *v = find_key(json, array_key);
    if (!v) {
        return NULL;
    }
    if (*v != '[') {
        return NULL;
    }
    return v + 1;
}

int json_foreach_object_in_array(const char *json, const char *array_key,
                                 json_object_cb cb, void *userdata) {
    const char *p = find_array(json, array_key);
    int count = 0;
    if (!p || !cb) {
        return 0;
    }
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
        /* skip scalars */
        while (*p && *p != ',' && *p != ']') {
            p++;
        }
    }
    return count;
}
