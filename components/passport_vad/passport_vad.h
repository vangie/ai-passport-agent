#pragma once

#include "src/vad/vad_core.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    VadInstT core;
    int16_t frame[80];
    uint16_t recent;
    unsigned filled, part, divisor;
    int32_t sum;
    unsigned quiet_ms, frames, voice_frames;
    bool active, speech, ready;
} passport_vad_t;

bool passport_vad_init(passport_vad_t *vad, unsigned sample_rate);
void passport_vad_process(passport_vad_t *vad, const int16_t *pcm, size_t count);
void passport_vad_block(passport_vad_t *vad);
