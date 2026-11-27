#include "passport_muse.h"

extern "C" {
#include "passport_clip.h"
#include "passport_clock.h"
#include "passport_tls.h"
#include "passport_reply.h"
}
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_transport_ssl.h"
#include "mbedtls/base64.h"
#include "mbedtls/platform_util.h"
#include "passport_transport_ws.h"
#include <xplat/noise/core/ClientSession.h>
#include <xplat/noise/core/MbedtlsCryptoBackend.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>

namespace {
using namespace musegadgets::noise::core;

constexpr char api[] = "https://api.muse.ai";
constexpr char host[] = "hatch.metaaivm.com";
constexpr char registration_id[] = "passport-register";
constexpr size_t frame_capacity = 12 * 1024;
constexpr size_t upload_chunk = 768;  // A multiple of 3 keeps intermediate base64 chunks unpadded.
constexpr int control_stream = 1;
constexpr int note_stream = 16;
constexpr int reply_stream = 17;
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;

struct SecretFree {
    size_t length;
    void operator()(void *data) const {
        if (data) { mbedtls_platform_zeroize(data, length); free(data); }
    }
};

const cJSON *member(const cJSON *obj, const char *key) {
    return cJSON_GetObjectItemCaseSensitive(obj, key);
}

const char *str(const cJSON *obj, const char *key) {
    const char *value = cJSON_GetStringValue(member(obj, key));
    return value ? value : "";
}

bool bearer_value(const char *value, size_t capacity) {
    size_t size = strlen(value);
    if (!size || size >= capacity) return false;
    for (; *value; ++value) {
        if (static_cast<unsigned char>(*value) <= 32 || static_cast<unsigned char>(*value) >= 127) return false;
    }
    return true;
}

struct HttpBuffer {
    char *data;
    size_t used = 0;
    bool overflow = false;
};

esp_err_t http_event(esp_http_client_event_t *event) {
    if (passport_tls_cancelled()) return ESP_FAIL;
    auto &buffer = *static_cast<HttpBuffer *>(event->user_data);
    if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) return ESP_OK;
    size_t length = event->data_len;
    if (length > frame_capacity - buffer.used) {
        buffer.overflow = true;
        return ESP_FAIL;
    }
    memcpy(buffer.data + buffer.used, event->data, length);
    buffer.used += length;
    return ESP_OK;
}

int api_request(const char *path, const char *authorization, const char *body, HttpBuffer &buffer) {
    char url[80];
    snprintf(url, sizeof(url), "%s%s", api, path);
    buffer.used = 0;
    buffer.overflow = false;
    esp_http_client_config_t config{};
    config.url = url;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.disable_auto_redirect = true;
    config.timeout_ms = 8000;
    config.buffer_size = 1024;
    config.event_handler = http_event;
    config.user_data = &buffer;
    auto client = esp_http_client_init(&config);
    if (!client) return 0;
    esp_http_client_set_header(client, "Authorization", authorization);
    esp_http_client_set_header(client, "X-API-Version", "1.0.0");
    esp_http_client_set_header(client, "User-Agent", "AI-Passport-Muse/1");
    if (body) {
        esp_http_client_set_method(client, HTTP_METHOD_POST);
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, body, strlen(body));
    }
    esp_err_t result = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    passport_tls_forget_socket();
    return !buffer.overflow && (result == ESP_OK || status == 401 || status == 403) ? status : 0;
}

struct Vm {
    char id[128] = {};
    char authorization[2056] = {};
};

