/* Run the application audio worker, including late acknowledgement/retry and
 * allocation failures, against the existing deterministic RTOS boundary. */
#define bsp_audio_read fixture_audio_read
#define bsp_audio_write fixture_audio_write
#define esp_timer_get_time fixture_timer_get_time
#define vTaskDelay fixture_task_delay
#define xSemaphoreTake fixture_sem_take
#define bsp_audio_set_volume fixture_audio_volume
#include "demo_stubs/demo_runtime.c"
#undef bsp_audio_read
#undef bsp_audio_write
#undef esp_timer_get_time
#undef vTaskDelay
#undef xSemaphoreTake
#undef bsp_audio_set_volume

static unsigned volume_calls;
static uint8_t last_volume;
void bsp_audio_set_volume(uint8_t percent) { volume_calls++;last_volume=percent; }

static esp_err_t init_error;
esp_err_t bsp_audio_init(void) { return init_error; }
esp_err_t bsp_audio_deinit(void) { return ESP_OK; }
SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    SemaphoreHandle_t sem = xSemaphoreCreateBinary();
    if (sem) xSemaphoreGive(sem);
    return sem;
}

esp_err_t passport_clip_begin(void) { return ESP_OK; }
esp_err_t passport_clip_append(const void *p, size_t bytes) { (void)p; (void)bytes; return ESP_OK; }
bool passport_clip_full(void) { return false; }

esp_err_t bsp_audio_read(void *pcm, size_t bytes);
esp_err_t bsp_audio_write(const void *pcm, size_t bytes);
int64_t esp_timer_get_time(void);
void vTaskDelay(TickType_t ticks);
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks);

#include "../main/passport_audio.c"

static int16_t fixture_sample;
static bool stop_after_write;
static bool fixture_alternating;
static int64_t fixture_clock;
static unsigned fixture_read_ms = 16;
static int64_t synthetic_end;
static unsigned playback_waits;
static bool test_playback_wakeup;
static uint8_t wakeup_pcm[960];
static unsigned playback_refill_ms, playback_stream_writes;
static bool playback_stream;

BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks) {
    if (playback_stream && semaphore == s_play_ready) {
        if (playback_stream_writes == 1) {
            if ((uint64_t)ticks >= playback_refill_ms) {
                fixture_clock += (int64_t)playback_refill_ms * 1000;
                playback_refill_ms = 0;
                assert(passport_audio_live_push(wakeup_pcm, sizeof(wakeup_pcm)) == ESP_OK);
            } else {
                playback_refill_ms -= ticks;
                fixture_clock += (int64_t)ticks * 1000;
            }
        }
    }
    if (test_playback_wakeup && semaphore == s_play_ready && ticks == portMAX_DELAY) {
        playback_waits++;
        if (playback_waits == 1) assert(passport_audio_live_push(wakeup_pcm, sizeof(wakeup_pcm)) == ESP_OK);
        else { s_stop = true; xSemaphoreGive(semaphore); }
    }
    BaseType_t result = fixture_sem_take(semaphore, ticks);
    if (!playback_stream && semaphore == s_play_ready && ticks != portMAX_DELAY && result != pdTRUE)
        fixture_clock += (int64_t)ticks * 1000;
    return result;
}

void vTaskDelay(TickType_t ticks) {
    fixture_clock += (int64_t)ticks * 1000;
    if (synthetic_end && fixture_clock >= synthetic_end) s_stop = true;
}

int64_t esp_timer_get_time(void) { return fixture_clock; }

esp_err_t bsp_audio_read(void *pcm, size_t bytes) {
    esp_err_t result = fixture_audio_read(pcm, bytes);
    fixture_clock += (int64_t)fixture_read_ms * 1000;
    if (result == ESP_OK) {
        int16_t *samples = pcm;
        for (size_t index = 0; index < bytes / sizeof(*samples); index++)
            samples[index] = fixture_alternating && index % 2 ? -fixture_sample : fixture_sample;
    }
    return result;
}

esp_err_t bsp_audio_write(const void *pcm, size_t bytes) {
    esp_err_t result = fixture_audio_write(pcm, bytes);
    if (playback_stream) {
        assert(bytes == sizeof(wakeup_pcm) && !memcmp(pcm, wakeup_pcm, bytes));
        fixture_clock += (int64_t)bytes * 1000 / 48;
        if (++playback_stream_writes == 2) s_stop = true;
    }
    if (stop_after_write) { stop_after_write = false; s_stop = true; }
    return result;
}

static void stop_after_three_chunks(void) {
    if (test_read_calls == 3) s_stop = true;
}

static void delayed_capture(void) {
    fixture_read_ms = test_read_calls==2 ? 83 : 11;
    if (test_read_calls==3) s_stop=true;
}

