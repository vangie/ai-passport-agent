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
        for (unsigned gain = 1; gain <= 4; gain *= 2) {
            assert(passport_vad_init(&vad, rate));
            silence(&vad, 40);
            for (size_t index = 0; index < count; index++) {
                int16_t value = pcm[index] / (int)gain;
                passport_vad_process(&vad, &value, 1);
                if (rate == 24000 && index % 2) passport_vad_process(&vad, &value, 1);
            }
            printf("VAD rate=%u gain=1/%u frames=%u voice=%u state=%d\n", rate, gain, vad.frames, vad.voice_frames, vad.speech);
            assert(vad.speech);
            silence(&vad, 210);
            assert(vad.ready && !vad.active);
            passport_vad_block(&vad);
            assert(!vad.active && !vad.recent && !vad.filled && !vad.part);
        }
        free(pcm);
    }
    printf("VAD state: %zu bytes; silence/DC/impulses, speech/quiet speech and both capture rates PASS\n", sizeof(vad));
}
