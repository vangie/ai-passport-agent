#include "passport_vad.h"
#include <string.h>

bool passport_vad_init(passport_vad_t *vad, unsigned sample_rate) {
    if (!vad || (sample_rate != 16000 && sample_rate != 24000)) return false;
    memset(vad, 0, sizeof(*vad));
    vad->divisor = sample_rate / 8000;
    return WebRtcVad_InitCore(&vad->core) == 0 && WebRtcVad_set_mode_core(&vad->core, 2) == 0;
}

void passport_vad_block(passport_vad_t *vad) {
    vad->filled = vad->part = 0;
    vad->sum = 0;
    vad->recent = 0;
    vad->active = false;
    vad->quiet_ms = 0;
    vad->core.over_hang = vad->core.num_of_speech = 0;
}

void passport_vad_process(passport_vad_t *vad, const int16_t *pcm, size_t count) {
    if (!vad || !vad->divisor || !pcm) return;
    for (size_t index = 0; index < count; index++) {
        vad->sum += pcm[index];
        if (++vad->part != vad->divisor) continue;
        vad->frame[vad->filled++] = (int16_t)(vad->sum / (int32_t)vad->divisor);
        vad->sum = 0;
        vad->part = 0;
        if (vad->filled != 80) continue;
        vad->filled = 0;
        bool voice = WebRtcVad_CalcVad8khz(&vad->core, vad->frame, 80) == 1;
        vad->frames++;
        vad->voice_frames += voice;
        vad->recent = (uint16_t)((vad->recent << 1) | voice);
        if (__builtin_popcount((unsigned)vad->recent) >= 8) vad->active = vad->speech = true;
        if (voice) vad->quiet_ms = 0;
        else if (vad->quiet_ms < 2000) vad->quiet_ms += 10;
        if (vad->quiet_ms >= 320) vad->active = false;
        if (vad->speech && vad->quiet_ms >= 2000) vad->ready = true;
    }
}
