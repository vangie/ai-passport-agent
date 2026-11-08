#include "badge_ss.h"
#include "mbedtls/aes.h"
#include "mbedtls/md5.h"
#include <string.h>

/* Legacy Shadowsocks stream cipher (aes-256-cfb): IV, then a CFB stream.
 * Ciphertext and plaintext have the same length, so nothing is buffered. */

#define SS_KEY 32

typedef struct {
    mbedtls_aes_context aes;
    uint8_t key[SS_KEY];
    uint8_t enc_iv[16];
    uint8_t dec_iv[16];
    size_t dec_iv_have; /* the server's IV, read from the start of its stream */
    size_t enc_off;
    size_t dec_off;
    int fd;
    bool live;
} ss_session;

static ss_session g = { .fd = -1 };

static void wipe(void) {
    if (g.key[0] || g.live) mbedtls_aes_free(&g.aes);
    memset(&g, 0, sizeof(g));
    g.fd = -1;
}

bool badge_ss_method(const char *method) {
    return method && !strcmp(method, "aes-256-cfb");
}

static int evp_bytes(const char *password, uint8_t out[SS_KEY]) {
    uint8_t block[16], mix[16 + 128];
    size_t pass_len = password ? strlen(password) : 0, filled = 0;
    if (!pass_len || pass_len > 128) return -1;
    for (int round = 0; filled < SS_KEY; round++) {
        size_t n = 0;
        if (round) { memcpy(mix, block, 16); n = 16; }
        memcpy(mix + n, password, pass_len);
        if (mbedtls_md5(mix, n + pass_len, block) != 0) return -1;
        memcpy(out + filled, block, 16);
        filled += 16;
    }
    memset(block, 0, sizeof(block));
    memset(mix, 0, sizeof(mix));
    return 0;
}

static int stream(int encrypt, const uint8_t *in, size_t len, uint8_t *out) {
    uint8_t *iv = encrypt ? g.enc_iv : g.dec_iv;
    size_t *off = encrypt ? &g.enc_off : &g.dec_off;
    return mbedtls_aes_crypt_cfb128(&g.aes, encrypt ? MBEDTLS_AES_ENCRYPT : MBEDTLS_AES_DECRYPT,
                                    len, off, iv, in, out) == 0 ? 0 : -1;
}

int badge_ss_hello(const uint8_t salt[32], const char *method, const char *password,
                   const char *host, int port, uint8_t *out, size_t cap, size_t *out_len) {
    size_t host_len = host ? strlen(host) : 0;
    if (!badge_ss_method(method) || !salt || !out || !out_len || host_len < 1 || host_len > 255
        || port < 1 || port > 65535 || cap < 20 + host_len) return -1;
    wipe();
    if (evp_bytes(password, g.key) != 0) return -1;
    mbedtls_aes_init(&g.aes);
    if (mbedtls_aes_setkey_enc(&g.aes, g.key, 256) != 0) { wipe(); return -1; }
    memcpy(g.enc_iv, salt, 16);
    uint8_t addr[1 + 1 + 255 + 2];
    addr[0] = 3;
    addr[1] = (uint8_t)host_len;
    memcpy(addr + 2, host, host_len);
    addr[2 + host_len] = (uint8_t)(port >> 8);
    addr[3 + host_len] = (uint8_t)port;
    memcpy(out, salt, 16);
    if (stream(1, addr, 4 + host_len, out + 16) != 0) { wipe(); return -1; }
    *out_len = 20 + host_len;
    return 0;
}

void badge_ss_bind(int fd) { g.fd = fd; g.live = fd >= 0; }
void badge_ss_close(void) { wipe(); }
bool badge_ss_owns(int fd) { return g.live && fd >= 0 && fd == g.fd; }

int badge_ss_seal(const uint8_t *plain, size_t len, uint8_t *out, size_t cap, size_t *out_len) {
    if (!g.key[0] || !plain || !len || cap < len) return -1;
    if (stream(1, plain, len, out) != 0) return -1;
    *out_len = len;
    return 0;
}

int badge_ss_consume(const uint8_t *in, size_t len, size_t *used, uint8_t *out, size_t cap, size_t *out_len) {
    if (!used || !out_len) return -1;
    *used = 0;
    *out_len = 0;
    if (!g.key[0] || !in || !out || !len || !cap) return 0;
    /* The server's stream starts with its own 16-byte IV. */
    if (g.dec_iv_have < sizeof(g.dec_iv)) {
        size_t take = sizeof(g.dec_iv) - g.dec_iv_have;
        if (take > len) take = len;
        memcpy(g.dec_iv + g.dec_iv_have, in, take);
        g.dec_iv_have += take;
        *used = take;
        return 0;
    }
    /* CFB has no framing, so stop the cipher exactly at `cap`. */
    size_t n = len < cap ? len : cap;
    if (stream(0, in, n, out) != 0) return -1;
    *used = n;
    *out_len = n;
    return 1;
}
