#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BADGE_PROXY_HOST_MAX 64
#define BADGE_PROXY_SECRET_MAX 64

/* Shadowsocks cipher id stored on the device. Only aes-256-cfb is
 * supported; the byte keeps room for others without a format change. */
#define BADGE_SS_AES_256_CFB 0
#define BADGE_SS_METHODS 1

/* Shadowsocks route for one OpenClaw endpoint. A gateway that is reachable
 * directly (for example a domestic tunnel) simply leaves this disabled. */
typedef struct {
    char host[BADGE_PROXY_HOST_MAX];
    char secret[BADGE_PROXY_SECRET_MAX];
    uint16_t port;
    uint8_t method;
    bool enabled;
} badge_proxy_t;

/* Empty is valid. Otherwise needs a host name or IPv4, a port and a known
 * method; an enabled route also needs its password. */
bool badge_proxy_valid(const badge_proxy_t *proxy);
/* Shadowsocks method name for an id, or NULL. */
const char *badge_proxy_method_name(unsigned method);
/* Id for a Shadowsocks method name, or -1. */
int badge_proxy_method_id(const char *name);
