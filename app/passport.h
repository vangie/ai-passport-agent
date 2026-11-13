#ifndef PASSPORT_H
#define PASSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "passport_proxy.h"

#define PASSPORT_LCD_W 240
#define PASSPORT_LCD_H 320
#define PASSPORT_NET_MAX 8
#define PASSPORT_AGENT_MAX 4
#define PASSPORT_KIND_MAX 16
#define PASSPORT_SSID_MAX 33
#define PASSPORT_PASS_MAX 65
#define PASSPORT_NAME_MAX 24
#define PASSPORT_URL_MAX 160
#define PASSPORT_TOKEN_MAX 256
#define PASSPORT_SECRET_MAX 96
#define PASSPORT_TEXT_MAX 480
#define PASSPORT_CHAT_TURNS 3
/* Shared geometry for the reply reader and its font-aware paginator. */
#define PASSPORT_REPLY_BODY_WIDTH 184
#define PASSPORT_CHAT_BODY_WIDTH (PASSPORT_REPLY_BODY_WIDTH - 12)
#define PASSPORT_REPLY_ROWS 9
#define PASSPORT_REPLY_LINE_SPACE 4
#define PASSPORT_PAGE_TEXT_MAX 1024
#define PASSPORT_AP_SSID "Passport-Setup"
#define PASSPORT_AP_PASS "passport-setup"
#define PASSPORT_PORTAL_URL "http://192.168.4.1"
#define PASSPORT_CONFIG_LINE_MAX 5120
#define PASSPORT_WAVEFORM_BARS 12
#define PASSPORT_VOLUME_DEFAULT 55

typedef enum {
    PASSPORT_BTN_UP = 0,
    PASSPORT_BTN_DOWN = 1,
    PASSPORT_BTN_OK = 2,
} passport_btn_t;

typedef enum {
    PASSPORT_CLICK = 0,
    PASSPORT_HOLD = 1,
    PASSPORT_RELEASE = 2,
    PASSPORT_PRESS = 3,
} passport_press_t;

typedef enum {
    PASSPORT_SCREEN_HOME = 0,
    PASSPORT_SCREEN_NETS = 1,
    PASSPORT_SCREEN_NET_MENU = 2,
    PASSPORT_SCREEN_ADD = 3,
    PASSPORT_SCREEN_TALK = 4,
    PASSPORT_SCREEN_SETTINGS = 5,
    PASSPORT_SCREEN_CAPTURE = 6,
    PASSPORT_SCREEN_PROGRESS = 7,
    PASSPORT_SCREEN_FAILURE = 8,
    PASSPORT_SCREEN_CANCELLING = 9,
    PASSPORT_SCREEN_PREFERENCES = 10,
    PASSPORT_SCREEN_LIVE = 12,
    PASSPORT_SCREEN_VOLUME = 13,
} passport_screen_t;

typedef enum {
    PASSPORT_ACT_NONE, PASSPORT_ACT_NETWORKS, PASSPORT_ACT_AGENTS, PASSPORT_ACT_RECORD,
    PASSPORT_ACT_SEND, PASSPORT_ACT_RETRY, PASSPORT_ACT_RERECORD, PASSPORT_ACT_CANCEL_RECORD,
    PASSPORT_ACT_NEXT_NETWORK, PASSPORT_ACT_OPEN_NETWORK, PASSPORT_ACT_ADD_NETWORK,
    PASSPORT_ACT_CONNECT_NETWORK, PASSPORT_ACT_NEXT_AGENT, PASSPORT_ACT_SELECT_AGENT,
    PASSPORT_ACT_CONFIGURE, PASSPORT_ACT_CLOSE_CONFIG, PASSPORT_ACT_BACK,
    PASSPORT_ACT_PREV_PAGE, PASSPORT_ACT_NEXT_PAGE, PASSPORT_ACT_PROGRESS, PASSPORT_ACT_READ_REPLY, PASSPORT_ACT_ENABLE_CONFIG,
    PASSPORT_ACT_DISCARD, PASSPORT_ACT_CANCEL_SEND, PASSPORT_ACT_RESTART_SEND,
    PASSPORT_ACT_CONTINUE,
    PASSPORT_ACT_PREFERENCES, PASSPORT_ACT_RESERVED_11, PASSPORT_ACT_RESERVED_12,
    PASSPORT_ACT_LIVE, PASSPORT_ACT_END_LIVE, PASSPORT_ACT_LIVE_MUTE, PASSPORT_ACT_LIVE_UNMUTE, PASSPORT_ACT_LIVE_RECONNECT,
    PASSPORT_ACT_NEXT_PREFERENCE, PASSPORT_ACT_VOLUME, PASSPORT_ACT_VOLUME_UP, PASSPORT_ACT_VOLUME_DOWN,
    PASSPORT_ACT_OPEN_PREFERENCE,
    PASSPORT_ACT_LIVE_INTERRUPT,
} passport_action_t;

