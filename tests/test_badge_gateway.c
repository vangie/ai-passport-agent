#define BADGE_GATEWAY_HOST_TEST 1
#include "../main/badge_gateway.c"
#include <assert.h>

static int64_t clock_us=1000000;
static bool online=true,cancelled,can_abort,worker_context,fail_connect,lose_upload,cancel_upload;
static bool disconnect_reply,suppress_history;
static bool reject_subscription;
static unsigned connections,closed,uploads,history,aborts,subscriptions,task_creations,pings;
static badge_agent_t selected;
static const char *expected_live_language = "zh";
static const char *events[256];
static size_t event_count,event_index,event_offset;
static char wire[16000];
static size_t wire_used;
static char clip_id[33]="clip-1";
static bool read_cancel;
static unsigned worker_priority = 3;

static void event(const char *json) { assert(event_count-event_index<256);events[event_count++%256]=json; }
static void clear_events(void) { event_count=event_index=event_offset=0; }
int64_t esp_timer_get_time(void) { return clock_us; }
void vTaskDelay(unsigned ticks) { clock_us+=(int64_t)ticks*1000; }
void vTaskPrioritySet(TaskHandle_t task, unsigned priority) {
    assert(!task && (priority == 3 || priority == 5));
    worker_priority = priority;
}
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return worker_context?s_worker:NULL; }
int xTaskCreate(void (*entry)(void *),const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *out) {
    (void)entry;(void)name;(void)stack;(void)arg;(void)priority;task_creations++;*out=(void *)3;return pdPASS;
}
void xTaskNotifyGive(TaskHandle_t task) { (void)task; }
unsigned ulTaskNotifyTake(int clear,unsigned timeout) { (void)clear;vTaskDelay(timeout);return 0; }
void esp_fill_random(void *out,size_t size) { memset(out,1,size); }
esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *out) { (void)name;(void)mode;*out=1;return ESP_OK; }
esp_err_t nvs_get_blob(nvs_handle_t handle,const char *name,void *out,size_t *size) { (void)handle;(void)name;memset(out,1,*size);return ESP_OK; }
esp_err_t nvs_set_blob(nvs_handle_t handle,const char *name,const void *data,size_t size) { (void)handle;(void)name;(void)data;(void)size;return ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t handle) { (void)handle;return ESP_OK; }
void nvs_close(nvs_handle_t handle) { (void)handle; }
bool esp_sntp_enabled(void) { return true; }
void esp_sntp_setoperatingmode(int mode) { (void)mode; }
void esp_sntp_setservername(int index,const char *name) { (void)index;(void)name; }
void esp_sntp_init(void) {}
esp_err_t esp_crt_bundle_attach(void *conf) { (void)conf;return ESP_OK; }
bool badge_wifi_is_online(void) { return online; }
esp_err_t badge_wifi_gateway_performance(bool enabled) { (void)enabled;return ESP_OK; }
bool badge_link_agent_snapshot(badge_agent_t *out) { *out=selected;return selected.url[0]!=0; }
bool badge_link_store_token(const badge_agent_t *expected,const char *token) { (void)expected;snprintf(selected.device_token,sizeof(selected.device_token),"%s",token);selected.bootstrap[0]=0;return true; }
esp_err_t badge_link_pause_bluetooth(void) { return ESP_OK; }
esp_err_t badge_link_resume_bluetooth(void) { return ESP_OK; }
esp_err_t badge_audio_release(void) { return ESP_OK; }
static unsigned live_starts, live_pushes, live_stops, live_clears;
static unsigned synthetic_starts;
static bool synthetic_fixture;
static bool capture_failed;
static unsigned talk_creates, talk_closes, talk_appends, talk_marks;
static bool talk_reject, talk_bad_format, talk_cancel, talk_wrong_owner, talk_malformed, talk_no_ack, talk_input;
static bool talk_scope_denied, talk_no_ready, talk_append_reject, talk_tools, tool_rejected, tool_early;
static bool talk_provider_error, talk_early_error;
static unsigned talk_recovery, talk_fault;
static unsigned talk_duplex, duplex_appends, duplex_backlog;
static struct { int64_t ready_at; char json[200]; } capture_acks[32];
static unsigned capture_ack_head, capture_ack_tail;
static int64_t append_write_delay;
static int64_t capture_started;
static size_t capture_chunks, capture_queued, capture_dropped, capture_uploaded;
static size_t capture_high_water;
static size_t first_upload_queued;
static size_t capture_capacity = 13440;
static size_t capture_pulled;
static int64_t capture_receive_resume;
static bool capture_receive_stalled;
static uint32_t live_completed;
static bool slow_playback;
static unsigned delayed_ack;
static bool deferred_playback, received_during_playback;
static int64_t playback_ready_at;
static int64_t talk_event_delay;
static unsigned tool_calls, tool_results;
static int64_t tool_wait_us, tool_final_at;
static char delayed_tool_final[400];
static bool cancel_tool_wait;
static unsigned tool_wait_appends;
static int64_t tool_resume_at;
static bool tool_resume_test;
static bool live_response;
static bool talk_compact;
static bool talk_ima;
static unsigned ima_starts, ima_pushes, ima_uploads;
static char batched_audio_event[5500];
static char burst_marks[20][180];
static size_t batched_audio_bytes;
static void capture_progress(void) {
    if (talk_duplex != 4 && talk_duplex < 6) return;
    size_t chunks = (size_t)((clock_us - capture_started) * 24000 / 1000000 / 256);
    while (capture_chunks < chunks) {
        capture_chunks++;
        if (capture_queued + 512 <= capture_capacity) capture_queued += 512;
        else capture_dropped++;
        if (capture_queued > capture_high_water) capture_high_water = capture_queued;
    }
}
esp_err_t badge_audio_live_start(void) {
    assert(worker_priority == 5);
    live_starts++;
    capture_started = clock_us;
    capture_chunks = capture_queued = capture_dropped = capture_uploaded = 0;
    capture_high_water = 0;
    first_upload_queued = 0;
    capture_ack_head = capture_ack_tail = 0;
    capture_pulled = 0;
    capture_receive_resume = 0;
    capture_receive_stalled = false;
    live_completed = 0;
    return ESP_OK;
}
esp_err_t badge_audio_live_test_start(void) {
    assert(synthetic_fixture);
    synthetic_starts++;
    return badge_audio_live_start();
}
esp_err_t badge_audio_live_ima_start(bool synthetic) {
    ima_starts++;
    return synthetic ? badge_audio_live_test_start() : badge_audio_live_start();
}
esp_err_t badge_audio_live_push_ima(const uint8_t *block, size_t bytes) {
    int16_t pcm[480];
    size_t samples = badge_ima_decode(block, bytes, pcm, 480);
    assert(samples);
    ima_pushes++;
    live_pushes++; live_completed += samples * 2;
    return ESP_OK;
}
bool badge_audio_finished(void) { return capture_failed && s_talk_trace.appends >= 5; }
esp_err_t badge_audio_live_pull(void *out, size_t cap, size_t *bytes) {
    assert(cap<=960 && !(cap%2));
    capture_progress();
    if (!capture_pulled) first_upload_queued = capture_queued;
    bool capture_timed=talk_duplex==4 || talk_duplex>=6;
    *bytes=talk_input?(capture_timed && capture_queued<cap?capture_queued:cap):0;
    if (capture_timed) capture_queued-=*bytes;
    if (capture_timed) {
        for (size_t index=0;index<*bytes;index++) ((uint8_t *)out)[index]=(uint8_t)(capture_pulled+index);
        capture_pulled+=*bytes;
    } else memset(out,0,*bytes);
    return ESP_OK;
}
size_t badge_audio_live_available(void) {
    capture_progress();
    return talk_input?(talk_duplex==4 || talk_duplex>=6?capture_queued:talk_duplex==5?3840:960):0;
}
uint32_t badge_audio_live_dropped(void) { capture_progress();return (uint32_t)capture_dropped; }
uint32_t badge_audio_live_completed(void) { return talk_fault==3 || (deferred_playback && clock_us<playback_ready_at)?0:live_completed; }
esp_err_t badge_audio_live_push(const void *pcm, size_t bytes) {
    if (talk_recovery==10) {
        assert(bytes==960);
        for (size_t index=0;index<bytes;index++) assert(((const uint8_t *)pcm)[index]==0x35);
        batched_audio_bytes+=bytes;
    } else assert(bytes==4);
    live_pushes++;live_completed+=bytes;
    if (slow_playback) clock_us+=700000;
    if (deferred_playback) playback_ready_at=clock_us+200000;
    return ESP_OK;
}
esp_err_t badge_audio_live_clear(void) { live_clears++;return ESP_OK; }
esp_err_t badge_audio_stop(void) { live_stops++;return ESP_OK; }
void badge_audio_live_response(bool speaking) { live_response=speaking; }
bool badge_audio_live_drained(void) { return talk_fault!=3 && (!deferred_playback || clock_us>=playback_ready_at); }
void badge_memory_report(const char *phase) { (void)phase; }
void badge_tls_set_proxy(const badge_proxy_t *proxy) { (void)proxy; }
const char *badge_tls_proxy_error(void) { return NULL; }
esp_err_t badge_tls_control_prepare(void) { return ESP_OK; }
void badge_tls_control_begin(void) { cancelled=can_abort=false; }
bool badge_tls_cancelled(void) { return cancelled; }
void badge_tls_request_cancel(bool cooperative) { cancelled=true;can_abort=cooperative; }
bool badge_tls_cancel_can_write(void) { return can_abort; }
void badge_tls_set_writing(bool writing) { (void)writing; }
void badge_tls_track_socket(int socket) { (void)socket; }
void badge_tls_forget_socket(void) {}
esp_transport_handle_t esp_transport_ssl_init(void) { return (void *)1; }
esp_transport_handle_t esp_transport_tcp_init(void) { return (void *)1; }
void esp_transport_ssl_crt_bundle_attach(esp_transport_handle_t handle,esp_err_t (*attach)(void *)) { (void)handle;(void)attach; }
esp_transport_handle_t passport_transport_ws_init(esp_transport_handle_t parent) { (void)parent;return (void *)2; }
esp_err_t passport_transport_ws_set_config(esp_transport_handle_t handle,const passport_transport_ws_config_t *config) {
    (void)handle;assert(config->ws_path);
    assert((strstr(config->ws_path,"/passport/v1")!=NULL)==s_talk_auth);
    return ESP_OK;
}
esp_err_t passport_transport_ws_set_headers(esp_transport_handle_t handle,const char *headers) { (void)handle;assert(headers);return ESP_OK; }
int esp_transport_get_socket(esp_transport_handle_t handle) { (void)handle;return -1; }
int esp_transport_connect(esp_transport_handle_t handle,const char *host,int port,int timeout) {
    (void)handle;(void)host;(void)port;(void)timeout;connections++;clear_events();
    if (fail_connect) return -1;
    event("{\"type\":\"event\",\"event\":\"connect.challenge\",\"payload\":{\"nonce\":\"fixture\",\"ts\":1700000000000}}");return 0;
}
int esp_transport_poll_read(esp_transport_handle_t handle,int timeout) { (void)handle;(void)timeout;return talk_duplex == 12 || event_index<event_count; }
int passport_transport_ws_get_read_opcode(esp_transport_handle_t handle) { (void)handle;return WS_TRANSPORT_OPCODES_TEXT; }
int64_t passport_transport_ws_last_receive_us(esp_transport_handle_t handle) { (void)handle;return 0; }
const char *passport_transport_ws_failure(esp_transport_handle_t handle) { (void)handle;return "fixture-transport"; }
int esp_transport_read(esp_transport_handle_t handle,char *out,int size,int timeout) {
    (void)handle;
    if (read_cancel) { read_cancel=false;assert(badge_gateway_cancel()); }
    if (cancelled && !can_abort) return -1;
    if (capture_receive_resume && clock_us<capture_receive_resume) {
        clock_us+=(int64_t)timeout*1000;
        return 0;
    }
    if (talk_duplex>=6 && capture_ack_tail!=capture_ack_head &&
        capture_acks[capture_ack_tail%32].ready_at<=clock_us) {
        event(capture_acks[capture_ack_tail%32].json);
        capture_ack_tail++;
    }
    if (event_index==event_count) {
        if (tool_final_at) {
            assert(live_response && s_state==BADGE_SEND_WAITING);
            assert(talk_appends>=tool_wait_appends);
            clock_us+=(int64_t)timeout*1000;
            s_last_rx_us=clock_us;
            if (cancel_tool_wait) { assert(badge_gateway_cancel());return -1; }
            if (clock_us<tool_final_at) return 0;
            tool_final_at=0;event(delayed_tool_final);
        } else if (tool_resume_at) {
            clock_us+=(int64_t)timeout*1000;
            if (clock_us<tool_resume_at) return 0;
            tool_resume_at=0;
            assert(!live_response && !tool_resume_test);
            event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"close\",\"reason\":\"completed\"}}");
        } else if (talk_recovery==7) {
            clock_us+=10000000;
            event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"error\",\"message\":\"Realtime provider error.\"}}");
        } else { clock_us+=(int64_t)timeout*1000;return 0; }
    }
    const char *next_event=events[event_index%256];
    if (!next_event) { event_index++;return -1; }
    if (deferred_playback && clock_us<playback_ready_at && strstr(next_event,"passport-a-")) received_during_playback=true;
    if (talk_recovery && !event_offset && !strcmp(next_event,"{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"ready\"}}") && s_state==BADGE_SEND_CONNECTING) {
        clock_us+=talk_event_delay;
        talk_event_delay=0;
    }
    size_t count=strlen(next_event)-event_offset;
    if (count>(size_t)size) count=(size_t)size;
    if (talk_duplex) {
        if (count>37) count=37;
        clock_us+=talk_duplex==5?5000:500;
    }
    memcpy(out,next_event+event_offset,count);event_offset+=count;
    if (talk_duplex==9 && !capture_receive_stalled && event_offset &&
        strstr(next_event,"\"type\":\"inputAudio\"")) {
        capture_receive_stalled=true;
        capture_receive_resume=clock_us+900000;
    }
    if (!next_event[event_offset]) { event_index++;event_offset=0; }
    return (int)count;
}
int passport_transport_ws_send_raw(esp_transport_handle_t handle,int opcode,const char *data,int size,int timeout) {
    (void)handle;(void)timeout;
    if ((opcode&15)==WS_TRANSPORT_OPCODES_PING) { pings++;return 0; }
    if ((opcode&15)==WS_TRANSPORT_OPCODES_TEXT) wire_used=0;
    assert(wire_used+(size_t)size<sizeof(wire));memcpy(wire+wire_used,data,(size_t)size);wire_used+=(size_t)size;wire[wire_used]=0;
    if (!(opcode&WS_TRANSPORT_OPCODES_FIN)) {
        if (cancel_upload) { cancel_upload=false;assert(badge_gateway_cancel());return -1; }
        if (lose_upload) { lose_upload=false;return -1; }
        return size;
    }
    cJSON *frame=cJSON_Parse(wire);assert(frame);
    const char *method=str(frame,"method");
    if (!strcmp(method,"connect")) event(talk_scope_denied ?
        "{\"type\":\"res\",\"id\":\"passport-connect\",\"ok\":true,\"payload\":{\"passportProtocol\":1,\"auth\":{\"scopes\":[\"operator.read\",\"operator.write\"]}}}" :
        "{\"type\":\"res\",\"id\":\"passport-connect\",\"ok\":true,\"payload\":{\"passportProtocol\":1,\"policy\":{\"maxPayload\":2000000},\"auth\":{\"deviceToken\":\"fixture-device-token\",\"scopes\":[\"operator.read\",\"operator.write\",\"operator.talk\"]}}}");
    if (!strcmp(method,"sessions.messages.subscribe")) {
        subscriptions++;assert(!strcmp(str(cJSON_GetObjectItem(frame,"params"),"key"),s_session));
        event(reject_subscription ? "{\"type\":\"res\",\"id\":\"passport-subscribe\",\"ok\":false}" :
            "{\"type\":\"res\",\"id\":\"passport-subscribe\",\"ok\":true,\"payload\":{\"subscribed\":true}}");
    }
    if (!strcmp(method,"chat.send")) {
        uploads++;assert(strstr(wire,clip_id));
        event("{\"type\":\"res\",\"id\":\"passport-send\",\"ok\":true,\"payload\":{\"runId\":\"fixture-run\"}}");
        if (disconnect_reply) { disconnect_reply=false;event(NULL); }
    }
    if (!strcmp(method,"chat.history")) {
        history++;
        if (!suppress_history)
        event("{\"type\":\"res\",\"id\":\"passport-history\",\"ok\":true,\"payload\":{\"messages\":[{\"role\":\"assistant\",\"stopReason\":\"stop\",\"__openclaw\":{\"runId\":\"fixture-run\"},\"content\":\"fixture reply\"}]}}");
    }
    if (!strcmp(method,"chat.abort")) { aborts++;event("{\"type\":\"res\",\"id\":\"passport-cancel\",\"ok\":true}"); }
    if (!strcmp(method,"talk.session.create")) {
        talk_creates++;
        const cJSON *params=cJSON_GetObjectItem(frame,"params");
        assert(!strcmp(str(params,"mode"),"realtime"));
        assert(!strcmp(str(params,"brain"),"agent-consult"));
        assert(!strcmp(str(params,"language"),expected_live_language));
        assert(!strcmp(str(params,"transport"),"gateway-relay"));
        if (synthetic_fixture) {
            char key[sizeof(s_session) + 12];
            snprintf(key, sizeof(key), "%s-synthetic", s_session);
            assert(!strcmp(str(params,"sessionKey"),key));
        } else assert(!strcmp(str(params,"sessionKey"),s_session));
        if (talk_reject) event("{\"type\":\"res\",\"id\":\"passport-talk-create\",\"ok\":false,\"error\":{\"message\":\"unknown method\"}}");
        else {
            if (talk_early_error) {
                event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"error\",\"message\":\"401 Unauthorized fixture-secret\",\"phase\":\"connect\"}}");
                event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"close\",\"reason\":\"error\"}}");
            }
            if (talk_recovery==1) event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"error\",\"message\":\"Realtime provider error.\",\"phase\":\"connect\"}}");
            if (!talk_no_ready && talk_recovery!=2 && talk_recovery!=5 && talk_recovery!=7)
                event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"ready\"}}");
            event(talk_bad_format ? "{\"type\":\"res\",\"id\":\"passport-talk-create\",\"ok\":true,\"payload\":{\"sessionId\":\"live-1\",\"audio\":{\"inputEncoding\":\"pcm16\",\"inputSampleRateHz\":16000}}}" : talk_ima ?
                "{\"type\":\"res\",\"id\":\"passport-talk-create\",\"ok\":true,\"payload\":{\"passportIma\":true,\"passportSilence\":true,\"sessionId\":\"live-1\",\"audio\":{\"inputEncoding\":\"pcm16\",\"inputSampleRateHz\":24000,\"outputEncoding\":\"pcm16\",\"outputSampleRateHz\":24000}}}" : talk_compact ?
                "{\"type\":\"res\",\"id\":\"passport-talk-create\",\"ok\":true,\"payload\":{\"passportSilence\":true,\"sessionId\":\"live-1\",\"audio\":{\"inputEncoding\":\"pcm16\",\"inputSampleRateHz\":24000,\"outputEncoding\":\"pcm16\",\"outputSampleRateHz\":24000}}}" :
                "{\"type\":\"res\",\"id\":\"passport-talk-create\",\"ok\":true,\"payload\":{\"sessionId\":\"live-1\",\"audio\":{\"inputEncoding\":\"pcm16\",\"inputSampleRateHz\":24000,\"outputEncoding\":\"pcm16\",\"outputSampleRateHz\":24000}}}");
            if (talk_fault==1) event(NULL);
            else if (talk_duplex || talk_fault==2) { duplex_appends=duplex_backlog=0; }
            else if (synthetic_fixture) {}
            else if (talk_recovery==12) {
                int16_t pcm[480] = {0};uint8_t block[246];char encoded[329];int index=0;size_t length=0;
                assert(badge_ima_encode(pcm,480,&index,block,sizeof(block))==246);
                assert(!mbedtls_base64_encode((uint8_t *)encoded,sizeof(encoded),&length,block,sizeof(block)));
                snprintf(batched_audio_event,sizeof(batched_audio_event),"{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"audio\",\"audioBase64\":\"passport-ima:%s\"}}",encoded);
                for (unsigned mark=0;mark<20;mark++) {
                    event(batched_audio_event);
                    snprintf(burst_marks[mark],sizeof(burst_marks[mark]),"{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"mark\",\"markName\":\"audio-%u\"}}",mark+1);
                    event(burst_marks[mark]);
                }
                event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"audioDone\"}}");
            }
            else if (talk_recovery==10 || talk_recovery==11) {
                uint8_t pcm[3840]; memset(pcm,0x35,sizeof(pcm));
                char encoded[5121];size_t length=0;
                if (talk_recovery==11) {
                    uint8_t blocks[984]; int index=0;
                    for (unsigned part=0;part<4;part++)
                        assert(badge_ima_encode((int16_t *)(pcm+part*960),480,&index,blocks+part*246,246)==246);
                    assert(!mbedtls_base64_encode((uint8_t *)encoded,sizeof(encoded),&length,blocks,sizeof(blocks)));
                } else assert(!mbedtls_base64_encode((uint8_t *)encoded,sizeof(encoded),&length,pcm,sizeof(pcm)));
                snprintf(batched_audio_event,sizeof(batched_audio_event),"{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"audio\",\"audioBase64\":\"%s%s\",\"playbackSequence\":1}}",talk_recovery==11?"passport-ima:":"",encoded);
                event(batched_audio_event);
                event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"audioDone\",\"playbackSequence\":2}}");
                event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"mark\",\"markName\":\"batch\",\"playbackSequence\":3}}");
                event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"close\"}}");
            }
            else if (talk_recovery==9) {
                event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"inputAudio\",\"talkEvent\":{\"seq\":900}}}");
                event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"audio\",\"audioBase64\":\"AQIDBA==\",\"playbackSequence\":1,\"talkEvent\":{\"seq\":2}}}");
                event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"audio\",\"audioBase64\":\"AQIDBA==\",\"playbackSequence\":1,\"talkEvent\":{\"seq\":2}}}");
                event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"audioDone\",\"playbackSequence\":2,\"talkEvent\":{\"seq\":3}}}");
                event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"mark\",\"markName\":\"paced\",\"playbackSequence\":3}} ");
                event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"close\",\"talkEvent\":{\"seq\":901}}}");
            }
            else if (talk_recovery) {
                if (talk_recovery!=1 && talk_recovery!=8) {
                    if (talk_recovery>=3 && talk_recovery!=5 && talk_recovery!=7)
                        event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"audio\",\"audioBase64\":\"AQIDBA==\"}}");
                    event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"stranger\",\"type\":\"error\",\"message\":\"unknown method\"}}");
                    event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"error\",\"message\":\"Realtime provider error.\",\"phase\":\"stream\"}}");
                    event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"error\",\"message\":\"Realtime provider is unavailable. Try again later.\"}}");
                }
                if (talk_recovery!=5 && talk_recovery!=6 && talk_recovery!=7) {
                    if (talk_recovery==3 || talk_recovery==4 || talk_recovery==8) {
                        if (talk_recovery==8) event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"audio\",\"audioBase64\":\"AQIDBA==\"}}");
                        event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"stranger\",\"type\":\"clear\"}}");
                        event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"clear\"}}");
                    }
                    if (talk_recovery==2 || talk_recovery==3) event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"ready\"}}");
                    event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"audio\",\"audioBase64\":\"AQIDBA==\"}}");
                    event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"audioDone\"}}");
                    event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"mark\",\"markName\":\"recovered\"}}");
                    event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"close\",\"reason\":\"completed\"}}");
                }
            }
            else if (talk_cancel) assert(badge_gateway_cancel());
            else if (talk_provider_error) {
                event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"stranger\",\"type\":\"error\",\"message\":\"unknown method\"}}");
                event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"error\",\"message\":\"401 Unauthorized fixture-secret\",\"phase\":\"connect\"}}");
                event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"close\",\"reason\":\"error\"}}");
            }
            else if (talk_tools) {
                event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"other\",\"type\":\"toolCall\",\"callId\":\"wrong\",\"name\":\"openclaw_agent_consult\",\"args\":{\"question\":\"wrong\"}}}");
                event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"toolCall\",\"callId\":\"call-1\",\"name\":\"openclaw_agent_consult\",\"args\":{\"question\":\"现在几点\",\"context\":\"助手\",\"responseStyle\":\"简洁\"}}}");
                event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"toolCall\",\"callId\":\"call-1\",\"name\":\"openclaw_agent_consult\",\"args\":{\"question\":\"重复\"}}}");
            } else if (!talk_no_ack && !talk_no_ready && !talk_append_reject) {
                if (talk_wrong_owner) event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"stranger\",\"type\":\"audio\",\"audioBase64\":\"AQIDBA==\"}}");
                event(talk_malformed ? "{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"audio\",\"audioBase64\":\"invalid!\"}}" :
                    "{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"audio\",\"audioBase64\":\"AQIDBA==\",\"talkEvent\":{\"seq\":2}}}");
                event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"audio\",\"audioBase64\":\"AQIDBA==\",\"talkEvent\":{\"seq\":2}}}");
                event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"audioDone\",\"talkEvent\":{\"seq\":3}}}");
                event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"mark\",\"markName\":\"done-1\"}}");
                if (talk_fault==5) {
                    event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"audio\",\"audioBase64\":\"AQIDBA==\",\"talkEvent\":{\"seq\":4}}}");
                    event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"audioDone\",\"talkEvent\":{\"seq\":5}}}");
                    event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"mark\",\"markName\":\"done-2\"}}");
                }
                if (talk_fault!=3) event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"close\",\"reason\":\"error\"}}");
            }
        }
    }
    if (!strcmp(method,"talk.session.appendAudio")) {
        assert(timeout==1500 && size<(int)sizeof(s_receive->output));
        const cJSON *params=cJSON_GetObjectItemCaseSensitive(frame,"params");
        assert(!strcmp(str(params,"sessionId"),"live-1"));
        uint8_t decoded[3840];size_t decoded_size=0;
        const char *audio=str(params,"audioBase64");
        if (!strncmp(audio,"passport-silence:",17)) {
            assert(talk_compact && talk_duplex==1);
            decoded_size=(size_t)strtoul(audio+17,NULL,10);
            assert(decoded_size && decoded_size<=3840 && !(decoded_size%2));
            memset(decoded,0,decoded_size);
        } else if (!strncmp(audio,"passport-ima:",13)) {
            assert(talk_ima);
            uint8_t blocks[984]; size_t bytes=0;
            assert(!mbedtls_base64_decode(blocks,sizeof(blocks),&bytes,(const uint8_t *)audio+13,strlen(audio+13)));
            for (size_t offset=0;offset<bytes;) {
                assert(bytes-offset>=6);
                size_t count=(size_t)blocks[offset+4]|(size_t)blocks[offset+5]<<8;
                size_t block_bytes=6+count/2;
                assert(block_bytes<=bytes-offset && decoded_size+count*2<=sizeof(decoded));
                assert(badge_ima_decode(blocks+offset,block_bytes,(int16_t *)(decoded+decoded_size),480)==count);
                decoded_size+=count*2; offset+=block_bytes;
            }
            ima_uploads++;
        } else assert(!mbedtls_base64_decode(decoded,sizeof(decoded),&decoded_size,(const uint8_t *)audio,strlen(audio)));
        assert(decoded_size && decoded_size<=3840 && !(decoded_size%2));
        if (talk_duplex==4 || talk_duplex>=6) {
            if (!talk_ima) for (size_t index=0;index<decoded_size;index++) assert(decoded[index]==(uint8_t)(capture_uploaded+index));
        }
        if (talk_fault==2) { cJSON_Delete(frame);return -37; }
        talk_appends++;
        if (tool_resume_test && tool_resume_at) tool_resume_test=false;
        if (talk_duplex) {
            clock_us+=talk_duplex==11 && !(duplex_appends%20)?600000:
                talk_duplex==8 && !(duplex_appends%20)?213000:
                append_write_delay ? append_write_delay : talk_duplex==4?50000:talk_duplex>=6?10000:100000;
            capture_uploaded+=decoded_size;
            duplex_appends++;
            if (duplex_appends>1) {
                char diagnostic[256];badge_gateway_diagnostic(diagnostic,sizeof(diagnostic));
                assert(s_talk_retained.appends>0 && s_talk_retained.elapsed_ms>0 && strstr(diagnostic,"replies="));
            }
            event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"inputAudio\",\"byteLength\":900,\"talkEvent\":{\"type\":\"input.audio.delta\",\"payload\":{\"byteLength\":900}}}}");
            if (talk_duplex<6 && !(duplex_appends%20)) {
                event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"responseStarted\"}}");
                for (unsigned index=0;index<6;index++)
                    event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"audio\",\"audioBase64\":\"AQIDBA==\"}}");
                event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"audioDone\"}}");
                event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"mark\",\"markName\":\"duplex\"}}");
            }
        }
        if (!talk_no_ack) {
            if (delayed_ack) { clock_us+=delayed_ack;delayed_ack=0; }
            static char replies[256][200];static unsigned next_reply;
            char *reply=replies[next_reply++ % 256];
            snprintf(reply,200,"{\"type\":\"res\",\"id\":\"%s\",\"ok\":%s}",str(frame,"id"),talk_append_reject?"false":"true");
            if (talk_duplex>=6) {
                assert(capture_ack_head-capture_ack_tail<32);
                unsigned slot=capture_ack_head++%32;
                capture_acks[slot].ready_at=clock_us+(talk_duplex==7?1800000:450000);
                snprintf(capture_acks[slot].json,sizeof(capture_acks[slot].json),"%s",reply);
            } else event(reply);
        }
        if (talk_duplex) {
            if (event_count-event_index>duplex_backlog) duplex_backlog=(unsigned)(event_count-event_index);
            if (talk_duplex==2 && duplex_appends==10) read_cancel=true;
            if (duplex_appends==100 && !synthetic_fixture) {
                talk_input=false;
                event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"close\",\"reason\":\"completed\"}}");
            }
        }
    }
    if (!strcmp(method,"talk.client.toolCall")) {
        tool_calls++;
        const cJSON *params=cJSON_GetObjectItem(frame,"params");
        assert(!strcmp(str(params,"sessionKey"),s_session) && !strcmp(str(params,"relaySessionId"),"live-1"));
        assert(!strcmp(str(params,"callId"),"call-1"));
        const cJSON *args=cJSON_GetObjectItem(params,"args");
        assert(!strcmp(str(args,"question"),"现在几点") && !strcmp(str(args,"context"),"助手"));
        static char tool_reply[350], tool_final[400], wrong_final[400];
        snprintf(tool_reply,sizeof(tool_reply),"{\"type\":\"res\",\"id\":\"%s\",\"ok\":%s,\"payload\":{\"runId\":\"voice-run\",\"agentSessionKey\":\"%s\"}}",str(frame,"id"),tool_rejected?"false":"true",s_session);
        snprintf(tool_final,sizeof(tool_final),"{\"event\":\"chat\",\"payload\":{\"runId\":\"voice-run\",\"sessionKey\":\"%s\",\"state\":\"final\",\"message\":{\"content\":\"助手查询结果\"}}}",s_session);
        snprintf(wrong_final,sizeof(wrong_final),"{\"event\":\"chat\",\"payload\":{\"runId\":\"other-run\",\"sessionKey\":\"%s\",\"state\":\"final\",\"message\":{\"content\":\"错误结果\"}}}",s_session);
        event(wrong_final);
        if (tool_early) event(tool_final);
        event(tool_reply);
        if (!tool_early) {
            if (!tool_rejected) {
                event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"toolResult\",\"callId\":\"call-1\",\"talkEvent\":{\"type\":\"tool.progress\"}}}");
                event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"toolResult\",\"callId\":\"call-1\",\"talkEvent\":{\"type\":\"tool.result\",\"final\":false}}}");
            }
            if (tool_wait_us) {
                tool_final_at=clock_us+tool_wait_us;
                tool_wait_appends=talk_appends;
                snprintf(delayed_tool_final,sizeof(delayed_tool_final),"%s",tool_final);
            } else event(tool_final);
        }
    }
    if (!strcmp(method,"talk.session.submitToolResult")) {
        tool_results++;
        const cJSON *params=cJSON_GetObjectItem(frame,"params"), *result=cJSON_GetObjectItem(params,"result");
        assert(!strcmp(str(params,"sessionId"),"live-1") && !strcmp(str(params,"callId"),"call-1"));
        assert(tool_rejected ? str(result,"error")[0]!=0 : !strcmp(str(result,"result"),"助手查询结果"));
        static char tool_ack[180];
        snprintf(tool_ack,sizeof(tool_ack),"{\"type\":\"res\",\"id\":\"%s\",\"ok\":true}",str(frame,"id"));event(tool_ack);
        if (tool_resume_test) {
            event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"audio\",\"audioBase64\":\"AQIDBA==\"}}");
            event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"audioDone\"}}");
            tool_resume_at=clock_us+200000;
        } else event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"close\",\"reason\":\"completed\"}}");
    }
    if (!strcmp(method,"talk.session.close")) {
        talk_closes++;
        const cJSON *params=cJSON_GetObjectItem(frame,"params");
        if (!strcmp(str(params,"sessionId"),"transcribe-1") && !cJSON_IsTrue(cJSON_GetObjectItem(params,"discard"))) {
            event("{\"event\":\"talk.event\",\"payload\":{\"transcriptionSessionId\":\"transcribe-1\",\"type\":\"transcript\",\"text\":\"听写内容\",\"final\":true}}");
            event("{\"event\":\"talk.event\",\"payload\":{\"transcriptionSessionId\":\"transcribe-1\",\"type\":\"close\",\"reason\":\"completed\"}}");
        }
    }
    if (!strcmp(method,"talk.session.acknowledgeMark")) {
        if (talk_fault==4) { cJSON_Delete(frame);return -38; }
        talk_marks++;
        if (talk_recovery==12) {
            assert(clock_us>=playback_ready_at && live_completed==19200);
            if (!strcmp(str(cJSON_GetObjectItem(frame,"params"),"markName"),"audio-20"))
                event("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"close\"}}");
        }
        if (talk_recovery && talk_recovery!=12) assert(s_state==BADGE_SEND_LISTENING && !live_response);
    }
    cJSON_Delete(frame);return size;
}
int esp_transport_close(esp_transport_handle_t handle) { (void)handle;closed++;return 0; }
int esp_transport_destroy(esp_transport_handle_t handle) { (void)handle;return 0; }
size_t badge_clip_size(void) { return 8; }
size_t badge_clip_wire_size(void) { return 66; }
const char *badge_clip_id(void) { return clip_id; }
void badge_clip_stream_header(bool streaming) { (void)streaming; }
esp_err_t badge_clip_read(size_t offset,void *out,size_t count) { (void)offset;memset(out,1,count);return ESP_OK; }

