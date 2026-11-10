#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum { PASSPORT_BARGE_REFERENCE = 1024 };
typedef struct {
    int16_t reference[PASSPORT_BARGE_REFERENCE];
    int64_t end, probe_at, cooldown;
    unsigned evidence, confirmed;
    bool probing;
    int16_t onset[768];
} passport_barge_t;

static inline void passport_barge_reference(passport_barge_t *barge, const int16_t *pcm,
                                         size_t count, int64_t now) {
    int64_t start = now * 6 / 1000;
    if (start < barge->end) start = barge->end;
    if (start - barge->end >= PASSPORT_BARGE_REFERENCE) memset(barge->reference, 0, sizeof(barge->reference));
    else for (int64_t tick = barge->end; tick < start; tick++) barge->reference[tick & (PASSPORT_BARGE_REFERENCE - 1)] = 0;
    for (size_t index = 0; index + 3 < count; index += 4) {
        int32_t value = (int32_t)pcm[index] + pcm[index + 1] + pcm[index + 2] + pcm[index + 3];
        barge->reference[start++ & (PASSPORT_BARGE_REFERENCE - 1)] = (int16_t)(value / 4);
    }
    barge->end = start;
}

static inline int16_t passport_barge_sample(const passport_barge_t *barge, int64_t tick) {
    return tick >= 0 && tick < barge->end && tick >= barge->end - PASSPORT_BARGE_REFERENCE
        ? barge->reference[tick & (PASSPORT_BARGE_REFERENCE - 1)] : 0;
}

static inline bool passport_barge_process(passport_barge_t *barge, const int16_t *pcm,
                                       size_t count, int64_t now, bool playback, bool muted) {
    if (muted || !playback) {
        barge->probing = false; barge->evidence = barge->confirmed = 0;
        return false;
    }
    if (now < barge->cooldown || count != 256) return false;
    int16_t microphone[64];
    int32_t mean = 0;
    for (size_t index = 0; index < 64; index++) {
        int32_t value = 0;
        for (size_t part = 0; part < 4; part++) value += pcm[index * 4 + part];
        microphone[index] = (int16_t)(value / 4); mean += microphone[index];
    }
    mean /= 64;
    uint64_t energy = 0;
    for (size_t index = 0; index < 64; index++) {
        int32_t value = microphone[index] - mean;
        energy += (int64_t)value * value;
    }
    bool voiced = energy > (uint64_t)64 * 360 * 360;
    if (barge->probing) {
        if (now - barge->probe_at < 80000) return false;
        barge->confirmed = voiced ? barge->confirmed + 1 : 0;
        if (barge->confirmed && barge->confirmed <= 3)
            memcpy(barge->onset + (barge->confirmed - 1) * 256, pcm, 512);
        if (barge->confirmed >= 3) {
            barge->probing = false; barge->evidence = barge->confirmed = 0;
            barge->cooldown = now + 500000;
            return true;
        }
        if (now - barge->probe_at >= 150000) {
            barge->probing = false; barge->evidence = barge->confirmed = 0;
            barge->cooldown = now + 800000;
        }
        return false;
    }
    int64_t first = now * 6 / 1000 - 64;
    bool echo = false;
    for (unsigned delay = 0; voiced && delay <= 768; delay += 4) {
        int64_t correlation = 0;
        int32_t reference_mean = 0;
        uint64_t reference_energy = 0;
        for (size_t index = 0; index < 64; index++) reference_mean += passport_barge_sample(barge, first + (int64_t)index - delay);
        reference_mean /= 64;
        for (size_t index = 0; index < 64; index++) {
            int32_t reference = passport_barge_sample(barge, first + (int64_t)index - delay) - reference_mean;
            correlation += (int64_t)(microphone[index] - mean) * reference;
            reference_energy += (int64_t)reference * reference;
        }
        if (reference_energy > (uint64_t)64 * 40 * 40 &&
            (correlation / 256) * (correlation / 256) > (int64_t)((energy / 256) * (reference_energy / 256) * 3 / 4)) {
            echo = true; break;
        }
    }
    barge->evidence = voiced && !echo ? barge->evidence + 1 : 0;
    if (barge->evidence >= 9) {
        barge->probing = true; barge->probe_at = now; barge->confirmed = 0;
    }
    return false;
}
