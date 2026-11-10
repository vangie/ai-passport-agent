#pragma once

#include "passport.h"

typedef struct {
    void *context;
    const char *(*join)(void *context, const char *ssid, const char *password, bool keep);
    bool (*save)(void *context, const passport_store_t *store);
} passport_network_io_t;

passport_network_state_t passport_network_connect(passport_store_t *store, int index,
    uint32_t generation, const passport_network_io_t *io);
const char *passport_network_feedback(passport_network_state_t state, bool chinese, bool online);
