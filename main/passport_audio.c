#include "passport_audio.h"

#include "bsp_audio.h"
#include "passport_clip.h"
#include "passport_ima.h"
#include "passport_vad.h"
#include "passport_barge.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

static const char *TAG = "passport_audio";
#define AUDIO_RATE 16000
#define AUDIO_BITS 16
#define AUDIO_CHANNELS 1
#define AUDIO_CHUNK 256

static TaskHandle_t s_task;
static TaskHandle_t s_play_task;
static SemaphoreHandle_t s_play_stopped;
static SemaphoreHandle_t s_play_ready;
static SemaphoreHandle_t s_stopped;
static SemaphoreHandle_t s_lock;
static atomic_bool s_stop;
static atomic_bool s_finished;
static atomic_bool s_submit_ready;
static atomic_bool s_has_speech;
static passport_vad_t *s_vad;
static atomic_int s_error;
static atomic_uint s_chunks;
static atomic_uint s_peak;
static atomic_uint s_mean_square, s_clipped, s_played;
static atomic_int s_dc;
static const char *s_phase = "idle";
static uint8_t s_waveform[PASSPORT_WAVEFORM_BARS];
static volatile uint32_t s_waveform_seq;

static bool s_initialized;
static uint8_t s_output_volume = PASSPORT_VOLUME_DEFAULT;
static bool s_retain;
enum {
    LIVE_INPUT_BYTES = 27 * AUDIO_CHUNK * 2, LIVE_OUTPUT_BYTES = 10336,
    LIVE_INPUT_BLOCK = PASSPORT_IMA_HEADER + AUDIO_CHUNK / 2,
    LIVE_INPUT_STORAGE = (27 * LIVE_INPUT_BLOCK + 7) & ~7,
    LIVE_PREROLL_BLOCKS = 16,
    LIVE_PREROLL_STORAGE = LIVE_PREROLL_BLOCKS * LIVE_INPUT_BLOCK
};
static uint8_t *s_live_pcm;
static uint8_t *s_live_input;
static passport_barge_t *s_barge;
static uint8_t *s_preroll;
static atomic_bool s_interrupt, s_output_blocked, s_input_reset;
static size_t s_input_head, s_input_tail, s_output_head, s_output_tail;
static size_t s_input_silence;
static int s_input_codec_index;
static atomic_bool s_response, s_playing;
static atomic_bool s_muted;
static atomic_uint s_echo_guard;
static atomic_uint s_live_dropped;
static atomic_uint s_live_voice_dropped;
static atomic_uint s_completed;
static atomic_uint s_voice_chunks, s_silence_chunks, s_input_suppressed;
static atomic_uint s_input_rms, s_input_threshold;
static atomic_uint s_capture_ms, s_read_gap_ms, s_clip_total, s_input_high_water;
static atomic_uint s_capture_rate;
static atomic_uint s_mute_chunks, s_response_chunks, s_echo_chunks, s_input_clipped;
static atomic_uint s_play_stalls, s_play_gap_ms;
static atomic_uint s_voice_interrupts, s_button_interrupts;
static atomic_uint s_response_epoch;
static bool s_live_capture;
static bool s_synthetic;
static bool s_ima;
static size_t s_output_pcm;
static size_t s_play_bytes;

static void ring_copy(uint8_t *ring, size_t capacity, size_t offset, void *buffer, size_t bytes, bool write) {
    size_t first = capacity - offset % capacity;
    if (first > bytes) first = bytes;
    if (write) {
        memcpy(ring + offset % capacity, buffer, first);
        memcpy(ring, (uint8_t *)buffer + first, bytes - first);
    } else {
        memcpy(buffer, ring + offset % capacity, first);
        memcpy((uint8_t *)buffer + first, ring, bytes - first);
    }
}

static void live_input_push(const int16_t *samples) {
    uint8_t *block = s_live_input + (s_input_head / (AUDIO_CHUNK * 2) % 27) * LIVE_INPUT_BLOCK;
    passport_ima_encode(samples, AUDIO_CHUNK, &s_input_codec_index, block, LIVE_INPUT_BLOCK);
    s_input_head += AUDIO_CHUNK * 2;
}

