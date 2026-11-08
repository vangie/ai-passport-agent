#ifndef BADGE_JSON_H
#define BADGE_JSON_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BADGE_JSON_OUTPUT_MAX 6144
#define BADGE_JSON_DEPTH_MAX 32
/* Single-pass projection. Unused subtrees are validated and discarded without
 * allocating. Text is shortened only at UTF-8/escape boundaries; identity and
 * credential strings must fit in full. */
typedef struct {
    char output[BADGE_JSON_OUTPUT_MAX];
    size_t output_len;
    struct { uint8_t kind, state; bool keep, first; } frames[BADGE_JSON_DEPTH_MAX];
    unsigned depth;
    char key[48], primitive[64], unit[12];
    size_t key_len, primitive_len, unit_len, unit_need, string_len;
    bool key_invalid, in_string, is_key, keep_string, shorten_string, truncated;
    bool in_primitive, keep_primitive, started, done, failed;
} badge_json_filter_t;
void badge_json_filter_init(badge_json_filter_t *f);
bool badge_json_filter_feed(badge_json_filter_t *f, const char *data, size_t size);
int badge_json_filter_finish(const badge_json_filter_t *f, char *out, size_t cap);
#endif