typedef struct { passport_action_t action; bool enabled; } passport_key_binding_t;

typedef enum {
    PASSPORT_NETWORK_IDLE, PASSPORT_NETWORK_CONNECTING, PASSPORT_NETWORK_CONNECTED,
    PASSPORT_NETWORK_PASSWORD, PASSPORT_NETWORK_NOT_FOUND, PASSPORT_NETWORK_SECURITY,
    PASSPORT_NETWORK_TIMEOUT, PASSPORT_NETWORK_FAILED, PASSPORT_NETWORK_STALE,
    PASSPORT_NETWORK_STORAGE, PASSPORT_NETWORK_BUSY,
} passport_network_state_t;

typedef enum {
    PASSPORT_LINK_OFF = 0,
    PASSPORT_LINK_JOINING = 1,
    PASSPORT_LINK_ONLINE = 2,
    PASSPORT_LINK_GATEWAY = 3,
    PASSPORT_LINK_FAILED = 4,
} passport_link_t;

typedef struct {
    char name[PASSPORT_NAME_MAX];
    char ssid[PASSPORT_SSID_MAX];
    char pass[PASSPORT_PASS_MAX];
    int priority;
    bool enabled;
} passport_net_t;

typedef struct {
    char name[PASSPORT_NAME_MAX];
    char kind[PASSPORT_KIND_MAX];
    char url[PASSPORT_URL_MAX];
    /* 只写不读。bootstrap 是一次性配对令牌，用过就清掉。
     * device_token 是网关发给这台工牌的凭据。 */
    char bootstrap[PASSPORT_TOKEN_MAX];
    char device_token[PASSPORT_TOKEN_MAX];
    char access_id[PASSPORT_SECRET_MAX];
    char access_secret[PASSPORT_SECRET_MAX];
    /* Optional Shadowsocks route to this gateway; off means direct. */
    passport_proxy_t proxy;
} passport_agent_t;

typedef struct {
    passport_net_t nets[PASSPORT_NET_MAX];
    size_t count;
    int connected;
    passport_agent_t agents[PASSPORT_AGENT_MAX];
    size_t agent_count;
    int active_agent;
    uint32_t generation;
    char lang[8];
} passport_store_t;

typedef struct {
    char question[PASSPORT_TEXT_MAX];
    char answer[PASSPORT_TEXT_MAX];
} passport_chat_turn_t;

typedef struct {
    uint16_t offset;
    uint16_t length;
    bool assistant;
    bool caption;
} passport_chat_row_t;

typedef struct {
    passport_screen_t screen;
    int cursor;
    bool recording;
    bool sending;
    bool retry_send;
    bool reply_unread;
    bool cancel_pending;
    bool restart_after_cancel;
    passport_screen_t setup_parent;
    char notice[96];
    uint8_t waveform[PASSPORT_WAVEFORM_BARS];
    passport_link_t link;
    char status[96];
    char reply[PASSPORT_TEXT_MAX];
    char heard[PASSPORT_TEXT_MAX];
    passport_chat_turn_t chat[PASSPORT_CHAT_TURNS + 1];
    uint32_t chat_scope;
    uint8_t chat_count;
    bool chat_pending;
    bool chat_evicted;
    bool chat_visible;
    bool chat_zh;
    char chat_name[PASSPORT_NAME_MAX];
    int reply_page;
    int battery_soc;
    bool wifi_online;
    bool bluetooth_ready;
    bool bluetooth_connected;
    bool bluetooth_busy;
    passport_network_state_t network_state;
    int network_target;
    bool live_speaking;
    bool live_waiting;
    bool live_ready;
    bool live_muted;
    bool live_failed;
    uint8_t preference_cursor;
    uint8_t speaker_volume;
} passport_view_t;

typedef struct {
    passport_btn_t btn;
    passport_press_t press;
} passport_input_t;

void passport_store_init(passport_store_t *store);
bool passport_store_add(passport_store_t *store, const char *name, const char *ssid,
                     const char *pass);
bool passport_store_forget(passport_store_t *store, size_t index);
bool passport_store_promote(passport_store_t *store, size_t index);
int passport_store_pick(const passport_store_t *store, const bool *visible,
                     const int *rssi, size_t scan_count);

/* 同名更新。地址只接受 ws:// 或 wss://。第一套自动成为当前。
 * 密钥字段传 NULL 表示沿用原来的；传空串表示清掉。 */
bool passport_agent_upsert(passport_store_t *store, const char *name, const char *kind,
                        const char *url, const char *bootstrap, const char *device_token,
                        const char *access_id, const char *access_secret);
bool passport_agent_select(passport_store_t *store, size_t index);
bool passport_agent_rename(passport_store_t *store, size_t index, const char *name);
/* Move a paired gateway to a replacement WSS endpoint after confirming its
 * existing URL. Only the URL changes; device identity and tokens stay put. */
