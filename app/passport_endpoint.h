#pragma once

#include <stdbool.h>

typedef struct {
    char host[96];
    char path[192];
    int port;
    bool tls;
} passport_endpoint_t;

bool passport_endpoint_parse(const char *url, bool native, passport_endpoint_t *out);
