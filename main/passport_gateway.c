#include "passport_gateway.h"
#include "passport_ima.h"
#include "passport_clip.h"
#include "passport_audio.h"
#include "passport_json.h"
#include "passport_memory.h"
#include "passport_reply.h"
#include "passport_tls.h"
#include <stdatomic.h>
#include "cJSON.h"
#ifdef PASSPORT_GATEWAY_HOST_TEST
#include "gateway_platform.h"
#else
#include "esp_crt_bundle.h"
#include "esp_random.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "lwip/netdb.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "lwip/tcp.h"
#include "esp_transport.h"
#include "esp_transport_tcp.h"
#include "esp_transport_ssl.h"
#include "passport_transport_ws.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
#include "mbedtls/base64.h"
#include "mbedtls/sha256.h"
#include "monocypher-ed25519.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* No gateway-wide password, proxy, or phone bridge. Every connection proves
 * the passport's own persistent Ed25519 identity and uses a role-scoped token. */
#define CLIENT_ID "gateway-client"
#define PLATFORM "esp32c3"
#define FAMILY "ai-passport"
#define SCOPES "operator.read,operator.write"

static TaskHandle_t s_worker;
static passport_send_state_t s_state;
static portMUX_TYPE s_guard = portMUX_INITIALIZER_UNLOCKED;
static char s_message[PASSPORT_TEXT_MAX];
static char s_transcript[PASSPORT_TEXT_MAX];
static passport_agent_t s_agent;
static uint8_t s_secret[64], s_public[32];
static char s_device_id[65];
static char s_session[100];
static char s_token[PASSPORT_TOKEN_MAX];
static bool s_identity_ready;
static bool s_probe;
static passport_gateway_check_t s_check;
static atomic_bool s_busy, s_enabled, s_cancel_operation;
static atomic_bool s_resources;
static atomic_int s_command = ATOMIC_VAR_INIT(-1);
static bool s_background;
static passport_agent_t s_requested_agent;
static char s_live[PASSPORT_TEXT_MAX];
static bool s_live_zh;
static uint64_t s_live_seq;
static int64_t s_last_rx_us;
static char s_resume_requested[PASSPORT_RUN_ID_MAX];
static passport_reply_tracker_t s_reply;
static atomic_bool s_authenticated, s_accepted;
/* Pre-connect: the worker authenticates while the user is still speaking,
 * then waits here until recording stops (go) or is abandoned (cancel). */
static atomic_bool s_go, s_preconnect;
enum { GATEWAY_LIVE = 4, GATEWAY_LIVE_TEST = 5, GATEWAY_CHECK = 6 };
static atomic_int s_operation;
static bool s_talk_auth;
static bool s_aborting;
static struct { char run_id[PASSPORT_RUN_ID_MAX]; uint8_t scope[32]; bool valid; } s_deferred_abort;
static size_t s_max_payload, s_max_attachment;
static bool s_connect_retryable;
static bool s_rx_timed_out;
static bool s_reply_interrupted;
/* Keep upload memory static and small. A C3 can have enough total heap but no
 * contiguous 7 KB block after LVGL, audio and TLS have run. */
static uint8_t s_upload_samples[960];
static passport_json_filter_t *s_receive;
static int64_t s_recording_stopped_us;
typedef struct {
    const char *step;
    const char *transport;
    int result;
    uint32_t elapsed_ms, appends, audio, marks, replies, ack_ms, inflight;
    uint32_t write_ms, oldest_ms, pending, consults;
    uint32_t upload_bytes, write_total_ms, receive_ms;
    uint32_t inputs, turns, skipped;
    uint32_t loops, empty, window_waits, receive_waits, loop_gap_ms, dropped;
    uint32_t during_response, during_consult;
    uint32_t silence_appends, wire_bytes, ima;
    uint64_t receive_us, prepare_us, write_us;
} talk_trace_t;
static talk_trace_t s_talk_trace, s_talk_retained;

void passport_gateway_diagnostic(char *out, size_t cap) {
    portENTER_CRITICAL(&s_guard);
    talk_trace_t trace = s_talk_retained;
    portEXIT_CRITICAL(&s_guard);
    snprintf(out, cap, "LIVE step=%s transport=%s result=%d ms=%lu appends=%lu audio=%lu marks=%lu replies=%lu ack_ms=%lu inflight=%lu write_ms=%lu oldest_ms=%lu pending=%lu consults=%lu inputs=%lu turns=%lu skipped=%lu upload_bytes=%lu write_total_ms=%lu receive_ms=%lu ima=%lu",
             trace.step ? trace.step : "idle", trace.transport ? trace.transport : "none", trace.result, (unsigned long)trace.elapsed_ms,
             (unsigned long)trace.appends, (unsigned long)trace.audio, (unsigned long)trace.marks,
             (unsigned long)trace.replies, (unsigned long)trace.ack_ms, (unsigned long)trace.inflight,
             (unsigned long)trace.write_ms, (unsigned long)trace.oldest_ms,
             (unsigned long)trace.pending, (unsigned long)trace.consults,
             (unsigned long)trace.inputs, (unsigned long)trace.turns, (unsigned long)trace.skipped,
             (unsigned long)trace.upload_bytes, (unsigned long)trace.write_total_ms, (unsigned long)trace.receive_ms, (unsigned long)trace.ima);
}

void passport_gateway_timing_diagnostic(char *out, size_t cap) {
    portENTER_CRITICAL(&s_guard);
    talk_trace_t trace = s_talk_retained;
    portEXIT_CRITICAL(&s_guard);
    uint64_t measured = trace.receive_us + trace.prepare_us + trace.write_us;
    uint64_t elapsed = (uint64_t)trace.elapsed_ms * 1000;
    snprintf(out, cap, "LIVE_TIME loops=%lu empty=%lu window_waits=%lu receive_waits=%lu loop_gap_ms=%lu dropped=%lu receive_total_ms=%llu prepare_total_ms=%llu write_total_ms=%llu other_ms=%llu response_appends=%lu consult_appends=%lu silence_appends=%lu wire_bytes=%lu",
        (unsigned long)trace.loops, (unsigned long)trace.empty,
        (unsigned long)trace.window_waits, (unsigned long)trace.receive_waits,
        (unsigned long)trace.loop_gap_ms, (unsigned long)trace.dropped,
        (unsigned long long)(trace.receive_us / 1000), (unsigned long long)(trace.prepare_us / 1000),
        (unsigned long long)(trace.write_us / 1000),
        (unsigned long long)(elapsed > measured ? (elapsed - measured) / 1000 : 0),
        (unsigned long)trace.during_response, (unsigned long)trace.during_consult,
        (unsigned long)trace.silence_appends, (unsigned long)trace.wire_bytes);
}

static void begin_connection(void) {
    passport_tls_control_begin();
    if (!s_background && atomic_load(&s_cancel_operation)) passport_tls_request_cancel(false);
}

/* Monotonic phase timings only: never log handshake bodies or credentials. */
static void timing(const char *phase, int64_t start) {
    ESP_LOGI("passport_timing", "%s=%lldms", phase,
             (long long)((esp_timer_get_time() - start) / 1000));
}

bool passport_wifi_is_online(void);
esp_err_t passport_wifi_gateway_performance(bool enabled);
bool passport_link_agent_snapshot(passport_agent_t *out);
bool passport_link_store_token(const passport_agent_t *expected, const char *token, const char *kind);
esp_err_t passport_link_pause_bluetooth(void);
esp_err_t passport_link_resume_bluetooth(void);

static void state(passport_send_state_t value, const char *message) {
    if (xTaskGetCurrentTaskHandle() == s_worker && s_background) return;
    portENTER_CRITICAL(&s_guard);
    s_state = value;
    snprintf(s_message, sizeof(s_message), "%s", message ? message : "");
    portEXIT_CRITICAL(&s_guard);
    if (value!=PASSPORT_SEND_PARTIAL && value!=PASSPORT_SEND_LISTENING && value!=PASSPORT_SEND_SPEAKING) {
        ESP_LOGI("passport_gateway", "stage=%d", value);
        passport_memory_report(value == PASSPORT_SEND_UPLOAD ? "upload" : value == PASSPORT_SEND_WAITING ? "reply" : "network_state");
    }
}

static const char *str(const cJSON *obj, const char *key) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(v) ? v->valuestring : "";
}

static void base64url(char *out, size_t cap, const uint8_t *data, size_t len) {
    size_t n = 0;
    if (mbedtls_base64_encode((uint8_t *)out, cap, &n, data, len)) { out[0] = 0; return; }
    for (size_t i = 0; i < n; i++) {
        if (out[i] == '+') out[i] = '-';
        if (out[i] == '/') out[i] = '_';
        if (out[i] == '=') { n = i; break; }
    }
    out[n] = 0;
}

static bool identity(void) {
    if (s_identity_ready) return true;
    nvs_handle_t handle;
    if (nvs_open("passport_id", NVS_READWRITE, &handle) != ESP_OK) return false;
    uint8_t seed[32];
    size_t size = sizeof(seed);
    esp_err_t err = nvs_get_blob(handle, "seed", seed, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        esp_fill_random(seed, sizeof(seed));
        err = nvs_set_blob(handle, "seed", seed, sizeof(seed));
        if (err == ESP_OK) err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK || size != sizeof(seed)) return false;
    crypto_ed25519_key_pair(s_secret, s_public, seed); /* Wipes seed. */
    uint8_t digest[32];
    mbedtls_sha256(s_public, sizeof(s_public), digest, 0);
    for (int i = 0; i < 32; i++) snprintf(s_device_id + 2 * i, 3, "%02x", digest[i]);
    /* The passport talks to the gateway's "passport" agent: 助手's identity with a
     * small skill and tool set, so each voice turn carries a short prompt.
     * Only the conversation is device-specific. */
    snprintf(s_session, sizeof(s_session), "agent:passport:passport-%s", s_device_id);
    s_identity_ready = true;
    return true;
}

void passport_gateway_identity(char out[65]) { snprintf(out, 65, "%s", s_device_id); }

static bool send_fragment(esp_transport_handle_t ws, int opcode, const char *data, size_t len) {
    if (passport_tls_cancelled() && !s_aborting) return false;
    passport_tls_set_writing(true);
    if (passport_tls_cancelled() && !s_aborting) { passport_tls_set_writing(false);return false; }
    int written = passport_transport_ws_send_raw(ws, opcode, data, (int)len, s_aborting ? 250 : s_talk_auth ? 1500 : 30000);
    if (s_talk_auth && written != (int)len) s_talk_trace.result = written;
    passport_tls_set_writing(false);
    if (written != (int)len) ESP_LOGW("passport_gateway", "write failed opcode=%d expected=%u actual=%d", opcode, (unsigned)len, written);
    return written == (int)len;
}

static bool send_json(esp_transport_handle_t ws, cJSON *obj) {
    if (!obj) return false;
    char *text = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (!text) return false;
    bool ok = send_fragment(ws, WS_TRANSPORT_OPCODES_TEXT | WS_TRANSPORT_OPCODES_FIN,
                            text, strlen(text));
    free(text);
    return ok;
}

/* Bounded receive. Retain frame boundaries, tolerate ping/pong and fragments,
 * and fail explicitly instead of overflowing RAM on oversized gateway state. */
static cJSON *receive_json(esp_transport_handle_t ws, int budget_ms, bool hello_projection) {
    int64_t receive_start = esp_timer_get_time(), first_byte = 0;
    size_t received_bytes = 0;
    unsigned idle_reads = 0;
    s_rx_timed_out = false;
    int64_t deadline = receive_start + (int64_t)budget_ms * 1000;
    int64_t absolute_limit = receive_start + 90LL * 1000 * 1000;
    while (esp_timer_get_time() < deadline) {
        if (passport_tls_cancelled() && !s_aborting) return NULL;
        char chunk[512];
        int remaining=(int)((deadline-esp_timer_get_time()+999)/1000);
        if (remaining<=0) break;
        int n = esp_transport_read(ws, chunk, sizeof(chunk), remaining<100 ? remaining : 100);
        int64_t activity=passport_transport_ws_last_receive_us(ws);
        if (activity>s_last_rx_us) s_last_rx_us=activity;
        if (passport_tls_cancelled() && !s_aborting) return NULL;
        if (n < 0) {
            if (s_talk_auth) s_talk_trace.result = n;
            ESP_LOGW("passport_gateway", "receive failed bytes=%u after=%lldms", (unsigned)received_bytes, (long long)((esp_timer_get_time()-receive_start)/1000)); return NULL;
        }
        int opcode = passport_transport_ws_get_read_opcode(ws) & 0x0f;
        if (opcode == WS_TRANSPORT_OPCODES_CLOSE) return NULL;
        if (n == 0) ++idle_reads;
        if (n > 0 && !first_byte) first_byte = esp_timer_get_time();
        if (n > 0) {
            s_last_rx_us = esp_timer_get_time();
            received_bytes += (size_t)n;
            /* A large hello must keep making progress. Give its active
             * transfer an idle budget, with a separate hard total bound. */
            if (hello_projection) {
                int64_t extended = esp_timer_get_time() + (int64_t)budget_ms * 1000;
                deadline = extended < absolute_limit ? extended : absolute_limit;
            }
        }
        if (n == 0 || opcode == WS_TRANSPORT_OPCODES_PING || opcode == WS_TRANSPORT_OPCODES_PONG) continue;
        if (opcode != WS_TRANSPORT_OPCODES_TEXT && opcode != WS_TRANSPORT_OPCODES_CONT) return NULL;
        if (!passport_json_filter_feed(s_receive,chunk,(size_t)n)) {
            state(PASSPORT_SEND_FAILED,"网关消息格式或大小异常"); return NULL;
        }
        if (s_receive->done) {
            /* Parse once, after projection. cJSON owns copies of selected
             * strings; no full message buffer or repeated partial parse. */
            int64_t parse_start = esp_timer_get_time();
            cJSON *frame = cJSON_Parse(s_receive->output);
            ESP_LOGD("passport_timing", "rx bytes=%u first=%lldms total=%lldms parse=%lldms idle=%u",
                     (unsigned)received_bytes, (long long)((first_byte - receive_start) / 1000),
                     (long long)((esp_timer_get_time() - receive_start) / 1000),
                     (long long)((esp_timer_get_time() - parse_start) / 1000), idle_reads);
            passport_json_filter_init(s_receive);
            if (!frame) {
                passport_memory_report("json_failed");
                state(PASSPORT_SEND_FAILED,"回复解析失败，请重试");
            }
            return frame;
        }
    }
    /* Preserve a partially received message across short heartbeat drains and
     * idle timeouts. A fresh socket explicitly resets the projection. */
    if (received_bytes) ESP_LOGD("passport_timing", "rx_partial bytes=%u", (unsigned)received_bytes);
    s_rx_timed_out = true;
    return NULL;
}

