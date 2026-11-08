#ifndef BADGE_H
#define BADGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "badge_proxy.h"

#define BADGE_LCD_W 240
#define BADGE_LCD_H 320
#define BADGE_NET_MAX 8
#define BADGE_AGENT_MAX 4
#define BADGE_KIND_MAX 16
#define BADGE_SSID_MAX 33
#define BADGE_PASS_MAX 65
#define BADGE_NAME_MAX 24
#define BADGE_URL_MAX 160
#define BADGE_TOKEN_MAX 256
#define BADGE_SECRET_MAX 96
#define BADGE_TEXT_MAX 480
/* Shared geometry for the reply reader and its font-aware paginator. */
#define BADGE_REPLY_BODY_WIDTH 184
#define BADGE_REPLY_ROWS 9
#define BADGE_REPLY_LINE_SPACE 4
#define BADGE_PAGE_TEXT_MAX (BADGE_TEXT_MAX + BADGE_REPLY_ROWS + 1)
#define BADGE_AP_SSID "Badge-Setup"
#define BADGE_AP_PASS "badge-setup"
#define BADGE_PORTAL_URL "http://192.168.4.1"
#define BADGE_CONFIG_LINE_MAX 5120
#define BADGE_WAVEFORM_BARS 12
#define BADGE_VOLUME_DEFAULT 55

typedef enum {
    BADGE_BTN_UP = 0,
    BADGE_BTN_DOWN = 1,
    BADGE_BTN_OK = 2,
} badge_btn_t;

typedef enum {
    BADGE_CLICK = 0,
    BADGE_HOLD = 1,
    BADGE_RELEASE = 2,
    BADGE_PRESS = 3,
} badge_press_t;

typedef enum {
    BADGE_SCREEN_HOME = 0,
    BADGE_SCREEN_NETS = 1,
    BADGE_SCREEN_NET_MENU = 2,
    BADGE_SCREEN_ADD = 3,
    BADGE_SCREEN_TALK = 4,
    BADGE_SCREEN_SETTINGS = 5,
    BADGE_SCREEN_CAPTURE = 6,
    BADGE_SCREEN_PROGRESS = 7,
    BADGE_SCREEN_FAILURE = 8,
    BADGE_SCREEN_CANCELLING = 9,
    BADGE_SCREEN_PREFERENCES = 10,
    BADGE_SCREEN_LIVE = 12,
    BADGE_SCREEN_VOLUME = 13,
} badge_screen_t;

typedef enum {
    BADGE_ACT_NONE, BADGE_ACT_NETWORKS, BADGE_ACT_AGENTS, BADGE_ACT_RECORD,
    BADGE_ACT_SEND, BADGE_ACT_RETRY, BADGE_ACT_RERECORD, BADGE_ACT_CANCEL_RECORD,
    BADGE_ACT_NEXT_NETWORK, BADGE_ACT_OPEN_NETWORK, BADGE_ACT_ADD_NETWORK,
    BADGE_ACT_PRIORITIZE, BADGE_ACT_NEXT_AGENT, BADGE_ACT_SELECT_AGENT,
    BADGE_ACT_CONFIGURE, BADGE_ACT_CLOSE_CONFIG, BADGE_ACT_BACK,
    BADGE_ACT_PREV_PAGE, BADGE_ACT_NEXT_PAGE, BADGE_ACT_PROGRESS, BADGE_ACT_READ_REPLY, BADGE_ACT_ENABLE_CONFIG,
    BADGE_ACT_DISCARD, BADGE_ACT_CANCEL_SEND, BADGE_ACT_RESTART_SEND,
    BADGE_ACT_CONTINUE,
    BADGE_ACT_PREFERENCES, BADGE_ACT_RESERVED_11, BADGE_ACT_RESERVED_12,
    BADGE_ACT_LIVE, BADGE_ACT_END_LIVE, BADGE_ACT_LIVE_MUTE, BADGE_ACT_LIVE_UNMUTE, BADGE_ACT_LIVE_RECONNECT,
    BADGE_ACT_NEXT_PREFERENCE, BADGE_ACT_VOLUME, BADGE_ACT_VOLUME_UP, BADGE_ACT_VOLUME_DOWN,
} badge_action_t;