static void stop_live_playback(void) {
    if (test_read_calls == 1) s_stop = true;
}

static void submit_capture_fixture(void) {
    fixture_alternating=true;
    fixture_sample=test_read_calls>30 && test_read_calls<=50 ? 1000 : 40;
    if (test_read_calls==180) s_stop=true;
}

static void test_submit_capture_worker(void) {
    test_auto_ack=false;test_task_count=0;
    fixture_read_ms=16;test_read_hook=submit_capture_fixture;test_read_calls=0;
    assert(passport_audio_capture()==ESP_OK && !passport_audio_submit_ready());
    test_run_worker(s_task);
    assert(s_submit_ready && !passport_audio_submit_ready());
    assert(passport_audio_stop()==ESP_OK && passport_audio_release()==ESP_OK);
    test_task_count=0;test_read_calls=0;
    assert(passport_audio_start()==ESP_OK && !s_submit_ready);
    test_run_worker(s_task);
    assert(!s_submit_ready);
    assert(passport_audio_stop()==ESP_OK && passport_audio_release()==ESP_OK);
    fixture_alternating=false;fixture_sample=0;test_read_hook=NULL;test_read_calls=0;
}

static void input_fixture(int16_t *samples, int amplitude, int offset) {
    for (size_t index = 0; index < AUDIO_CHUNK; index++)
        samples[index] = (int16_t)(offset + (index % 2 ? amplitude : -amplitude));
}

static void test_ask_endpoint(void) {
    passport_endpoint_t endpoint = {0};
    for (unsigned window=0;window<750;window++)
        assert(!passport_endpoint_feed(&endpoint,40,40));
    assert(!endpoint.speech && !endpoint.ready);
    endpoint=(passport_endpoint_t){0};
    for (unsigned window=0;window<10;window++) assert(!passport_endpoint_feed(&endpoint,10000,40));
    for (unsigned window=0;window<60;window++) assert(!passport_endpoint_feed(&endpoint,40,40));
    assert(!endpoint.speech);
    assert(!passport_endpoint_feed(&endpoint,3000,40));
    for (unsigned window=0;window<60;window++) assert(!passport_endpoint_feed(&endpoint,40,40));
    assert(!endpoint.speech);
    for (unsigned window=0;window<6;window++) assert(!passport_endpoint_feed(&endpoint,1200,40));
    assert(endpoint.speech);
    for (unsigned window=0;window<25;window++) assert(!passport_endpoint_feed(&endpoint,40,40));
    assert(!passport_endpoint_feed(&endpoint,1000,40));
    for (unsigned window=0;window<49;window++) assert(!passport_endpoint_feed(&endpoint,40,40));
    assert(passport_endpoint_feed(&endpoint,40,40));
    assert(passport_endpoint_feed(&endpoint,1000,40));
    for (unsigned turn=0;turn<4;turn++) {
        endpoint=(passport_endpoint_t){0};
        for (unsigned window=0;window<10;window++) assert(!passport_endpoint_feed(&endpoint,1000,40));
        assert(!endpoint.speech && !endpoint.ready);
        for (unsigned window=0;window<10;window++) assert(!passport_endpoint_feed(&endpoint,1000,40));
        assert(endpoint.speech);
        for (unsigned window=0;window<49;window++) assert(!passport_endpoint_feed(&endpoint,40,40));
        assert(passport_endpoint_feed(&endpoint,40,40));
    }
    atomic_store(&s_submit_ready,true);s_retain=true;s_live_capture=false;s_stop=false;s_finished=false;
    assert(passport_audio_submit_ready());
    s_live_capture=true;assert(!passport_audio_submit_ready());
    s_live_capture=false;s_retain=false;assert(!passport_audio_submit_ready());
    s_retain=true;s_stop=true;assert(!passport_audio_submit_ready());
    s_stop=false;s_finished=true;assert(!passport_audio_submit_ready());
    s_finished=false;s_retain=false;atomic_store(&s_submit_ready,false);
}