typedef struct { char host[96], path[192]; int port; bool tls; } endpoint_t;
static bool endpoint(const char *url, endpoint_t *out) {
    if (strpbrk(url, "\r\n\t ")) return false;
    memset(out, 0, sizeof(*out));
    out->tls = strncmp(url, "wss://", 6) == 0;
    if (!out->tls && strncmp(url, "ws://", 5)) return false;
    const char *p = url + (out->tls ? 6 : 5);
    const char *end = strpbrk(p, "/:?#");
    if (!end) end = p + strlen(p);
    size_t n = end - p;
    if (!n || n >= sizeof(out->host) || memchr(p, '@', n)) return false;
    memcpy(out->host, p, n);
    out->port = out->tls ? 443 : 80;
    if (*end == ':') {
        char *after;
        long port = strtol(end + 1, &after, 10);
        if (port < 1 || port > 65535 || (*after && *after != '/')) return false;
        out->port = (int)port; end = after;
    }
    if (*end == '#' || *end == '?') return false;
    snprintf(out->path, sizeof(out->path), "%s", *end ? end : "/");
    return true;
}

/* TLS needs a real clock. Try a domestic server first; Cloudflare's NTP
 * does not answer on some mainland networks, and the build allows only as
 * many servers as CONFIG_LWIP_SNTP_MAX_SERVERS. */
static bool sync_time(void) {
    if (time(NULL) > 1700000000) return true;
    if (!esp_sntp_enabled()) {
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "ntp.aliyun.com");
        esp_sntp_setservername(1, "pool.ntp.org");
        esp_sntp_setservername(2, "time.cloudflare.com");
        esp_sntp_init();
    }
    for (int i = 0; i < 100 && time(NULL) <= 1700000000 && !passport_tls_cancelled(); i++) vTaskDelay(pdMS_TO_TICKS(100));
    return time(NULL) > 1700000000;
}

static void auth_error(const cJSON *frame) {
    s_connect_retryable = false;
    const cJSON *error = cJSON_GetObjectItemCaseSensitive(frame, "error");
    const cJSON *details = cJSON_GetObjectItemCaseSensitive(error, "details");
    const char *code = str(details, "code");
    if (strstr(code, "PAIRING_REQUIRED")) state(PASSPORT_SEND_FAILED, "请在网关批准设备后重试");
    else if (strstr(code, "BOOTSTRAP")) state(PASSPORT_SEND_FAILED, "配对码失效，请重新配对");
    else if (strstr(code, "SCOPE")) state(PASSPORT_SEND_FAILED, "设备权限不足，请重新配对");
    else {
        char message[120];
        snprintf(message, sizeof(message), "网关拒绝：%.70s", *code ? code : str(error, "code"));
        state(PASSPORT_SEND_FAILED, message);
    }
}

static cJSON *connect_frame(const char *nonce, int64_t timestamp, bool bootstrap) {
    const char *role = "operator";
    const char *mode = "ui";
    const char *token = bootstrap ? s_agent.bootstrap : s_token;
    char payload[700], signature[90], public_key[48];
    /* The signed scope list must be identical to params.scopes even during
     * the bootstrap connection.  The gateway verifies the device signature
     * before it can create the pending pairing record. */
    int len = snprintf(payload, sizeof(payload), "v3|%s|%s|%s|%s|%s|%lld|%s|%s|%s|%s",
                       s_device_id, CLIENT_ID, mode, role, s_talk_auth ? SCOPES ",operator.talk" : SCOPES,
                       (long long)timestamp, token, nonce, PLATFORM, FAMILY);
    if (len < 0 || (size_t)len >= sizeof(payload)) return NULL;
    uint8_t proof[64];
    int64_t sign_start = esp_timer_get_time();
    crypto_ed25519_sign(proof, s_secret, (uint8_t *)payload, len);
    timing("ed25519_sign", sign_start);
    crypto_wipe(payload, sizeof(payload));
    base64url(signature, sizeof(signature), proof, sizeof(proof));
    base64url(public_key, sizeof(public_key), s_public, sizeof(s_public));
    cJSON *frame = cJSON_CreateObject();
    cJSON_AddStringToObject(frame, "type", "req");
    cJSON_AddStringToObject(frame, "id", "passport-connect");
    cJSON_AddStringToObject(frame, "method", "connect");
    cJSON *params = cJSON_AddObjectToObject(frame, "params");
    cJSON_AddNumberToObject(params, "minProtocol", 3);
    cJSON_AddNumberToObject(params, "maxProtocol", 4);
    cJSON *client = cJSON_AddObjectToObject(params, "client");
    cJSON_AddStringToObject(client, "id", CLIENT_ID);
    cJSON_AddStringToObject(client, "displayName", "AI Passport Agent");
    cJSON_AddStringToObject(client, "version", "0.2");
    cJSON_AddStringToObject(client, "platform", PLATFORM);
    cJSON_AddStringToObject(client, "deviceFamily", FAMILY);
    cJSON_AddStringToObject(client, "mode", mode);
    cJSON_AddStringToObject(params, "role", role);
    cJSON *scopes = cJSON_AddArrayToObject(params, "scopes");
    cJSON_AddItemToArray(scopes, cJSON_CreateString("operator.read"));
    cJSON_AddItemToArray(scopes, cJSON_CreateString("operator.write"));
    if (s_talk_auth) cJSON_AddItemToArray(scopes, cJSON_CreateString("operator.talk"));
    cJSON *caps = cJSON_AddArrayToObject(params, "caps");
    cJSON_AddItemToArray(caps, cJSON_CreateString("hello-health-on-demand"));
    cJSON_AddItemToArray(caps, cJSON_CreateString("hello-presence-self"));
    cJSON_AddItemToArray(caps, cJSON_CreateString("session-scoped-events"));
    cJSON_AddItemToArray(caps, cJSON_CreateString("passport-ima-v1"));
    cJSON_AddArrayToObject(params, "commands");
    cJSON *auth = cJSON_AddObjectToObject(params, "auth");
    cJSON_AddStringToObject(auth, bootstrap ? "bootstrapToken" : "deviceToken", token);
    cJSON *device = cJSON_AddObjectToObject(params, "device");
    cJSON_AddStringToObject(device, "id", s_device_id);
    cJSON_AddStringToObject(device, "publicKey", public_key);
    cJSON_AddStringToObject(device, "signature", signature);
    cJSON_AddNumberToObject(device, "signedAt", (double)timestamp);
    cJSON_AddStringToObject(device, "nonce", nonce);
    return frame;
}

static esp_transport_handle_t open_gateway(bool bootstrap, esp_transport_handle_t *parent) {
    int64_t connect_start = esp_timer_get_time(), phase_start;
    s_connect_retryable = false;
    passport_json_filter_init(s_receive);
    endpoint_t target;
    if (!endpoint(s_agent.url, &target)) { state(PASSPORT_SEND_FAILED, "网关地址无效"); return NULL; }
    if (s_talk_auth) {
        size_t length = strlen(target.path);
        while (length && target.path[length - 1] == '/') length--;
        if (length < 12 || strncmp(target.path + length - 12, "/passport/v1", 12)) {
            if (length + sizeof("/passport/v1") > sizeof(target.path)) {
                state(PASSPORT_SEND_FAILED, "网关地址无效"); return NULL;
            }
            memcpy(target.path + length, "/passport/v1", sizeof("/passport/v1"));
        } else target.path[length] = 0;
    }
    phase_start = esp_timer_get_time();
    /* DNS belongs to the transport; do not perform a second blocking lookup. */
    if (passport_tls_cancelled()) return NULL;
    timing("endpoint", phase_start);
    phase_start = esp_timer_get_time();
    if (target.tls && !sync_time()) { state(PASSPORT_SEND_FAILED, "时间同步失败，请重试"); return NULL; }
    timing("clock_sync", phase_start);
    *parent = target.tls ? esp_transport_ssl_init() : esp_transport_tcp_init();
    if (!*parent) { state(PASSPORT_SEND_FAILED, "内存不足，请重试"); return NULL; }
    if (target.tls) {
        esp_transport_ssl_crt_bundle_attach(*parent, esp_crt_bundle_attach);
        /* Keep one legal-size receive buffer after the handshake. Dynamic
         * RX uses a temporary record header, which is unsafe to discard and
         * reconstruct across fragmented reads/timeouts. Peak record size is
         * unchanged, while its state and allocation remain stable. */
#if CONFIG_MBEDTLS_DYNAMIC_BUFFER
        esp_transport_ssl_set_esp_tls_dyn_buf_strategy(*parent, ESP_TLS_DYN_BUF_RX_STATIC);
#endif
    }
    esp_transport_handle_t ws = passport_transport_ws_init(*parent);
    if (!ws) { state(PASSPORT_SEND_FAILED, "内存不足，请重试"); return NULL; }
    passport_transport_ws_config_t config = { .ws_path = target.path };
    if (passport_transport_ws_set_config(ws, &config) != ESP_OK) goto fail;
    if (s_agent.access_id[0] && s_agent.access_secret[0]) {
        char headers[256];
        snprintf(headers, sizeof(headers), "CF-Access-Client-Id: %s\r\nCF-Access-Client-Secret: %s\r\n", s_agent.access_id, s_agent.access_secret);
        if (passport_transport_ws_set_headers(ws, headers) != ESP_OK) goto fail;
    }
    s_connect_retryable = true;
    phase_start = esp_timer_get_time();
    if (esp_transport_connect(ws, target.host, target.port, 20000) < 0) {
        timing("transport_failed", phase_start);
        const char *proxy_error=passport_tls_proxy_error();
        if (proxy_error) s_connect_retryable=false;
        state(PASSPORT_SEND_FAILED, proxy_error?proxy_error:s_agent.proxy.enabled?"连不上网关，请检查代理":"连不上网关，请重试"); goto fail;
    }
    timing("tcp_tls_ws", phase_start);
    int socket = esp_transport_get_socket(ws);
    int nodelay = 1;
    struct timeval read_timeout = { .tv_sec = 1 };
    if (socket >= 0) (void)setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &read_timeout, sizeof(read_timeout));
    if (socket >= 0) (void)setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
    phase_start = esp_timer_get_time();
    cJSON *challenge = receive_json(ws, 10000, false);
    timing("challenge_wait", phase_start);
    if (!challenge) { state(PASSPORT_SEND_FAILED, s_rx_timed_out ? "网关握手超时，请重试" : "网关握手中断，请重试"); goto fail; }
    const cJSON *body = cJSON_GetObjectItemCaseSensitive(challenge, "payload");
    const cJSON *ts = cJSON_GetObjectItemCaseSensitive(body, "ts");
    cJSON *connect = NULL;
    if (!strcmp(str(challenge, "event"), "connect.challenge") &&
        cJSON_IsNumber(ts) && ts->valuedouble >= 0 && str(body, "nonce")[0] && strlen(str(body, "nonce")) < 128) {
        connect = connect_frame(str(body, "nonce"), (int64_t)ts->valuedouble, bootstrap);
    }
    cJSON_Delete(challenge);
    phase_start = esp_timer_get_time();
    if (!send_json(ws, connect)) { state(PASSPORT_SEND_FAILED, "认证发送失败，请重试"); goto fail; }
    timing("connect_write", phase_start);
    phase_start = esp_timer_get_time();
    cJSON *hello = receive_json(ws, 30000, true);
    timing("hello_wait", phase_start);
    if (!hello) { if (s_state != PASSPORT_SEND_FAILED) state(PASSPORT_SEND_FAILED, s_rx_timed_out ? "认证回复超时，请重试" : "认证连接中断，请重试"); goto fail; }
    timing("authenticated", connect_start);
    ESP_LOGI("passport_gateway", "received connect response");
    bool ok = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(hello, "ok"));
    if (!ok) { auth_error(hello); cJSON_Delete(hello); goto fail; }
    s_connect_retryable = false;
    const cJSON *payload = cJSON_GetObjectItemCaseSensitive(hello, "payload");
    const char *kind = !strcmp(str(payload, "backend"), "hermes") ? "hermes" : "openclaw";
    bool platform_changed = strcmp(s_agent.kind, kind) != 0;
    snprintf(s_agent.kind, sizeof(s_agent.kind), "%s", kind);
    const cJSON *passport_protocol = cJSON_GetObjectItemCaseSensitive(payload, "passportProtocol");
    if (s_talk_auth && (!cJSON_IsNumber(passport_protocol) || passport_protocol->valueint != 1)) {
        cJSON_Delete(hello);
        state(PASSPORT_SEND_FAILED, "网关不支持实时语音，请升级插件"); goto fail;
    }
    const cJSON *policy = cJSON_GetObjectItemCaseSensitive(payload, "policy");
    const cJSON *max_payload = cJSON_GetObjectItemCaseSensitive(policy, "maxPayload");
    const cJSON *attachments = cJSON_GetObjectItemCaseSensitive(policy, "attachments");
    const cJSON *max_bytes = cJSON_GetObjectItemCaseSensitive(attachments, "maxBytes");
    s_max_payload = cJSON_IsNumber(max_payload) && max_payload->valuedouble > 0 ? (size_t)max_payload->valuedouble : 0;
    s_max_attachment = cJSON_IsNumber(max_bytes) && max_bytes->valuedouble > 0 ? (size_t)max_bytes->valuedouble : 20 * 1024 * 1024;
    const cJSON *auth = cJSON_GetObjectItemCaseSensitive(payload, "auth");
    if (s_talk_auth) {
        bool granted = false;
        const cJSON *scope;
        cJSON_ArrayForEach(scope, cJSON_GetObjectItemCaseSensitive(auth, "scopes")) {
            if (cJSON_IsString(scope) && (!strcmp(scope->valuestring,"operator.talk") || !strcmp(scope->valuestring,"operator.admin"))) granted = true;
        }
        if (!granted) {
            cJSON_Delete(hello); state(PASSPORT_SEND_FAILED,"请在网关为此设备授权 operator.talk"); goto fail;
        }
    }
    const char *token = str(auth, "deviceToken");
    ESP_LOGI("passport_gateway", "hello auth role=%s node_token=%d handoff_tokens=%d",
             str(auth, "role"), token[0] != 0,
             cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(auth, "deviceTokens"))
                 ? cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(auth, "deviceTokens")) : 0);
    if (bootstrap) {
        const cJSON *item;
        cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(auth, "deviceTokens")) {
            if (!strcmp(str(item, "role"), "operator")) token = str(item, "deviceToken");
        }
    }
    if (*token && strlen(token) < sizeof(s_token)) snprintf(s_token, sizeof(s_token), "%s", token);
    cJSON_Delete(hello);
    if ((bootstrap || platform_changed) && (!s_token[0] || !passport_link_store_token(&s_agent, s_token, s_agent.kind))) {
        state(PASSPORT_SEND_FAILED, "配对凭据未保存，请重新配对"); goto fail;
    }
    if (bootstrap) {
        snprintf(s_agent.device_token,sizeof(s_agent.device_token),"%s",s_token);
        s_agent.bootstrap[0]=0;
    }
    struct timeval responsive_read={.tv_sec=1};
    if (socket>=0) (void)setsockopt(socket,SOL_SOCKET,SO_RCVTIMEO,&responsive_read,sizeof(responsive_read));
    atomic_store(&s_authenticated,true);
    s_last_rx_us = esp_timer_get_time();
    if (!bootstrap) {
        cJSON *subscribe=cJSON_CreateObject();
        cJSON_AddStringToObject(subscribe,"type","req");
        cJSON_AddStringToObject(subscribe,"id","passport-subscribe");
        cJSON_AddStringToObject(subscribe,"method","sessions.messages.subscribe");
        cJSON *params=cJSON_AddObjectToObject(subscribe,"params");
        cJSON_AddStringToObject(params,"key",s_session);
        if (!send_json(ws,subscribe)) goto fail;
    }
    return ws;