esp_err_t passport_audio_live_pull(void *out, size_t capacity, size_t *bytes) {
    if (!bytes) return ESP_ERR_INVALID_ARG;
    *bytes = 0;
    if (!s_live_pcm || !out || capacity % 2) return ESP_ERR_INVALID_STATE;
    if (s_finished) return s_error == ESP_OK ? ESP_FAIL : s_error;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(5)) != pdTRUE) return ESP_OK;
    size_t silence = s_input_silence < capacity ? s_input_silence : capacity;
    memset(out, 0, silence);
    s_input_silence -= silence;
    size_t count = s_input_head - s_input_tail;
    capacity -= silence;
    if (count > capacity) count = capacity;
    int16_t decoded[AUDIO_CHUNK];
    size_t copied = 0;
    while (copied < count) {
        const uint8_t *block = s_live_input + (s_input_tail / sizeof(decoded) % 27) * LIVE_INPUT_BLOCK;
        if (passport_ima_decode(block, LIVE_INPUT_BLOCK, decoded, AUDIO_CHUNK) != AUDIO_CHUNK) {
            xSemaphoreGive(s_lock); return ESP_FAIL;
        }
        size_t offset = s_input_tail % sizeof(decoded);
        size_t portion = count - copied;
        if (portion > sizeof(decoded) - offset) portion = sizeof(decoded) - offset;
        memcpy((uint8_t *)out + silence + copied, (uint8_t *)decoded + offset, portion);
        s_input_tail += portion; copied += portion;
    }
    if (atomic_load(&s_muted)) memset((uint8_t *)out + silence, 0, count);
    *bytes = silence + count;
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

size_t passport_audio_live_available(void) {
    if (!s_live_pcm || xSemaphoreTake(s_lock, pdMS_TO_TICKS(5)) != pdTRUE) return 0;
    size_t count = s_input_silence + s_input_head - s_input_tail;
    xSemaphoreGive(s_lock);
    return count;
}

uint32_t passport_audio_live_dropped(void) {
    return atomic_load(&s_live_dropped);
}

uint32_t passport_audio_live_completed(void) { return atomic_load(&s_completed); }

esp_err_t passport_audio_live_push(const void *pcm, size_t bytes) {
    if (!s_live_pcm || s_ima || !pcm || bytes % 2) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(5)) != pdTRUE) return ESP_ERR_TIMEOUT;
    if (atomic_load(&s_output_blocked)) { xSemaphoreGive(s_lock); return ESP_OK; }
    bool fits = bytes <= LIVE_OUTPUT_BYTES - (s_output_head - s_output_tail);
    if (fits) {
        ring_copy(s_live_pcm, LIVE_OUTPUT_BYTES, s_output_head, (void *)pcm, bytes, true);
        s_output_head += bytes;
    }
    xSemaphoreGive(s_lock);
    if (fits && s_play_ready) xSemaphoreGive(s_play_ready);
    return fits ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t passport_audio_live_push_ima(const uint8_t *block, size_t bytes) {
    size_t samples = passport_ima_samples(block, bytes);
    if (!s_live_pcm || !s_ima || !samples) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(5)) != pdTRUE) return ESP_ERR_TIMEOUT;
    if (atomic_load(&s_output_blocked)) { xSemaphoreGive(s_lock); return ESP_OK; }
    bool fits = PASSPORT_IMA_BYTES <= LIVE_OUTPUT_BYTES - (s_output_head - s_output_tail);
    if (fits) {
        ring_copy(s_live_pcm, LIVE_OUTPUT_BYTES, s_output_head, (void *)block, bytes, true);
        s_output_head += PASSPORT_IMA_BYTES;
        s_output_pcm += samples * 2;
    }
    xSemaphoreGive(s_lock);
    if (fits && s_play_ready) xSemaphoreGive(s_play_ready);
    return fits ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t passport_audio_live_clear(void) {
    if (!s_live_pcm) return ESP_OK;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) != pdTRUE) return ESP_ERR_TIMEOUT;
    atomic_fetch_add(&s_completed, s_ima ? s_output_pcm : s_output_head - s_output_tail);
    s_output_tail = s_output_head;
    s_output_pcm = 0;
    atomic_store(&s_output_blocked, false);
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

static void live_interrupt_locked(unsigned echo_guard) {
    atomic_store(&s_output_blocked, true);
    atomic_store(&s_interrupt, true);
    atomic_fetch_add(&s_completed, s_ima ? s_output_pcm : s_output_head - s_output_tail);
    s_output_tail = s_output_head; s_output_pcm = 0;
    atomic_store(&s_response, false);
    atomic_fetch_add(&s_response_epoch, 1);
    atomic_store(&s_echo_guard, echo_guard);
    if (s_barge) s_barge->probing = false;
}

bool passport_audio_live_interrupt(void) {
    if (!s_live_pcm || xSemaphoreTake(s_lock, pdMS_TO_TICKS(5)) != pdTRUE) return false;
    atomic_fetch_add(&s_button_interrupts,1);
    live_interrupt_locked(6);
    xSemaphoreGive(s_lock);
    return true;
}

bool passport_audio_live_take_interrupt(void) { return atomic_exchange(&s_interrupt, false); }

