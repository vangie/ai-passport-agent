#pragma once

#include "src/vad/vad_core.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The codec's start-up thump, its decay and the key click that started the
 * recording all fall in the first moments; nothing there counts as speech. */
#define PASSPORT_VAD_SETTLE_MS 400
/* A recording holds speech once this much voice has been heard in bursts that
 * each lasted PASSPORT_VAD_BURST_MS. A short word is about 250 ms of voice;
 * a click, a cough or a door is one burst well under that. */
#define PASSPORT_VAD_BURST_MS 120
#define PASSPORT_VAD_SPEECH_MS 250

typedef struct {
    VadInstT core;
    int16_t frame[80];
    uint16_t recent;
    unsigned filled, part, divisor;
    int32_t sum;
    unsigned quiet_ms, frames, voice_frames;
    unsigned run_ms, longest_ms, spoken_ms, elapsed_ms;
    bool active, speech, ready;
} passport_vad_t;

bool passport_vad_init(passport_vad_t *vad, unsigned sample_rate);
void passport_vad_process(passport_vad_t *vad, const int16_t *pcm, size_t count);
void passport_vad_block(passport_vad_t *vad);