fail:
    atomic_store(&s_authenticated,false);
    passport_tls_forget_socket();
    esp_transport_close(ws); esp_transport_destroy(ws);
    return NULL;
}

/* Cloudflare's anycast endpoint can occasionally time out while Wi-Fi is
 * still online. Give the connection a small bounded retry window so a
 * transient route/TLS failure does not turn a valid request into a user-visible
 * failure immediately. The caller owns the parent transport after failure. */
static esp_transport_handle_t open_gateway_retry(bool bootstrap,
                                                 esp_transport_handle_t *parent) {
    for (int attempt = 0; attempt < 3 && !passport_tls_cancelled(); attempt++) {
        if (attempt > 0) {
            state(PASSPORT_SEND_CONNECTING, "正在重试连接");
            for (int tick=0;tick<12 && !passport_tls_cancelled();tick++) vTaskDelay(pdMS_TO_TICKS(100));
            if (passport_tls_cancelled()) break;
        }
        esp_transport_handle_t ws = open_gateway(bootstrap, parent);
        if (ws) return ws;
        if (*parent) {
            esp_transport_destroy(*parent);
            *parent = NULL;
        }
        if (!s_connect_retryable || !passport_wifi_is_online()) break;
    }
    return NULL;
}

/* Upload the clip as one fragmented chat.send. When `streaming`, the clip is
 * still being recorded: chunks go out as soon as they are written, and the
 * frame is finished only after recording stops (s_go). */
static bool upload(esp_transport_handle_t ws, bool streaming) {
    char prefix[512];
    snprintf(prefix, sizeof(prefix),
             "{\"type\":\"req\",\"id\":\"passport-send\",\"method\":\"chat.send\","
             "\"params\":{\"sessionKey\":\"%s\",\"message\":\"\",\"thinking\":\"off\",\"fastMode\":true,\"idempotencyKey\":\"%s\","
             "\"attachments\":[{\"type\":\"audio\",\"mimeType\":\"audio/wav\","
             "\"fileName\":\"passport.wav\",\"content\":\"", s_session, passport_clip_id());
    size_t limit = streaming ? PASSPORT_CLIP_MAX_BYTES / 2 + PASSPORT_CLIP_WAV_HEADER_BYTES : passport_clip_wire_size();
    int64_t upload_start = esp_timer_get_time();
    int64_t write_us = 0;
    int64_t longest_write_us = 0;
    size_t sent_bytes = 0;
    size_t tail_bytes = 0;
    unsigned fragments = 0;
    if (!s_max_payload || limit > s_max_attachment ||
        strlen(prefix) + 4 * ((limit + 2) / 3) + 6 > s_max_payload) {
        state(PASSPORT_SEND_FAILED, "录音超过网关限制，请重录"); return false;
    }
    char *encoded = malloc(1281);
    if (!encoded) { state(PASSPORT_SEND_FAILED, "内存不足，请重试"); return false; }
    passport_clip_stream_header(streaming);
    bool ok = false;
    if (!send_fragment(ws, WS_TRANSPORT_OPCODES_TEXT, prefix, strlen(prefix))) goto done;
    /* Every non-final chunk must be divisible by 3. Otherwise each local
     * base64 block gets its own '=' padding and the concatenated attachment
     * is rejected by the gateway as invalid base64. */
    enum { CHUNK = 747, ENCODED = 1281 };
    uint8_t *samples = s_upload_samples;
    for (size_t offset = 0;;) {
        if (passport_tls_cancelled()) goto done;
        /* Read the stop flag before the size: once set, the size is final. */
        bool recorded = !streaming || atomic_load(&s_go);
        size_t total = passport_clip_wire_size();
        size_t avail = total - offset;
        if (recorded && total == PASSPORT_CLIP_WAV_HEADER_BYTES) { state(PASSPORT_SEND_FAILED, "没有录音，请重新说话"); goto done; }
        if (recorded && !avail) break;
        size_t count = avail >= CHUNK ? CHUNK : recorded ? avail : 0;
        if (count) {
            if (passport_clip_read(offset, samples, count) != ESP_OK) goto done;
            size_t size;
            if (mbedtls_base64_encode((uint8_t *)encoded, ENCODED, &size, samples, count)) goto done;
            int64_t write_start = esp_timer_get_time();
            bool written = send_fragment(ws, WS_TRANSPORT_OPCODES_CONT, encoded, size);
            int64_t elapsed = esp_timer_get_time() - write_start;
            write_us += elapsed;
            if (elapsed > longest_write_us) longest_write_us = elapsed;
            if (!written) goto done;
            offset += count;
            sent_bytes = offset;
            fragments++;
            if (recorded) tail_bytes += count;
        }
        /* The request is still fragmented, so no chat.send response can
         * arrive yet. Drain unsolicited events so ws_read can answer PINGs. */
        if (esp_transport_poll_read(ws, count ? 0 : 50) > 0) {
            cJSON *event = receive_json(ws, 50, false);
            cJSON_Delete(event);
            if (s_state == PASSPORT_SEND_FAILED) goto done;
        }
        if (count) vTaskDelay(1);
    }
    /* passport_transport_ws_send_raw masks payloads in-place, even though its
     * signature says const; every send buffer must be writable. */
    char suffix[] = "\"}]}}";
    ok = send_fragment(ws, WS_TRANSPORT_OPCODES_CONT | WS_TRANSPORT_OPCODES_FIN, suffix, strlen(suffix));
done:
    free(encoded);
    passport_clip_stream_header(false);
    ESP_LOGI("passport_timing", "upload wire_bytes=%u pcm_bytes=%u fragments=%u write_ms=%lld max_write_ms=%lld tail_bytes=%u tail_ms=%lld ok=%d",
             (unsigned)sent_bytes, (unsigned)passport_clip_size(), fragments, (long long)(write_us / 1000),
             (long long)(longest_write_us / 1000), (unsigned)tail_bytes,
             (long long)((atomic_load(&s_go) && s_recording_stopped_us) ?
                         (esp_timer_get_time() - s_recording_stopped_us) / 1000 : -1), ok);
    timing("upload", upload_start);
    passport_memory_report("upload_end");
    return ok;
}

static bool message_text(const cJSON *msg, char *reply, size_t cap) {
    const cJSON *content = cJSON_GetObjectItemCaseSensitive(msg, "content");
    reply[0]=0;
    if (cJSON_IsString(content)) passport_reply_live_apply(reply,cap,content->valuestring,true);
    const cJSON *item;
    cJSON_ArrayForEach(item, content) {
        if (!strcmp(str(item, "type"), "text")) {
            passport_reply_live_apply(reply,cap,str(item,"text"),false);
        }
    }
    return reply[0]!=0;
}

static bool show_reply(const cJSON *msg) {
    char reply[PASSPORT_TEXT_MAX];
    if (!message_text(msg,reply,sizeof(reply))) return false;
    snprintf(s_live,sizeof(s_live),"%s",reply);
    s_reply.complete = true;
    state(PASSPORT_SEND_DONE, reply);
    return true;
}

/* A cached chat.send reply contains only runId/status; it does not replay
 * chat.final on this connection. Read the bounded history for that exact run,
 * never the latest arbitrary assistant message in the session. */
static bool request_reply_history(esp_transport_handle_t ws) {
    cJSON *frame = cJSON_CreateObject();
    cJSON_AddStringToObject(frame, "type", "req");
    cJSON_AddStringToObject(frame, "id", "passport-history");
    cJSON_AddStringToObject(frame, "method", "chat.history");
    cJSON *params = cJSON_AddObjectToObject(frame, "params");
    cJSON_AddStringToObject(params, "sessionKey", s_session);
    cJSON_AddNumberToObject(params, "limit", 12);
    cJSON_AddNumberToObject(params, "maxBytes", 8192);
    cJSON_AddNumberToObject(params, "maxChars", PASSPORT_TEXT_MAX);
    return send_json(ws, frame);
}

static bool history_run_active(const cJSON *payload) {
    const cJSON *legacy = cJSON_GetObjectItemCaseSensitive(payload,"inFlightRun");
    if (passport_reply_matches(&s_reply,str(legacy,"runId"))) return true;
    const cJSON *info = cJSON_GetObjectItemCaseSensitive(payload,"sessionInfo");
    const cJSON *id;
    cJSON_ArrayForEach(id,cJSON_GetObjectItemCaseSensitive(info,"activeRunIds")) {
        if (cJSON_IsString(id) && passport_reply_matches(&s_reply,id->valuestring)) return true;
    }
    return cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(info,"hasActiveRun")) &&
           passport_reply_matches(&s_reply,str(info,"lastRunId"));
}

static void clear_transcript(void) {
    portENTER_CRITICAL(&s_guard);
    s_transcript[0] = 0;
    portEXIT_CRITICAL(&s_guard);
}

static bool request_transcript(esp_transport_handle_t ws) {
    cJSON *frame = cJSON_CreateObject();
    cJSON_AddStringToObject(frame, "type", "req");
    cJSON_AddStringToObject(frame, "id", "passport-transcript");
    cJSON_AddStringToObject(frame, "method", "passport.transcript");
    cJSON *params = cJSON_AddObjectToObject(frame, "params");
    cJSON_AddStringToObject(params, "sessionKey", s_session);
    cJSON_AddStringToObject(params, "runId", s_reply.run_id);
    cJSON_AddStringToObject(params, "requestId", s_reply.request_id[0] ? s_reply.request_id : s_reply.run_id);
    return send_json(ws, frame);
}