uint32_t passport_audio_live_position(void) {
    if (!s_live_pcm || xSemaphoreTake(s_lock, pdMS_TO_TICKS(5)) != pdTRUE) return atomic_load(&s_completed);
    uint32_t position = atomic_load(&s_completed) + s_play_bytes;
    xSemaphoreGive(s_lock);
    return position;
}

void passport_audio_live_response(bool speaking) {
    if (atomic_exchange(&s_response, speaking) != speaking) atomic_fetch_add(&s_response_epoch, 1);
}

void passport_audio_live_mute(bool muted) {
    atomic_store(&s_muted, muted);
    if (muted) atomic_store(&s_input_reset, true);
    if (muted && s_live_pcm && xSemaphoreTake(s_lock, pdMS_TO_TICKS(5)) == pdTRUE) {
        s_input_silence += s_input_head - s_input_tail;
        s_input_tail = s_input_head;
        xSemaphoreGive(s_lock);
    }
}

bool passport_audio_live_drained(void) {
    if (!s_live_pcm) return true;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(5)) != pdTRUE) return false;
    bool drained = s_output_head == s_output_tail && !atomic_load(&s_playing);
    xSemaphoreGive(s_lock);
    return drained;
}

static void playback_task(void *arg) {
    (void)arg;
    int16_t playback[480];
    uint8_t compressed[PASSPORT_IMA_BYTES];
    int64_t starved_at = 0;
    int64_t buffering_since = -1;
    bool primed = false;
    unsigned starved_epoch = 0;
    unsigned primed_epoch = atomic_load(&s_response_epoch);
    while (!s_stop) {
        if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(10)) != pdTRUE) continue;
        if (s_barge && s_barge->probing) {
            passport_barge_paused(s_barge,esp_timer_get_time());
            xSemaphoreGive(s_lock);
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        unsigned epoch = atomic_load(&s_response_epoch);
        if (primed_epoch != epoch) {
            primed = false;
            buffering_since = -1;
            primed_epoch = epoch;
        }
        size_t queued = s_ima ? s_output_pcm : s_output_head - s_output_tail;
        if (!queued && primed) {
            int64_t now = esp_timer_get_time();
            if (!starved_at) { starved_at = now; starved_epoch = epoch; }
            int64_t remaining = 10000 - (now - starved_at);
            if (remaining > 0) {
                xSemaphoreGive(s_lock);
                (void)xSemaphoreTake(s_play_ready, pdMS_TO_TICKS((remaining + 999) / 1000));
                continue;
            }
        }
        if (!primed && queued && queued < (s_ima ? 30720u : 7680u)) {
            int64_t now = esp_timer_get_time();
            if (buffering_since < 0) buffering_since = now;
            if (now - buffering_since < (s_ima ? 640000 : 160000)) {
                xSemaphoreGive(s_lock);
                vTaskDelay(pdMS_TO_TICKS(5));
                continue;
            }
        }
        size_t play = queued < sizeof(playback) ? queued : sizeof(playback);
        if (play) {
            primed = true;
            buffering_since = -1;
            if (starved_at) {
                uint32_t gap_ms = (uint32_t)((esp_timer_get_time() - starved_at) / 1000);
                if (starved_epoch == epoch && gap_ms >= 10) {
                    atomic_fetch_add(&s_play_stalls, 1);
                    if (gap_ms > atomic_load(&s_play_gap_ms)) atomic_store(&s_play_gap_ms, gap_ms);
                }
                starved_at = 0;
            }
            if (s_ima) {
                ring_copy(s_live_pcm, LIVE_OUTPUT_BYTES, s_output_tail, compressed, sizeof(compressed), false);
                size_t bytes = PASSPORT_IMA_HEADER + ((size_t)compressed[4] | (size_t)compressed[5] << 8) / 2;
                size_t samples = bytes <= sizeof(compressed) ? passport_ima_decode(compressed, bytes, playback, 480) : 0;
                if (!samples) { xSemaphoreGive(s_lock); s_error = ESP_FAIL; s_stop = true; break; }
                play = samples * 2;
                s_output_tail += PASSPORT_IMA_BYTES;
                s_output_pcm -= play;
            } else {
                ring_copy(s_live_pcm, LIVE_OUTPUT_BYTES, s_output_tail, playback, play, false);
                s_output_tail += play;
            }
            atomic_store(&s_playing, true);
            s_play_bytes = play;
            atomic_store(&s_echo_guard, 20);
            if (atomic_load(&s_output_blocked)) memset(playback, 0, play);
            if (s_barge) passport_barge_reference(s_barge, playback, play / 2, esp_timer_get_time());
        }
        xSemaphoreGive(s_lock);
        if (play) {
            esp_err_t err = ESP_OK;
            if (s_synthetic) vTaskDelay(pdMS_TO_TICKS((play + 47) / 48));
            else err = bsp_audio_write(playback, play);
            if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
                atomic_fetch_add(&s_completed, play);
                s_play_bytes = 0;
                atomic_store(&s_playing, false);
                xSemaphoreGive(s_lock);
            }
            if (err != ESP_OK) { s_error = err; s_stop = true; break; }
            atomic_fetch_add(&s_played, play);
        } else {
            primed = false;
            buffering_since = -1;
            if (!starved_at && atomic_load(&s_response) && atomic_load(&s_completed)) {
                starved_at = esp_timer_get_time();
                starved_epoch = atomic_load(&s_response_epoch);
            }
            (void)xSemaphoreTake(s_play_ready, portMAX_DELAY);
        }
    }
    xSemaphoreGive(s_play_stopped);
    vTaskSuspend(NULL);
}