int fetch_vm(const char *access, HttpBuffer &buffer, Vm &vm) {
    char authorization[1032];
    if (!bearer_value(access, 1025)) return 0;
    snprintf(authorization, sizeof(authorization), "Bearer %s", access);
    int status = api_request("/fetch_vms", authorization, nullptr, buffer);
    mbedtls_platform_zeroize(authorization, sizeof(authorization));
    if (status != 200) return status;
    Json json(cJSON_ParseWithLength(buffer.data, buffer.used), cJSON_Delete);
    const cJSON *item;
    cJSON_ArrayForEach(item, member(json.get(), "vm_list")) {
        const char *id = str(item, "vm_id");
        if (!*id) id = str(item, "vm_name");
        const char *token = str(item, "vm_auth_token");
        if (!*id || strlen(id) >= sizeof(vm.id) || !bearer_value(token, sizeof(vm.authorization) - 7)) continue;
        bool preferred = cJSON_IsTrue(member(item, "default"));
        if (!vm.id[0] || preferred) {
            snprintf(vm.id, sizeof(vm.id), "%s", id);
            snprintf(vm.authorization, sizeof(vm.authorization), "Bearer %s", token);
        }
        if (preferred) break;
    }
    return vm.id[0] ? 200 : 0;
}

int refresh(passport_muse_credentials_t &credentials, const char *node, HttpBuffer &buffer) {
    const char *raw = strrchr(credentials.refresh, ':');
    raw = raw ? raw + 1 : credentials.refresh;
    if (!bearer_value(raw, sizeof(credentials.refresh))) return 0;
    char authorization[2072];
    snprintf(authorization, sizeof(authorization), "Bearer hatch_refresh:%s", raw);
    char sdk[49];
    if (!passport_muse_token_load(sdk)) {
        mbedtls_platform_zeroize(authorization, sizeof(authorization));
        return 0;
    }
    char body[104];
    snprintf(body, sizeof(body), "{\"device_id\":\"%s\",\"sdk_token\":\"%s\"}", node, sdk);
    mbedtls_platform_zeroize(sdk, sizeof(sdk));
    int status = api_request("/device_token/refresh", authorization, body, buffer);
    mbedtls_platform_zeroize(authorization, sizeof(authorization));
    mbedtls_platform_zeroize(body, sizeof(body));
    if (status != 200) return status;
    Json json(cJSON_ParseWithLength(buffer.data, buffer.used), cJSON_Delete);
    const cJSON *tokens = member(json.get(), "payload");
    if (!cJSON_IsObject(tokens)) tokens = json.get();
    const char *access = str(tokens, "access_token");
    const char *new_refresh = str(tokens, "refresh_token");
    if (!bearer_value(access, sizeof(credentials.access)) ||
        !bearer_value(new_refresh, sizeof(credentials.refresh))) return 0;
    mbedtls_platform_zeroize(credentials.access, sizeof(credentials.access));
    mbedtls_platform_zeroize(credentials.refresh, sizeof(credentials.refresh));
    strcpy(credentials.access, access);
    strcpy(credentials.refresh, new_refresh);
    return passport_muse_credentials_save(&credentials) ? 200 : 0;
}