static void test_live_input_boundaries(void) {
    live_input_gate_t gate = {0};
    int16_t samples[AUDIO_CHUNK];
    unsigned forwarded = 0;
    for (unsigned chunk = 0; chunk < 180; chunk++) {
        input_fixture(samples, 50, 600);
        if (live_input_prepare(&gate, samples, AUDIO_CHUNK, false)) {
            forwarded++;
            for (size_t index = 0; index < AUDIO_CHUNK; index++)
                assert(samples[index] == (index % 2 ? 650 : 550));
        }
    }
    assert(forwarded == 180);
    gate = (live_input_gate_t){0};
    input_fixture(samples, 166, 24);
    assert(live_input_prepare(&gate, samples, AUDIO_CHUNK, false) && samples[0] == -142);
    for (unsigned turn = 0; turn < 4; turn++) {
        input_fixture(samples, 500, -70);
        assert(live_input_prepare(&gate, samples, AUDIO_CHUNK, false));
        assert(samples[0] == -570 && samples[1] == 430);
        unsigned silence = 0;
        for (unsigned chunk = 0; chunk < 180; chunk++) {
            input_fixture(samples, 50, 60);
            if (!live_input_prepare(&gate, samples, AUDIO_CHUNK, false)) continue;
            silence++;
            assert(samples[0] == 10 && samples[1] == 110);
        }
        assert(silence == 180);
    }
    input_fixture(samples, 1000, 0);
    assert(!live_input_prepare(&gate, samples, AUDIO_CHUNK, true));
    assert(!gate.voiced);
    input_fixture(samples, 500, 0);
    assert(live_input_prepare(&gate, samples, AUDIO_CHUNK, false));
    input_fixture(samples, 80, 32000);
    gate = (live_input_gate_t){0};
    assert(live_input_prepare(&gate, samples, AUDIO_CHUNK, false) && samples[0] == 31920);
    input_fixture(samples, 32767, 0);
    assert(live_input_prepare(&gate, samples, AUDIO_CHUNK, false) && samples[0] == -32767);
    assert(s_input_rms >= 32766 && s_input_threshold >= 120 && s_input_threshold <= 360);
}

