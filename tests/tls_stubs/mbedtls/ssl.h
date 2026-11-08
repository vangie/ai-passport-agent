#pragma once
typedef struct { int unused; } mbedtls_ssl_context;
const char *mbedtls_ssl_get_version(const mbedtls_ssl_context *);
const char *mbedtls_ssl_get_ciphersuite(const mbedtls_ssl_context *);