bool path_for_vm(const char *id, char (&path)[408]) {
    const char prefix[] = "/v1/noise?vm_id=";
    memcpy(path, prefix, sizeof(prefix) - 1);
    size_t offset = sizeof(prefix) - 1;
    for (auto p = reinterpret_cast<const unsigned char *>(id); *p; ++p) {
        bool plain = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                     (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' || *p == '.' || *p == '~';
        if (offset + (plain ? 1 : 3) >= sizeof(path)) return false;
        if (plain) path[offset++] = *p;
        else { snprintf(path + offset, 4, "%%%02X", *p); offset += 3; }
    }
    path[offset] = 0;
    return true;
}

class Session {
public:
    explicit Session() : noise_(crypto_) {}
    ~Session() {
        passport_tls_forget_socket();
        if (ws_) { esp_transport_close(ws_); esp_transport_destroy(ws_); }
        if (parent_) esp_transport_destroy(parent_);
        mbedtls_platform_zeroize(bytes_.data(), bytes_.size());
        mbedtls_platform_zeroize(decoded_.data(), decoded_.size());
        mbedtls_platform_zeroize(assembled_.data(), assembled_.size());
    }

    bool connect(const Vm &vm) {
        char path[408];
        if (!path_for_vm(vm.id, path)) return false;
        parent_ = esp_transport_ssl_init();
        if (!parent_) return false;
        esp_transport_ssl_crt_bundle_attach(parent_, esp_crt_bundle_attach);
        ws_ = passport_transport_ws_init(parent_);
        if (!ws_) return false;
        passport_transport_ws_config_t config{};
        config.ws_path = path;
        config.user_agent = "AI-Passport-Muse/1";
        config.auth = vm.authorization;
        if (passport_transport_ws_set_config(ws_, &config) != ESP_OK) return false;
        int result = esp_transport_connect(ws_, host, 443, 10000);
        (void)passport_transport_ws_set_auth(ws_, nullptr);
        if (result < 0) return false;
        passport_tls_track_socket(esp_transport_get_socket(ws_));

        auto first = noise_.WriteHandshakeMessage1(bytes_);
        if (!first.ok() || !send(first.size())) return false;
        int length = receive(8000);
        uint8_t peer[256];
        size_t used = 0;
        if (length <= 0 || !noise_.ReadHandshakeMessage2({bytes_.data(), static_cast<size_t>(length)}, peer, used).ok())
            return false;
        auto third = noise_.WriteHandshakeMessage3({}, bytes_);
        return third.ok() && send(third.size()) && noise_.isEstablished();
    }

    bool request(ServiceType service, int id, const char *path, ConstByteSpan body, bool end) {
        bool control = id == control_stream;
        const HeaderView headers[] = {
            {"x-app-id", "hatch-web"}, {"content-type", "application/json"},
            {"accept", id == reply_stream ? "application/x-ndjson" : "application/json"},
        };
        ApplicationRequestView request{"POST", path, control ? Span<const HeaderView>{} : Span<const HeaderView>{headers}, body, end};
        return noise_.StartOutboundApplicationRequest(service, id, request, scratch_, envelope_).ok() && flush();
    }

    bool body(ServiceType service, int id, ConstByteSpan data, bool end = false) {
        return noise_.StartOutboundBodyChunk(service, id, {data, end}, scratch_, envelope_).ok() && flush();
    }

    int next(ServiceFrameView &frame, unsigned wait_ms) {
        int length = receive(wait_ms);
        if (length <= 0) return length;
        auto result = noise_.ProcessInboundWebSocketPayload({bytes_.data(), static_cast<size_t>(length)},
                                                             decoded_, assembled_, headers_);
        if (!result.ok()) return -1;
        if (result.frame_status != InboundFrameStatus::Complete) return 0;
        frame = result.frame;
        return 1;
    }

private:
    bool send(size_t length) {
        if (passport_tls_cancelled()) return false;
        return passport_transport_ws_send_raw(ws_,
            static_cast<ws_transport_opcodes_t>(WS_TRANSPORT_OPCODES_FIN | WS_TRANSPORT_OPCODES_BINARY),
            reinterpret_cast<const char *>(bytes_.data()), length, 5000) == static_cast<int>(length);
    }

    bool flush() {
        while (noise_.HasOutboundWebSocketPayload()) {
            auto payload = noise_.WriteNextOutboundWebSocketPayload(bytes_);
            if (!payload.ok() || !send(payload.size())) return false;
        }
        return true;
    }

    int receive(unsigned wait_ms) {
        int64_t deadline = esp_timer_get_time() + static_cast<int64_t>(wait_ms) * 1000;
        do {
            if (passport_tls_cancelled() || received_ == bytes_.size()) return -1;
            int64_t remaining = (deadline - esp_timer_get_time()) / 1000;
            int timeout = static_cast<int>(std::clamp<int64_t>(remaining, 0, 100));
            if (!frame_left_ && !received_) {
                int ready = esp_transport_poll_read(ws_, timeout);
                if (ready < 0) return -1;
                if (!ready) continue;
            }
            int n = esp_transport_read(ws_, reinterpret_cast<char *>(bytes_.data() + received_),
                                       bytes_.size() - received_, timeout);
            if (n < 0) return -1;
            if (!n) continue;
            int opcode = passport_transport_ws_get_read_opcode(ws_) & 0x0f;
            if (!frame_left_) {
                if (opcode != (received_ ? WS_TRANSPORT_OPCODES_CONT : WS_TRANSPORT_OPCODES_BINARY)) return -1;
                int length = passport_transport_ws_get_read_payload_len(ws_);
                if (length < n || static_cast<size_t>(length) > bytes_.size() - received_) return -1;
                frame_left_ = length;
            }
            if (static_cast<size_t>(n) > frame_left_) return -1;
            received_ += n;
            frame_left_ -= n;
            if (!frame_left_ && passport_transport_ws_get_fin_flag(ws_)) {
                int count = received_;
                received_ = 0;
                return count;
            }
        } while (esp_timer_get_time() < deadline);
        return 0;
    }

    esp_transport_handle_t parent_ = nullptr, ws_ = nullptr;
    MbedtlsCryptoBackend crypto_;
    ClientSession noise_;
    std::array<uint8_t, frame_capacity + 1024> bytes_{};
    std::array<uint8_t, frame_capacity> decoded_{}, assembled_{};
    std::array<uint8_t, 1536> scratch_{}, envelope_{};
    HeaderView headers_[32]{};
    size_t received_ = 0, frame_left_ = 0;
};

ConstByteSpan span(const char *text) {
    return {reinterpret_cast<const uint8_t *>(text), strlen(text)};
}

bool frame_body(const ServiceFrameView &frame, int &status, ConstByteSpan &body, bool &end) {
    if (frame.kind == ServiceFrameKind::Response) {
        status = frame.response.status;
        body = frame.response.body;
        end = frame.response.end_body;
    } else if (frame.kind == ServiceFrameKind::BodyChunk) {
        body = frame.body_chunk.data;
        end = frame.body_chunk.end_body;
    } else if (frame.kind == ServiceFrameKind::Reset) return false;
    return true;
}

bool register_device(Session &session, const char *node) {
    char message[448];
    int length = snprintf(message + 4, sizeof(message) - 4,
        "{\"type\":\"req\",\"id\":\"%s\",\"method\":\"link.register\",\"params\":{"
        "\"node_id\":\"%s\",\"display_name\":\"AI Passport\",\"platform\":\"esp32\","
        "\"version\":\"1.0.0\",\"device_family\":\"gadget\",\"model_id\":\"muse-gadget-ai-passport\","
        "\"metadata\":{\"model_name\":\"FoloToy AI Passport\"},"
        "\"is_wakeup_supported\":false,\"commands_v2\":{}}}", registration_id, node);
    if (length < 0 || length >= static_cast<int>(sizeof(message) - 4)) return false;
    for (unsigned i = 0; i < 4; ++i) message[i] = static_cast<uint32_t>(length) >> (i * 8);
    if (!session.request(ServiceType::Gadget, control_stream, "/api/gadget/control", {}, false) ||
        !session.body(ServiceType::Gadget, control_stream,
                      {reinterpret_cast<const uint8_t *>(message), static_cast<size_t>(length + 4)})) return false;

    uint8_t prefix[4];
    size_t used = 0, want = 0;
    int status = 0;
    int64_t deadline = esp_timer_get_time() + 10000000;
    while (esp_timer_get_time() < deadline) {
        ServiceFrameView frame;
        int result = session.next(frame, 100);
        if (result < 0) return false;
        if (result == 0 || frame.stream_id != control_stream) continue;
        ConstByteSpan data;
        bool end = false;
        if (!frame_body(frame, status, data, end) || end || status >= 300) return false;
        for (size_t i = 0; i < data.size(); ++i) {
            uint8_t byte = data[i];
            if (used < sizeof(prefix)) {
                prefix[used++] = byte;
                if (used != sizeof(prefix)) continue;
                for (unsigned i = 0; i < 4; ++i) want |= static_cast<uint32_t>(prefix[i]) << (i * 8);
                if (!want || want >= sizeof(message)) return false;
            } else {
                message[used++ - sizeof(prefix)] = byte;
                if (used != want + sizeof(prefix)) continue;
                Json json(cJSON_ParseWithLength(message, want), cJSON_Delete);
                if (!strcmp(str(json.get(), "id"), registration_id)) {
                    const cJSON *error = member(json.get(), "error");
                    return (!error || cJSON_IsNull(error) || cJSON_IsFalse(error)) &&
                           !cJSON_IsFalse(member(json.get(), "ok"));
                }
                used = want = 0;
            }
        }
    }
    return false;
}

void put_le(uint8_t *out, uint32_t value, unsigned count) {
    for (unsigned i = 0; i < count; ++i) out[i] = value >> (i * 8);
}

bool send_recording(Session &session, bool &submission_started) {
    constexpr char head[] =
        "{\"message\":\"\",\"output_modality\":\"text\",\"device\":\"mcu-wearable\",\"items\":["
        "{\"type\":\"file\",\"mime_type\":\"audio/wav\",\"filename\":\"voice_note.wav\",\"data_base64\":\"";
    if (!session.request(ServiceType::Daemon, note_stream, "/chat/stream", {}, false) ||
        !session.body(ServiceType::Daemon, note_stream, span(head))) return false;

    const uint32_t pcm = passport_clip_size();
    uint8_t chunk[upload_chunk] = {};
    unsigned char encoded[upload_chunk / 3 * 4 + 1];
    memcpy(chunk, "RIFF", 4);
    put_le(chunk + 4, pcm + 36, 4);
    memcpy(chunk + 8, "WAVEfmt ", 8);
    put_le(chunk + 16, 16, 4);
    put_le(chunk + 20, 1, 2);
    put_le(chunk + 22, 1, 2);
    put_le(chunk + 24, 16000, 4);
    put_le(chunk + 28, 32000, 4);
    put_le(chunk + 32, 2, 2);
    put_le(chunk + 34, 16, 2);
    memcpy(chunk + 36, "data", 4);
    put_le(chunk + 40, pcm, 4);

    size_t header = 44;
    for (size_t offset = 0; offset < pcm;) {
        size_t size = std::min<size_t>(sizeof(chunk) - header, pcm - offset);
        size_t written = 0;
        if (passport_clip_read_pcm(offset, chunk + header, size) != ESP_OK ||
            mbedtls_base64_encode(encoded, sizeof(encoded), &written, chunk, size + header) ||
            !session.body(ServiceType::Daemon, note_stream, {encoded, written})) return false;
        offset += size;
        header = 0;
    }
    if (!session.request(ServiceType::Daemon, reply_stream, "/chat/subscribe", span("{}"), true)) return false;
    if (passport_tls_cancelled()) return false;
    submission_started = true;
    return session.body(ServiceType::Daemon, note_stream, span("\"}]}"), true);
}

class Replies {
public:
    using Show = void (*)(passport_send_state_t, const char *);
    using Transcript = void (*)(const char *);
    explicit Replies(Show show, Transcript transcript) : show_(show), transcript_(transcript) {}

    bool read(Session &session) {
        int64_t deadline = esp_timer_get_time() + 90000000;
        while (esp_timer_get_time() < deadline) {
            ServiceFrameView frame;
            int result = session.next(frame, 100);
            if (result < 0) return answer_[0];
            if (result == 0) continue;
            if (frame.stream_id == note_stream) {
                if (!acknowledge(frame)) return false;
            } else if (frame.stream_id == reply_stream) {
                if (!consume(frame)) return answer_[0];
            } else if (frame.stream_id == control_stream && frame.kind == ServiceFrameKind::Reset) {
                return answer_[0];
            }
            if (answer_[0]) return true;
        }
        return false;
    }

    const char *answer() const { return answer_; }

private:
    struct Message {
        char id[80] = {};
        char parent[80] = {};
        char text[PASSPORT_TEXT_MAX] = {};
        bool final = false;
        bool transcript = false;
    };

    bool belongs(const Message &message) const {
        return note_[0] && message.parent[0] &&
            (!strcmp(message.parent, note_) || (parent_[0] && !strcmp(message.parent, parent_)));
    }

    void publish(const Message &message) {
        if (!message.text[0]) return;
        if (message.transcript) {
            if (strcmp(message.id, note_) && (!parent_[0] || strcmp(message.id, parent_))) return;
            char text[PASSPORT_TEXT_MAX];
            snprintf(text, sizeof(text), "%s", message.text);
            if (char *attachment = strstr(text, "\n[file:")) *attachment = 0;
            if (strcmp(text, "[Voice note]")) transcript_(text);
        } else if (belongs(message)) {
            if (message.final) passport_reply_live_apply(answer_, sizeof(answer_), message.text, true);
            else show_(PASSPORT_SEND_PARTIAL, message.text);
        }
    }

    bool acknowledge(const ServiceFrameView &frame) {
        ConstByteSpan data;
        bool end = false;
        if (!frame_body(frame, note_status_, data, end) || note_status_ >= 300 ||
            data.size() > sizeof(ack_) - ack_length_) return false;
        if (data.size()) memcpy(ack_ + ack_length_, data.data(), data.size());
        ack_length_ += data.size();
        if (!end) return true;
        Json json(cJSON_ParseWithLength(ack_, ack_length_), cJSON_Delete);
        const cJSON *root = member(json.get(), "result");
        if (!cJSON_IsObject(root)) root = json.get();
        const char *id = str(root, "message_id");
        const char *parent = str(root, "reply_to_message_id");
        if (!*id || strlen(id) >= sizeof(note_) || strlen(parent) >= sizeof(parent_)) return false;
        strcpy(note_, id);
        strcpy(parent_, parent);
        for (const auto &entry : messages_) publish(entry);
        return true;
    }

    bool consume(const ServiceFrameView &frame) {
        ConstByteSpan data;
        bool end = false;
        if (!frame_body(frame, reply_status_, data, end) || reply_status_ >= 300) return false;
        for (size_t i = 0; i < data.size(); ++i) {
            uint8_t byte = data[i];
            if (byte == '\n') {
                if (!overflow_ && used_) event();
                used_ = 0;
                overflow_ = false;
            } else if (used_ < sizeof(line_)) {
                line_[used_++] = byte;
            } else overflow_ = true;
        }
        if (end && !overflow_ && used_) event();
        if (end) {
            used_ = 0;
            overflow_ = false;
        }
        return !end || !note_[0];
    }

    void event() {
        Json json(cJSON_ParseWithLength(line_, used_), cJSON_Delete);
        if (strcmp(str(json.get(), "type"), "event")) return;
        const char *event = str(json.get(), "event_name");
        if (!*event) event = str(json.get(), "event");
        bool start = !strcmp(event, "delta.message_start");
        bool delta = !strcmp(event, "delta.text_append");
        bool done = !strcmp(event, "delta.message_done");
        bool full = !strcmp(event, "message.assistant");
        bool user = !strcmp(event, "message.user");
        if (!start && !delta && !done && !full && !user) return;
        const cJSON *payload = member(json.get(), "payload");
        const char *id = str(payload, "message_id");
        if (!*id) id = str(payload, "id");
        if (!*id) id = str(json.get(), "message_id");
        const char *parent = str(payload, "reply_to_message_id");
        if (!*parent) parent = str(payload, "parent_message_id");
        if (!*parent) parent = str(json.get(), "reply_to_message_id");
        if (!*id || strlen(id) >= sizeof(Message::id) || strlen(parent) >= sizeof(Message::parent)) return;
        const cJSON *seq = member(json.get(), "seq");
        if (cJSON_IsNumber(seq) && seq->valuedouble > 0) {
            if (seq->valuedouble <= last_sequence_) return;
            last_sequence_ = seq->valuedouble;
        }
        if (user) strcpy(last_user_, id);
        auto found = std::find_if(messages_.begin(), messages_.end(), [&](const Message &entry) { return !strcmp(entry.id, id); });
        if (found == messages_.end()) {
            if (!user && !*parent) parent = last_user_;
            if (!user && !*parent) return;
            found = messages_.begin() + (next_++ % messages_.size());
            *found = {};
            strcpy(found->id, id);
            found->transcript = user;
        }
        if (*parent) strcpy(found->parent, parent);
        if (full && cJSON_IsFalse(member(payload, "display_text_ready"))) return;
        const char *text = str(payload, "display_text");
        if (!*text) text = str(payload, "content");
        if (!*text) text = str(payload, "text");
        if (*text) passport_reply_live_apply(found->text, sizeof(found->text), text, !delta);
        if (done || full) found->final = true;
        publish(*found);
    }

    Show show_;
    Transcript transcript_;
    std::array<Message, 4> messages_{};
    char line_[frame_capacity], ack_[2048];
    char note_[80]{}, parent_[80]{}, last_user_[80]{}, answer_[PASSPORT_TEXT_MAX]{};
    size_t next_ = 0, used_ = 0, ack_length_ = 0;
    double last_sequence_ = 0;
    int note_status_ = 0, reply_status_ = 0;
    bool overflow_ = false;
};
}  // namespace