static void test_live_buffers(void) {
    test_auto_ack=false;
    assert(passport_audio_release() == ESP_OK);
    passport_audio_live_mute(true);
    assert(passport_audio_live_start() == ESP_OK);
    assert(s_muted);
    passport_audio_live_mute(false);
    assert(s_live_capture && s_task && s_play_task && s_live_pcm);
    assert(s_live_input != s_live_pcm && (uintptr_t)s_barge % _Alignof(passport_barge_t) == 0);
    uint8_t input[512], output[960], decoded[960];
    memset(input, 0x5a, sizeof(input)); memset(output, 0x35, sizeof(output));
    size_t bytes = 0;
    assert(passport_audio_live_pull(input, sizeof(input), NULL) == ESP_ERR_INVALID_ARG);
    assert(passport_audio_live_pull(input, sizeof(input)-1, &bytes) == ESP_ERR_INVALID_STATE);
    assert(passport_audio_live_pull(input, sizeof(input), &bytes) == ESP_OK && !bytes);
    s_input_head = s_input_tail = LIVE_INPUT_BYTES-512;
    live_input_push((const int16_t *)input);
    memset(decoded,0,sizeof(decoded));
    assert(passport_audio_live_pull(decoded,sizeof(input),&bytes)==ESP_OK && bytes==sizeof(input));
    assert(!memcmp(input,decoded,bytes));
    live_input_push((const int16_t *)input);
    passport_audio_live_mute(true);
    assert(passport_audio_live_pull(decoded,sizeof(input),&bytes)==ESP_OK && bytes==sizeof(input));
    for(size_t index=0;index<bytes;index++) assert(decoded[index]==0);
    passport_audio_live_mute(false);
    s_output_head=s_output_tail=LIVE_OUTPUT_BYTES-80;
    assert(passport_audio_live_push(output,sizeof(output))==ESP_OK && !passport_audio_live_drained());
    ring_copy(s_live_pcm,LIVE_OUTPUT_BYTES,s_output_tail,decoded,sizeof(decoded),false);
    assert(!memcmp(decoded,output,sizeof(output)));
    for(unsigned block=1;block<LIVE_OUTPUT_BYTES/sizeof(output);block++) assert(passport_audio_live_push(output,sizeof(output))==ESP_OK);
    size_t remainder=LIVE_OUTPUT_BYTES%sizeof(output);
    if (remainder) assert(passport_audio_live_push(output,remainder)==ESP_OK);
    assert(passport_audio_live_push(output,2)==ESP_ERR_NO_MEM);
    assert(passport_audio_live_push(output,3)==ESP_ERR_INVALID_ARG);
    passport_audio_live_response(true);
    assert(s_response && s_input_head==s_input_tail);
    unsigned queued_head=s_output_head;
    unsigned completed=passport_audio_live_completed();
    atomic_store(&s_playing,true);
    atomic_store(&s_echo_guard,7);
    passport_audio_live_mute(true);
    assert(passport_audio_live_clear()==ESP_OK && s_output_tail==queued_head && s_response && s_muted);
    assert(passport_audio_live_completed()==completed+LIVE_OUTPUT_BYTES);
    assert(!passport_audio_live_drained() && s_playing && s_echo_guard==7);
    atomic_store(&s_playing,false);
    assert(passport_audio_live_drained());
    assert(passport_audio_live_push(output,sizeof(output))==ESP_OK && !passport_audio_live_drained());
    ring_copy(s_live_pcm,LIVE_OUTPUT_BYTES,s_output_tail,decoded,sizeof(decoded),false);
    assert(!memcmp(decoded,output,sizeof(output)));
    live_input_push((const int16_t *)input);
    passport_audio_live_mute(true);
    assert(passport_audio_live_pull(decoded,sizeof(input),&bytes)==ESP_OK && bytes==sizeof(input));
    for(size_t index=0;index<bytes;index++) assert(decoded[index]==0);
    passport_audio_live_mute(false);
    live_input_push((const int16_t *)input);
    assert(passport_audio_live_clear()==ESP_OK && s_input_head-s_input_tail==sizeof(input) && passport_audio_live_drained());
    passport_audio_live_mute(false);
    assert(passport_audio_live_pull(decoded,sizeof(input),&bytes)==ESP_OK && bytes==sizeof(input));
    assert(!memcmp(input,decoded,bytes));
    passport_audio_live_response(false);
    passport_audio_live_mute(false);
    assert(passport_audio_live_push(output,sizeof(output))==ESP_OK);
    assert(s_input_head==s_input_tail);
    test_read_calls=0;test_read_hook=stop_after_three_chunks;
    test_run_worker(s_task);
    assert(passport_audio_live_available()==3*AUDIO_CHUNK*sizeof(int16_t));
    s_finished=false;
    size_t silent_bytes=0;
    assert(passport_audio_live_pull(decoded,sizeof(decoded),&silent_bytes)==ESP_OK && silent_bytes==sizeof(decoded));
    for(size_t index=0;index<silent_bytes;index++) assert(decoded[index]==0);
    s_finished=true;
    ring_copy(s_live_pcm,LIVE_OUTPUT_BYTES,s_output_tail,decoded,sizeof(decoded),false);
    assert(!memcmp(decoded,output,sizeof(output)));
    s_stop=false;test_read_hook=NULL;
    assert(passport_audio_live_clear()==ESP_OK);
    assert(s_task && s_play_task && !s_stop);
    TaskHandle_t capture=s_task, playback=s_play_task;
    assert(passport_audio_live_push(output,sizeof(output))==ESP_OK);
    stop_after_write=true;
    test_run_worker(playback);test_run_worker(capture);
    passport_audio_diagnostics_t diag;
    passport_audio_diagnostics(&diag);
    assert(diag.played==sizeof(output) && passport_audio_live_drained());
    assert(passport_audio_stop()==ESP_OK && capture->deleted && playback->deleted);
    assert(passport_audio_release()==ESP_OK && !s_live_pcm && !s_live_capture);
    assert(passport_audio_live_start()==ESP_OK);
    assert(passport_audio_live_completed()==0);
    test_read_calls=0;test_read_hook=stop_live_playback;
    assert(passport_audio_live_push(output,sizeof(output))==ESP_OK);
    test_write_calls=0;test_write_fail_at=1;
    test_run_worker(s_play_task);
    assert(s_error==ESP_FAIL && s_stop);
    passport_audio_diagnostics(&diag);
    assert(diag.played==0);
    test_run_worker(s_task);
    assert(passport_audio_stop()==ESP_FAIL);
    assert(passport_audio_release()==ESP_OK && !s_live_pcm);
    test_write_fail_at=0;test_read_hook=NULL;
    assert(passport_audio_live_start()==ESP_OK);
    s_input_head=s_input_tail+LIVE_INPUT_BYTES;
    test_read_calls=0;test_read_hook=stop_after_three_chunks;
    test_run_worker(s_task);
    assert(s_error==ESP_OK && passport_audio_live_dropped()==3 && passport_audio_live_available()==LIVE_INPUT_BYTES);
    passport_audio_diagnostics(&diag);
    assert(diag.voice_dropped==0);
    assert(diag.sample_rate==24000 && diag.capture_ms==48 && diag.read_gap_ms==16);
    assert(diag.clipped_total==0 && diag.input_high_water==0);
    test_run_worker(s_play_task);
    assert(passport_audio_stop()==ESP_OK && passport_audio_release()==ESP_OK);
    assert(passport_audio_live_start()==ESP_OK);
    s_input_head=s_input_tail+LIVE_INPUT_BYTES;
    fixture_sample=1000;fixture_alternating=true;
    test_read_calls=0;test_run_worker(s_task);
    passport_audio_diagnostics(&diag);
    assert(diag.voice_dropped==3 && passport_audio_live_dropped()==3);
    test_run_worker(s_play_task);
    assert(passport_audio_stop()==ESP_OK && passport_audio_release()==ESP_OK);
    fixture_sample=0;fixture_alternating=false;
    test_read_hook=NULL;
}

