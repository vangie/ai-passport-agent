#include "passport_vad.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void silence(passport_vad_t *vad, unsigned frames) {
    int16_t samples[240] = {0};
    for (unsigned frame = 0; frame < frames; frame++)
        passport_vad_process(vad, samples, vad->divisor * 80);
}

static int16_t *speech_samples(size_t *count) {
    FILE *file = fopen("assets/audio/vad-speech.wav", "rb");
    assert(file);
    unsigned char header[12];
    assert(fread(header, 1, sizeof(header), file) == sizeof(header));
    assert(!memcmp(header, "RIFF", 4) && !memcmp(header + 8, "WAVE", 4));
    unsigned char chunk[8];
    while (fread(chunk, 1, sizeof(chunk), file) == sizeof(chunk)) {
        unsigned bytes = chunk[4] | (unsigned)chunk[5] << 8 | (unsigned)chunk[6] << 16 | (unsigned)chunk[7] << 24;
        if (!memcmp(chunk, "data", 4)) {
            *count = bytes / 2;
            int16_t *samples = malloc(bytes);
            assert(samples && fread(samples, 1, bytes, file) == bytes);
            fclose(file);
            return samples;
        }
        assert(fseek(file, bytes + (bytes & 1), SEEK_CUR) == 0);
    }
    abort();
}

static void test_noise(unsigned rate) {
    for (unsigned kind = 0; kind < 4; kind++) {
        passport_vad_t vad;
        assert(passport_vad_init(&vad, rate));
        silence(&vad, 40);
        uint32_t random = 19;
        int32_t filtered = 0;
        int16_t samples[240];
        for (unsigned frame = 0; frame < 1000; frame++) {
            for (unsigned index = 0; index < rate / 100; index++) {
                random = random * 1664525u + 1013904223u;
                int32_t noise = (int32_t)(random >> 16) - 32768;
                filtered = (filtered * 7 + noise) / 8;
                samples[index] = kind == 0 ? noise / 16 : kind == 1 ? filtered / 16
                    : kind == 2 ? (int16_t)(2000 * sin(2 * 3.141592653589793 * 50 * (frame * (rate / 100) + index) / rate))
                    : (frame % 100 == 0 && index < 10 ? 32000 : 0);
            }
            passport_vad_process(&vad, samples, rate / 100);
        }
        printf("Noise rate=%u kind=%u voice=%u speech=%d\n", rate, kind, vad.voice_frames, vad.speech);
        assert(!vad.speech && !vad.ready);
    }
}

/* Feed `ms` of the speech sample starting at `from_ms`, then silence. */
static void burst(passport_vad_t *vad, const int16_t *pcm, size_t count, unsigned rate,
                  unsigned from_ms, unsigned ms) {
    size_t start = (size_t)from_ms * 16, end = start + (size_t)ms * 16;
    for (size_t index = start; index < end && index < count; index++) {
        passport_vad_process(vad, &pcm[index], 1);
        if (rate == 24000 && index % 2) passport_vad_process(vad, &pcm[index], 1);
    }
}

static void test_short_sounds(unsigned rate, const int16_t *pcm, size_t count) {
    passport_vad_t vad;
    /* The start-up thump and key click: voice-like audio in the first
     * moments, then a silent room for longer than the auto-send pause. */
    assert(passport_vad_init(&vad, rate));
    burst(&vad, pcm, count, rate, 600, PASSPORT_VAD_SETTLE_MS - 20);
    silence(&vad, 300);
    printf("Short rate=%u start-up voice=%u longest=%u speech=%d\n", rate, vad.voice_frames, vad.longest_ms, vad.speech);
    assert(!vad.speech && !vad.ready);
    /* A cough or a knock: one voice-like burst shorter than a word. */
    assert(passport_vad_init(&vad, rate));
    silence(&vad, 60);
    burst(&vad, pcm, count, rate, 600, 100);
    silence(&vad, 300);
    printf("Short rate=%u single burst voice=%u longest=%u speech=%d\n", rate, vad.voice_frames, vad.longest_ms, vad.speech);
    assert(!vad.speech && !vad.ready);
    /* Several such sounds spread out do not add up to speech either. */
    assert(passport_vad_init(&vad, rate));
    silence(&vad, 60);
    for (unsigned repeat = 0; repeat < 6; repeat++) {
        burst(&vad, pcm, count, rate, 600 + repeat * 150, 60);
        silence(&vad, 80);
    }
    silence(&vad, 300);
    printf("Short rate=%u scattered voice=%u longest=%u speech=%d\n", rate, vad.voice_frames, vad.longest_ms, vad.speech);
    assert(!vad.speech && !vad.ready);
    /* A short real word after the settling time still counts. */
    assert(passport_vad_init(&vad, rate));
    silence(&vad, 60);
    burst(&vad, pcm, count, rate, 600, 450);
    printf("Short rate=%u word voice=%u longest=%u spoken=%u speech=%d\n", rate, vad.voice_frames, vad.longest_ms, vad.spoken_ms, vad.speech);
    assert(vad.speech);
    silence(&vad, 210);
    assert(vad.ready);
}

int main(void) {
    passport_vad_t vad;
    assert(!passport_vad_init(NULL, 16000));
    assert(!passport_vad_init(&vad, 44100));
    for (unsigned rate = 16000; rate <= 24000; rate += 8000) {
        test_noise(rate);
        assert(passport_vad_init(&vad, rate));
        silence(&vad, 1000);
        assert(!vad.speech && !vad.active && !vad.ready);
        int16_t samples[240];
        for (unsigned frame = 0; frame < 1000; frame++) {
            for (unsigned index = 0; index < rate / 100; index++) samples[index] = 16000;
            passport_vad_process(&vad, samples, rate / 100);
        }
        assert(!vad.speech);
        assert(passport_vad_init(&vad, rate));
        silence(&vad, 40);
        for (unsigned impulse = 0; impulse < 10; impulse++) {
            memset(samples, 0, sizeof(samples));
            samples[20] = 30000;
            passport_vad_process(&vad, samples, rate / 100);
            silence(&vad, 100);
        }
        assert(!vad.speech && !vad.ready);
        size_t count;
        int16_t *pcm = speech_samples(&count);
        test_short_sounds(rate, pcm, count);
        for (unsigned gain = 1; gain <= 4; gain *= 2) {
            assert(passport_vad_init(&vad, rate));
            silence(&vad, 40);
            for (size_t index = 0; index < count; index++) {
                int16_t value = pcm[index] / (int)gain;
                passport_vad_process(&vad, &value, 1);
                if (rate == 24000 && index % 2) passport_vad_process(&vad, &value, 1);
            }
            printf("VAD rate=%u gain=1/%u frames=%u voice=%u longest=%u spoken=%u state=%d\n", rate, gain, vad.frames, vad.voice_frames, vad.longest_ms, vad.spoken_ms, vad.speech);
            assert(vad.speech);
            silence(&vad, 210);
            assert(vad.ready && !vad.active);
            passport_vad_block(&vad);
            assert(!vad.active && !vad.recent && !vad.filled && !vad.part);
        }
        free(pcm);
    }
    printf("VAD state: %zu bytes; silence/DC/impulses, start-up and short sounds rejected, words and quiet speech accepted, both capture rates PASS\n", sizeof(vad));
}