static bool await_reply(esp_transport_handle_t ws) {
    s_reply_interrupted=false;
    uint64_t started = esp_timer_get_time()/1000;
    bool slow_reported = false, running = false;
    bool transcript_supported = true;
    uint64_t next_transcript_ms = 0;
    s_reply.poll_pending = false; s_reply.next_poll_ms = 0;
    while ((uint64_t)(esp_timer_get_time()/1000)-started < PASSPORT_REPLY_MAX_MS) {
        uint64_t now = esp_timer_get_time()/1000;
        if (s_reply.acknowledged && now-started >= PASSPORT_REPLY_SOFT_MS && !slow_reported) {
            if (!s_live[0]) state(PASSPORT_SEND_WAITING, running ? "助手仍在处理，可返回" : "正在查询回复，可返回");
            slow_reported = true;
        }
        if (!s_reply.acknowledged && now-started > 45000) {
            state(PASSPORT_SEND_FAILED,"发送结果待确认，请重试");return false;
        }
        if (passport_reply_poll_due(&s_reply,now)) {
            if (!request_reply_history(ws)) {
                s_reply_interrupted=true;
                state(PASSPORT_SEND_FAILED,"查询回复失败，请重试");return false;
            }
            passport_reply_poll_sent(&s_reply,now);
        }
        if (s_reply.acknowledged && transcript_supported && !s_transcript[0] && now >= next_transcript_ms) {
            if (!request_transcript(ws)) { s_reply_interrupted = true; return false; }
            next_transcript_ms = now + 3000;
        }
        cJSON *frame = receive_json(ws, 3000, false);
        if (!frame) {
            if (s_rx_timed_out) continue;
            s_reply_interrupted=!passport_tls_cancelled();
            if (s_state != PASSPORT_SEND_FAILED) state(PASSPORT_SEND_FAILED,"回复连接中断，请重试");
            return false;
        }
        const cJSON *payload = cJSON_GetObjectItemCaseSensitive(frame,"payload");
        if (!strcmp(str(frame,"id"),"passport-transcript")) {
            bool transcript_ok = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(frame,"ok"));
            const cJSON *error = cJSON_GetObjectItemCaseSensitive(frame,"error");
            if (!transcript_ok && (strstr(str(error,"message"),"unknown method") ||
                !strcmp(str(error,"code"),"METHOD_NOT_FOUND"))) transcript_supported = false;
            const char *request_id = s_reply.request_id[0] ? s_reply.request_id : s_reply.run_id;
            if (transcript_ok && passport_reply_matches(&s_reply,str(payload,"runId")) &&
                !strcmp(str(payload,"requestId"),request_id) && *str(payload,"text") && !passport_tls_cancelled()) {
                portENTER_CRITICAL(&s_guard);
                passport_reply_live_apply(s_transcript,sizeof(s_transcript),str(payload,"text"),true);
                portEXIT_CRITICAL(&s_guard);
            }
        }
        if (!strcmp(str(frame,"id"),"passport-send")) {
            if (!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(frame,"ok"))) {
                auth_error(frame);cJSON_Delete(frame);return false;
            }
            if (!passport_reply_ack(&s_reply,str(payload,"runId"))) {
                state(PASSPORT_SEND_FAILED,"网关任务编号异常，请重试");cJSON_Delete(frame);return false;
            }
            atomic_store(&s_accepted,true);
            ESP_LOGI("passport_gateway","accepted run=%s",s_reply.run_id);
            timing("upload_ack", (int64_t)started * 1000);
            if (atomic_load(&s_go) && s_recording_stopped_us) timing("stop_to_ack", s_recording_stopped_us);
            state(PASSPORT_SEND_WAITING,"已发送，助手处理中");
        }
        if (!strcmp(str(frame,"id"),"passport-history")) {
            s_reply.poll_pending=false;
            if (!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(frame,"ok"))) {
                auth_error(frame);cJSON_Delete(frame);return false;
            }
            running=history_run_active(payload);
            const cJSON *messages=cJSON_GetObjectItemCaseSensitive(payload,"messages");
            ESP_LOGI("passport_gateway","history run=%s active=%d messages=%d",s_reply.run_id,running,cJSON_GetArraySize(messages));
            for (int i=cJSON_GetArraySize(messages)-1;i>=0;i--) {
                const cJSON *msg=cJSON_GetArrayItem(messages,i);
                const cJSON *meta=cJSON_GetObjectItemCaseSensitive(msg,"__openclaw");
                const char *stop=str(msg,"stopReason");
                if (passport_reply_terminal_text(&s_reply,str(meta,"runId"),str(msg,"role"),stop,running) && show_reply(msg)) {
                    cJSON_Delete(frame);return true;
                }
                if (passport_reply_matches(&s_reply,str(meta,"runId")) && !strcmp(str(msg,"role"),"assistant") &&
                    (!strcmp(stop,"error") || !strcmp(stop,"aborted"))) {
                    state(PASSPORT_SEND_FAILED,"助手处理失败，可重录");cJSON_Delete(frame);return false;
                }
            }
        }
        if (!strcmp(str(frame,"id"),"passport-subscribe") &&
            !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(frame,"ok"))) {
            ESP_LOGW("passport_gateway","session subscription unavailable; history fallback retained");
        }
        if (!strcmp(str(frame,"event"),"chat") && !strcmp(str(payload,"sessionKey"),s_session) &&
            passport_reply_matches(&s_reply,str(payload,"runId"))) {
            const char *stage=str(payload,"state");
            const cJSON *sequence=cJSON_GetObjectItemCaseSensitive(payload,"seq");
            uint64_t seq=cJSON_IsNumber(sequence) && sequence->valuedouble>0 ? (uint64_t)sequence->valuedouble : 0;
            if (seq && seq<=s_live_seq) { cJSON_Delete(frame);continue; }
            if (seq) s_live_seq=seq;
            if (!strcmp(stage,"delta")) {
                char snapshot[PASSPORT_TEXT_MAX];
                const cJSON *msg=cJSON_GetObjectItemCaseSensitive(payload,"message");
                const cJSON *delta=cJSON_GetObjectItemCaseSensitive(payload,"deltaText");
                if (msg) {
                    (void)message_text(msg,snapshot,sizeof(snapshot));
                    passport_reply_live_apply(s_live,sizeof(s_live),snapshot,true);
                } else if (cJSON_IsString(delta)) {
                    passport_reply_live_apply(s_live,sizeof(s_live),delta->valuestring,
                        cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(payload,"replace")));
                }
                if (s_live[0]) {
                    if (s_state!=PASSPORT_SEND_PARTIAL && s_recording_stopped_us) timing("stop_to_first_text",s_recording_stopped_us);
                    state(PASSPORT_SEND_PARTIAL,s_live);
                }
            }
            if (!strcmp(stage,"final")) {
                const cJSON *msg=cJSON_GetObjectItemCaseSensitive(payload,"message");
                if (!show_reply(msg)) { s_reply.complete=true;state(PASSPORT_SEND_DONE,s_live[0]?s_live:"助手已完成"); }
                cJSON_Delete(frame);return true;
            }
            if (!strcmp(stage,"error") || !strcmp(stage,"aborted")) {
                state(PASSPORT_SEND_FAILED,"助手处理失败，可重录");cJSON_Delete(frame);return false;
            }
        }
        cJSON_Delete(frame);
    }
    state(PASSPORT_SEND_FAILED,"回复暂未取回，请重试");return false;
}

static void reply_scope(uint8_t scope[32]) {
    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);mbedtls_sha256_starts(&hash,0);
    mbedtls_sha256_update(&hash,(const uint8_t *)s_agent.url,strlen(s_agent.url)+1);
    mbedtls_sha256_update(&hash,(const uint8_t *)s_device_id,strlen(s_device_id));
    mbedtls_sha256_finish(&hash,scope);mbedtls_sha256_free(&hash);
}

/* Targeted cancellation is best effort. It never aborts other sessions, and
 * only an acknowledgement lets us forget a deferred cancellation. */
static bool abort_run(esp_transport_handle_t ws, const char *run_id) {
    s_aborting=true;
    cJSON *frame=cJSON_CreateObject();
    cJSON_AddStringToObject(frame,"type","req");cJSON_AddStringToObject(frame,"id","passport-cancel");
    cJSON_AddStringToObject(frame,"method","chat.abort");
    cJSON *params=cJSON_AddObjectToObject(frame,"params");
    cJSON_AddStringToObject(params,"sessionKey",s_session);cJSON_AddStringToObject(params,"runId",run_id);
    bool confirmed=false;
    if (send_json(ws,frame)) {
        int64_t end=esp_timer_get_time()+1000000;
        while (esp_timer_get_time()<end) {
            cJSON *response=receive_json(ws,100,false);
            if (!response) { if (s_rx_timed_out) continue;break; }
            bool matched=!strcmp(str(response,"id"),"passport-cancel");
            if (matched) confirmed=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(response,"ok"));
            cJSON_Delete(response);if (matched) break;
        }
    }
    s_aborting=false;
    ESP_LOGI("passport_gateway","run cancellation confirmed=%d",confirmed);
    return confirmed;
}

static void close_gateway(esp_transport_handle_t *ws, esp_transport_handle_t *parent) {
    passport_tls_forget_socket();
    atomic_store(&s_authenticated,false);
    if (*ws) { esp_transport_close(*ws);esp_transport_destroy(*ws);*ws=NULL; }
    if (*parent) { esp_transport_destroy(*parent);*parent=NULL; }
}

static bool same_route(const passport_agent_t *left, const passport_agent_t *right) {
    return !strcmp(left->url,right->url) && !strcmp(left->device_token,right->device_token) &&
        !strcmp(left->bootstrap,right->bootstrap) && !strcmp(left->access_id,right->access_id) &&
        !strcmp(left->access_secret,right->access_secret) && left->proxy.enabled==right->proxy.enabled &&
        (!left->proxy.enabled || (left->proxy.port==right->proxy.port && left->proxy.method==right->proxy.method &&
        !strcmp(left->proxy.host,right->proxy.host) && !strcmp(left->proxy.secret,right->proxy.secret)));
}

static cJSON *talk_request(const char *id, const char *method, const char *session) {
    cJSON *frame = cJSON_CreateObject();
    cJSON_AddStringToObject(frame, "type", "req");
    cJSON_AddStringToObject(frame, "id", id);
    cJSON_AddStringToObject(frame, "method", method);
    cJSON *params = cJSON_AddObjectToObject(frame, "params");
    if (session && *session) cJSON_AddStringToObject(params, "sessionId", session);
    return frame;
}

static size_t talk_audio_prefix(char *out, size_t capacity, const char *session, uint32_t sequence) {
    int prefix = snprintf(out, capacity,
        "{\"type\":\"req\",\"id\":\"passport-a-%lu\",\"method\":\"talk.session.appendAudio\",\"params\":{\"sessionId\":",
        (unsigned long)sequence);
    if (prefix < 0 || (size_t)prefix >= capacity) return 0;
    cJSON *name = cJSON_CreateStringReference(session);
    bool encoded = name && cJSON_PrintPreallocated(name, out + prefix,
        (int)(capacity - prefix), false);
    cJSON_Delete(name);
    if (!encoded) return 0;
    size_t length = strlen(out);
    static const char suffix[] = ",\"audioBase64\":\"";
    if (length + sizeof(suffix) >= capacity) return 0;
    memcpy(out + length, suffix, sizeof(suffix));
    return length + sizeof(suffix) - 1;
}

static void talk_close(esp_transport_handle_t ws, const char *session) {
    cJSON *frame = talk_request("passport-talk-close", "talk.session.close", session);
    (void)send_json(ws, frame);
}

typedef struct {
    char call[128], run[PASSPORT_RUN_ID_MAX], key[100], answer[PASSPORT_TEXT_MAX];
    char early_run[PASSPORT_RUN_ID_MAX], completed[4][128];
    char request[40], submit[40];
    unsigned serial, next_completed;
    bool terminal, failed, submitting, control;
    int64_t deadline;
} talk_consult_t;

static bool talk_tool_result(esp_transport_handle_t ws, const char *session, const char *call,
                             const char *id, const char *answer, bool failed) {
    cJSON *frame = talk_request(id, "talk.session.submitToolResult", session);
    cJSON *params = cJSON_GetObjectItemCaseSensitive(frame, "params");
    cJSON_AddStringToObject(params, "callId", call);
    cJSON *result = cJSON_AddObjectToObject(params, "result");
    cJSON_AddStringToObject(result, failed ? "error" : "result", answer);
    if (!failed) cJSON_AddBoolToObject(result, "excerpt", true);
    return send_json(ws, frame);
}

static void talk_tool_complete(talk_consult_t *tool) {
    memcpy(tool->completed[tool->next_completed++ % 4], tool->call, sizeof(tool->call));
    tool->call[0] = tool->run[0] = tool->early_run[0] = tool->answer[0] = 0;
    tool->terminal = tool->failed = tool->submitting = tool->control = false;
}

static bool talk_tool_call(esp_transport_handle_t ws, const char *session, const cJSON *payload,
                           talk_consult_t *tool) {
    const char *call = str(payload, "callId"), *name = str(payload, "name");
    if (!*call || strlen(call) >= sizeof(tool->call)) return false;
    if (!strcmp(call, tool->call)) return true;
    for (size_t index = 0; index < 4; index++) if (!strcmp(call, tool->completed[index])) return true;
    if (tool->call[0]) return talk_tool_result(ws, session, call, "passport-tool-busy", "Another voice task is active", true);
    const cJSON *args = cJSON_GetObjectItemCaseSensitive(payload, "args");
    cJSON *parsed = cJSON_IsString(args) ? cJSON_Parse(args->valuestring) : NULL;
    if (parsed) args = parsed;
    bool consult = !strcmp(name, "openclaw_agent_consult");
    bool control = !strcmp(name, "openclaw_agent_control");
    const char *text = str(args, control ? "text" : "question");
    bool valid = (consult || control) && cJSON_IsObject(args) && *text;
    if (!valid) {
        cJSON_Delete(parsed);
        return talk_tool_result(ws, session, call, "passport-tool-invalid", "Unsupported voice tool or arguments", true);
    }
    snprintf(tool->call, sizeof(tool->call), "%s", call);
    snprintf(tool->key, sizeof(tool->key), "%s", s_session);
    snprintf(tool->request, sizeof(tool->request), "passport-tool-%u", ++tool->serial);
    snprintf(tool->submit, sizeof(tool->submit), "passport-result-%u", tool->serial);
    tool->control = control;
    tool->deadline = esp_timer_get_time() + 120000000;
    cJSON *frame = talk_request(tool->request, control ? "talk.session.steer" : "talk.client.toolCall", control ? session : NULL);
    cJSON *params = cJSON_GetObjectItemCaseSensitive(frame, "params");
    cJSON_AddStringToObject(params, "sessionKey", s_session);
    if (control) {
        cJSON_AddStringToObject(params, "text", text);
        const char *mode = str(args, "mode");
        if (*mode) cJSON_AddStringToObject(params, "mode", mode);
    } else {
        cJSON_AddStringToObject(params, "relaySessionId", session);
        cJSON_AddStringToObject(params, "callId", call);
        cJSON_AddStringToObject(params, "name", name);
        cJSON_AddItemToObject(params, "args", cJSON_Duplicate(args, true));
    }
    cJSON_Delete(parsed);
    return send_json(ws, frame);
}