/* One bar per 640 samples (40 ms at 16 kHz). Speech energy follows RMS
 * much more closely than the peak of a single 16 ms chunk, which jumps on
 * every click and lags on vowels. The bars scroll: index 0 is the oldest. */
#define LEVEL_WINDOW 640
/* Measured on this microphone: room noise sits at 40-200 RMS depending on
 * the place, speech at 400-1800, loud speech up to ~5000. The floor follows
 * the room (it drops quickly and rises slowly, so speech does not lift it),
 * and the bar is log-scaled from just above the floor to LEVEL_FULL_RMS. */
#define LEVEL_FULL_RMS 3000u
#define LEVEL_FLOOR_MIN 80u
#define LEVEL_FLOOR_MAX 300u
/* The codec's start-up thump and its decay fill the first ~320 ms. */
#define LEVEL_SETTLE_WINDOWS 10

static uint32_t log2_eighths(uint32_t ratio_x8) {
    uint32_t steps = 0;
    while (ratio_x8 >= 16) { ratio_x8 >>= 1; steps += 8; }
    return steps + ratio_x8 - 8;
}

static uint8_t rms_level(uint32_t rms, uint32_t floor) {
    if (rms <= floor) return 4;
    if (rms >= LEVEL_FULL_RMS) return 100;
    uint32_t steps = log2_eighths(rms * 8 / floor);
    uint32_t full = log2_eighths(LEVEL_FULL_RMS * 8 / floor);
    uint32_t level = 4 + steps * 96 / (full ? full : 1);
    return (uint8_t)(level > 100 ? 100 : level);
}

static void push_level(uint8_t level) {
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(5)) != pdTRUE) return;
    memmove(s_waveform, s_waveform + 1, PASSPORT_WAVEFORM_BARS - 1);
    s_waveform[PASSPORT_WAVEFORM_BARS - 1] = level;
    s_waveform_seq++;
    xSemaphoreGive(s_lock);
}

typedef struct {
    uint32_t noise;
    bool voiced;
    unsigned history, cursor;
    int codec_index;
} live_input_gate_t;

static bool live_input_prepare(live_input_gate_t *gate, int16_t *samples, size_t count, bool blocked) {
    if (blocked) {
        gate->voiced = false;
        gate->history = gate->cursor = 0;
        if (s_vad) passport_vad_block(s_vad);
        atomic_fetch_add(&s_input_suppressed, 1);
        return false;
    }
    int64_t sum = 0;
    uint64_t energy = 0;
    for (size_t index = 0; index < count; index++) {
        int32_t value = samples[index];
        sum += value;
        energy += (uint64_t)(value * value);
    }
    int32_t mean = (int32_t)(sum / (int64_t)count);
    uint32_t variance = (uint32_t)(energy / count) - (uint32_t)(mean * mean);
    uint32_t rms = variance;
    for (unsigned iteration = 0; rms && iteration < 20; iteration++) rms = (rms + variance / rms) / 2;
    if (!gate->noise) gate->noise = 150;
    uint32_t threshold = gate->noise * 2;
    if (threshold < 120) threshold = 120;
    if (threshold > 360) threshold = 360;
    atomic_store(&s_input_rms, rms);
    atomic_store(&s_input_threshold, threshold);
    if (s_vad) passport_vad_process(s_vad, samples, count);
    gate->voiced = s_synthetic || (s_vad && s_vad->active);
    if (!gate->voiced) gate->noise = (gate->noise * 63 + (rms > 180 ? 180 : rms)) / 64;
    if (gate->voiced) {
        atomic_fetch_add(&s_voice_chunks, 1);
    } else atomic_fetch_add(&s_silence_chunks, 1);
    return true;
}

