#pragma once
#include <stddef.h>
#include <stdint.h>

enum { PASSPORT_IMA_SAMPLES = 480, PASSPORT_IMA_HEADER = 6, PASSPORT_IMA_BYTES = 246 };

static const int passport_ima_steps[89] = {
    7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,
    88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,
    598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,
    2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,
    11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767
};
static const int passport_ima_indices[8] = {-1,-1,-1,-1,2,4,6,8};

static inline int passport_ima_update(unsigned code, int *predictor, int *index) {
    int step = passport_ima_steps[*index];
    int change = step / 8;
    if (code & 1) change += step / 4;
    if (code & 2) change += step / 2;
    if (code & 4) change += step;
    *predictor += code & 8 ? -change : change;
    if (*predictor < -32768) *predictor = -32768;
    if (*predictor > 32767) *predictor = 32767;
    *index += passport_ima_indices[code & 7];
    if (*index < 0) *index = 0;
    if (*index > 88) *index = 88;
    return *predictor;
}

static inline size_t passport_ima_samples(const uint8_t *block, size_t bytes) {
    if (!block || bytes < PASSPORT_IMA_HEADER || block[2] > 88 || block[3]) return 0;
    size_t count = block[4] | (size_t)block[5] << 8;
    if (!count || count > PASSPORT_IMA_SAMPLES || bytes != PASSPORT_IMA_HEADER + count / 2) return 0;
    if (!(count & 1) && (block[bytes - 1] & 0xf0)) return 0;
    return count;
}

static inline size_t passport_ima_encode(const int16_t *pcm, size_t count, int *index,
                                      uint8_t *block, size_t capacity) {
    if (!pcm || !index || *index < 0 || *index > 88 || !block || !count || count > PASSPORT_IMA_SAMPLES ||
        capacity < PASSPORT_IMA_HEADER + count / 2) return 0;
    int predictor = pcm[0];
    block[0] = (uint8_t)predictor; block[1] = (uint8_t)((uint16_t)predictor >> 8);
    block[2] = (uint8_t)*index; block[3] = 0;
    block[4] = (uint8_t)count; block[5] = (uint8_t)(count >> 8);
    for (size_t sample = 1; sample < count; sample++) {
        int delta = (int)pcm[sample] - predictor;
        unsigned code = delta < 0 ? 8 : 0;
        if (delta < 0) delta = -delta;
        int step = passport_ima_steps[*index];
        if (delta >= step) { code |= 4; delta -= step; }
        if (delta >= step / 2) { code |= 2; delta -= step / 2; }
        if (delta >= step / 4) code |= 1;
        passport_ima_update(code, &predictor, index);
        size_t offset = PASSPORT_IMA_HEADER + (sample - 1) / 2;
        if (sample & 1) block[offset] = (uint8_t)code;
        else block[offset] |= (uint8_t)(code << 4);
    }
    return PASSPORT_IMA_HEADER + count / 2;
}

static inline size_t passport_ima_decode(const uint8_t *block, size_t bytes, int16_t *pcm, size_t capacity) {
    size_t count = passport_ima_samples(block, bytes);
    if (!count || !pcm || capacity < count) return 0;
    int predictor = (int16_t)((unsigned)block[0] | (unsigned)block[1] << 8);
    int index = block[2];
    pcm[0] = (int16_t)predictor;
    for (size_t sample = 1; sample < count; sample++) {
        unsigned packed = block[PASSPORT_IMA_HEADER + (sample - 1) / 2];
        unsigned code = sample & 1 ? packed & 15 : packed >> 4;
        pcm[sample] = (int16_t)passport_ima_update(code, &predictor, &index);
    }
    return count;
}