typedef struct { badge_action_t action; bool enabled; } badge_key_binding_t;

typedef enum {
    BADGE_LINK_OFF = 0,
    BADGE_LINK_JOINING = 1,
    BADGE_LINK_ONLINE = 2,
    BADGE_LINK_GATEWAY = 3,
    BADGE_LINK_FAILED = 4,
} badge_link_t;

typedef struct {
    char name[BADGE_NAME_MAX];
    char ssid[BADGE_SSID_MAX];
    char pass[BADGE_PASS_MAX];
    int priority;
    bool enabled;
} badge_net_t;

typedef struct {
    char name[BADGE_NAME_MAX];
    char kind[BADGE_KIND_MAX];
    char url[BADGE_URL_MAX];
    /* 只写不读。bootstrap 是一次性配对令牌，用过就清掉。
     * device_token 是网关发给这台工牌的凭据。 */
    char bootstrap[BADGE_TOKEN_MAX];
    char device_token[BADGE_TOKEN_MAX];
    char access_id[BADGE_SECRET_MAX];
    char access_secret[BADGE_SECRET_MAX];
    /* Optional Shadowsocks route to this gateway; off means direct. */
    badge_proxy_t proxy;
} badge_agent_t;

typedef struct {
    badge_net_t nets[BADGE_NET_MAX];
    size_t count;
    int connected;
    badge_agent_t agents[BADGE_AGENT_MAX];
    size_t agent_count;
    int active_agent;
    uint32_t generation;
    char lang[8];
} badge_store_t;

typedef struct {
    badge_screen_t screen;
    int cursor;
    bool recording;
    bool sending;
    bool retry_send;
    bool reply_unread;
    bool cancel_pending;
    bool restart_after_cancel;
    badge_screen_t setup_parent;
    char notice[96];
    uint8_t waveform[BADGE_WAVEFORM_BARS];
    badge_link_t link;
    char status[96];
    char reply[BADGE_TEXT_MAX];
    char heard[BADGE_TEXT_MAX];
    int reply_page;
    int battery_soc;
    bool wifi_online;
    bool bluetooth_ready;
    bool live_speaking;
    bool live_waiting;
    bool live_ready;
    bool live_muted;
    bool live_failed;
    uint8_t preference_cursor;
    uint8_t speaker_volume;
} badge_view_t;

typedef struct {
    badge_btn_t btn;
    badge_press_t press;
} badge_input_t;

void badge_store_init(badge_store_t *store);
bool badge_store_add(badge_store_t *store, const char *name, const char *ssid,
                     const char *pass);
bool badge_store_forget(badge_store_t *store, size_t index);
bool badge_store_promote(badge_store_t *store, size_t index);
int badge_store_pick(const badge_store_t *store, const bool *visible,
                     const int *rssi, size_t scan_count);

/* 同名更新。地址只接受 ws:// 或 wss://。第一套自动成为当前。
 * 密钥字段传 NULL 表示沿用原来的；传空串表示清掉。 */
bool badge_agent_upsert(badge_store_t *store, const char *name, const char *kind,
                        const char *url, const char *bootstrap, const char *device_token,
                        const char *access_id, const char *access_secret);
bool badge_agent_select(badge_store_t *store, size_t index);
bool badge_agent_rename(badge_store_t *store, size_t index, const char *name);
/* Move a paired gateway to a replacement WSS endpoint after confirming its
 * existing URL. Only the URL changes; device identity and tokens stay put. */
bool badge_agent_move_endpoint(badge_store_t *store, size_t index,
                               const char *expected_url, const char *url);
