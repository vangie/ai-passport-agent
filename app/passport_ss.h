#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Legacy Shadowsocks stream cipher: aes-256-cfb. */
bool passport_ss_method(const char *method);
/* Builds the 16-byte IV followed by the encrypted target address. */
int passport_ss_hello(const uint8_t salt[32], const char *method, const char *password,
                   const char *host, int port, uint8_t *out, size_t cap, size_t *out_len);
void passport_ss_bind(int fd);
void passport_ss_close(void);
bool passport_ss_owns(int fd);
int passport_ss_seal(const uint8_t *plain, size_t len, uint8_t *out, size_t cap, size_t *out_len);
/* Decrypts the server's stream, which starts with the server's IV.
 * 1: plaintext produced, 0: need more input (IV bytes may have been used),
 * -1: cipher failure. */
int passport_ss_consume(const uint8_t *in, size_t len, size_t *used,
                     uint8_t *out, size_t cap, size_t *out_len);