bool passport_agent_move_endpoint(passport_store_t *store, size_t index,
                               const char *expected_url, const char *url);
bool passport_agent_drop(passport_store_t *store, size_t index);
/* Set or clear one gateway's Shadowsocks route. NULL secret keeps the saved
 * password. Rejects invalid routes without changing anything. */
bool passport_agent_set_proxy(passport_store_t *store, size_t index, const passport_proxy_t *proxy,
                           const char *secret);
const passport_agent_t *passport_agent_active(const passport_store_t *store);
const char *passport_agent_platform(const passport_agent_t *agent);
void passport_store_bump(passport_store_t *store);
bool passport_lang_set(passport_store_t *store, const char *lang);
bool passport_lang_zh(const passport_store_t *store);

void passport_view_init(passport_view_t *view);
passport_action_t passport_on_key(passport_view_t *view, passport_store_t *store,
                  passport_btn_t btn, passport_press_t press);
passport_key_binding_t passport_key_binding(const passport_view_t *view, const passport_store_t *store, passport_btn_t btn);
bool passport_capture_input_valid(const passport_view_t *view, const passport_store_t *store,
                               passport_btn_t btn, passport_action_t expected_action,
                               uint32_t expected_generation, uint32_t capture_generation);
const char *passport_action_label(passport_action_t action, bool zh);
void passport_finish_send(passport_view_t *view, bool success, bool retained_clip, const char *text);
void passport_offer_retry(passport_view_t *view, const char *text);
bool passport_finish_cancel(passport_view_t *view, const char *text);
void passport_set_reply(passport_view_t *view, const char *text);
void passport_chat_sync(passport_view_t *view, const passport_store_t *store);
void passport_chat_update(passport_view_t *view, const char *answer);
void passport_chat_page(const passport_view_t *view, char *out, size_t cap,
                     passport_chat_row_t rows[PASSPORT_REPLY_ROWS]);
/* Screen installs its actual font advances; host logic has a 16/8 px default. */
void passport_reply_set_glyph_width(int (*width)(uint32_t codepoint, uint32_t next));
int passport_reply_pages(const passport_view_t *view);
void passport_reply_page_text(const passport_view_t *view, char *out, size_t cap);
void passport_set_status(passport_view_t *view, passport_link_t link, const char *text);

/* 热点配置页提交。连上目标 Wi-Fi 之后才调用；同 SSID 更新密码。 */
bool passport_portal_save(passport_store_t *store, const char *ssid, const char *pass);

int passport_store_save(const passport_store_t *store, uint8_t *out, size_t cap);
bool passport_store_load(passport_store_t *store, const uint8_t *blob, size_t len);

/* WIFI:T:WPA;S:Passport-Setup;P:passport-setup;; */
int passport_setup_wifi_qr(char *out, size_t cap);

typedef struct {
    char ssid[PASSPORT_SSID_MAX];
    char pass[PASSPORT_PASS_MAX];
    char gateway_url[PASSPORT_URL_MAX];
    char bootstrap[PASSPORT_TOKEN_MAX];
    char access_id[PASSPORT_SECRET_MAX];
    char access_secret[PASSPORT_SECRET_MAX];
    char agent_name[PASSPORT_NAME_MAX];
    char agent_kind[PASSPORT_KIND_MAX];
} passport_portal_form_t;

/* 成功时写入 store 并返回 "ok"。失败返回机器可读的原因。 */
const char *passport_portal_apply(passport_store_t *store, const passport_portal_form_t *form);

typedef struct {
    char session_key[48];
    char audio_format[16];
    char transcript[PASSPORT_TEXT_MAX];
    bool has_audio;
} passport_turn_t;

int passport_openclaw_chat_send(char *out, size_t out_len, const passport_turn_t *turn);

#define PASSPORT_SCAN_MAX 32

typedef struct {
    char ssid[PASSPORT_SSID_MAX];
    int rssi;
    bool has_bssid;
    uint8_t bssid[6];
    bool personal_hotspot;
} passport_scan_t;

typedef struct {
    passport_store_t *store;
    const passport_scan_t *scan;
    size_t scan_count;
    int (*reply)(void *ctx, const char *line);
    void *reply_ctx;
    /* 返回 NULL 表示连上，否则是原因：pass、notfound、security、timeout、fail。
     * keep 为 false 时试完回到原来的网络。 */
    const char *(*join)(void *ctx, const char *ssid, const char *pass, bool keep);
    void *join_ctx;
    /* 正在运行的固件标识。Mac 用来和刚编出来的那份比较。 */
    const char *build;
    const char *capabilities;
} passport_usb_t;

/* 配置只存在 AI Passport 上。读取用 NETS 和 AGENTS。写入带读到的版本号，不符则拒绝。 */
void passport_usb_line(passport_usb_t *usb, const char *line);
int passport_usb_scan_row(void *ctx, const passport_scan_t *network);

#endif
