#include "passport_barge.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static uint32_t random_state = 17;
static int16_t noise_sample(void) {
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
            if (barge.probing) passport_barge_paused(&barge,now);
            for (unsigned sample = 0; sample < 256; sample++) playback[sample] = barge.probing?0:noise_sample();
            passport_barge_reference(&barge, playback, 256, now);
            for (unsigned sample = 0; sample < 256; sample++) {
                int64_t tick = now * 6 / 1000 - 64 - 96 + sample / 4;
                int32_t echo = passport_barge_sample(&barge, tick);
                microphone[sample] = (int16_t)(echo / 2 + 700);
                if ((scenario == 1 || scenario == 4 || scenario == 6) && frame > 30) microphone[sample] += noise_sample() * 2;
                if (scenario == 2) microphone[sample] = 18000;
                if (scenario == 3 && frame == 30) microphone[sample] += noise_sample() * 4;
                if (scenario == 5) microphone[sample] += noise_sample() * 3;
            }
            bool current = passport_barge_process(&barge, microphone, 256, now + 10667, true, scenario == 5, scenario != 6 && scenario != 4);
            probed |= barge.probing;
            if (current) { interrupted = true; break; }
        }
        assert(interrupted == (scenario == 1));
        if (scenario == 0 || scenario == 2 || scenario == 3 || scenario == 5) assert(!probed);
        if (scenario == 4) assert(probed);
        assert(!barge.probing || scenario == 4 || scenario == 6);
    }
}

static void test_echo_during_confirmation(void) {
    passport_barge_t barge = {.probing=true,.probe_at=1000000,.evidence=9,.confirmed=2};
    int16_t playback[256], microphone[256];
    for (unsigned sample=0;sample<256;sample++) playback[sample]=(int16_t)(noise_sample()*2);
    passport_barge_reference(&barge,playback,256,1090000);
    for (unsigned sample=0;sample<256;sample++) {
        int64_t tick=1090000*6/1000+sample/4;
        microphone[sample]=(int16_t)(passport_barge_sample(&barge,tick)/2+700);
    }
    assert(!passport_barge_process(&barge,microphone,256,1100667,true,false,true));
    assert(!barge.probing && !barge.confirmed && barge.cooldown>1100667);
}

static void test_probe_waits_for_playback(void) {
    passport_barge_t barge={.probing=true,.probe_at=1000000,.evidence=9};
    int16_t microphone[256];
    for (unsigned frame=0;frame<20;frame++) {
        for (unsigned sample=0;sample<256;sample++) microphone[sample]=noise_sample()*2;
        assert(!passport_barge_process(&barge,microphone,256,1000000+frame*10667,true,false,true));
    }
}

static void test_unmatched_echo_probe(void) {
    for (unsigned speaking=0;speaking<2;speaking++) {
        passport_barge_t barge={0};
        int16_t playback[256],microphone[256];
        bool interrupted=false,paused=false;
        for (unsigned frame=0;frame<80;frame++) {
            int64_t now=1000000+(int64_t)frame*10667;
            if (barge.probing) { passport_barge_paused(&barge,now);paused=true; }
            for (unsigned sample=0;sample<256;sample++) {
                playback[sample]=noise_sample();
                bool residual=!barge.quiet_at || now-barge.quiet_at<70000;
                microphone[sample]=(speaking || residual)?noise_sample()*2:0;
            }
            passport_barge_reference(&barge,playback,256,now);
            if (passport_barge_process(&barge,microphone,256,now,true,false,true)) { interrupted=true;break; }
        }
        assert(paused && interrupted==(speaking!=0));
    }
}

static int16_t *speech;
static size_t speech_count;

static void load_speech(void) {
    FILE *source=fopen("assets/audio/vad-speech.wav","rb");
    assert(source);
    unsigned char header[12],chunk[8];
    assert(fread(header,1,sizeof(header),source)==sizeof(header));
    assert(!memcmp(header,"RIFF",4) && !memcmp(header+8,"WAVE",4));
    while (fread(chunk,1,sizeof(chunk),source)==sizeof(chunk)) {
        unsigned bytes=chunk[4]|(unsigned)chunk[5]<<8|(unsigned)chunk[6]<<16|(unsigned)chunk[7]<<24;
        if (!memcmp(chunk,"data",4)) {
            speech_count=bytes/2;
            speech=malloc(bytes);
            assert(speech && fread(speech,1,bytes,source)==bytes);
            fclose(source);
            return;
        }
        assert(!fseek(source,bytes+(bytes&1),SEEK_CUR));
    }
    abort();
}

static int32_t speech_at(int64_t index) {
    if (index<0) return 0;
    int64_t phase=index*2;
    size_t origin=(size_t)(phase/3);
    return origin+1<speech_count ? (speech[origin]*(3-phase%3)+speech[origin+1]*(phase%3))/3 : 0;
}

static void test_filtered_speech_echo(void) {
    load_speech();
    for (unsigned double_talk=0;double_talk<2;double_talk++) {
    for (unsigned scenario=0;scenario<5;scenario++) {
        passport_barge_t barge={0};
        int16_t playback[480],microphone[256];
        size_t queued=0;
        unsigned interrupts=0;
        int32_t filtered=0;
        for (size_t cursor=0;cursor+256<speech_count*3/2;cursor+=256) {
            int64_t frame_at=1000000+(int64_t)cursor*1000000/24000;
            if (barge.probing) passport_barge_paused(&barge,frame_at);
            while (queued<cursor+256) {
                for (size_t index=0;index<480;index++) playback[index]=barge.probing?0:(int16_t)speech_at(queued+index);
                passport_barge_reference(&barge,playback,480,1000000+(int64_t)queued*1000000/24000);
                queued+=480;
            }
            for (size_t index=0;index<256;index++) {
                int64_t position=(int64_t)(cursor+index)-583-scenario*13;
                int32_t value=speech_at(position);
                if (scenario>=1) value=(value*3+speech_at(position-7)*2)/5;
                if (scenario>=2) { filtered=(filtered*3+value)/4;value=filtered; }
                if (scenario>=3) value=value*3/2;
                if (barge.quiet_at && frame_at-barge.quiet_at>=50000) value=0;
                if (double_talk) value=value/2+speech_at((int64_t)(cursor+index)-24000)*2;
                if (value>12000) value=12000;
                if (value < -12000) value=-12000;
                microphone[index]=(int16_t)value;
            }
            int64_t now=1000000+(int64_t)(cursor+256)*1000000/24000;
            interrupts+=passport_barge_process(&barge,microphone,256,now,true,false,true);
        }
        printf("Speech echo scenario=%u double_talk=%u interrupts=%u\n",scenario,double_talk,interrupts);
        assert((interrupts>0)==(double_talk!=0));
    }
    }
    free(speech);
}

int main(void) {
    test_probe_waits_for_playback();
    test_unmatched_echo_probe();
    test_echo_and_double_talk();
    test_echo_during_confirmation();
    test_filtered_speech_echo();
    passport_barge_t barge = {.probing = true, .evidence = 5};
    int16_t silence[256] = {0};
    assert(!passport_barge_process(&barge, silence, 256, 100000, false, false, true));
    assert(!barge.probing && !barge.evidence);
    puts("Voice interruption: delayed echo, DC, clicks, false probes, double-talk and mute PASS");
}