static void live_input_remember(live_input_gate_t *gate, const int16_t *samples) {
    passport_ima_encode(samples, AUDIO_CHUNK, &gate->codec_index,
        s_preroll + gate->cursor * LIVE_INPUT_BLOCK, LIVE_INPUT_BLOCK);
    gate->cursor = (gate->cursor + 1) % LIVE_PREROLL_BLOCKS;
    if (gate->history < LIVE_PREROLL_BLOCKS) gate->history++;
}

static void live_input_replay(live_input_gate_t *gate) {
    size_t available = LIVE_INPUT_BYTES - (s_input_silence + s_input_head - s_input_tail);
    unsigned blocks = available / (AUDIO_CHUNK * 2);
    unsigned replay = blocks ? blocks - 1 : 0;
    if (replay > gate->history) replay = gate->history;
    for (unsigned block = 0; block < replay; block++) {
        unsigned slot = (gate->cursor + LIVE_PREROLL_BLOCKS - replay + block) % LIVE_PREROLL_BLOCKS;
        uint8_t *target = s_live_input + (s_input_head / (AUDIO_CHUNK * 2) % 27) * LIVE_INPUT_BLOCK;
        memcpy(target, s_preroll + slot * LIVE_INPUT_BLOCK, LIVE_INPUT_BLOCK);
        s_input_head += AUDIO_CHUNK * 2;
    }
    atomic_fetch_add(&s_live_voice_dropped, gate->history - replay);
    atomic_fetch_add(&s_live_dropped, gate->history - replay);
    gate->history = 0;
}