static void test_synthetic_capture(void) {
    test_task_count = 0;
    unsigned reads = test_read_calls, writes = test_write_calls;
    init_error = ESP_FAIL;
    assert(passport_audio_live_test_start() == ESP_OK);
    assert(s_synthetic && !s_initialized);
    fixture_clock = 0;
    synthetic_end = 100000;
    test_run_worker(s_task);
    test_run_worker(s_play_task);
    assert(s_chunks == 9 && s_input_head == 9 * 512 && !passport_audio_live_dropped());
    int codec_index=0;
    for (size_t chunk = 0; chunk < 9; chunk++) {
        int16_t level = 80 + chunk * 16;
        int16_t decoded[AUDIO_CHUNK], expected[AUDIO_CHUNK];
        uint8_t encoded[LIVE_INPUT_BLOCK];
        for (size_t index=0;index<AUDIO_CHUNK;index++) expected[index]=index%2?level:-level;
        assert(passport_ima_encode(expected,AUDIO_CHUNK,&codec_index,encoded,sizeof(encoded))==sizeof(encoded));
        assert(passport_ima_decode(encoded,sizeof(encoded),expected,AUDIO_CHUNK)==AUDIO_CHUNK);
        const uint8_t *block = s_live_input + chunk * LIVE_INPUT_BLOCK;
        assert(passport_ima_decode(block,LIVE_INPUT_BLOCK,decoded,AUDIO_CHUNK)==AUDIO_CHUNK);
        assert(!memcmp(decoded,expected,sizeof(decoded)));
    }
    assert(test_read_calls == reads && test_write_calls == writes);
    assert(passport_audio_stop() == ESP_OK && passport_audio_release() == ESP_OK);
    assert(!s_synthetic);
    synthetic_end = 0;
    test_task_count = 0;
    assert(passport_audio_live_test_start() == ESP_OK);
    test_playback_wakeup = true;
    playback_waits = 0;
    int64_t playback_started = fixture_clock;
    test_run_worker(s_play_task);
    test_run_worker(s_task);
    assert(playback_waits == 2 && passport_audio_live_completed() == sizeof(wakeup_pcm));
    assert(fixture_clock - playback_started >= 160000 && fixture_clock - playback_started < 200000);
    assert(test_read_calls == reads && test_write_calls == writes);
    test_playback_wakeup = false;
    assert(passport_audio_stop() == ESP_OK && passport_audio_release() == ESP_OK);
    init_error = ESP_OK;
    synthetic_end = 0;
}

static void test_live_interrupt(void) {
    test_task_count = 0;
    assert(passport_audio_live_start() == ESP_OK);
    uint8_t output[960], input[512], decoded[512];
    memset(output, 0x31, sizeof(output)); memset(input, 0x54, sizeof(input));
    for (unsigned turn = 0; turn < 5; turn++) {
        passport_audio_live_response(true);
        assert(passport_audio_live_push(output, sizeof(output)) == ESP_OK);
        live_input_push((const int16_t *)input);
        TaskHandle_t capture = s_task, playback = s_play_task;
        bool muted = turn % 2;
        passport_audio_live_mute(muted);
        assert(passport_audio_live_interrupt() && s_output_blocked && !s_response && !s_output_pcm);
        assert(s_task == capture && s_play_task == playback && !s_stop && s_muted == muted);
        assert(passport_audio_live_take_interrupt() && !passport_audio_live_take_interrupt());
        assert(passport_audio_live_push(output, sizeof(output)) == ESP_OK && passport_audio_live_drained());
        assert(passport_audio_live_position() == (turn + 1) * sizeof(output));
        size_t bytes = 0;
        assert(passport_audio_live_clear() == ESP_OK && !s_output_blocked);
        assert(passport_audio_live_pull(decoded, sizeof(decoded), &bytes) == ESP_OK && bytes == sizeof(input));
        for (size_t index = 0; index < bytes; index++) assert(decoded[index] == (muted ? 0 : input[index]));
    }
    s_stop = true; test_run_worker(s_task); test_run_worker(s_play_task);
    assert(passport_audio_stop() == ESP_OK && passport_audio_release() == ESP_OK);
}