static void step(gateway_service_t *service) { worker_context=true;service_step(service);worker_context=false; }
static void selected_route(const char *url) {
    memset(&selected,0,sizeof(selected));snprintf(selected.url,sizeof(selected.url),"%s",url);
    snprintf(selected.device_token,sizeof(selected.device_token),"fixture-device-token");
}
static void chat_event(const char *session,const char *run,const char *fields) {
    static char buffers[16][1000];static unsigned next;
    assert(next<16);snprintf(buffers[next],sizeof(buffers[next]),"{\"type\":\"event\",\"event\":\"chat\",\"payload\":{\"sessionKey\":\"%s\",\"runId\":\"%s\",%s}}",session,run,fields);
    event(buffers[next++]);
}
int main(void) {
    assert(talk_mark_sequence("audio-20")==20);
    assert(!talk_mark_sequence("audio-20junk") && !talk_mark_sequence("audio-") && !talk_mark_sequence("audio--1"));
    assert(!talk_mark_sequence("audio-18446744073709551616"));
    char s_upload_encoded[1281];
    const char *sessions[]={"live-1","quote\"slash\\\n中文"};
    for (size_t index=0;index<sizeof(sessions)/sizeof(sessions[0]);index++) {
        size_t prefix=talk_audio_prefix(s_upload_encoded,sizeof(s_upload_encoded),sessions[index],UINT32_MAX);
        assert(prefix>0);
        memcpy(s_upload_encoded+prefix,"\"}}",4);
        cJSON *request=cJSON_Parse(s_upload_encoded);assert(request);
        assert(!strcmp(str(request,"id"),"passport-a-4294967295"));
        assert(!strcmp(str(cJSON_GetObjectItemCaseSensitive(request,"params"),"sessionId"),sessions[index]));
        cJSON_Delete(request);
    }
    char oversized[1281];memset(oversized,'x',sizeof(oversized)-1);oversized[sizeof(oversized)-1]=0;
    assert(!talk_audio_prefix(s_upload_encoded,sizeof(s_upload_encoded),oversized,0));
    gateway_service_t service={0};selected_route("wss://fixture.example");
    badge_gateway_maintain(true);step(&service);
    assert(connections==1 && service.ws && subscriptions==1 && uploads==0 && !badge_gateway_active());
    s_message[0]=0;s_state=BADGE_SEND_IDLE;
    clock_us+=21000000;step(&service);assert(pings==1 && !s_message[0] && s_state==BADGE_SEND_IDLE);
    assert(badge_gateway_send(NULL)==ESP_OK);step(&service);
    assert(s_state==BADGE_SEND_DONE && !badge_gateway_active() && service.ws && connections==1 && uploads==1);
    assert(badge_gateway_abandon());snprintf(clip_id,sizeof(clip_id),"clip-2");
    assert(badge_gateway_preconnect()==ESP_OK);assert(badge_gateway_send(NULL)==ESP_OK);step(&service);
    assert(connections==1 && uploads==2 && task_creations==1 && service.ws);
    assert(badge_gateway_probe()==ESP_OK);step(&service);assert(uploads==2 && connections==1);
    assert(badge_gateway_history_probe()==ESP_OK);step(&service);assert(uploads==2);
    snprintf(selected.access_secret,sizeof(selected.access_secret),"changed");step(&service);
    assert(connections==2 && service.ws);
    badge_gateway_maintain(false);step(&service);assert(!service.ws && badge_gateway_quiescent());
    badge_gateway_maintain(true);step(&service);assert(service.ws);
    online=false;step(&service);assert(!service.ws && badge_gateway_quiescent());online=true;
    clock_us+=40000000;step(&service);assert(service.ws);
    unsigned before=connections;event(NULL);step(&service);assert(!service.ws);
    step(&service);assert(connections==before);clock_us+=2000000;step(&service);assert(connections==before+1);
    fail_connect=true;event(NULL);step(&service);clock_us+=2000000;step(&service);before=connections;
    assert(s_state==BADGE_SEND_DONE);
    step(&service);assert(connections==before);clock_us+=3000000;fail_connect=false;step(&service);assert(service.ws);
    assert(badge_gateway_abandon());snprintf(clip_id,sizeof(clip_id),"clip-3");lose_upload=true;
    assert(badge_gateway_send(NULL)==ESP_OK);step(&service);assert(s_state==BADGE_SEND_DONE && uploads==3);
    assert(badge_gateway_abandon());snprintf(clip_id,sizeof(clip_id),"clip-4");disconnect_reply=true;
    assert(badge_gateway_send(NULL)==ESP_OK);step(&service);assert(s_state==BADGE_SEND_DONE && uploads==4);
    assert(badge_gateway_abandon());snprintf(clip_id,sizeof(clip_id),"clip-5");cancel_upload=true;
    assert(badge_gateway_send(NULL)==ESP_OK);step(&service);assert(s_state==BADGE_SEND_CANCELLED && !service.ws && uploads==4);
    assert(badge_gateway_abandon());assert(badge_gateway_send(NULL)==ESP_OK);assert(badge_gateway_cancel());step(&service);
    assert(s_state==BADGE_SEND_CANCELLED && uploads==4);
    assert(badge_gateway_abandon());assert(badge_gateway_preconnect()==ESP_OK);assert(badge_gateway_cancel());step(&service);
    assert(s_state==BADGE_SEND_CANCELLED && uploads==4);
    reject_subscription=true;assert(badge_gateway_abandon());assert(badge_gateway_send(NULL)==ESP_OK);step(&service);
    assert(s_state==BADGE_SEND_DONE && uploads==5);reject_subscription=false;
    unsigned stale_closed=closed;clock_us+=91000000;step(&service);assert(closed==stale_closed+1 && !service.ws);
    clock_us+=2000000;step(&service);assert(service.ws);
    badge_agent_t route=selected;snprintf(route.name,sizeof(route.name),"renamed");assert(same_route(&route,&selected));
    route.proxy.enabled=true;assert(!same_route(&route,&selected));
    cancelled=false;suppress_history=true;atomic_store(&s_cancel_operation,false);atomic_store(&s_busy,true);
    uint8_t scope[32]={0};badge_reply_resume(&s_reply,"fixture-run",scope);atomic_store(&s_accepted,true);
    atomic_store(&s_authenticated,true);s_background=false;clear_events();read_cancel=true;
    assert(!await_reply((void *)2));assert(cancelled && can_abort);assert(abort_run((void *)2,"fixture-run"));assert(aborts==1);
    atomic_store(&s_busy,false);cancelled=false;s_live[0]=0;s_live_seq=0;clear_events();suppress_history=true;
    s_reply.poll_pending=false;s_reply.next_poll_ms=UINT64_MAX;
    chat_event("other-session","fixture-run","\"seq\":1,\"state\":\"delta\",\"deltaText\":\"WRONG\"");
    chat_event(s_session,"other-run","\"seq\":1,\"state\":\"final\",\"message\":{\"content\":\"WRONG\"}");
    chat_event(s_session,"fixture-run","\"seq\":2,\"state\":\"delta\",\"deltaText\":\"春\"");
    chat_event(s_session,"fixture-run","\"seq\":2,\"state\":\"delta\",\"deltaText\":\"重复\"");
    chat_event(s_session,"fixture-run","\"seq\":3,\"state\":\"delta\",\"deltaText\":\"庭\"");
    chat_event(s_session,"fixture-run","\"seq\":4,\"state\":\"delta\",\"message\":{\"content\":[{\"type\":\"text\",\"text\":\"助手回复\"}]},\"deltaText\":\"must not append\"");
    chat_event(s_session,"fixture-run","\"seq\":5,\"state\":\"delta\",\"deltaText\":\"新的回复\",\"replace\":true");
    chat_event(s_session,"fixture-run","\"seq\":4,\"state\":\"final\",\"message\":{\"content\":\"stale\"}");
    chat_event(s_session,"fixture-run","\"seq\":6,\"state\":\"final\"");
    assert(await_reply((void *)2));assert(s_state==BADGE_SEND_DONE && !strcmp(s_message,"新的回复") && s_reply.complete);
    badge_gateway_maintain(false);step(&service);assert(badge_gateway_quiescent());
    online=true;selected_route("wss://fixture.example");badge_gateway_maintain(true);
    talk_wrong_owner=true;
    unsigned uploads_before_live=uploads;
    assert(badge_gateway_live(true)==ESP_OK);
    assert(badge_gateway_live(false)==ESP_ERR_INVALID_STATE);
    step(&service);
    assert(s_state==BADGE_SEND_FAILED && live_starts==1 && live_pushes==1 && live_stops==1 && !live_response);
    assert(talk_marks==1 && uploads==uploads_before_live && !s_talk_auth && !service.ws);
    talk_bad_format=true;
    expected_live_language="en";
    assert(badge_gateway_live(false)==ESP_OK);
    assert(badge_gateway_live(true)==ESP_ERR_INVALID_STATE);
    step(&service);assert(live_starts==1 && s_state==BADGE_SEND_FAILED);
    expected_live_language="zh";
    talk_bad_format=false;talk_malformed=true;
    assert(badge_gateway_live(true)==ESP_OK);step(&service);assert(live_starts==2 && live_pushes==1 && s_state==BADGE_SEND_FAILED);
    talk_malformed=false;talk_reject=true;
    assert(badge_gateway_live(true)==ESP_OK);step(&service);assert(live_starts==2 && s_state==BADGE_SEND_FAILED && strstr(s_message,"升级 OpenClaw"));
    talk_reject=false;talk_cancel=true;
    assert(badge_gateway_live(true)==ESP_OK);step(&service);assert(s_state==BADGE_SEND_CANCELLED && live_starts==2);
    talk_cancel=false;talk_input=talk_no_ack=true;
    assert(badge_gateway_live(true)==ESP_OK);step(&service);assert(s_state==BADGE_SEND_FAILED && talk_appends<=32 && !live_response);
    talk_input=talk_no_ack=false;
    assert(talk_creates==6 && talk_closes>0);
    talk_scope_denied=true;
    unsigned creates_before=talk_creates,starts_before=live_starts;
    assert(badge_gateway_live(true)==ESP_OK);step(&service);
    assert(talk_creates==creates_before && live_starts==starts_before && s_state==BADGE_SEND_FAILED && strstr(s_message,"operator.talk"));
    talk_scope_denied=false;talk_no_ready=true;
    assert(badge_gateway_live(true)==ESP_OK);step(&service);
    assert(live_starts==starts_before && s_state==BADGE_SEND_FAILED);
    talk_no_ready=false;talk_append_reject=true;talk_input=true;
    assert(badge_gateway_live(true)==ESP_OK);step(&service);assert(s_state==BADGE_SEND_FAILED && strstr(s_message,"拒绝实时音频"));
    talk_append_reject=false;talk_input=false;talk_tools=true;
    for (unsigned attempt=0;attempt<3;attempt++) {
        tool_early=attempt==1;tool_rejected=attempt==2;
        unsigned previous_calls=tool_calls,previous_results=tool_results;
        assert(badge_gateway_live(true)==ESP_OK);step(&service);
        assert(tool_calls==previous_calls+1 && tool_results==previous_results+1 && !badge_gateway_active());
    }
    talk_tools=tool_rejected=tool_early=false;
    talk_tools=talk_input=tool_resume_test=true;tool_wait_us=25000000;
    unsigned appends_before_tool=talk_appends,results_before_tool=tool_results;
    int64_t before_tool=clock_us;
    assert(badge_gateway_live(true)==ESP_OK);step(&service);
    assert(tool_results==results_before_tool+1 && clock_us-before_tool>=25000000);
    assert(talk_appends>appends_before_tool && !tool_resume_test && !tool_resume_at);
    assert(s_talk_retained.consults==1 && !tool_final_at);
    assert(s_talk_retained.during_consult>100);
    assert(!strcmp(s_talk_retained.step,"session-close") && !live_response);
    cancel_tool_wait=true;
    assert(badge_gateway_live(true)==ESP_OK);step(&service);
    assert(s_state==BADGE_SEND_CANCELLED && !live_response && !badge_gateway_active());
    tool_wait_us=tool_final_at=0;talk_tools=talk_input=cancel_tool_wait=false;
    talk_tools=talk_input=true;tool_wait_us=130000000;
    assert(badge_gateway_live(true)==ESP_OK);step(&service);
    assert(!strcmp(s_talk_retained.step,"tool-timeout") && !live_response && !badge_gateway_active());
    tool_wait_us=tool_final_at=0;talk_tools=talk_input=false;
    unsigned starts_before_error=live_starts;
    talk_no_ready=talk_provider_error=true;
    assert(badge_gateway_live(true)==ESP_OK);step(&service);
    assert(s_state==BADGE_SEND_FAILED && strstr(s_message,"凭据无效") && !strstr(s_message,"fixture-secret") && live_starts==starts_before_error);
    talk_early_error=true;
    assert(badge_gateway_live(true)==ESP_OK);step(&service);
    assert(s_state==BADGE_SEND_FAILED && strstr(s_message,"凭据无效") && !strstr(s_message,"fixture-secret") && live_starts==starts_before_error);
    talk_no_ready=talk_provider_error=talk_early_error=false;
    for (unsigned scenario=1;scenario<=11;scenario++) {
        talk_recovery=scenario;
        talk_ima=scenario==11;
        talk_event_delay=scenario==2 || scenario==3 ? 4000000 : 0;
        unsigned previous_starts=live_starts,previous_pushes=live_pushes,previous_marks=talk_marks,previous_clears=live_clears;
        int64_t before=clock_us;
        assert(badge_gateway_live(true)==ESP_OK);step(&service);
        assert(s_state==BADGE_SEND_FAILED && !badge_gateway_active());
        assert(live_starts==previous_starts+(scenario==5 || scenario==7 ? 0 : 1));
        if (scenario!=5 && scenario!=6 && scenario!=7) {
            assert(!talk_event_delay);
            assert(talk_marks==previous_marks+1);
            assert(live_pushes==previous_pushes+(scenario>=10 ? 4 : scenario==3 || scenario==4 || scenario==8 ? 2 : 1));
            assert(live_clears==previous_clears+(scenario==3 || scenario==4 || scenario==8 ? 1 : 0));
            assert(!strstr(s_message,"服务出错"));
            if (scenario==9) assert(s_talk_retained.skipped==1 && s_talk_retained.turns==1);
            if (scenario==10) assert(batched_audio_bytes==3840 && s_talk_retained.audio==1 && s_talk_retained.turns==1);
            if (scenario==11) assert(ima_starts==1 && ima_pushes==4 && s_talk_retained.audio==1 && s_talk_retained.turns==1 && s_talk_retained.ima==1);
        } else {
            assert(talk_marks==previous_marks && clock_us-before>=30000000 && clock_us-before<42000000);
            assert(strstr(s_message,"检查网关日志"));
        }
    }
    talk_ima=false;
    talk_recovery=12;talk_ima=deferred_playback=true;playback_ready_at=clock_us+800000;
    unsigned before_burst_marks=talk_marks,before_burst_pushes=ima_pushes;
    assert(badge_gateway_live(true)==ESP_OK);step(&service);
    assert(talk_marks==before_burst_marks+8 && ima_pushes==before_burst_pushes+20);
    assert(s_talk_retained.turns==1 && !badge_gateway_active());
    talk_ima=deferred_playback=false;
    talk_recovery=0;
    for (unsigned scenario=1;scenario<=5;scenario++) {
        talk_fault=scenario;talk_input=scenario==2;
        unsigned previous_starts=live_starts,previous_pushes=live_pushes,previous_marks=talk_marks;
        assert(badge_gateway_live(true)==ESP_OK);step(&service);
        assert(s_state==BADGE_SEND_FAILED && !badge_gateway_active());
        assert(live_starts==previous_starts+1 && !live_response);
        const char *expected=scenario==1?"receive":scenario==2?"append-write":scenario==3?"mark-drain":scenario==4?"mark-write":"session-close";
        assert(!strcmp(s_talk_retained.step,expected));
        assert(s_talk_retained.result==(scenario==1?-1:scenario==2?-37:scenario==4?-38:0));
        assert(s_talk_retained.audio==(scenario<3?0:scenario==5?2:1));
        assert(s_talk_retained.marks==(scenario==5?2:0));
        char diagnostic[256];badge_gateway_diagnostic(diagnostic,sizeof(diagnostic));
        assert(strstr(diagnostic,expected) && strstr(diagnostic,"transport=fixture-transport"));
        assert(!strstr(diagnostic,"fixture-secret") && !strstr(diagnostic,"live-1"));
        if (scenario==5) assert(live_pushes==previous_pushes+2 && talk_marks==previous_marks+2);
    }
    talk_fault=0;talk_input=false;
    for (unsigned scenario=1;scenario<=12;scenario++) {
        if (scenario==5) continue;
        talk_duplex=scenario;talk_input=true;talk_no_ack=scenario==3;
        if (scenario==10) append_write_delay=400000;
        unsigned previous_pushes=live_pushes,previous_marks=talk_marks;
        if (scenario==8) {
            capture_capacity=7680;
            assert(badge_gateway_live(true)==ESP_OK);step(&service);
            assert(capture_dropped>0 && s_talk_retained.write_ms==213);
            printf("Live 213ms write, old capacity: captured=%zu uploaded=%zu dropped=%zu PASS\n",
                capture_chunks*512,capture_uploaded,capture_dropped);
            capture_capacity=13440;talk_input=true;
        }
        int64_t before=clock_us;
        assert(badge_gateway_live(true)==ESP_OK);step(&service);
        assert(worker_priority == 3);
        if (scenario==10) {
            assert(s_state==BADGE_SEND_FAILED && !strcmp(s_talk_retained.step,"capture-loss"));
            assert(capture_dropped>0 && clock_us-before<7000000);
            assert(s_talk_retained.write_us>=3000000 && s_talk_retained.loops>0);
            char diagnostic[640];badge_gateway_timing_diagnostic(diagnostic,sizeof(diagnostic));
            assert(strstr(diagnostic,"LIVE_TIME loops=") && strstr(diagnostic,"write_total_ms="));
            append_write_delay=0;
            puts("Live sustained capture loss: bounded failure and timing diagnostics PASS");
        } else if (scenario>=6) {
            if (scenario == 12) {
                printf("Always-readable: queued=%zu dropped=%zu\n",capture_high_water,capture_dropped); fflush(stdout);
                assert(capture_high_water <= 8192 && !capture_dropped);
                assert(first_upload_queued <= 4352);
            }
            if (scenario==9) {
                printf("Live fragmented receive: captured=%zu uploaded=%zu dropped=%zu\n",
                    capture_chunks*512,capture_uploaded,capture_dropped);
                fflush(stdout);
                assert(capture_receive_stalled);
                assert(s_talk_retained.upload_bytes==capture_uploaded);
                assert(!s_receive->started);
            }
            assert(duplex_appends==100 && duplex_backlog<16);
            assert((scenario==11 ? capture_dropped>0 : !capture_dropped) && capture_uploaded>=370000);
            assert(s_talk_retained.ack_ms>=(scenario==7?1800u:450u));
            if (scenario==8) assert(s_talk_retained.write_ms==213);
            assert(s_talk_retained.inflight<=32 && !strcmp(s_talk_retained.step,"session-close"));
            printf("Live delayed ACK: %ums, captured=%zu uploaded=%zu dropped=%zu inflight=%u PASS\n",
                scenario==7?1800u:450u,capture_chunks*512,capture_uploaded,capture_dropped,s_talk_retained.inflight);
        } else if (scenario==1 || scenario==4) {
            assert(duplex_appends==100 && duplex_backlog<16);
            assert(s_talk_retained.ack_ms<300 && s_talk_retained.inflight<=2 && s_talk_retained.replies>=247);
            assert(live_pushes==previous_pushes+30 && talk_marks==previous_marks+5);
            assert(!strcmp(s_talk_retained.step,"session-close"));
            if (scenario==1) assert(clock_us-before>10000000 && clock_us-before<20000000);
            else {
                assert(!capture_dropped && capture_uploaded>=380000);
                assert(capture_uploaded*1000000/48000>=(size_t)(clock_us-capture_started)*9/10);
            }
        } else if (scenario==2) {
            assert(duplex_appends==10 && s_state==BADGE_SEND_CANCELLED);
            assert(!strcmp(s_talk_retained.step,"cancelled"));
        } else {
            assert(!strcmp(s_talk_retained.step,"append-timeout"));
            assert(clock_us-before>10000000 && clock_us-before<12000000);
        }
        assert(!live_response && !badge_gateway_active());
    }
    talk_duplex=0;talk_input=talk_no_ack=false;
    talk_duplex=1;talk_input=true;talk_compact=true;
    assert(badge_gateway_live(true)==ESP_OK);step(&service);
    assert(duplex_appends==100 && !strcmp(s_talk_retained.step,"session-close"));
    assert(s_talk_retained.silence_appends==100 && s_talk_retained.wire_bytes<25000);
    assert(s_talk_retained.upload_bytes==96000);
    talk_duplex=6;
    assert(badge_gateway_live(true)==ESP_OK);step(&service);
    assert(s_talk_retained.silence_appends==0 && s_talk_retained.upload_bytes==capture_uploaded);
    talk_compact=false;talk_duplex=0;talk_input=false;
    talk_ima=true;talk_duplex=6;talk_input=true;
    assert(badge_gateway_live(true)==ESP_OK);step(&service);
    assert(ima_uploads && s_talk_retained.upload_bytes==capture_uploaded);
    assert(s_talk_retained.wire_bytes<s_talk_retained.upload_bytes/2);
    talk_ima=false;talk_duplex=0;talk_input=false;
    talk_duplex=1;talk_input=true;slow_playback=true;delayed_ack=3500000;
    unsigned previous_pushes=live_pushes;
    assert(badge_gateway_live(true)==ESP_OK);step(&service);
    assert(duplex_appends==100 && live_pushes==previous_pushes+30);
    assert(!strcmp(s_talk_retained.step,"session-close") && s_talk_retained.ack_ms>=3500);
    assert(!s_receive->started && !badge_gateway_active());
    slow_playback=false;talk_duplex=0;talk_input=false;suppress_history=false;
    talk_duplex=1;talk_input=true;deferred_playback=true;
    previous_pushes=live_pushes;
    assert(badge_gateway_live(true)==ESP_OK);step(&service);
    assert(duplex_appends==100 && live_pushes==previous_pushes+30 && received_during_playback);
    assert(!strcmp(s_talk_retained.step,"session-close"));
    deferred_playback=false;talk_duplex=0;talk_input=false;
    talk_duplex=5;talk_input=true;append_write_delay=1300000;
    assert(badge_gateway_live(true)==ESP_OK);step(&service);
    printf("Slow write trace: step=%s appends=%u ack=%u pending=%u backlog=%u\n",s_talk_retained.step,duplex_appends,s_talk_retained.ack_ms,s_talk_retained.pending,duplex_backlog); fflush(stdout);
    assert(duplex_appends==100 && !strcmp(s_talk_retained.step,"session-close"));
    assert(s_talk_retained.ack_ms<3500 && duplex_backlog<16);
    append_write_delay=0;talk_duplex=0;talk_input=false;
    synthetic_fixture=true;
    talk_duplex=6;talk_input=true;
    unsigned tests_before=synthetic_starts;
    assert(badge_gateway_live_test()==ESP_OK);step(&service);
    assert(s_state==BADGE_SEND_DONE && !strcmp(s_talk_retained.step,"synthetic-complete"));
    assert(synthetic_starts==tests_before+1 && !capture_dropped && capture_uploaded>=950000);
    assert(!badge_gateway_active() && !live_response && worker_priority==3);
    synthetic_fixture=false;talk_duplex=0;talk_input=false;
    capture_failed=true;talk_duplex=6;talk_input=true;
    assert(badge_gateway_live(true)==ESP_OK);step(&service);
    assert(s_state==BADGE_SEND_FAILED && !strcmp(s_talk_retained.step,"capture-stopped"));
    assert(!badge_gateway_active() && !live_response && worker_priority==3);
    capture_failed=false;talk_duplex=0;talk_input=false;
    for (unsigned transition=0;transition<8;transition++) {
        assert(badge_gateway_abandon());
        snprintf(clip_id,sizeof(clip_id),"transition-%u",transition);
        assert(badge_gateway_send(NULL)==ESP_OK);step(&service);
        assert(s_state==BADGE_SEND_DONE && !badge_gateway_active() && service.ws);
        assert(badge_gateway_abandon());
        assert(badge_gateway_live(true)==ESP_OK);
        assert(badge_gateway_send(NULL)==ESP_ERR_INVALID_STATE);
        step(&service);
        assert(!badge_gateway_active() && !service.ws && !service.parent);
    }
    cJSON *failure=cJSON_Parse("{\"message\":\"upstream timed out: fixture-secret\"}");
    talk_failure_report(failure);cJSON_Delete(failure);
    assert(strstr(s_message,"网关出口") && !strstr(s_message,"fixture-secret"));
    failure=cJSON_Parse("{\"message\":\"model is unavailable\"}");
    assert(strstr(talk_failure(failure),"模型不可用"));cJSON_Delete(failure);
    failure=cJSON_Parse("{\"message\":\"Realtime provider authentication failed. Check the provider credentials and try again.\"}");
    assert(strstr(talk_failure(failure),"凭据无效"));cJSON_Delete(failure);
    failure=cJSON_Parse("{\"message\":\"No configured realtime provider\"}");
    assert(strstr(talk_failure(failure),"未配置"));cJSON_Delete(failure);
    failure=cJSON_Parse("{\"message\":\"insufficient quota\"}");
    assert(strstr(talk_failure(failure),"额度不足"));cJSON_Delete(failure);
    failure=cJSON_Parse("{\"message\":\"missing scope operator.talk\"}");
    assert(strstr(talk_failure(failure),"权限不足"));cJSON_Delete(failure);
    assert(strstr(talk_failure(NULL),"检查网关日志"));
    cancelled=false;s_background=false;clear_events();
    talk_consult_t tool={0};
    cJSON *call=cJSON_Parse("{\"callId\":\"control-1\",\"name\":\"openclaw_agent_control\",\"args\":\"{\\\"text\\\":\\\"取消任务\\\",\\\"mode\\\":\\\"cancel\\\"}\"}");
    assert(talk_tool_call((void *)2,"live-1",call,&tool));
    assert(tool.control && !strcmp(tool.call,"control-1") && strstr(wire,"talk.session.steer") && strstr(wire,"取消任务"));
    cJSON_Delete(call);
    talk_tool_complete(&tool);
    assert(!tool.call[0] && !tool.control && !strcmp(tool.completed[0],"control-1"));
    call=cJSON_Parse("{\"callId\":\"control-1\",\"name\":\"openclaw_agent_control\",\"args\":{\"text\":\"取消任务\"}}");
    assert(talk_tool_call((void *)2,"live-1",call,&tool) && !tool.call[0]);cJSON_Delete(call);
    s_background=true;
    badge_gateway_maintain(false);step(&service);assert(badge_gateway_quiescent());
    printf("Gateway runtime: reuse, route changes, BLE release, backoff, upload/reply recovery, cancellation and isolated live replies PASS\n");
}