static void audio_task(void *arg) {
    (void)arg;
    int16_t samples[AUDIO_CHUNK];
    uint64_t energy = 0;
    int32_t sum = 0;
    unsigned clipped = 0;
    unsigned counted = 0, windows = 0;
    uint32_t noise = 150;
    int64_t capture_started = esp_timer_get_time(), last_read = 0;
    live_input_gate_t input_gate = {0};
    while (!s_stop) {
        esp_err_t err = ESP_OK;
        if (s_synthetic) {
            int64_t target = capture_started + ((int64_t)s_chunks + 1) * AUDIO_CHUNK * 1000000 / 24000;
            while (!s_stop && esp_timer_get_time() < target)
                vTaskDelay(pdMS_TO_TICKS(1));
            if (s_stop) break;
            for (size_t index = 0; index < AUDIO_CHUNK; index++) {
                int16_t level = 80 + (s_chunks % 80) * 16;
                samples[index] = index % 2 ? level : -level;
            }
        } else err = bsp_audio_read(samples, sizeof(samples));
        if (err != ESP_OK) {
            s_error = err;
            break;
        }
        int64_t read_at = esp_timer_get_time();
        if (last_read) {
            uint32_t gap_ms = (uint32_t)((read_at - last_read) / 1000);
            if (gap_ms > atomic_load(&s_read_gap_ms)) atomic_store(&s_read_gap_ms, gap_ms);
        }
        last_read = read_at;
        atomic_store(&s_capture_ms, (uint32_t)((read_at - capture_started) / 1000));
        unsigned raw_clipped = 0;
        for (size_t index = 0; index < AUDIO_CHUNK; index++)
            if (samples[index] >= 32760 || samples[index] <= -32760) raw_clipped++;
        atomic_fetch_add(&s_clip_total, raw_clipped);
        if (s_retain && !s_live_capture && s_vad) {
            passport_vad_process(s_vad, samples, AUDIO_CHUNK);
            atomic_store(&s_has_speech, s_vad->speech);
            if (s_vad->ready) atomic_store(&s_submit_ready, true);
        }
        if (s_retain) {
            err = passport_clip_append(samples, sizeof(samples));
            if (err != ESP_OK) { s_error = err; break; }
            if (passport_clip_full()) s_stop = true;
        }
        if (s_live_capture) {
            if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(10)) != pdTRUE) { s_error = ESP_ERR_TIMEOUT; break; }
            if (atomic_exchange(&s_input_reset, false)) {
                input_gate.history = input_gate.cursor = 0;
                passport_vad_block(s_vad);
            }
            unsigned echo_guard = atomic_load(&s_echo_guard);
            bool response = atomic_load(&s_response), muted = atomic_load(&s_muted);
            bool playback = s_output_head != s_output_tail || atomic_load(&s_playing);
            bool blocked = response || muted || echo_guard || playback;
            if (s_barge && !atomic_load(&s_playing)) passport_barge_paused(s_barge,read_at);
            bool probe = s_barge && !muted && passport_barge_listen(s_barge,read_at);
            bool forward = live_input_prepare(&input_gate, samples, AUDIO_CHUNK, blocked && !probe);
            bool interrupted = s_barge && passport_barge_process(s_barge, samples, AUDIO_CHUNK, read_at,
                response || playback || echo_guard, muted, input_gate.voiced);
            if (interrupted) {
                atomic_fetch_add(&s_voice_interrupts,1);
                s_input_tail = s_input_head; s_input_silence = 0;
                live_interrupt_locked(0);
                blocked = false;
            }
            if (muted) atomic_fetch_add(&s_mute_chunks, 1);
            else if (response) atomic_fetch_add(&s_response_chunks, 1);
            else if (blocked) atomic_fetch_add(&s_echo_chunks, 1);
            else atomic_fetch_add(&s_input_clipped, raw_clipped);
            if (echo_guard) (void)atomic_compare_exchange_strong(&s_echo_guard, &echo_guard, echo_guard - 1);
            if (blocked) {
                if (probe) live_input_remember(&input_gate, samples);
                memset(samples, 0, sizeof(samples));
            } else if (!s_synthetic && !input_gate.voiced) {
                live_input_remember(&input_gate, samples);
                memset(samples, 0, sizeof(samples));
            }
            if (!blocked && input_gate.voiced && input_gate.history) live_input_replay(&input_gate);
            bool fits = sizeof(samples) <= LIVE_INPUT_BYTES - (s_input_silence + s_input_head - s_input_tail);
            if (fits && (forward || blocked)) {
                if ((blocked || !input_gate.voiced) && s_input_head == s_input_tail) {
                    s_input_silence += sizeof(samples);
                } else {
                    if (!input_gate.voiced || blocked) s_input_codec_index = 0;
                    live_input_push(samples);
                }
                size_t queued = s_input_silence + s_input_head - s_input_tail;
                if (queued > atomic_load(&s_input_high_water)) atomic_store(&s_input_high_water, queued);
            } else if (!fits && forward) {
                atomic_fetch_add(&s_live_dropped, 1);
                if (input_gate.voiced) atomic_fetch_add(&s_live_voice_dropped, 1);
            }
            xSemaphoreGive(s_lock);
        }
        int peak = 0;
        for (size_t i = 0; i < AUDIO_CHUNK; i++) {
            int32_t value = samples[i] < 0 ? -(int32_t)samples[i] : samples[i];
            if (value > peak) peak = value;
            energy += (uint64_t)((int32_t)samples[i] * (int32_t)samples[i]);
            sum += samples[i];
            if (value >= 32760) clipped++;
            if (++counted == LEVEL_WINDOW) {
                uint32_t mean = (uint32_t)(energy / LEVEL_WINDOW), rms = mean;
                atomic_store(&s_mean_square, mean);
                atomic_store(&s_dc, sum / LEVEL_WINDOW);
                atomic_store(&s_clipped, clipped);
                for (int k = 0; rms && k < 16; k++) rms = (rms + mean / rms) / 2;
                if (++windows > LEVEL_SETTLE_WINDOWS) {
                    if (rms < noise) noise = (noise * 3 + rms) / 4;
                    else noise += 1;
                    uint32_t floor = noise * 8 / 5;
                    if (floor < LEVEL_FLOOR_MIN) floor = LEVEL_FLOOR_MIN;
                    if (floor > LEVEL_FLOOR_MAX) floor = LEVEL_FLOOR_MAX;
                    push_level(rms_level(rms, floor));
                }
                energy = 0; counted = 0; sum = 0; clipped = 0;
            }
        }
        s_chunks++;
        if ((unsigned)peak > s_peak) s_peak = (unsigned)peak;
    }
    if (s_live_capture) s_stop = true;
    s_finished = true;
    xSemaphoreGive(s_stopped);
    /* The owner reaps the parked worker after its acknowledgement, before
     * another capture can allocate a task stack. */
    vTaskSuspend(NULL);
}

esp_err_t passport_audio_set_volume(uint8_t percent) {
    if (percent > 100) return ESP_ERR_INVALID_ARG;
    if (s_task || s_play_task) return ESP_ERR_INVALID_STATE;
    s_output_volume = percent;
    bsp_audio_set_volume(percent);
    return ESP_OK;
}

esp_err_t passport_audio_init(void) {
    if (s_initialized) return ESP_OK;
    if (s_lock == NULL) s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) return ESP_ERR_NO_MEM;
    esp_err_t err = bsp_audio_init();
    if (err == ESP_OK) {
        bsp_audio_set_volume(s_output_volume);
        s_initialized = true;
    }
    return err;
}