static void test_compressed_input_ring(void) {
    test_task_count=0;
    assert(passport_audio_live_start()==ESP_OK);
    assert(LIVE_INPUT_BYTES==13824 && LIVE_INPUT_STORAGE==3624);
    int16_t samples[AUDIO_CHUNK], expected[AUDIO_CHUNK];
    uint8_t encoded[LIVE_INPUT_BLOCK], output[512];
    int codec_index=0;
    for (unsigned block=0;block<90;block++) {
        for (unsigned sample=0;sample<AUDIO_CHUNK;sample++) samples[sample]=(int16_t)((sample*83+block*19)%8000-4000);
        assert(passport_ima_encode(samples,AUDIO_CHUNK,&codec_index,encoded,sizeof(encoded))==sizeof(encoded));
        assert(passport_ima_decode(encoded,sizeof(encoded),expected,AUDIO_CHUNK)==AUDIO_CHUNK);
        live_input_push(samples);
        assert(passport_audio_live_push(output,4)==ESP_OK);
        size_t offset=0;
        while (offset<sizeof(expected)) {
            size_t count=(block%7+1)*14,bytes=0;
            if (count>sizeof(expected)-offset) count=sizeof(expected)-offset;
            assert(passport_audio_live_pull(output,count,&bytes)==ESP_OK && bytes==count);
            assert(!memcmp(output,(uint8_t *)expected+offset,bytes));offset+=bytes;
        }
        assert(passport_audio_live_clear()==ESP_OK && !passport_audio_live_available());
    }
    s_stop=true;test_run_worker(s_task);test_run_worker(s_play_task);
    assert(passport_audio_stop()==ESP_OK && passport_audio_release()==ESP_OK);
}

static void test_live_silence_clock(void) {
    test_task_count=0;
    assert(passport_audio_live_test_start()==ESP_OK);
    assert(!s_input_silence);
    fixture_clock=0;
    synthetic_end=100000;
    passport_audio_live_response(true);
    test_run_worker(s_task);
    assert(s_chunks==9 && s_input_silence==9*512 && s_input_head==s_input_tail);
    assert(s_response_chunks==9 && !s_input_clipped);
    s_finished=false;
    uint8_t output[960];size_t bytes=0,total=0;
    while(passport_audio_live_available()) {
        memset(output,0x7f,sizeof(output));
        assert(passport_audio_live_pull(output,sizeof(output),&bytes)==ESP_OK && bytes>0);
        for(size_t index=0;index<bytes;index++) assert(output[index]==0);
        total+=bytes;
    }
    assert(total==9*512);
    s_finished=true;
    test_run_worker(s_play_task);
    assert(passport_audio_stop()==ESP_OK && passport_audio_release()==ESP_OK);
    test_task_count=0;
    assert(passport_audio_live_test_start()==ESP_OK);
    fixture_clock=0;synthetic_end=100000;
    passport_audio_live_mute(true);
    test_run_worker(s_task);
    assert(s_chunks==9 && s_mute_chunks==9 && s_input_silence==9*512);
    assert(s_input_silence<=LIVE_INPUT_BYTES);
    passport_audio_live_mute(false);
    assert(s_input_silence==9*512 && s_input_head==s_input_tail);
    s_finished=false;s_stop=false;
    synthetic_end=fixture_clock+160000;
    test_run_worker(s_task);
    assert(s_input_head>s_input_tail);
    size_t silence=s_input_silence;
    s_finished=false;
    while(silence) {
        size_t capacity=silence<sizeof(output)?silence:sizeof(output);
        assert(passport_audio_live_pull(output,capacity,&bytes)==ESP_OK && bytes==capacity);
        for(size_t index=0;index<bytes;index++) assert(output[index]==0);
        silence-=bytes;
    }
    assert(passport_audio_live_pull(output,sizeof(output),&bytes)==ESP_OK && bytes>0);
    bool signal=false;
    for(size_t index=0;index<bytes;index++) signal|=output[index]!=0;
    assert(signal);
    size_t retained=passport_audio_live_available();
    assert(passport_audio_live_clear()==ESP_OK && passport_audio_live_available()==retained);
    s_finished=true;
    test_run_worker(s_play_task);
    assert(passport_audio_stop()==ESP_OK && passport_audio_release()==ESP_OK);
    synthetic_end=0;
}

static void test_playback_packet_jitter(void) {
    for (unsigned delay = 5; delay <= 60; delay += 55) {
        test_task_count = 0;
        assert(passport_audio_live_start() == ESP_OK);
        fixture_clock = 1000000;
        memset(wakeup_pcm, 0x35, sizeof(wakeup_pcm));
        assert(passport_audio_live_push(wakeup_pcm, sizeof(wakeup_pcm)) == ESP_OK);
        assert(xSemaphoreTake(s_play_ready, 0) == pdTRUE);
        playback_stream_writes = 0;
        playback_refill_ms = delay;
        playback_stream = true;
        passport_audio_live_response(true);
        test_run_worker(s_play_task);
        playback_stream = false;
        test_run_worker(s_task);
        passport_audio_diagnostics_t diag;
        passport_audio_diagnostics(&diag);
        assert(playback_stream_writes == 2 && diag.played == 2 * sizeof(wakeup_pcm));
        if (delay == 5) {
            assert(fixture_clock == 1205000 && !diag.play_stalls && !diag.play_gap_ms);
        } else {
            assert(fixture_clock == 1420000 && diag.play_stalls == 1 && diag.play_gap_ms == 220);
        }
        assert(passport_audio_stop() == ESP_OK && passport_audio_release() == ESP_OK);
    }
}