static bool talk_tool_event(esp_transport_handle_t ws, const char *session, const cJSON *frame,
                            talk_consult_t *tool) {
    const cJSON *payload = cJSON_GetObjectItemCaseSensitive(frame, "payload");
    const char *id = str(frame, "id");
    if (!tool->call[0]) return true;
    if (!strcmp(id, tool->submit)) {
        if (!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(frame, "ok"))) return false;
        talk_tool_complete(tool);
    } else if (!strcmp(id, tool->request)) {
        if (!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(frame, "ok"))) {
            snprintf(tool->answer, sizeof(tool->answer), "The agent could not start the voice task");
            tool->terminal = tool->failed = true;
        } else if (tool->control) {
            passport_reply_live_apply(tool->answer, sizeof(tool->answer), str(payload, "message"), true);
            tool->terminal = true;
        } else {
            const char *run = str(payload, "runId"), *key = str(payload, "agentSessionKey");
            if (!*run || strlen(run) >= sizeof(tool->run) || !*key || strlen(key) >= sizeof(tool->key)) return false;
            snprintf(tool->run, sizeof(tool->run), "%s", run);
            snprintf(tool->key, sizeof(tool->key), "%s", key);
            if (strcmp(tool->early_run, run)) { tool->answer[0] = 0; tool->terminal = tool->failed = false; }
        }
    } else if (!strcmp(str(frame, "event"), "chat") && !tool->submitting && !tool->control) {
        const char *run = str(payload, "runId"), *key = str(payload, "sessionKey");
        if (strcmp(key, tool->key) || !*run || strlen(run) >= sizeof(tool->early_run) ||
            (tool->run[0] && strcmp(run, tool->run))) return true;
        if (!tool->run[0]) snprintf(tool->early_run, sizeof(tool->early_run), "%s", run);
        const char *status = str(payload, "state");
        if (!strcmp(status, "final")) {
            message_text(cJSON_GetObjectItemCaseSensitive(payload, "message"), tool->answer, sizeof(tool->answer));
            if (!tool->answer[0]) snprintf(tool->answer, sizeof(tool->answer), "The agent finished with no text");
            tool->terminal = true;
        } else if (!strcmp(status, "error") || !strcmp(status, "aborted")) {
            snprintf(tool->answer, sizeof(tool->answer), "The voice task failed or was cancelled");
            tool->terminal = tool->failed = true;
        }
    }
    if (tool->call[0] && tool->terminal && !tool->submitting && (tool->failed || tool->control || tool->run[0]) && passport_audio_live_drained()) {
        tool->submitting = true;
        tool->deadline = esp_timer_get_time() + 15000000;
        return talk_tool_result(ws, session, tool->call, tool->submit, tool->answer, tool->failed);
    }
    return true;
}

static const char *talk_failure(const cJSON *detail) {
    const char *code = str(detail, "code"), *message = str(detail, "message");
    if (strstr(message, "unknown method") || strstr(message, "Unknown method"))
        return "网关不支持实时语音，请升级插件";
    if (strstr(message, "scope") || strstr(message, "permission") || strstr(code, "FORBIDDEN"))
        return "实时语音权限不足，请检查网关授权";
    if (strstr(message, "401") || strstr(message, "API key") || strstr(message, "API Key") ||
        strstr(message, "api_key") || strstr(message, "Unauthorized") || strstr(message, "unauthorized") ||
        strstr(message, "authentication failed"))
        return "实时服务凭据无效，请在网关检查";
    if (strstr(message, "quota") || strstr(message, "429") || strstr(message, "rate limit"))
        return "实时服务额度不足或限流，请在网关检查";
    if (strstr(message, "model") || strstr(message, "Model"))
        return "实时模型不可用，请检查网关语音配置";
    if (strstr(message, "configured") || strstr(message, "not configured") || strstr(message, "credentials"))
        return "网关未配置可用的实时语音服务";
    if (strstr(message, "403") || strstr(message, "region") || strstr(message, "country"))
        return "实时服务拒绝访问，请检查网关出口";
    if (strstr(message, "timeout") || strstr(message, "timed out") || strstr(message, "ECONN") ||
        strstr(message, "ENOTFOUND") || strstr(message, "fetch failed") || strstr(message, "connect"))
        return "网关连不上实时服务，请检查网关出口";
    return "网关实时语音服务出错，请检查网关日志";
}

static void talk_failure_report(const cJSON *detail) {
    const char *reason = talk_failure(detail);
    ESP_LOGW("passport_gateway", "talk_failure=%s", reason);
    state(PASSPORT_SEND_FAILED, reason);
}

static uint64_t talk_mark_sequence(const char *name) {
    if (strncmp(name, "audio-", 6) || !name[6]) return 0;
    uint64_t sequence = 0;
    for (const unsigned char *digit = (const unsigned char *)name + 6; *digit; digit++) {
        if (*digit < '0' || *digit > '9' || sequence > (UINT64_MAX - (*digit - '0')) / 10) return 0;
        sequence = sequence * 10 + *digit - '0';
    }
    return sequence;
}

