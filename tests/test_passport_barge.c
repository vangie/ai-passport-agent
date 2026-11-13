#include "passport_barge.h"
#include <assert.h>
#include <stdio.h>

static uint32_t random_state = 17;
static int16_t signal(void) {
    random_state = random_state * 1664525u + 1013904223u;
    return (int16_t)((random_state >> 20) - 2048);
}

static void test_echo_and_double_talk(void) {
    for (unsigned scenario = 0; scenario < 7; scenario++) {
        passport_barge_t barge = {0};
        int16_t playback[256], microphone[256];
        bool interrupted = false, probed = false;
        for (unsigned frame = 0; frame < 220; frame++) {
            int64_t now = 1000000 + (int64_t)frame * 256 * 1000000 / 24000;
            bool ducked = barge.probing;
            for (unsigned sample = 0; sample < 256; sample++) playback[sample] = ducked ? 0 : signal();
            passport_barge_reference(&barge, playback, 256, now);
            for (unsigned sample = 0; sample < 256; sample++) {
                int64_t tick = now * 6 / 1000 - 64 - 96 + sample / 4;
                int32_t echo = passport_barge_sample(&barge, tick);
                microphone[sample] = (int16_t)(echo / 2 + 700);
                if ((scenario == 1 || scenario == 6) && frame > 30) microphone[sample] += signal() * 2;
                if (scenario == 2) microphone[sample] = 18000;
                if (scenario == 3 && frame == 30) microphone[sample] += signal() * 4;
                if (scenario == 4 && !ducked) microphone[sample] = signal() * 4;
                if (scenario == 5) microphone[sample] += signal() * 3;
            }
            bool current = passport_barge_process(&barge, microphone, 256, now + 10667, true, scenario == 5, scenario != 6);
            probed |= barge.probing;
            if (current) { interrupted = true; break; }
        }
        assert(interrupted == (scenario == 1));
        if (scenario == 0 || scenario == 2 || scenario == 3 || scenario == 5) assert(!probed);
        if (scenario == 4) assert(probed);
        assert(!barge.probing || scenario == 4 || scenario == 6);
    }
}

int main(void) {
    test_echo_and_double_talk();
    passport_barge_t barge = {.probing = true, .evidence = 5};
    int16_t silence[256] = {0};
    assert(!passport_barge_process(&barge, silence, 256, 100000, false, false, true));
    assert(!barge.probing && !barge.evidence);
    puts("Voice interruption: delayed echo, DC, clicks, false probes, double-talk and mute PASS");
}