static void test_compressed_playback(void) {
    test_task_count = 0;
    assert(passport_audio_live_ima_start(false) == ESP_OK && s_ima);
    uint8_t block[PASSPORT_IMA_BYTES];
    int16_t pcm[480];
    int index = 0;
    for (unsigned sample = 0; sample < 480; sample++) pcm[sample] = (int16_t)(sample * 17 - 4000);
    assert(passport_ima_encode(pcm, 480, &index, block, sizeof(block)) == sizeof(block));
    s_output_head = s_output_tail = LIVE_OUTPUT_BYTES - 100;
    unsigned blocks = LIVE_OUTPUT_BYTES / PASSPORT_IMA_BYTES;
    for (unsigned frame = 0; frame < blocks; frame++) assert(passport_audio_live_push_ima(block, sizeof(block)) == ESP_OK);
    assert(blocks >= 40 && s_output_pcm == blocks * 960);
    assert(passport_audio_live_push_ima(block, sizeof(block)) == ESP_ERR_NO_MEM);
    assert(passport_audio_live_push(pcm, sizeof(pcm)) == ESP_ERR_INVALID_ARG);
    assert(!passport_audio_live_drained());
    stop_after_write = true;
    test_run_worker(s_play_task);
    test_run_worker(s_task);
    assert(s_played == 960 && passport_audio_live_completed() == 960 && s_output_pcm == (blocks - 1) * 960);
    assert(passport_audio_live_clear() == ESP_OK && passport_audio_live_completed() == blocks * 960);
    assert(passport_audio_live_drained() && s_output_pcm == 0);
    assert(passport_audio_stop() == ESP_OK && passport_audio_release() == ESP_OK);
    test_task_count = 0;
    assert(passport_audio_live_ima_start(false) == ESP_OK);
    index = 0;
    size_t bytes = passport_ima_encode(pcm, 3, &index, block, sizeof(block));
    assert(bytes == 7 && passport_audio_live_push_ima(block, bytes) == ESP_OK);
    fixture_clock = 1000000;
    stop_after_write = true;
    test_run_worker(s_play_task);
    test_run_worker(s_task);
    assert(s_played == 6 && passport_audio_live_completed() == 6);
    assert(fixture_clock == 1640000 && passport_audio_live_drained());
    assert(passport_audio_stop() == ESP_OK && passport_audio_release() == ESP_OK && !s_ima);
}