static bool talk_operation(esp_transport_handle_t ws, bool synthetic) {
    static uint32_t sequence;
    enum { UPLOAD_CAPACITY = 5440 };
    char *upload = malloc(UPLOAD_CAPACITY);
    if (!upload) { state(PASSPORT_SEND_FAILED, "内存不足，请重试"); return false; }
    char session[80] = "", early_ready[80] = "", early_failed[80] = "";
    const char *early_failure = NULL, *failure = NULL;
    uint32_t first_sequence = sequence, pending = 0;
    unsigned receive_turns = 0;
    uint32_t sent_at[32] = {0};
    struct { char name[96]; uint32_t target; } marks[8] = {0};
    unsigned mark_head = 0, mark_tail = 0;
    uint32_t submitted = 0;
    int64_t mark_deadline = 0;
    bool created = false, ready = false, result = false, audio_started = false;
    bool speaking = false, audio_done = false, early_closed = false;
    bool compact_silence = false;
    bool compact_ima = false;
    bool interrupt_pending = false;
    int64_t interrupt_deadline = 0;
    int ima_index = 0;
    uint64_t event_sequence = 0, playback_sequence = 0;
    talk_consult_t tool = {0};
    int64_t started = esp_timer_get_time(), last_ping = started, recovery_deadline = 0;
    int64_t last_upload = started;
    int64_t receive_until = 0;
    int64_t last_iteration = started, loss_checked = started;
    int64_t test_started = 0;
    uint32_t previous_dropped = 0;
    unsigned loss_windows = 0;
    s_talk_trace = (talk_trace_t){ .step = "create" };
    cJSON *frame = talk_request("passport-talk-create", "talk.session.create", NULL);
    cJSON *params = cJSON_GetObjectItemCaseSensitive(frame, "params");
    cJSON_AddStringToObject(params, "mode", "realtime");
    cJSON_AddStringToObject(params, "transport", "gateway-relay");
    cJSON_AddStringToObject(params, "brain", "agent-consult");
    cJSON_AddStringToObject(params, "language", s_live_zh ? "zh" : "en");
    cJSON_AddNumberToObject(params,"silenceDurationMs",600);
    if (synthetic) {
        char key[sizeof(s_session) + 12];
        snprintf(key, sizeof(key), "%s-synthetic", s_session);
        cJSON_AddStringToObject(params, "sessionKey", key);
    } else cJSON_AddStringToObject(params, "sessionKey", s_session);
    s_live[0] = 0;
    if (!send_json(ws, frame)) goto done;
    while (!passport_tls_cancelled()) {
        vTaskDelay(pdMS_TO_TICKS(1));
        int64_t now = esp_timer_get_time();
        uint32_t loop_gap = (uint32_t)((now - last_iteration) / 1000);
        if (loop_gap > s_talk_trace.loop_gap_ms) s_talk_trace.loop_gap_ms = loop_gap;
        last_iteration = now;
        s_talk_trace.loops++;
        if (audio_started && passport_audio_finished()) {
            s_talk_trace.step = "capture-stopped";
            state(PASSPORT_SEND_FAILED, "实时音频不可用");
            break;
        }
        if (synthetic && test_started && now - test_started >= 20000000) {
            s_talk_trace.step = "synthetic-complete";
            talk_close(ws, session);
            result = true;
            state(PASSPORT_SEND_DONE, "助手已连接");
            break;
        }
        s_talk_trace.dropped = audio_started ? passport_audio_live_dropped() : 0;
        if (now - loss_checked >= 1000000) {
            uint32_t lost = s_talk_trace.dropped - previous_dropped;
            bool impaired = ready && !speaking && !tool.call[0] &&
                (uint64_t)lost * 512 * 1000000 >= (uint64_t)(now - loss_checked) * 12000;
            loss_windows = impaired ? loss_windows + 1 : 0;
            previous_dropped = s_talk_trace.dropped;
            loss_checked = now;
            if (loss_windows >= 3) {
                s_talk_trace.step = "capture-loss";
                state(PASSPORT_SEND_FAILED, "实时音频跟不上网络，请重试");
                break;
            }
        }
        uint32_t oldest_ms = pending ? (uint32_t)(now / 1000) - sent_at[first_sequence % 32] : 0;
        if (audio_started && passport_audio_live_take_interrupt() && !interrupt_pending) {
            cJSON *interrupt = talk_request("passport-interrupt", "talk.session.cancelOutput", session);
            cJSON_AddStringToObject(cJSON_GetObjectItemCaseSensitive(interrupt, "params"), "reason", "barge-in");
            if (!send_json(ws, interrupt)) break;
            interrupt_pending = true;
            interrupt_deadline = now + 10000000;
            mark_tail = mark_head;
            tool = (talk_consult_t){0};
            speaking = audio_done = false;
            passport_audio_live_response(false);
            state(PASSPORT_SEND_LISTENING, "可以继续说话");
        }
        if (interrupt_pending && now > interrupt_deadline) {
            state(PASSPORT_SEND_FAILED, "实时语音超时，请重新连接"); break;
        }
        s_talk_trace.pending = (uint32_t)__builtin_popcount(pending);
        if (oldest_ms > s_talk_trace.oldest_ms) s_talk_trace.oldest_ms = oldest_ms;
        s_talk_trace.elapsed_ms = (uint32_t)((now - started) / 1000);
        portENTER_CRITICAL(&s_guard);
        s_talk_retained = s_talk_trace;
        portEXIT_CRITICAL(&s_guard);
        if ((!audio_started && now - started > 30000000) || (recovery_deadline && now > recovery_deadline)) {
            s_talk_trace.step = "ready-timeout";
            state(PASSPORT_SEND_FAILED, failure ? failure : "实时语音超时，请重新连接");
            break;
        }
        bool append_expired = pending && oldest_ms > 10000;
        bool tool_expired = tool.call[0] && now > tool.deadline;
        if (append_expired || tool_expired || now - s_last_rx_us > 90000000) {
            s_talk_trace.step = append_expired ? "append-timeout" : tool_expired ? "tool-timeout" : "receive-timeout";
            state(PASSPORT_SEND_FAILED, "实时语音超时，请重新连接");
            break;
        }
        if (mark_head != mark_tail) {
            s_talk_trace.step = "mark-drain";
            if ((int32_t)(passport_audio_live_completed() - marks[mark_tail % 8].target) >= 0) {
                cJSON *ack = talk_request("passport-mark", "talk.session.acknowledgeMark", session);
                cJSON_AddStringToObject(cJSON_GetObjectItemCaseSensitive(ack, "params"), "markName", marks[mark_tail % 8].name);
                s_talk_trace.step = "mark-write";
                if (!send_json(ws, ack)) break;
                mark_tail++;
                mark_deadline = now + 2000000;
                s_talk_trace.marks++;
            } else if (now > mark_deadline) break;
        }
        if (ready && !audio_started) {
            s_talk_trace.step = "capture-start";
            esp_err_t audio_error = compact_ima ? passport_audio_live_ima_start(synthetic) :
                synthetic ? passport_audio_live_test_start() : passport_audio_live_start();
            if (audio_error != ESP_OK) { state(PASSPORT_SEND_FAILED, "实时音频不可用"); break; }
            audio_started = true;
            test_started = now;
            passport_memory_report("live_ready");
            timing("talk_ready", started);
            state(PASSPORT_SEND_LISTENING, "可以直接说话，停顿后自动回复");
        }
        if (ready && speaking && audio_done && passport_audio_live_drained()) {
            speaking = audio_done = false;
            passport_audio_live_response(tool.call[0] != 0);
            state(tool.call[0] ? PASSPORT_SEND_WAITING : PASSPORT_SEND_LISTENING,
                tool.call[0] ? "助手正在处理，请稍候" : s_live[0] ? s_live : "可以继续说话");
        }
        int64_t prepare_started = esp_timer_get_time();
        size_t available = ready ? passport_audio_live_available() : 0;
        bool receive_due = receive_turns && now < receive_until && available < 7680 &&
            (s_receive->started || esp_transport_poll_read(ws, 0) > 0);
        if (ready && !available) s_talk_trace.empty++;
        if (available && receive_due) s_talk_trace.receive_waits++;
        if (available && (uint32_t)(sequence - first_sequence) >= 32u) s_talk_trace.window_waits++;
        if (ready && available &&
            (available >= 3840 || now - last_upload >= 80000) &&
            (uint32_t)(sequence - first_sequence) < 32u && !receive_due) {
            s_talk_trace.step = "capture-pull";
            char *out = upload;
            size_t prefix = talk_audio_prefix(out, UPLOAD_CAPACITY, session, sequence);
            if (!prefix) break;
            size_t remaining = available < 3840 ? available : 3840;
            size_t length = prefix;
            if (compact_ima) {
                memcpy(out + length, "passport-ima:", 13);
                length += 13;
            }
            size_t captured = 0;
            size_t compressed_bytes = 0;
            uint8_t *compressed_audio = (uint8_t *)upload + UPLOAD_CAPACITY - 1024;
            bool pulled = true;
            bool silent = compact_silence;
            while (remaining) {
                size_t capacity = remaining < sizeof(s_upload_samples) ? remaining : sizeof(s_upload_samples);
                size_t bytes = 0, encoded = 0;
                if (passport_audio_live_pull(s_upload_samples, capacity, &bytes) != ESP_OK) {
                    state(PASSPORT_SEND_FAILED, "实时音频跟不上网络，请重新连接"); pulled = false; break;
                }
                if (!bytes) break;
                for (size_t index = 0; silent && index < bytes; index++) silent = s_upload_samples[index] == 0;
                const uint8_t *source = s_upload_samples;
                size_t source_bytes = bytes;
                if (compact_ima) {
                    source_bytes = passport_ima_encode((const int16_t *)s_upload_samples, bytes / 2,
                        &ima_index, compressed_audio + compressed_bytes, 1024 - compressed_bytes);
                    if (!source_bytes) { pulled = false; break; }
                    compressed_bytes += source_bytes;
                } else {
                    if (mbedtls_base64_encode((uint8_t *)out + length, UPLOAD_CAPACITY - length - 3,
                        &encoded, source, source_bytes)) { pulled = false; break; }
                    length += encoded;
                }
                captured += bytes;
                remaining -= bytes;
            }
            if (!pulled) break;
            if (compact_ima && compressed_bytes) {
                size_t encoded = 0;
                if (mbedtls_base64_encode((uint8_t *)out + length, (size_t)(compressed_audio - (uint8_t *)out) - length,
                    &encoded, compressed_audio, compressed_bytes)) break;
                length += encoded;
            }
            if (captured) {
                if (silent) {
                    int count = snprintf(out + prefix, UPLOAD_CAPACITY - prefix, "passport-silence:%u", (unsigned)captured);
                    if (count < 0 || (size_t)count + prefix + 4 > UPLOAD_CAPACITY) break;
                    length = prefix + (size_t)count;
                    s_talk_trace.silence_appends++;
                }
                uint32_t index = sequence - first_sequence;
                if (index >= 32 || (pending & (1u << index))) { state(PASSPORT_SEND_FAILED, "实时音频确认异常"); break; }
                memcpy(out + length, "\"}}", 4);
                sent_at[sequence % 32] = (uint32_t)(now / 1000);
                sequence++;
                s_talk_trace.step = "append-write";
                if (speaking) s_talk_trace.during_response++;
                if (tool.call[0]) s_talk_trace.during_consult++;
                int64_t write_started = esp_timer_get_time();
                s_talk_trace.prepare_us += (uint64_t)(write_started - prepare_started);
                bool written = send_fragment(ws, WS_TRANSPORT_OPCODES_TEXT | WS_TRANSPORT_OPCODES_FIN,
                    out, length + 3);
                int64_t write_finished = esp_timer_get_time();
                s_talk_trace.write_us += (uint64_t)(write_finished - write_started);
                prepare_started = write_finished;
                uint32_t write_ms = (uint32_t)((write_finished - write_started) / 1000);
                if (write_ms > s_talk_trace.write_ms) s_talk_trace.write_ms = write_ms;
                s_talk_trace.write_total_ms += write_ms;
                if (!written) break;
                s_talk_trace.wire_bytes += length + 3;
                s_talk_trace.upload_bytes += captured;
                s_talk_trace.appends++;
                pending |= 1u << index;
                last_upload = esp_timer_get_time();
                receive_turns = 8;
                receive_until = write_ms >= 80 ? write_finished + 160000 : write_started + 80000;
                if (index + 1 > s_talk_trace.inflight) s_talk_trace.inflight = index + 1;
            }
        }
        s_talk_trace.prepare_us += (uint64_t)(esp_timer_get_time() - prepare_started);
        if (now - last_ping > 20000000) {
            s_talk_trace.step = "ping-write";
            if (!send_fragment(ws, WS_TRANSPORT_OPCODES_PING | WS_TRANSPORT_OPCODES_FIN, "", 0)) break;
            last_ping = now;
        }
        s_talk_trace.step = "receive";
        int64_t receive_started = esp_timer_get_time();
        frame = receive_json(ws, 10, false);
        int64_t receive_us = esp_timer_get_time() - receive_started;
        s_talk_trace.receive_us += (uint64_t)receive_us;
        uint32_t receive_ms = (uint32_t)(receive_us / 1000);
        if (receive_ms > s_talk_trace.receive_ms) s_talk_trace.receive_ms = receive_ms;
        if (!frame) {
            if (s_rx_timed_out) {
                s_talk_trace.step = "tool-result";
                if (!interrupt_pending && !talk_tool_event(ws, session, NULL, &tool)) break;
                continue;
            }
            break;
        }
        if (receive_turns) receive_turns--;
        s_talk_trace.replies++;
        s_talk_trace.step = "tool-event";
        bool was_consulting = tool.call[0] != 0;
        if (!interrupt_pending && !talk_tool_event(ws, session, frame, &tool)) { cJSON_Delete(frame); break; }
        if (was_consulting && !tool.call[0]) {
            passport_audio_live_response(speaking || !ready);
            if (ready) state(speaking ? PASSPORT_SEND_SPEAKING : PASSPORT_SEND_LISTENING, s_live[0] ? s_live : "可以继续说话");
        }
        const cJSON *payload = cJSON_GetObjectItemCaseSensitive(frame, "payload");
        const char *id = str(frame, "id"), *event = str(payload, "type");
        if (!strcmp(id, "passport-talk-create")) {
            s_talk_trace.step = "create-reply";
            const char *selected_session = str(payload, "sessionId");
            const cJSON *audio = cJSON_GetObjectItemCaseSensitive(payload, "audio");
            const cJSON *rate = cJSON_GetObjectItemCaseSensitive(audio, "inputSampleRateHz");
            const cJSON *output_rate = cJSON_GetObjectItemCaseSensitive(audio, "outputSampleRateHz");
            bool format = !strcmp(str(audio, "inputEncoding"), "pcm16") &&
                cJSON_IsNumber(rate) && rate->valueint == 24000 &&
                !strcmp(str(audio, "outputEncoding"), "pcm16") && cJSON_IsNumber(output_rate) && output_rate->valueint == 24000;
            if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(frame, "ok")) && *selected_session && strlen(selected_session) < sizeof(session)) {
                snprintf(session, sizeof(session), "%s", selected_session);
                created = format;
                compact_silence = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(payload, "passportSilence"));
                compact_ima = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(payload, "passportIma"));
                s_talk_trace.ima = compact_ima;
                ready = !strcmp(early_ready, session);
            }
            if (!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(frame, "ok"))) {
                talk_failure_report(cJSON_GetObjectItemCaseSensitive(frame, "error"));
                cJSON_Delete(frame); break;
            }
            if (!created) {
                state(PASSPORT_SEND_FAILED, "网关返回的语音格式不兼容");
                cJSON_Delete(frame); break;
            }
            if (!strcmp(early_failed, session)) {
                failure = early_failure;
                if (early_closed) {
                    state(PASSPORT_SEND_FAILED, failure ? failure : "实时服务就绪前已关闭，请检查网关日志");
                    cJSON_Delete(frame); break;
                }
            }
        } else if (!strcmp(id, "passport-interrupt")) {
            if (!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(frame, "ok"))) {
                state(PASSPORT_SEND_FAILED, "网关拒绝实时音频"); cJSON_Delete(frame); break;
            }
            interrupt_pending = false;
            if (passport_audio_live_clear() != ESP_OK) { cJSON_Delete(frame); break; }
            submitted = passport_audio_live_position();
            tool = (talk_consult_t){0};
            passport_audio_live_response(false);
            state(PASSPORT_SEND_LISTENING, "可以继续说话");
        } else if (!strncmp(id, "passport-a-", 11)) {
            s_talk_trace.step = "append-reply";
            char *end = NULL;
            unsigned long number = strtoul(id + 11, &end, 10);
            uint32_t index = (uint32_t)number - first_sequence;
            if (end && !*end && number <= UINT32_MAX && index < 32 && (pending & (1u << index))) {
                uint32_t elapsed = (uint32_t)(esp_timer_get_time() / 1000) - sent_at[number % 32];
                if (elapsed > s_talk_trace.ack_ms) s_talk_trace.ack_ms = elapsed;
                if (!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(frame, "ok"))) {
                    state(PASSPORT_SEND_FAILED, "网关拒绝实时音频"); cJSON_Delete(frame); break;
                }
                pending &= ~(1u << index);
                while (first_sequence != sequence && !(pending & 1)) { pending >>= 1; first_sequence++; }
            }
        } else if (!strcmp(str(frame, "event"), "talk.event")) {
            const char *owner = str(payload, "relaySessionId");
            if (!created && *owner && !strcmp(event, "ready") && strlen(owner) < sizeof(early_ready)) {
                snprintf(early_ready, sizeof(early_ready), "%s", owner);
                if (!strcmp(early_failed, owner) && !early_closed) early_failure = NULL;
            }
            if (!created && *owner && (!strcmp(event, "error") || !strcmp(event, "close")) &&
                strlen(owner) < sizeof(early_failed) && (!early_failed[0] || !strcmp(early_failed, owner))) {
                snprintf(early_failed, sizeof(early_failed), "%s", owner);
                if (!strcmp(event, "error")) early_failure = talk_failure(payload);
                else early_closed = true;
                if (!strcmp(early_ready, owner)) early_ready[0] = 0;
            }
            if (created && !strcmp(owner, session)) {
                const cJSON *talk_event = cJSON_GetObjectItemCaseSensitive(payload, "talkEvent");
                const cJSON *seq = cJSON_GetObjectItemCaseSensitive(talk_event, "seq");
                const cJSON *playback_seq = cJSON_GetObjectItemCaseSensitive(payload, "playbackSequence");
                bool playback_event = !strcmp(event, "audio") || !strcmp(event, "mark") ||
                    !strcmp(event, "audioDone") || !strcmp(event, "responseStarted") ||
                    ((!strcmp(event, "transcript") || !strcmp(event, "partial")) && !strcmp(str(payload, "role"), "assistant"));
                if (interrupt_pending && (playback_event || !strcmp(event, "toolCall") || !strcmp(event, "toolResult"))) {
                    cJSON_Delete(frame); continue;
                }
                if (playback_event && cJSON_IsNumber(playback_seq) && playback_seq->valuedouble > 0) {
                    uint64_t current = (uint64_t)playback_seq->valuedouble;
                    if (current <= playback_sequence) { s_talk_trace.skipped++; cJSON_Delete(frame); continue; }
                    playback_sequence = current;
                } else if (cJSON_IsNumber(seq) && seq->valuedouble > 0) {
                    uint64_t current = (uint64_t)seq->valuedouble;
                    if (current <= event_sequence) { s_talk_trace.skipped++; cJSON_Delete(frame); continue; }
                    event_sequence = current;
                }
                bool activity = !strcmp(event, "audio") || !strcmp(event, "responseStarted") ||
                    !strcmp(event, "transcript") || !strcmp(event, "partial");
                if (!strcmp(event, "ready") || (audio_started && activity)) {
                    if (!ready && audio_started) {
                        passport_audio_live_response(speaking || tool.call[0]);
                        state(tool.call[0] ? PASSPORT_SEND_WAITING : speaking ? PASSPORT_SEND_SPEAKING : PASSPORT_SEND_LISTENING,
                            tool.call[0] ? "助手正在处理，请稍候" : s_live[0] ? s_live : "可以继续说话");
                    }
                    ready = true;
                    recovery_deadline = 0;
                    failure = NULL;
                }
                if (!strcmp(event, "toolCall")) {
                    s_talk_trace.step = "tool-call";
                    if (synthetic) {
                        if (!talk_tool_result(ws, session, str(payload, "callId"), "passport-synthetic-tool",
                            "Synthetic audio test; tools are disabled", true)) { cJSON_Delete(frame); break; }
                        cJSON_Delete(frame);
                        continue;
                    }
                    bool consulting = tool.call[0] != 0;
                    if (!talk_tool_call(ws, session, payload, &tool)) { cJSON_Delete(frame); break; }
                    if (!consulting && tool.call[0]) {
                        s_talk_trace.consults++;
                        passport_audio_live_response(true);
                        state(PASSPORT_SEND_WAITING, "助手正在处理，请稍候");
                    }
                } else if (!strcmp(event, "toolCallCancelled") || !strcmp(event, "toolResult")) {
                    const cJSON *final = cJSON_GetObjectItemCaseSensitive(talk_event, "final");
                    if (!strcmp(str(payload, "callId"), tool.call) &&
                        (!strcmp(event, "toolCallCancelled") || (!cJSON_IsFalse(final) && strcmp(str(talk_event, "type"), "tool.progress")))) {
                        talk_tool_complete(&tool);
                        passport_audio_live_response(speaking || !ready);
                        if (ready) state(speaking ? PASSPORT_SEND_SPEAKING : PASSPORT_SEND_LISTENING, s_live[0] ? s_live : "可以继续说话");
                    }
                }
                else if (!strcmp(event, "error")) {
                    s_talk_trace.step = "provider-error";
                    failure = talk_failure(payload);
                    if (!strcmp(str(payload, "phase"), "response")) {
                        state(PASSPORT_SEND_FAILED, failure);
                        cJSON_Delete(frame); break;
                    }
                    ESP_LOGW("passport_gateway", "talk_recovering=%s", failure);
                    if (!recovery_deadline) recovery_deadline = now + 30000000;
                    ready = false;
                    passport_audio_live_response(true);
                    state(PASSPORT_SEND_CONNECTING, "正在连接助手");
                } else if (!strcmp(event, "close")) {
                    s_talk_trace.step = "session-close";
                    if (audio_started && !failure && !strcmp(str(payload, "reason"), "completed")) {
                        result = true;
                        state(PASSPORT_SEND_DONE, "通话已结束");
                    } else state(PASSPORT_SEND_FAILED, failure ? failure : audio_started ? "实时会话已断开，请重新开始" : "实时服务就绪前已关闭，请检查网关日志");
                    cJSON_Delete(frame); break;
                } else if (!strcmp(event, "transcript") || !strcmp(event, "partial")) {
                    const char *caption = str(payload, "text");
                    if (!strcmp(str(payload, "role"), "user") &&
                        cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(payload, "final"))) s_talk_trace.inputs++;
                    if (!strcmp(str(payload, "role"), "assistant")) {
                        bool snapshot = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(payload, "final")) || !strcmp(str(payload, "textMode"), "snapshot");
                        passport_reply_live_tail(s_live, sizeof(s_live), caption, snapshot);
                        state(tool.call[0] ? PASSPORT_SEND_WAITING : speaking ? PASSPORT_SEND_SPEAKING : PASSPORT_SEND_LISTENING, s_live);
                    } else if (*caption && !speaking && !tool.call[0]) {
                        passport_reply_live_tail(s_live, sizeof(s_live), caption, true);
                        state(PASSPORT_SEND_LISTENING, s_live);
                    }
                } else if (!strcmp(event, "responseStarted") || !strcmp(event, "audio")) {
                    if (!speaking) { s_live[0] = 0; speaking = true; passport_audio_live_response(true); }
                    audio_done = false;
                    state(PASSPORT_SEND_SPEAKING, s_live[0] ? s_live : "正在语音回复");
                    if (!strcmp(event, "audio")) {
                        s_talk_trace.step = "audio-decode";
                        size_t bytes = 0;
                        const char *encoded = str(payload, "audioBase64");
                        bool compressed = !strncmp(encoded, "passport-ima:", 13);
                        if (compressed && !compact_ima) {
                            state(PASSPORT_SEND_FAILED, "实时音频格式不支持"); cJSON_Delete(frame); break;
                        }
                        if (compressed) encoded += 13;
                        size_t encoded_length = strlen(encoded);
                        if (!encoded_length || encoded_length > (compressed ? 1312u : 5120u) || encoded_length % 4) {
                            state(PASSPORT_SEND_FAILED, "实时音频格式不支持"); cJSON_Delete(frame); break;
                        }
                        bool accepted = true;
                        size_t stride = compressed ? 328 : 1280;
                        for (size_t offset = 0; offset < encoded_length; offset += stride) {
                            size_t length = encoded_length - offset;
                            if (length > stride) length = stride;
                            if (mbedtls_base64_decode(s_upload_samples, sizeof(s_upload_samples), &bytes,
                                (const uint8_t *)encoded + offset, length) || !bytes || (!compressed && bytes % 2) ||
                                (offset + length < encoded_length && memchr(encoded + offset, '=', length))) {
                                state(PASSPORT_SEND_FAILED, "实时音频格式不支持"); accepted = false; break;
                            }
                            esp_err_t pushed;
                            size_t samples = compressed ? passport_ima_samples(s_upload_samples, bytes) : bytes / 2;
                            if (!samples) { state(PASSPORT_SEND_FAILED, "实时音频格式不支持"); accepted = false; break; }
                            s_talk_trace.step = "audio-playback";
                            int64_t limit = esp_timer_get_time() + 2000000;
                            while ((pushed = compressed ? passport_audio_live_push_ima(s_upload_samples, bytes) :
                                    passport_audio_live_push(s_upload_samples, bytes)) == ESP_ERR_NO_MEM &&
                                   !passport_tls_cancelled() && esp_timer_get_time() < limit) vTaskDelay(pdMS_TO_TICKS(2));
                            if (pushed != ESP_OK) { state(PASSPORT_SEND_FAILED, "实时播放缓冲不足"); accepted = false; break; }
                            submitted += samples * 2;
                        }
                        if (!accepted) { cJSON_Delete(frame); break; }
                        s_talk_trace.audio++;
                    }
                } else if (!strcmp(event, "audioDone")) { audio_done = true; s_talk_trace.turns++; }
                else if (!strcmp(event, "mark")) {
                    const char *name = str(payload, "markName");
                    if (!*name || strlen(name) >= sizeof(marks[0].name)) {
                        cJSON_Delete(frame); break;
                    }
                    unsigned slot = mark_head % 8;
                    if (mark_head - mark_tail == 8) {
                        slot = (mark_head - 1) % 8;
                        uint64_t previous = talk_mark_sequence(marks[slot].name);
                        if (!previous || talk_mark_sequence(name) <= previous) {
                            state(PASSPORT_SEND_FAILED, "实时播放缓冲不足"); cJSON_Delete(frame); break;
                        }
                    } else mark_head++;
                    if (mark_head - mark_tail == 1) mark_deadline = now + 2000000;
                    snprintf(marks[slot].name, sizeof(marks[0].name), "%s", name);
                    marks[slot].target = submitted;
                } else if (!strcmp(event, "clear")) {
                    s_talk_trace.step = "playback-clear";
                    if (passport_audio_live_clear() != ESP_OK) {
                        state(PASSPORT_SEND_FAILED, "实时播放缓冲不足"); cJSON_Delete(frame); break;
                    }
                    mark_tail = mark_head;
                    speaking = audio_done = false;
                    s_live[0] = 0;
                    passport_audio_live_response(!interrupt_pending && (tool.call[0] || !ready));
                    if (ready) state(tool.call[0] ? PASSPORT_SEND_WAITING : PASSPORT_SEND_LISTENING,
                        tool.call[0] ? "助手正在处理，请稍候" : "可以继续说话");
                }
            }
        }
        cJSON_Delete(frame);
    }
