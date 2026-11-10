#include "passport_proxy.h"
#include <string.h>

static bool host_name(const char *host) {
    size_t label = 0;
    if (!host || !host[0] || strlen(host) >= PASSPORT_PROXY_HOST_MAX) return false;
    for (const char *p = host;; p++) {
        if (*p == '.' || *p == 0) {
            if (!label || label > 63) return false;
            if (*p == 0) return true;
            label = 0;
        } else {
            char c = *p;
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-'))
                return false;
            label++;
        }
    }
}

static const char *const k_methods[PASSPORT_SS_METHODS] = { "aes-256-cfb" };

const char *passport_proxy_method_name(unsigned method) {
    return method < PASSPORT_SS_METHODS ? k_methods[method] : NULL;
}

int passport_proxy_method_id(const char *name) {
    for (int i = 0; name && i < PASSPORT_SS_METHODS; i++)
        if (!strcmp(name, k_methods[i])) return i;
    return -1;
}

bool passport_proxy_valid(const passport_proxy_t *proxy) {
    if (!proxy || proxy->method >= PASSPORT_SS_METHODS) return false;
    if (!proxy->enabled && !proxy->host[0] && !proxy->port && !proxy->secret[0]) return true;
    if (!proxy->port || !host_name(proxy->host)) return false;
    return !proxy->enabled || proxy->secret[0];
}
