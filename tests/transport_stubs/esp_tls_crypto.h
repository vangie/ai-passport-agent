#pragma once
#include <stddef.h>
int esp_crypto_sha1(const unsigned char *, size_t, unsigned char *);
int esp_crypto_base64_encode(unsigned char *, size_t, size_t *, const unsigned char *, size_t);