done:
    if (passport_tls_cancelled()) s_talk_trace.step = "cancelled";
    s_talk_trace.transport = passport_transport_ws_failure(ws);
    s_talk_trace.elapsed_ms = (uint32_t)((esp_timer_get_time() - started) / 1000);
    portENTER_CRITICAL(&s_guard);
    s_talk_retained = s_talk_trace;
    portEXIT_CRITICAL(&s_guard);
    if (audio_started) {
        ESP_LOGI("passport_timing", "live_dropped_chunks=%u", (unsigned)passport_audio_live_dropped());
        esp_err_t stopped = passport_audio_stop();
        if (stopped != ESP_ERR_TIMEOUT) (void)passport_audio_release();
        else state(PASSPORT_SEND_FAILED, "实时音频停止超时");
        passport_audio_live_response(false);
    }
    if (session[0] && !result && (!passport_tls_cancelled() || passport_tls_cancel_can_write())) {
        s_aborting = passport_tls_cancelled();
        talk_close(ws, session);
        s_aborting = false;
    }
    if (!result && s_state != PASSPORT_SEND_FAILED && !passport_tls_cancelled()) state(PASSPORT_SEND_FAILED, "实时连接中断，请重新连接");
    free(upload);
    return result;
}

static cJSON *check_request(esp_transport_handle_t ws, const char *method, const char *request_id) {
    cJSON *request = cJSON_CreateObject();
    cJSON_AddStringToObject(request, "type", "req");
    cJSON_AddStringToObject(request, "id", request_id);
    cJSON_AddStringToObject(request, "method", method);
    cJSON *params = cJSON_AddObjectToObject(request, "params");
    if (!strcmp(method, "chat.history")) {
        cJSON_AddStringToObject(params, "sessionKey", s_session);
        cJSON_AddNumberToObject(params, "limit", 1);
    } else cJSON_AddStringToObject(params, "agentId", "passport");
    if (!send_json(ws, request)) return NULL;
    int64_t deadline = esp_timer_get_time() + 8000000;
    while (esp_timer_get_time() < deadline && !passport_tls_cancelled()) {
        cJSON *response = receive_json(ws, 250, false);
        if (!response) { if (s_rx_timed_out) continue; return NULL; }
        if (!strcmp(str(response, "id"), request_id)) return response;
        cJSON_Delete(response);
    }
    return NULL;
}

static void check_capabilities(esp_transport_handle_t ws) {
    passport_gateway_check_t result = { .gateway = true };
    cJSON *response = check_request(ws, "chat.history", "passport-check-history");
    result.ask = response && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(response, "ok"));
    cJSON_Delete(response);
    response = check_request(ws, "passport.status", "passport-check-status");
    result.plugin = response ? 2 : 4;
    if (response && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(response, "ok"))) {
        const cJSON *payload = cJSON_GetObjectItemCaseSensitive(response, "payload");
        const cJSON *protocol = cJSON_GetObjectItemCaseSensitive(payload, "protocol");
        result.plugin = cJSON_IsNumber(protocol) && protocol->valueint == 1 ? 1 : 3;
        result.call = result.plugin != 1 ? 0 :
            cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(payload, "upstreamAvailable")) &&
            cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(payload, "upstreamConfigured")) &&
            cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(payload, "providerRegistered")) ? 1 : 2;
    }
    cJSON_Delete(response);
    portENTER_CRITICAL(&s_guard);
    s_check = result;
    portEXIT_CRITICAL(&s_guard);
    state(PASSPORT_SEND_DONE, "助手已连接");
}

static void send_operation(int mode, esp_transport_handle_t *connection, esp_transport_handle_t *transport) {
    /* Each task owns its operation mode. A diagnostic connection must never
     * fall through into upload because of a shared flag changing later. */
    const bool probe = mode == 1 || mode == 2 || mode == GATEWAY_CHECK;
    const bool history_probe = mode == 2;
    const bool resume_only = mode == 3;
    const bool talk = mode == GATEWAY_LIVE || mode == GATEWAY_LIVE_TEST;
    esp_transport_handle_t parent = *transport, ws = *connection;
    int64_t identity_start = esp_timer_get_time();
    if (!identity()) { state(PASSPORT_SEND_FAILED, "设备身份保存失败"); goto done; }
    timing("identity", identity_start);
    snprintf(s_token, sizeof(s_token), "%s", s_agent.device_token);
    /* Paired: connect now and stream the clip while it is recorded.
     * Not paired yet: wait for the recording, then pair as usual. */
    const bool streaming = !probe && !talk && atomic_load(&s_preconnect) && s_token[0];
    if (streaming) {
        if (!ws) ws = open_gateway_retry(false, &parent);
        if (!ws) goto done;
    } else if (!probe && atomic_load(&s_preconnect)) {
        /* Not paired yet: wait for the recording, then pair as usual. */
        while (!atomic_load(&s_go) && !passport_tls_cancelled()) vTaskDelay(pdMS_TO_TICKS(50));
        if (passport_tls_cancelled()) goto done;
        if (!passport_clip_size()) { state(PASSPORT_SEND_FAILED, "没有录音，请重新说话"); goto done; }
    }
    if (!probe && !talk) {
        uint8_t scope[32];reply_scope(scope);
        if (resume_only) {
            if (s_resume_requested[0]) {
                if (!passport_reply_matches(&s_reply,s_resume_requested) || memcmp(s_reply.scope,scope,32)) {
                    s_live[0]=0;s_live_seq=0;clear_transcript();
                }
                (void)passport_reply_resume(&s_reply,s_resume_requested,scope);
            }
            if (!s_reply.acknowledged || memcmp(s_reply.scope,scope,32)) {
                state(PASSPORT_SEND_FAILED,"请切回原助手取回回复");goto done;
            }
        } else {
            bool same=s_reply.bound && !strcmp(s_reply.request_id,passport_clip_id()) && !memcmp(s_reply.scope,scope,32);
            passport_reply_bind(&s_reply,passport_clip_id(),scope);
            if (!same) { s_live[0]=0;s_live_seq=0;clear_transcript(); }
        }
        atomic_store(&s_accepted,s_reply.acknowledged);
    }
    if (!ws && !s_token[0]) {
        if (!s_agent.bootstrap[0]) { state(PASSPORT_SEND_FAILED, "请先配置助手"); goto done; }
        ws = open_gateway_retry(true, &parent);
        if (!ws) goto done;
        passport_tls_forget_socket();
        esp_transport_close(ws); esp_transport_destroy(ws); ws = NULL;
        esp_transport_destroy(parent); parent = NULL;
    }
    if (!ws) ws = open_gateway_retry(false, &parent);
    if (!ws) goto done;
    if (talk) { (void)talk_operation(ws, mode == GATEWAY_LIVE_TEST); goto done; }
    if (mode == GATEWAY_CHECK) { check_capabilities(ws); goto done; }
    if (probe) {
        if (history_probe) {
            if (!request_reply_history(ws)) goto done;
            int64_t deadline=esp_timer_get_time()+30LL*1000*1000;
            bool received=false;
            while (esp_timer_get_time()<deadline) {
                cJSON *frame=receive_json(ws,5000,false);
                if (!frame) { if (s_rx_timed_out) continue; break; }
                bool matched=!strcmp(str(frame,"id"),"passport-history");
                if (matched) received=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(frame,"ok"));
                if (matched && !received) auth_error(frame);
                cJSON_Delete(frame);
                if (matched) break;
            }
            if (!received) goto done;
        }
        state(PASSPORT_SEND_DONE, "助手已连接"); goto done;
    }
    if (s_deferred_abort.valid && !memcmp(s_deferred_abort.scope,s_reply.scope,32)) {
        if (abort_run(ws,s_deferred_abort.run_id)) s_deferred_abort.valid=false;
    }
    if (s_reply.acknowledged && !s_reply.complete) {
        ESP_LOGI("passport_gateway","resuming accepted run=%s without audio upload",s_reply.run_id);
        state(PASSPORT_SEND_WAITING,"正在取回回复");
    } else {
        state(PASSPORT_SEND_UPLOAD,"正在发送");
        bool sent = upload(ws, streaming);
        /* A dropped tunnel mid-upload is usually transient and the gateway
         * never saw the request, so reconnect and resend once. */
        if (!sent && s_state != PASSPORT_SEND_FAILED && !passport_tls_cancelled()) {
            ESP_LOGW("passport_gateway","upload interrupted; reconnecting once");
            passport_tls_forget_socket();
            esp_transport_close(ws); esp_transport_destroy(ws); ws = NULL;
            esp_transport_destroy(parent); parent = NULL;
            /* Resend the whole clip, so wait until recording has stopped. */
            while (streaming && !atomic_load(&s_go) && !passport_tls_cancelled()) vTaskDelay(pdMS_TO_TICKS(50));
            ws = passport_tls_cancelled() ? NULL : open_gateway_retry(false, &parent);
            if (ws) { state(PASSPORT_SEND_UPLOAD,"正在发送"); sent = upload(ws, streaming); }
        }
        if (!sent) {
            if (s_state != PASSPORT_SEND_FAILED) state(PASSPORT_SEND_FAILED,"语音发送中断，请重试");
            goto done;
        }
        state(PASSPORT_SEND_WAITING,"正在确认发送");
    }
    int64_t reply_start = esp_timer_get_time();
    bool replied=await_reply(ws);
    if (!replied && !passport_tls_cancelled() && s_reply_interrupted && s_reply.acknowledged &&
        !s_reply.complete && passport_wifi_is_online()) {
        close_gateway(&ws,&parent);
        begin_connection();
        ws=open_gateway_retry(false,&parent);
        if (ws) {
            state(s_live[0]?PASSPORT_SEND_PARTIAL:PASSPORT_SEND_WAITING,s_live[0]?s_live:"正在取回回复");
            replied=await_reply(ws);
        }
    }
    if (replied) {
        timing("reply", reply_start);
        if (atomic_load(&s_go) && s_recording_stopped_us) timing("stop_to_reply", s_recording_stopped_us);
    }
