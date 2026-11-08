#include "badge_pair.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int b64_value(int c) {
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 52;
    }
    if (c == '+' || c == '-') {
        return 62;
    }
    if (c == '/' || c == '_') {
        return 63;
    }
    return -1;
}

static int b64_decode(const char *src, uint8_t *out, size_t cap) {
    int val = 0;
    int valb = -8;
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)src; *p != '\0'; p++) {
        if (*p == '=' || isspace(*p)) {
            continue;
        }
        int d = b64_value(*p);
        if (d < 0) {
            return -1;
        }
        val = (val << 6) + d;
        valb += 6;
        if (valb >= 0) {
            if (n >= cap) {
                return -1;
            }
            out[n++] = (uint8_t)((val >> valb) & 0xff);
            valb -= 8;
        }
    }
    return (int)n;
}

static const char *json_string(const char *json, const char *key, char *out, size_t cap) {
    char needle[48];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *at = strstr(json, needle);
    if (at == NULL) {
        out[0] = '\0';
        return NULL;
    }
    at = strchr(at + strlen(needle), '"');
    if (at == NULL) {
        out[0] = '\0';
        return NULL;
    }
    at++;
    const char *end = strchr(at, '"');
    if (end == NULL || (size_t)(end - at) + 1 > cap) {
        out[0] = '\0';
        return NULL;
    }
    memcpy(out, at, (size_t)(end - at));
    out[end - at] = '\0';
    return out;
}

static const char *host_of(const char *url, char *out, size_t cap) {
    const char *start = strstr(url, "://");
    start = start != NULL ? start + 3 : url;
    size_t n = 0;
    while (start[n] != '\0' && start[n] != '/' && start[n] != ':' && start[n] != '?' && n + 1 < cap) {
        out[n] = start[n];
        n++;
    }
    out[n] = '\0';
    return n > 0 ? out : NULL;
}

const char *badge_pair_parse(const char *text, badge_pair_t *out) {
    char raw[BADGE_PAIR_MAX];
    uint8_t decoded[BADGE_PAIR_MAX];
    char url[BADGE_URL_MAX];
    char token[BADGE_TOKEN_MAX];
    if (out == NULL || text == NULL) {
        return "code";
    }
    memset(out, 0, sizeof(*out));
    while (*text == ' ' || *text == '\n' || *text == '\t') {
        text++;
    }
    if (strncmp(text, "oc-pair://", 10) == 0) {
        text += 10;
    }
    size_t n = 0;
    for (size_t i = 0; text[i] != '\0' && n + 1 < sizeof(raw); i++) {
        if (text[i] == ' ' || text[i] == '\n' || text[i] == '\r' || text[i] == '\t') {
            continue;
        }
        raw[n++] = text[i];
    }
    raw[n] = '\0';
    if (n < 8) {
        return "code";
    }
    int len = b64_decode(raw, decoded, sizeof(decoded) - 1);
    if (len < 2) {
        return "code";
    }
    decoded[len] = '\0';
    if (json_string((const char *)decoded, "url", url, sizeof(url)) == NULL ||
        (strncmp(url, "wss://", 6) != 0 && strncmp(url, "ws://", 5) != 0)) {
        return "url";
    }
    if (json_string((const char *)decoded, "bootstrapToken", token, sizeof(token)) == NULL ||
        token[0] == '\0') {
        return "token";
    }
    snprintf(out->url, sizeof(out->url), "%s", url);
    snprintf(out->bootstrap, sizeof(out->bootstrap), "%s", token);
    if (host_of(url, out->host, sizeof(out->host)) == NULL) {
        return "url";
    }
    return NULL;
}