void passport_muse_run(bool probe, void (*state)(passport_send_state_t, const char *),
                       void (*transcript)(const char *)) {
    char node[16];
    passport_muse_node_id(node);
    Vm vm;
    int status;
    {
        std::unique_ptr<passport_muse_credentials_t, SecretFree> credentials(
            static_cast<passport_muse_credentials_t *>(malloc(sizeof(passport_muse_credentials_t))),
            {sizeof(passport_muse_credentials_t)});
        if (!credentials || !passport_muse_credentials_load(credentials.get())) {
            state(PASSPORT_SEND_FAILED, "请先配对 Muse");
            return;
        }
        if (!passport_clock_wait()) {
            state(PASSPORT_SEND_FAILED, "时间同步失败，请重试");
            return;
        }
        std::unique_ptr<char, SecretFree> body(static_cast<char *>(malloc(frame_capacity)), {frame_capacity});
        if (!body) { state(PASSPORT_SEND_FAILED, "内存不足，请重试"); return; }
        HttpBuffer buffer{body.get()};
        status = fetch_vm(credentials->access, buffer, vm);
        if (status == 401 || status == 403) {
            status = refresh(*credentials, node, buffer);
            if (status == 200) status = fetch_vm(credentials->access, buffer, vm);
        }
    }
    if (status != 200) {
        state(PASSPORT_SEND_FAILED, status == 401 || status == 403 ? "请重新配对 Muse" : "无法连接 Muse，请重试");
        return;
    }
    auto session = std::unique_ptr<Session>(new (std::nothrow) Session);
    if (!session) { state(PASSPORT_SEND_FAILED, "内存不足，请重试"); return; }
    bool linked = session->connect(vm);
    mbedtls_platform_zeroize(&vm, sizeof(vm));
    if (!linked || !register_device(*session, node)) {
        state(PASSPORT_SEND_FAILED, "无法连接 Muse，请重试");
        return;
    }
    if (probe) { state(PASSPORT_SEND_DONE, "Muse 已连接"); return; }

    auto replies = std::unique_ptr<Replies>(new (std::nothrow) Replies(state, transcript));
    if (!replies) { state(PASSPORT_SEND_FAILED, "内存不足，请重试"); return; }
    state(PASSPORT_SEND_UPLOAD, "正在发送");
    bool submission_started = false;
    if (!send_recording(*session, submission_started)) {
        if (submission_started) passport_clip_discard();
        state(PASSPORT_SEND_FAILED, submission_started
            ? "请在 Muse App 查看录音" : "语音发送中断，请重试");
        return;
    }
    state(PASSPORT_SEND_WAITING, "正在等待回复");
    if (!replies->read(*session)) {
        passport_clip_discard();
        state(PASSPORT_SEND_FAILED, "未收到 Muse 回复，请在 App 查看");
        return;
    }
    state(PASSPORT_SEND_DONE, replies->answer());
}