done:
    if (passport_tls_cancelled() && ws && !probe && !talk && atomic_load(&s_authenticated) &&
        s_reply.acknowledged && !s_reply.complete && passport_tls_cancel_can_write()) {
        if (abort_run(ws,s_reply.run_id)) s_reply.complete=true;
    }
    bool keep=ws && !passport_tls_cancelled() && s_state==PASSPORT_SEND_DONE && passport_wifi_is_online();
    if (!keep) close_gateway(&ws,&parent);
    *connection=ws;*transport=parent;
    (void)passport_wifi_gateway_performance(false);
    passport_memory_report("network_end");
    ESP_LOGI("passport_gateway", "operation complete connection_reused=%d",keep);
    crypto_wipe(s_secret, sizeof(s_secret));
    s_identity_ready = false;
    crypto_wipe(s_token, sizeof(s_token));
    portENTER_CRITICAL(&s_guard);
    if (passport_tls_cancelled()) {
        s_state=PASSPORT_SEND_CANCELLED;
        snprintf(s_message,sizeof(s_message),"本次操作已取消");
    } else if (s_state != PASSPORT_SEND_DONE && s_state != PASSPORT_SEND_FAILED) {
        s_state = PASSPORT_SEND_FAILED;
        snprintf(s_message, sizeof(s_message), "连接中断，请重试");
    }
    portEXIT_CRITICAL(&s_guard);
}

typedef struct {
    esp_transport_handle_t ws,parent;
    int64_t retry_at,last_ping;
    unsigned failures;
} gateway_service_t;

static void service_step(gateway_service_t *service) {
    esp_transport_handle_t ws=service->ws,parent=service->parent;
    int64_t retry_at=service->retry_at,last_ping=service->last_ping;
    unsigned failures=service->failures;
    int mode=atomic_exchange(&s_command,-1);
    if (mode>=0) {
        s_background=false;
        bool talk = mode == GATEWAY_LIVE || mode == GATEWAY_LIVE_TEST;
        vTaskPrioritySet(NULL, talk ? 5 : 3);
        if (passport_tls_cancelled() || !same_route(&s_agent,&s_requested_agent) || s_talk_auth != talk) close_gateway(&ws,&parent);
        s_talk_auth = talk;
        s_agent=s_requested_agent;
        passport_tls_set_proxy(&s_agent.proxy);
        begin_connection();
        atomic_store(&s_accepted,false);
        s_aborting=false;
        atomic_store(&s_resources,true);
        if (!s_receive) s_receive=calloc(1,sizeof(*s_receive));
        if (!s_receive) {
            state(PASSPORT_SEND_FAILED,"内存不足，请重试");
        } else {
            if (!s_reply.acknowledged) { s_live[0]=0;s_live_seq=0; }
            send_operation(mode,&ws,&parent);
        }
        vTaskPrioritySet(NULL, 3);
        s_background=true;
        if (s_talk_auth) { close_gateway(&ws,&parent); s_talk_auth = false; }
        failures=0;retry_at=esp_timer_get_time()+1000000;last_ping=esp_timer_get_time();
        atomic_store(&s_busy,false);
        goto done;
    }
    s_background=true;
    passport_agent_t selected={0};
    bool configured=atomic_load(&s_enabled) && passport_wifi_is_online() &&
        passport_link_agent_snapshot(&selected) && selected.device_token[0];
    if (!configured) {
        close_gateway(&ws,&parent);
        free(s_receive);s_receive=NULL;
        atomic_store(&s_resources,false);
        (void)passport_wifi_gateway_performance(false);
        goto done;
    }
    if (!same_route(&s_agent,&selected)) {
        close_gateway(&ws,&parent);s_agent=selected;retry_at=0;failures=0;
    }
    if (passport_tls_cancelled()) close_gateway(&ws,&parent);
    if (!ws && esp_timer_get_time()>=retry_at) {
        atomic_store(&s_resources,true);
        if (!atomic_load(&s_enabled)) goto done;
        if (!s_receive) s_receive=calloc(1,sizeof(*s_receive));
        passport_tls_control_begin();passport_tls_set_proxy(&s_agent.proxy);
        if (!atomic_load(&s_enabled)) passport_tls_request_cancel(false);
        if (s_receive && identity()) {
            snprintf(s_token,sizeof(s_token),"%s",s_agent.device_token);
            ws=open_gateway(false,&parent);
        }
        crypto_wipe(s_secret,sizeof(s_secret));s_identity_ready=false;
        crypto_wipe(s_token,sizeof(s_token));
        if (!ws) {
            close_gateway(&ws,&parent);
            if (failures<5) failures++;
            retry_at=esp_timer_get_time()+(1LL<<failures)*1000000;
        } else {
            failures=0;last_ping=esp_timer_get_time();passport_memory_report("gateway_idle_connected");
        }
        if (atomic_load(&s_command)>=0 || !atomic_load(&s_enabled)) goto done;
    }
    if (ws) {
        cJSON *frame=receive_json(ws,100,false);
        bool lost=!frame && !s_rx_timed_out;
        cJSON_Delete(frame);
        int64_t now=esp_timer_get_time();
        if (!lost && now-last_ping>=20000000) {
            lost=!send_fragment(ws,WS_TRANSPORT_OPCODES_PING|WS_TRANSPORT_OPCODES_FIN,"",0);
            last_ping=now;
        }
        if (lost || now-s_last_rx_us>90000000) {
            close_gateway(&ws,&parent);retry_at=now+1000000;
        }
    }
done:
    *service=(gateway_service_t){ws,parent,retry_at,last_ping,failures};
}

static void send_worker(void *arg) {
    (void)arg;
    gateway_service_t service={0};
    for (;;) {
        service_step(&service);
        ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(50));
    }
}

static esp_err_t ensure_worker(void) {
    if (s_worker) return ESP_OK;
    if (passport_tls_control_prepare()!=ESP_OK) return ESP_ERR_NO_MEM;
    return xTaskCreate(send_worker,"passport_gateway",10240,NULL,3,&s_worker)==pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void passport_gateway_maintain(bool enabled) {
    atomic_store(&s_enabled,enabled);
    if (enabled) {
        if (ensure_worker()!=ESP_OK) return;
    } else if (atomic_load(&s_resources) && !atomic_load(&s_busy)) passport_tls_request_cancel(false);
    if (s_worker) xTaskNotifyGive(s_worker);
}

bool passport_gateway_quiescent(void) { return !atomic_load(&s_resources) && !atomic_load(&s_busy); }

static esp_err_t start_send_ex(int mode, bool preconnect) {
    bool probe=mode==1 || mode==2 || mode==GATEWAY_CHECK;
    bool resume=mode==3;
    if (atomic_load(&s_busy)) return ESP_ERR_INVALID_STATE;
    if (!passport_wifi_is_online()) { state(PASSPORT_SEND_FAILED, "未联网，请重试"); return ESP_FAIL; }
    if (!probe && !resume && mode != GATEWAY_LIVE && mode != GATEWAY_LIVE_TEST && !preconnect && !passport_clip_size()) { state(PASSPORT_SEND_FAILED, "没有录音，请重新说话"); return ESP_FAIL; }
    atomic_store(&s_preconnect, preconnect);
    s_recording_stopped_us = preconnect ? 0 : esp_timer_get_time();
    atomic_store(&s_go, !preconnect);
    if (!passport_link_agent_snapshot(&s_requested_agent)) { state(PASSPORT_SEND_FAILED, "请先配置助手"); return ESP_FAIL; }
    s_probe = probe;
    if (!resume) s_resume_requested[0]=0;
    state(PASSPORT_SEND_CONNECTING, "正在连接助手");
    (void)passport_wifi_gateway_performance(true);
    /* Release the old workers before allocating the network stack itself.
     * A pre-connect starts while the microphone is recording, so the audio
     * driver stays; the passport already released Bluetooth for capture. */
    if ((!preconnect && passport_audio_release() != ESP_OK) || passport_link_pause_bluetooth() != ESP_OK) {
        (void)passport_wifi_gateway_performance(false);
        state(PASSPORT_SEND_FAILED, "设备忙，请重试"); return ESP_FAIL;
    }
    passport_memory_report("network_start");
    if (ensure_worker()!=ESP_OK) {
        (void)passport_wifi_gateway_performance(false);
        state(PASSPORT_SEND_FAILED, "内存不足，请重试"); return ESP_ERR_NO_MEM;
    }
    atomic_store(&s_enabled,true);
    atomic_store(&s_cancel_operation,false);
    atomic_store(&s_busy,true);
    atomic_store(&s_operation,mode);
    atomic_store(&s_command,mode);
    xTaskNotifyGive(s_worker);
    return ESP_OK;
}

bool passport_gateway_has_pending_reply(void) { return s_reply.acknowledged && !s_reply.complete; }
static esp_err_t start_send(int mode) { return start_send_ex(mode, false); }

esp_err_t passport_gateway_send(passport_store_t *store) {
    (void)store;
    /* A pre-connected worker is already authenticated: release it to upload. */
    if (atomic_load(&s_busy) && atomic_load(&s_preconnect) && !atomic_load(&s_go)) {
        s_recording_stopped_us = esp_timer_get_time();
        if (passport_audio_release() != ESP_OK) return ESP_FAIL;
        atomic_store(&s_go, true);
        return ESP_OK;
    }
    if (!passport_clip_size() && passport_gateway_has_pending_reply()) { s_resume_requested[0]=0;return start_send(3); }
    return start_send(0);
}

/* Drop a pre-connect that never got its recording (cancel or re-record),
 * waiting briefly so the next capture finds the worker slot free. */
void passport_gateway_drop_preconnect(void) {
    if (!atomic_load(&s_busy) || !atomic_load(&s_preconnect) || atomic_load(&s_go)) return;
    atomic_store(&s_cancel_operation,true);
    passport_tls_request_cancel(false);
    for (int i = 0; i < 60; i++) {
        bool finished = !atomic_load(&s_busy);
        if (finished) break;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    char ignored[1];(void)passport_gateway_poll(ignored, sizeof(ignored));
    if (!atomic_load(&s_busy)) {
        portENTER_CRITICAL(&s_guard);
        s_state = PASSPORT_SEND_IDLE; s_message[0] = 0;
        portEXIT_CRITICAL(&s_guard);
    }
}

esp_err_t passport_gateway_preconnect(void) {
    if (passport_gateway_has_pending_reply()) return ESP_ERR_INVALID_STATE;
    return start_send_ex(0, true);
}
esp_err_t passport_gateway_live(bool chinese) {
    if (atomic_load(&s_busy)) return ESP_ERR_INVALID_STATE;
    passport_audio_live_mute(false);
    s_live_zh = chinese;
    return start_send(GATEWAY_LIVE);
}
esp_err_t passport_gateway_live_test(void) {
    if (atomic_load(&s_busy)) return ESP_ERR_INVALID_STATE;
    passport_audio_live_mute(false);
    return start_send(GATEWAY_LIVE_TEST);
}
esp_err_t passport_gateway_resume_reply(const char *run_id) {
    if (atomic_load(&s_busy) || !run_id || !*run_id || strlen(run_id)>=sizeof(s_resume_requested)) return ESP_ERR_INVALID_ARG;
    for (const unsigned char *p=(const unsigned char *)run_id;*p;p++)
        if (!((*p>='a' && *p<='z') || (*p>='A' && *p<='Z') || (*p>='0' && *p<='9') || *p=='-' || *p=='_')) return ESP_ERR_INVALID_ARG;
    snprintf(s_resume_requested,sizeof(s_resume_requested),"%s",run_id);return start_send(3);
}
esp_err_t passport_gateway_probe(void) { return start_send(1); }
esp_err_t passport_gateway_check(void) {
    if (passport_gateway_active()) return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&s_guard);
    memset(&s_check, 0, sizeof(s_check));
    portEXIT_CRITICAL(&s_guard);
    return start_send(GATEWAY_CHECK);
}
void passport_gateway_check_result(passport_gateway_check_t *result) {
    portENTER_CRITICAL(&s_guard);
    *result = s_check;
    portEXIT_CRITICAL(&s_guard);
}
esp_err_t passport_gateway_history_probe(void) { return start_send(2); }


bool passport_gateway_active(void) {
    return atomic_load(&s_busy);
}
passport_send_state_t passport_gateway_poll(char *message, size_t cap) {
    portENTER_CRITICAL(&s_guard);
    passport_send_state_t current = s_state;
    if (atomic_load(&s_busy) && (current == PASSPORT_SEND_FAILED || current == PASSPORT_SEND_DONE || current == PASSPORT_SEND_CANCELLED)) {
        current = PASSPORT_SEND_CONNECTING;
    }
    snprintf(message, cap, "%s", s_message);
    portEXIT_CRITICAL(&s_guard);
    return current;
}

void passport_gateway_transcript(char *text, size_t cap) {
    if (!text || !cap) return;
    text[0] = 0;
    portENTER_CRITICAL(&s_guard);
    passport_reply_live_apply(text,cap,s_transcript,true);
    portEXIT_CRITICAL(&s_guard);
}

bool passport_gateway_cancel(void) {
    bool active=atomic_load(&s_busy);
    if (!active) return false;
    atomic_store(&s_cancel_operation,true);
    int mode = atomic_load(&s_operation);
    passport_tls_request_cancel(!s_probe && atomic_load(&s_authenticated) &&
        (atomic_load(&s_accepted) || mode == GATEWAY_LIVE));
    return true;
}

bool passport_gateway_abandon(void) {
    bool busy=atomic_load(&s_busy);
    if (busy) return false;
    char ignored[1];(void)passport_gateway_poll(ignored,sizeof(ignored));
    if (s_reply.acknowledged && !s_reply.complete) {
        memcpy(s_deferred_abort.run_id,s_reply.run_id,sizeof(s_reply.run_id));
        memcpy(s_deferred_abort.scope,s_reply.scope,sizeof(s_reply.scope));s_deferred_abort.valid=true;
    }
    memset(&s_reply,0,sizeof(s_reply));
    clear_transcript();
    return true;
}
