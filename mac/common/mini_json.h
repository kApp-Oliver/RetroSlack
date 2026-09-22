#ifndef MINI_JSON_H
#define MINI_JSON_H

/* Tiny helpers for Slack JSON (no full parser). */

/* Find "key":"value" string; copies value into out (max out_sz-1). Returns 1 if found. */
int json_find_string(const char *json, const char *key, char *out, int out_sz);

/* Find "key":true/false/number into out as text. */
int json_find_raw(const char *json, const char *key, char *out, int out_sz);

/* Iterate array of objects: for each '{...}' at top level of array after key. */
typedef void (*json_object_cb)(const char *obj, int obj_len, void *userdata);
int json_foreach_object_in_array(const char *json, const char *array_key,
                                 json_object_cb cb, void *userdata);

#endif
