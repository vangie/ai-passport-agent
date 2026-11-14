#include "passport_endpoint.h"
#include <stdlib.h>
#include <string.h>

bool passport_endpoint_parse(const char *url, bool native, passport_endpoint_t *out) {
    if (!url || !out || strpbrk(url, "\r\n\t ?#")) return false;
    *out = (passport_endpoint_t){0};
    out->tls = strncmp(url, "wss://", 6) == 0;
    if (!out->tls && strncmp(url, "ws://", 5)) return false;
    const char *host = url + (out->tls ? 6 : 5);
    const char *end = strpbrk(host, "/:");
    if (!end) end = host + strlen(host);
    size_t length = (size_t)(end - host);
    if (!length || length >= sizeof(out->host) || memchr(host, '@', length)) return false;
    memcpy(out->host, host, length);
    out->port = out->tls ? 443 : 80;
    if (*end == ':') {
        if (end[1] < '0' || end[1] > '9') return false;
        char *after;
        long port = strtol(end + 1, &after, 10);
        if (port < 1 || port > 65535 || (*after && *after != '/')) return false;
        out->port = (int)port;
        end = after;
    }
    const char *path = *end ? end : "/";
    length = strlen(path);
    if (length >= sizeof(out->path)) return false;
    memcpy(out->path, path, length + 1);
    if (native) {
        const char suffix[] = "/passport/v1";
        while (length && out->path[length - 1] == '/') length--;
        if (length < sizeof(suffix) - 1 || memcmp(out->path + length - sizeof(suffix) + 1, suffix, sizeof(suffix) - 1)) {
            if (length + sizeof(suffix) > sizeof(out->path)) return false;
            memcpy(out->path + length, suffix, sizeof(suffix));
        } else out->path[length] = 0;
    }
    return true;
}