int main(void) {
    test_ask_endpoint();
    assert(passport_audio_set_volume(75)==ESP_OK && s_output_volume==75);
    assert(volume_calls==1 && last_volume==75);
    assert(passport_audio_set_volume(101)==ESP_ERR_INVALID_ARG && s_output_volume==75);
    assert(volume_calls==1);
    test_live_input_boundaries();
    passport_audio_diagnostics_t diag;
    test_sem_fails = true;
    assert(passport_audio_start() == ESP_ERR_NO_MEM);
    passport_audio_diagnostics(&diag);
    assert(diag.error == ESP_ERR_NO_MEM && !diag.worker_owned);
    test_sem_fails = false;
    init_error = ESP_FAIL;
    assert(passport_audio_start() == ESP_FAIL);
    assert(!s_initialized);
    init_error = ESP_OK;
    test_sem_fails = true;
    assert(passport_audio_start() == ESP_ERR_NO_MEM);
    passport_audio_diagnostics(&diag);
    assert(strcmp(diag.phase, "stop_signal") == 0);
    test_sem_fails = false;
    test_create_fails = true;
    assert(passport_audio_start() == ESP_ERR_NO_MEM);
    passport_audio_diagnostics(&diag);
    assert(strcmp(diag.phase, "worker") == 0 && !diag.worker_owned);
    test_create_fails = false;

    assert(passport_audio_start() == ESP_OK);
    TaskHandle_t first = s_task;
    assert(passport_audio_start() == ESP_ERR_INVALID_STATE);
    assert(passport_audio_stop() == ESP_ERR_TIMEOUT);
    assert(s_task == first && !first->deleted);
    test_run_worker(first);
    assert(passport_audio_finished());
    assert(passport_audio_stop() == ESP_OK && !s_task && first->deleted);

    for (int i = 0; i < 4; i++) {
        const int16_t signals[]={1000,INT16_MIN,0,INT16_MAX};
        fixture_sample=signals[i];
        test_read_calls = 0;
        test_read_hook = stop_after_three_chunks;
        assert(passport_audio_start() == ESP_OK);
        passport_audio_diagnostics(&diag);
        assert(!diag.mean_square && !diag.dc && !diag.clipped && !diag.played);
        test_run_worker(s_task);
        assert(passport_audio_finished());
        assert(passport_audio_stop() == ESP_OK);
        passport_audio_diagnostics(&diag);
        assert(diag.chunks == 3 && !diag.worker_owned && diag.error == ESP_OK);
        assert(diag.sample_rate==16000 && diag.capture_ms==48 && diag.read_gap_ms==16);
        assert(diag.clipped_total==(i==1 || i==3 ? 768u : 0u));
        assert(diag.mean_square==(uint32_t)((int32_t)fixture_sample*fixture_sample) && diag.dc==fixture_sample);
        assert(diag.clipped==(i==1 || i==3 ? LEVEL_WINDOW : 0));
    }
    fixture_sample=0;
    test_read_calls=0;test_read_hook=delayed_capture;
    assert(passport_audio_start()==ESP_OK);
    test_run_worker(s_task);
    assert(passport_audio_stop()==ESP_OK);
    passport_audio_diagnostics(&diag);
    assert(diag.capture_ms==105 && diag.read_gap_ms==83 && !diag.clipped_total);
    fixture_read_ms=16;
    fixture_sample=0;
    test_read_hook = NULL;
    test_read_calls = 0;
    test_read_fail_at = 1;
    assert(passport_audio_start() == ESP_OK);
    test_run_worker(s_task);
    assert(passport_audio_stop() == ESP_FAIL);
    passport_audio_diagnostics(&diag);
    assert(diag.chunks == 0 && diag.error == ESP_FAIL && !diag.worker_owned);
    assert(strcmp(diag.phase, "read") == 0);
    test_read_fail_at = 0;
    assert(passport_audio_start() == ESP_OK);
    test_auto_ack = true;
    assert(passport_audio_stop() == ESP_OK);
    assert(test_task_deletes == 8);
    test_auto_ack = false;
    assert(passport_audio_start() == ESP_OK);
    first = s_task;
    assert(passport_audio_stop() == ESP_ERR_TIMEOUT);
    passport_audio_diagnostics(&diag);
    assert(diag.error == ESP_ERR_TIMEOUT);
    test_run_worker(first);
    /* A fresh start must reap a worker that acknowledged after the timeout. */
    assert(passport_audio_start() == ESP_OK && first->deleted);
    test_auto_ack = true;
    assert(passport_audio_stop() == ESP_OK);
    assert(test_task_deletes == 10);
    test_submit_capture_worker();
    test_task_count=0;
    test_live_buffers();
    test_live_interrupt();
    test_compressed_input_ring();
    test_synthetic_capture();
    test_live_silence_clock();
    test_playback_packet_jitter();
    test_compressed_playback();
    test_task_count=0;
    for (unsigned transition=0;transition<4;transition++) {
        assert(!s_task && !s_play_task && !s_live_pcm && !s_live_capture);
        assert(passport_audio_capture()==ESP_OK);
        assert(s_retain && !s_play_task && !s_live_pcm);
        test_auto_ack=true;
        assert(passport_audio_stop()==ESP_OK && passport_audio_release()==ESP_OK);
        test_auto_ack=false;
        assert(passport_audio_live_start()==ESP_OK);
        unsigned calls_before=volume_calls;
        assert(s_output_volume==75 && passport_audio_set_volume(20)==ESP_ERR_INVALID_STATE);
        assert(last_volume==75 && volume_calls==calls_before);
        assert(!s_retain && s_play_task && s_live_pcm && passport_audio_live_completed()==0);
        assert(passport_audio_capture()==ESP_ERR_INVALID_STATE);
        test_auto_ack=false;
        s_stop=true;
        test_run_worker(s_play_task);
        test_run_worker(s_task);
        assert(passport_audio_stop()==ESP_OK && passport_audio_release()==ESP_OK);
        assert(!s_task && !s_play_task && !s_live_pcm && !s_live_capture);
    }
    assert(passport_audio_set_volume(0)==ESP_OK && s_output_volume==0);
    assert(last_volume==0);
    assert(passport_audio_set_volume(100)==ESP_OK && s_output_volume==100);
    assert(last_volume==100);
    puts("Passport audio: allocation recovery, capture/playback lifecycle, packet jitter/rebuffer, continuous silent clock, mute/unmute and speech-gated silence submit PASS");
}