static esp_err_t start_capture(bool retain) {
    if (s_task != NULL && s_finished) (void)passport_audio_stop();
    if (s_task != NULL) return ESP_ERR_INVALID_STATE;
    s_retain = retain;
    atomic_store(&s_submit_ready, false);
    atomic_store(&s_has_speech, false);
    if (retain || s_live_capture) {
        if (!s_vad) s_vad = calloc(1, sizeof(*s_vad));
        if (!s_vad) return ESP_ERR_NO_MEM;
        if (!passport_vad_init(s_vad, s_live_capture ? 24000 : AUDIO_RATE)) return ESP_FAIL;
    }
    s_chunks = 0;
    s_peak = 0;
    s_mean_square = s_clipped = s_played = 0;
    s_dc = 0;
    s_voice_chunks = s_silence_chunks = s_input_suppressed = 0;
    s_input_rms = s_input_threshold = 0;
    s_capture_ms = s_read_gap_ms = s_clip_total = s_input_high_water = 0;
    s_capture_rate = s_live_capture ? 24000 : AUDIO_RATE;
    s_mute_chunks = s_response_chunks = s_echo_chunks = s_input_clipped = 0;
    s_voice_interrupts = s_button_interrupts = 0;
    s_error = ESP_OK;
    esp_err_t err = s_synthetic ? ESP_OK : passport_audio_prepare();
    if (err != ESP_OK) return err;
    s_phase = "stop_signal";
    if (s_stopped == NULL) s_stopped = xSemaphoreCreateBinary();
    if (s_stopped == NULL) { s_error = ESP_ERR_NO_MEM; return s_error; }
    (void)xSemaphoreTake(s_stopped, 0);
    if (xSemaphoreTake(s_lock, 0) == pdTRUE) {
        memset(s_waveform, 0, sizeof(s_waveform));
        xSemaphoreGive(s_lock);
    }
    s_stop = false;
    s_finished = false;
    s_error = ESP_OK;
    s_phase = "worker";
    if (xTaskCreate(audio_task, "passport_audio", 4096, NULL, s_live_capture ? 6 : 4, &s_task) != pdPASS) {
        s_task = NULL;
        s_error = ESP_ERR_NO_MEM;
        return ESP_ERR_NO_MEM;
    }
    s_phase = "capture";
    return ESP_OK;
}

esp_err_t passport_audio_start(void) { return start_capture(false); }

esp_err_t passport_audio_capture(void) {
    if (s_live_capture) return ESP_ERR_INVALID_STATE;
    if (s_task != NULL && !s_finished) return ESP_ERR_INVALID_STATE;
    if (s_task != NULL) (void)passport_audio_stop();
    s_phase = "storage";
    esp_err_t err = passport_clip_begin();
    if (err != ESP_OK) { s_error = err; return err; }
    return start_capture(true);
}

