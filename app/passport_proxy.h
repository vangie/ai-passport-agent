#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PASSPORT_PROXY_HOST_MAX 64
#define PASSPORT_PROXY_SECRET_MAX 64

/* Shadowsocks cipher id stored on the device. Only aes-256-cfb is
 * supported; the byte keeps room for others without a format change. */
#define PASSPORT_SS_AES_256_CFB 0
#define PASSPORT_SS_METHODS 1

/* Shadowsocks route for one OpenClaw endpoint. A gateway that is reachable
 * directly (for example a domestic tunnel) simply leaves this disabled. */
typedef struct {
    char host[PASSPORT_PROXY_HOST_MAX];
    char secret[PASSPORT_PROXY_SECRET_MAX];
    uint16_t port;
    uint8_t method;
    bool enabled;
} passport_proxy_t;

/* Empty is valid. Otherwise needs a host name or IPv4, a port and a known
 * method; an enabled route also needs its password. */
bool passport_proxy_valid(const passport_proxy_t *proxy);
/* Shadowsocks method name for an id, or NULL. */
const char *passport_proxy_method_name(unsigned method);
/* Id for a Shadowsocks method name, or -1. */
int passport_proxy_method_id(const char *name);
