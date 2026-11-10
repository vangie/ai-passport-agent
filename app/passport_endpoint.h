#ifndef PASSPORT_ENDPOINT_H
#define PASSPORT_ENDPOINT_H

#include <stdbool.h>
#include <stdint.h>

#define PASSPORT_ENDPOINT_SILENCE_MS 2000u
#define PASSPORT_ENDPOINT_VOICE_MS 240u

typedef struct {
    uint32_t elapsed_ms;
    uint32_t last_voice_ms;
    uint32_t voice_ms;
    uint32_t noise;
    bool speech;
    bool ready;
} passport_endpoint_t;

static inline bool passport_endpoint_feed(passport_endpoint_t *endpoint, uint32_t rms, uint32_t duration_ms) {
    if (endpoint->ready) return true;
    endpoint->elapsed_ms += duration_ms;
    if (endpoint->elapsed_ms <= 400) return false;
    if (!endpoint->noise) endpoint->noise = 80;
    uint32_t threshold = endpoint->noise * 2 + 60;
    if (threshold < 160) threshold = 160;
    if (threshold > 1600) threshold = 1600;
    if (rms > threshold) {
        if (!endpoint->speech && endpoint->elapsed_ms - endpoint->last_voice_ms > 200)
            endpoint->voice_ms = 0;
        endpoint->voice_ms += duration_ms;
        endpoint->last_voice_ms = endpoint->elapsed_ms;
        if (endpoint->voice_ms >= PASSPORT_ENDPOINT_VOICE_MS) endpoint->speech = true;
    } else {
        endpoint->noise = rms < endpoint->noise ? (endpoint->noise * 7 + rms) / 8
                                               : (endpoint->noise * 63 + rms) / 64;
    }
    endpoint->ready = endpoint->speech &&
        endpoint->elapsed_ms - endpoint->last_voice_ms >= PASSPORT_ENDPOINT_SILENCE_MS;
    return endpoint->ready;
}

#endif