esp_err_t passport_audio_prepare(void) {
    s_phase = "codec_init";
    esp_err_t err = passport_audio_init();
    if (err != ESP_OK) {
        s_error = err;
        ESP_LOGE(TAG, "音频基础初始化失败: %s", esp_err_to_name(err));
        return err;
    }
    s_phase = "format";
    err = bsp_audio_set_format(s_live_capture ? 24000 : AUDIO_RATE, AUDIO_BITS, AUDIO_CHANNELS);
    if (err != ESP_OK) {
        s_error = err;
        ESP_LOGE(TAG, "音频格式/DMA 初始化失败: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t passport_audio_stop(void) {
    if (s_task == NULL) return s_error;
    s_stop = true;
    if (s_play_task) {
        xSemaphoreGive(s_play_ready);
        if (xSemaphoreTake(s_play_stopped, pdMS_TO_TICKS(1000)) != pdTRUE) return ESP_ERR_TIMEOUT;
        vTaskDelete(s_play_task); s_play_task = NULL;
    }
    if (xSemaphoreTake(s_stopped, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "录音任务停止超时");
        s_phase = "stop_timeout";
        return ESP_ERR_TIMEOUT;
    }
    vTaskDelete(s_task);
    s_task = NULL;
    s_retain = false;
    s_phase = s_error == ESP_OK ? "stopped" : "read";
    return s_error;
}

bool passport_audio_finished(void) {
    return s_task != NULL && s_finished;
}

esp_err_t passport_audio_release(void) {
    if (s_task != NULL || s_play_task != NULL) return ESP_ERR_INVALID_STATE;
    free(s_live_pcm); s_live_pcm = NULL; s_live_input = NULL; s_barge = NULL; s_preroll = NULL;
    free(s_vad); s_vad = NULL;
    s_live_capture = false; s_synthetic = false; s_ima = false;
    esp_err_t err = bsp_audio_deinit();
    s_initialized = false;
    return err;
}

static esp_err_t live_start(bool synthetic, bool ima) {
    if (s_task || s_live_pcm) return ESP_ERR_INVALID_STATE;
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    s_live_pcm = calloc(1, LIVE_OUTPUT_BYTES + LIVE_INPUT_STORAGE + sizeof(passport_barge_t) + LIVE_PREROLL_STORAGE);
    if (!s_live_pcm) return ESP_ERR_NO_MEM;
    s_live_input = s_live_pcm + LIVE_OUTPUT_BYTES;
    s_barge = (passport_barge_t *)(s_live_input + LIVE_INPUT_STORAGE);
    s_preroll = (uint8_t *)(s_barge + 1);
    s_input_codec_index = 0;
    atomic_store(&s_interrupt, false); atomic_store(&s_output_blocked, false);
    atomic_store(&s_input_reset, false);
    s_input_head = s_input_tail = s_output_head = s_output_tail = 0;
    s_output_pcm = 0;
    s_play_bytes = 0;
    s_ima = ima;
    s_input_silence = 0;
    atomic_store(&s_response, false); atomic_store(&s_playing, false);
    atomic_store(&s_echo_guard, 0);
    atomic_store(&s_live_dropped, 0);
    atomic_store(&s_live_voice_dropped, 0);
    atomic_store(&s_completed, 0);
    atomic_store(&s_play_stalls, 0); atomic_store(&s_play_gap_ms, 0);
    atomic_store(&s_response_epoch, 0);
    s_live_capture = true;
    s_synthetic = synthetic;
    esp_err_t err = start_capture(false);
    if (err == ESP_OK) {
        if (!s_play_ready) s_play_ready = xSemaphoreCreateBinary();
        if (!s_play_stopped) s_play_stopped = xSemaphoreCreateBinary();
        if (!s_play_stopped || !s_play_ready) err = ESP_ERR_NO_MEM;
        else {
            (void)xSemaphoreTake(s_play_ready, 0);
            (void)xSemaphoreTake(s_play_stopped, 0);
            if (xTaskCreate(playback_task, "passport_play", 3072, NULL, 6, &s_play_task) != pdPASS) err = ESP_ERR_NO_MEM;
        }
    }
    if (err != ESP_OK) { (void)passport_audio_stop(); (void)passport_audio_release(); }
    return err;
}

esp_err_t passport_audio_live_start(void) { return live_start(false, false); }
esp_err_t passport_audio_live_test_start(void) { return live_start(true, false); }
esp_err_t passport_audio_live_ima_start(bool synthetic) { return live_start(synthetic, true); }

bool passport_audio_submit_ready(void) {
    return s_retain && !s_live_capture && !s_stop && !s_finished && atomic_load(&s_submit_ready);
}

bool passport_audio_has_speech(void) { return atomic_load(&s_has_speech); }

void passport_audio_diagnostics(passport_audio_diagnostics_t *out) {
    *out = (passport_audio_diagnostics_t) {
        .phase = s_phase,
        .error = strcmp(s_phase, "stop_timeout") == 0 ? ESP_ERR_TIMEOUT : s_error,
        .chunks = s_chunks,
        .peak = s_peak, .worker_owned = s_task != NULL,
        .mean_square = s_mean_square, .dc = s_dc, .clipped = s_clipped, .played = s_played,
        .voice_chunks = s_voice_chunks, .silence_chunks = s_silence_chunks,
        .input_suppressed = s_input_suppressed, .response = s_response, .muted = s_muted,
        .input_rms = s_input_rms, .input_threshold = s_input_threshold,
        .voice_dropped = s_live_voice_dropped,
        .capture_ms = s_capture_ms, .read_gap_ms = s_read_gap_ms,
        .sample_rate = s_capture_rate,
        .clipped_total = s_clip_total, .input_high_water = s_input_high_water,
        .mute_chunks = s_mute_chunks, .response_chunks = s_response_chunks,
        .echo_chunks = s_echo_chunks, .input_clipped = s_input_clipped,
        .play_stalls = s_play_stalls, .play_gap_ms = s_play_gap_ms,
        .voice_interrupts = s_voice_interrupts, .button_interrupts = s_button_interrupts,
    };
}

bool passport_audio_snapshot(uint8_t *out, size_t count) {
    static uint32_t seen;
    if (out == NULL || count == 0 || s_lock == NULL) return false;
    if (s_waveform_seq == seen) return false;
    size_t copy = count < PASSPORT_WAVEFORM_BARS ? count : PASSPORT_WAVEFORM_BARS;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(5)) != pdTRUE) return false;
    memcpy(out, s_waveform, copy);
    seen = s_waveform_seq;
    xSemaphoreGive(s_lock);
    return true;
}
