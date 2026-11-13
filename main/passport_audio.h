#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
#include "passport.h"

esp_err_t passport_audio_init(void);
esp_err_t passport_audio_set_volume(uint8_t percent);
/* Initialize the codec/DMA lazily; all control calls share one owner task. */
esp_err_t passport_audio_prepare(void);
esp_err_t passport_audio_start(void);
esp_err_t passport_audio_capture(void);
esp_err_t passport_audio_stop(void);
esp_err_t passport_audio_release(void);
/* Copies the bars (oldest first) if they changed since the last call. */
bool passport_audio_snapshot(uint8_t *out, size_t count);

typedef struct {
    const char *phase;
    esp_err_t error;
    uint32_t chunks;
    uint32_t peak;
    uint32_t mean_square, clipped, played;
    uint32_t voice_chunks, silence_chunks, input_suppressed;
    uint32_t input_rms, input_threshold;
    uint32_t voice_dropped;
    uint32_t capture_ms, read_gap_ms, clipped_total, input_high_water;
    uint32_t sample_rate;
    uint32_t mute_chunks, response_chunks, echo_chunks, input_clipped;
    uint32_t play_stalls, play_gap_ms;
    int32_t dc;
    bool worker_owned, response, muted;
} passport_audio_diagnostics_t;

/* Control-task only. Contains counts and errors, never microphone samples. */
void passport_audio_diagnostics(passport_audio_diagnostics_t *out);
bool passport_audio_finished(void);
bool passport_audio_submit_ready(void);
bool passport_audio_has_speech(void);
esp_err_t passport_audio_live_start(void);
esp_err_t passport_audio_live_test_start(void);
esp_err_t passport_audio_live_ima_start(bool synthetic);
esp_err_t passport_audio_live_push_ima(const uint8_t *block, size_t bytes);
esp_err_t passport_audio_live_pull(void *out, size_t capacity, size_t *bytes);
esp_err_t passport_audio_live_push(const void *pcm, size_t bytes);
esp_err_t passport_audio_live_clear(void);
size_t passport_audio_live_available(void);
uint32_t passport_audio_live_dropped(void);
uint32_t passport_audio_live_completed(void);
void passport_audio_live_response(bool speaking);
void passport_audio_live_mute(bool muted);
bool passport_audio_live_interrupt(void);
bool passport_audio_live_take_interrupt(void);
uint32_t passport_audio_live_position(void);
bool passport_audio_live_drained(void);