bool badge_agent_drop(badge_store_t *store, size_t index);
/* Set or clear one gateway's Shadowsocks route. NULL secret keeps the saved
 * password. Rejects invalid routes without changing anything. */
bool badge_agent_set_proxy(badge_store_t *store, size_t index, const badge_proxy_t *proxy,
                           const char *secret);
const badge_agent_t *badge_agent_active(const badge_store_t *store);
void badge_store_bump(badge_store_t *store);
bool badge_lang_set(badge_store_t *store, const char *lang);
bool badge_lang_zh(const badge_store_t *store);

void badge_view_init(badge_view_t *view);
badge_action_t badge_on_key(badge_view_t *view, badge_store_t *store,
                  badge_btn_t btn, badge_press_t press);
badge_key_binding_t badge_key_binding(const badge_view_t *view, const badge_store_t *store, badge_btn_t btn);
const char *badge_action_label(badge_action_t action, bool zh);
void badge_finish_send(badge_view_t *view, bool success, bool retained_clip, const char *text);
void badge_offer_retry(badge_view_t *view, const char *text);
bool badge_finish_cancel(badge_view_t *view, const char *text);
void badge_set_reply(badge_view_t *view, const char *text);
/* Screen installs its actual font advances; host logic has a 16/8 px default. */
void badge_reply_set_glyph_width(int (*width)(uint32_t codepoint, uint32_t next));
int badge_reply_pages(const badge_view_t *view);
void badge_reply_page_text(const badge_view_t *view, char *out, size_t cap);
void badge_set_status(badge_view_t *view, badge_link_t link, const char *text);

/* 热点配置页提交。连上目标 Wi-Fi 之后才调用；同 SSID 更新密码。 */
bool badge_portal_save(badge_store_t *store, const char *ssid, const char *pass);

int badge_store_save(const badge_store_t *store, uint8_t *out, size_t cap);
bool badge_store_load(badge_store_t *store, const uint8_t *blob, size_t len);

/* WIFI:T:WPA;S:Badge-Setup;P:badge-setup;; */
int badge_setup_wifi_qr(char *out, size_t cap);

typedef struct {
    char ssid[BADGE_SSID_MAX];
    char pass[BADGE_PASS_MAX];
    char gateway_url[BADGE_URL_MAX];
    char bootstrap[BADGE_TOKEN_MAX];
    char access_id[BADGE_SECRET_MAX];
    char access_secret[BADGE_SECRET_MAX];
    char agent_name[BADGE_NAME_MAX];
    char agent_kind[BADGE_KIND_MAX];
} badge_portal_form_t;

/* 成功时写入 store 并返回 "ok"。失败返回机器可读的原因。 */
const char *badge_portal_apply(badge_store_t *store, const badge_portal_form_t *form);

typedef struct {
    char session_key[48];
    char audio_format[16];
    char transcript[BADGE_TEXT_MAX];
    bool has_audio;
} badge_turn_t;

int badge_openclaw_chat_send(char *out, size_t out_len, const badge_turn_t *turn);

#define BADGE_SCAN_MAX 8

typedef struct {
    char ssid[BADGE_SSID_MAX];
    int rssi;
    bool has_bssid;
    uint8_t bssid[6];
} badge_scan_t;

typedef struct {
    badge_store_t *store;
    const badge_scan_t *scan;
    size_t scan_count;
    int (*reply)(void *ctx, const char *line);
    void *reply_ctx;
    /* 返回 NULL 表示连上，否则是原因：pass、notfound、security、timeout、fail。
     * keep 为 false 时试完回到原来的网络。 */
    const char *(*join)(void *ctx, const char *ssid, const char *pass, bool keep);
    void *join_ctx;
    /* 正在运行的固件标识。Mac 用来和刚编出来的那份比较。 */
    const char *build;
} badge_usb_t;

/* 配置只存在 AI Passport 上。读取用 NETS 和 AGENTS。写入带读到的版本号，不符则拒绝。 */
void badge_usb_line(badge_usb_t *usb, const char *line);

#endif
