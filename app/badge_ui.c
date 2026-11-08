#include "badge.h"

#include <stdio.h>
#include <string.h>

static int utf8_len(const unsigned char *text);
static void copy_cap(char *dst, size_t cap, const char *src) {
    if (!cap) return;
    size_t used = 0;
    const unsigned char *p = (const unsigned char *)(src ? src : "");
    while (*p) {
        int n = utf8_len(p);
        if (used + (size_t)n >= cap) break;
        memcpy(dst + used, p, (size_t)n);
        used += n;
        p += n;
    }
    dst[used] = 0;
}

void badge_view_init(badge_view_t *view) {
    memset(view, 0, sizeof(*view));
    view->link = BADGE_LINK_OFF;
    copy_cap(view->status, sizeof(view->status), "未联网");
    view->battery_soc = -1;
    view->speaker_volume = BADGE_VOLUME_DEFAULT;
    view->wifi_online = false;
    view->bluetooth_ready = false;
    view->sending = false;
    memset(view->waveform, 0, sizeof(view->waveform));
}

static int utf8_len(const unsigned char *text) {
    int n = *text < 0x80 ? 1 : (*text & 0xe0) == 0xc0 ? 2 :
            (*text & 0xf0) == 0xe0 ? 3 : (*text & 0xf8) == 0xf0 ? 4 : 1;
    for (int i = 1; i < n; i++) if ((text[i] & 0xc0) != 0x80) return 1;
    return n;
}

static uint32_t codepoint(const char *p) {
    const unsigned char *u = (const unsigned char *)p;
    int n = utf8_len(u);
    uint32_t cp = n == 1 ? u[0] : u[0] & ((1u << (7-n)) - 1);
    for (int i = 1; i < n; i++) cp = (cp << 6) | (u[i] & 63);
    return cp;
}

static int (*s_glyph_width)(uint32_t, uint32_t);
void badge_reply_set_glyph_width(int (*width)(uint32_t, uint32_t)) {
    s_glyph_width = width;
}

/* Return one visible line plus the next source position. Explicit newlines
 * consume a row, wide Latin letters use their actual advances, and an English
 * word wraps at a preceding space when possible. Long words can still split. */
static bool closing_punctuation(uint32_t cp) {
    return cp == 0x3001 || cp == 0x3002 || cp == 0xff0c || cp == 0xff1b ||
           cp == 0xff1a || cp == 0xff01 || cp == 0xff1f || cp == 0xff09 ||
           cp == 0x3009 || cp == 0x300b || cp == 0x300d || cp == 0x300f || cp == 0x3011 ||
           cp == 0x3015 || cp == 0x3017 || cp == 0x3019 || cp == 0x301b ||
           cp == 0x2019 || cp == 0x201d || cp == 0x301e || cp == 0x301f ||
           cp == 0xff3d || cp == 0xff5d || cp == 0xff60 || cp == 0xff0e ||
           cp == 0xfe50 || cp == 0xfe51 || cp == 0xfe52 || cp == 0xfe54 ||
           cp == 0xfe55 || cp == 0xfe56 || cp == 0xfe57 || cp == 0xfe5a ||
           cp == 0xfe5c || cp == 0xfe5e ||
           (cp < 128 && strchr(",.;:!?)]}", (int)cp) != NULL);
}

static bool opening_punctuation(uint32_t cp) {
    return cp == 0xff08 || cp == 0x3008 || cp == 0x300a || cp == 0x300c || cp == 0x300e ||
           cp == 0x3010 || cp == 0x3014 || cp == 0x3016 || cp == 0x3018 ||
           cp == 0x301a || cp == 0x2018 || cp == 0x201c || cp == 0x301d ||
           cp == 0xff3b || cp == 0xff5b || cp == 0xff5f || cp == 0xfe59 ||
           cp == 0xfe5b || cp == 0xfe5d ||
           (cp < 128 && strchr("([{", (int)cp) != NULL);
}

static const char *reply_line(const char *start, const char **line_end) {
    const char *p = start, *space = NULL, *previous = NULL;
    int pixels = 0;
    while (*p && *p != '\n' && *p != '\r') {
        int n = utf8_len((const unsigned char *)p);
        uint32_t cp = codepoint(p), next = codepoint(p+n);
        int width = s_glyph_width ? s_glyph_width(cp, next) : cp < 128 ? 8 : 16;
        if (width < 0) width = 0;
        if (pixels + width > BADGE_REPLY_BODY_WIDTH && p != start) {
            if (space && space != start) {
                *line_end = space;
                p = space + 1;
                while (*p == ' ') p++;
                return p;
            }
            if (previous && previous > start &&
                (closing_punctuation(cp) || opening_punctuation(codepoint(previous)))) {
                *line_end = previous;
                return previous;
            }
            break;
        }
        if (*p == ' ') space = p;
        pixels += width;
        previous = p;
        p += n;
    }
    *line_end = p;
    if (*p == '\r') { p++; if (*p == '\n') p++; }
    else if (*p == '\n') p++;
    return p;
}

static const char *reply_page(const char *p, char *out, size_t cap) {
    size_t used = 0;
    for (int row = 0; row < BADGE_REPLY_ROWS && *p; row++) {
        const char *end;
        const char *next = reply_line(p, &end);
        if (out) {
            if (row && used + 1 < cap) out[used++] = '\n';
            while (p < end) {
                int n = utf8_len((const unsigned char *)p);
                if (used + (size_t)n >= cap) { out[used] = 0; return next; }
                memcpy(out + used, p, (size_t)n);
                used += n;
                p += n;
            }
        }
        p = next;
    }
    if (out && cap) out[used] = 0;
    return p;
}

int badge_reply_pages(const badge_view_t *view) {
    if (!view || !view->reply[0]) return 1;
    const char *p = view->reply;
    int pages = 0;
    do { p = reply_page(p, NULL, 0); pages++; } while (*p);
    return pages;
}

void badge_reply_page_text(const badge_view_t *view, char *out, size_t cap) {
    if (!out || !cap) return;
    out[0] = 0;
    if (!view) return;
    const char *p = view->reply;
    for (int i = 0; i < view->reply_page && *p; i++) p = reply_page(p, NULL, 0);
    (void)reply_page(p, out, cap);
}

void badge_set_reply(badge_view_t *view, const char *text) {
    copy_cap(view->reply, sizeof(view->reply), text);
    view->reply_page = 0;
    view->reply_unread = false;
    view->screen = BADGE_SCREEN_TALK;
}

void badge_set_status(badge_view_t *view, badge_link_t link, const char *text) {
    view->link = link;
    copy_cap(view->status, sizeof(view->status), text);
}

/* One binding table drives both on-screen labels and input handling. */
badge_key_binding_t badge_key_binding(const badge_view_t *v, const badge_store_t *s, badge_btn_t btn) {
    badge_key_binding_t key = { BADGE_ACT_NONE, false };
    if (!v || !s || btn < BADGE_BTN_UP || btn > BADGE_BTN_OK) return key;
    bool busy = v->recording || v->sending;
    key.enabled = true;
    switch (v->screen) {
        case BADGE_SCREEN_HOME:
            key.action = btn == BADGE_BTN_UP ? BADGE_ACT_PREFERENCES
                : btn == BADGE_BTN_DOWN ? BADGE_ACT_LIVE
                : v->sending ? BADGE_ACT_PROGRESS : v->retry_send ? BADGE_ACT_RETRY
                : v->reply_unread ? BADGE_ACT_READ_REPLY : BADGE_ACT_RECORD;
            key.enabled = !v->cancel_pending && (btn != BADGE_BTN_DOWN || (!busy && !v->retry_send));
            break;
        case BADGE_SCREEN_PREFERENCES:
            key.action = btn == BADGE_BTN_UP ? BADGE_ACT_NEXT_PREFERENCE
                : btn == BADGE_BTN_DOWN ? (v->preference_cursor == 0 ? BADGE_ACT_NETWORKS
                    : v->preference_cursor == 1 ? BADGE_ACT_AGENTS : BADGE_ACT_VOLUME) : BADGE_ACT_BACK;
            key.enabled = !busy;
            break;
        case BADGE_SCREEN_VOLUME:
            key.action = btn == BADGE_BTN_UP ? BADGE_ACT_VOLUME_UP
                : btn == BADGE_BTN_DOWN ? BADGE_ACT_VOLUME_DOWN : BADGE_ACT_BACK;
            key.enabled = !busy && !v->cancel_pending && (btn == BADGE_BTN_OK ||
                (btn == BADGE_BTN_UP ? v->speaker_volume < 100 : v->speaker_volume > 0));
            break;
        case BADGE_SCREEN_LIVE:
            key.action = btn == BADGE_BTN_UP ? (v->live_muted ? BADGE_ACT_LIVE_UNMUTE : BADGE_ACT_LIVE_MUTE)
                : btn == BADGE_BTN_DOWN ? BADGE_ACT_LIVE_RECONNECT : BADGE_ACT_END_LIVE;
            key.enabled = !v->cancel_pending && (btn != BADGE_BTN_UP || v->live_ready);
            break;
        case BADGE_SCREEN_FAILURE:
            key.action = btn == BADGE_BTN_UP ? BADGE_ACT_RETRY
                : btn == BADGE_BTN_DOWN ? BADGE_ACT_RERECORD : BADGE_ACT_DISCARD;
            break;
        case BADGE_SCREEN_CAPTURE:
            key.action = btn == BADGE_BTN_UP ? BADGE_ACT_CANCEL_RECORD
                : btn == BADGE_BTN_DOWN ? BADGE_ACT_RERECORD : BADGE_ACT_SEND;
            break;
        case BADGE_SCREEN_PROGRESS:
            key.action = btn == BADGE_BTN_UP ? BADGE_ACT_CANCEL_SEND
                : btn == BADGE_BTN_DOWN ? BADGE_ACT_RESTART_SEND : BADGE_ACT_BACK;
            break;
        case BADGE_SCREEN_CANCELLING:
            key.action = btn == BADGE_BTN_UP ? BADGE_ACT_CANCEL_SEND
                : btn == BADGE_BTN_DOWN ? BADGE_ACT_RESTART_SEND : BADGE_ACT_BACK;
            key.enabled = btn == BADGE_BTN_OK;
            break;
        case BADGE_SCREEN_TALK:
            /* At either end of the reply the paging key that has nowhere to
             * go becomes Back, so the reader never shows a dead key. */
            key.action = btn == BADGE_BTN_UP ? (v->reply_page > 0 ? BADGE_ACT_PREV_PAGE : BADGE_ACT_BACK)
                : btn == BADGE_BTN_DOWN ? (v->reply_page + 1 < badge_reply_pages(v) ? BADGE_ACT_NEXT_PAGE : BADGE_ACT_BACK)
                : v->sending ? BADGE_ACT_CANCEL_SEND : BADGE_ACT_CONTINUE;
            key.enabled = !v->recording && !v->cancel_pending;
            break;
        case BADGE_SCREEN_NETS:
            key.action = btn == BADGE_BTN_UP ? BADGE_ACT_NEXT_NETWORK
                : btn == BADGE_BTN_OK ? BADGE_ACT_BACK
                : v->cursor >= 0 && (size_t)v->cursor < s->count ? BADGE_ACT_OPEN_NETWORK : BADGE_ACT_ADD_NETWORK;
            key.enabled = btn != BADGE_BTN_UP || s->count > 0;
            break;
        case BADGE_SCREEN_NET_MENU: {
            bool has_net = v->cursor >= 0 && (size_t)v->cursor < s->count;
            key.action = btn == BADGE_BTN_UP ? BADGE_ACT_PRIORITIZE
                : btn == BADGE_BTN_OK ? BADGE_ACT_BACK : BADGE_ACT_CONFIGURE;
            key.enabled = btn == BADGE_BTN_OK || (!busy && !v->cancel_pending && has_net);
            break;
        }
        case BADGE_SCREEN_SETTINGS:
            key.action = btn == BADGE_BTN_UP ? BADGE_ACT_NEXT_AGENT : btn == BADGE_BTN_OK ? BADGE_ACT_BACK
                : s->agent_count && v->cursor >= 0 && (size_t)v->cursor < s->agent_count &&
                  v->cursor != s->active_agent ? BADGE_ACT_SELECT_AGENT : BADGE_ACT_CONFIGURE;
            key.enabled = btn == BADGE_BTN_UP ? s->agent_count > 1 : btn == BADGE_BTN_OK || !busy;
            break;
        case BADGE_SCREEN_ADD:
            key.action = btn == BADGE_BTN_UP ? BADGE_ACT_ENABLE_CONFIG
                : btn == BADGE_BTN_DOWN ? BADGE_ACT_CLOSE_CONFIG : BADGE_ACT_BACK;
            key.enabled = btn == BADGE_BTN_OK || (!busy && (btn != BADGE_BTN_DOWN || v->bluetooth_ready));
            break;
        default:
            key.action = BADGE_ACT_BACK;
            break;
    }
    return key;
}

const char *badge_action_label(badge_action_t action, bool zh) {
    static const char *const labels[][2] = {
        { "", "" }, { "网络", "Wi-Fi" }, { "助手", "Agent" }, { "提问", "Ask" },
        { "发送", "Send" }, { "重试", "Retry" }, { "重录", "New" }, { "取消", "Stop" },
        { "切换", "Next" }, { "打开", "Open" }, { "添加", "Add" }, { "优先", "First" },
        { "切换", "Next" }, { "使用", "Use" }, { "配置", "Set" }, { "关闭", "Off" },
        { "返回", "Back" }, { "上页", "Prev" }, { "下页", "Next" }, { "进度", "View" },
        { "查看", "Read" }, { "开启", "On" },
        { "取消", "Cancel" }, { "取消", "Cancel" }, { "重录", "New" },
        { "继续", "Talk" },
        { "设置", "Setup" }, { "", "" }, { "", "" },
        { "通话", "Call" }, { "结束", "End" },
        { "静音", "Mute" }, { "收音", "Unmute" }, { "重连", "Retry" },
        { "切换", "Next" }, { "打开", "Open" }, { "调高", "Louder" }, { "调低", "Softer" },
    };
    if ((unsigned)action >= sizeof(labels)/sizeof(labels[0])) return "";
    return labels[action][zh ? 0 : 1];
}

badge_action_t badge_on_key(badge_view_t *v, badge_store_t *s, badge_btn_t btn, badge_press_t press) {
    if (!v || !s || press != BADGE_CLICK) return BADGE_ACT_NONE;
    badge_key_binding_t key = badge_key_binding(v, s, btn);
    if (!key.enabled) return BADGE_ACT_NONE;
    v->notice[0] = 0;
    switch (key.action) {
        case BADGE_ACT_PREFERENCES:
            v->screen = BADGE_SCREEN_PREFERENCES;
            break;
        case BADGE_ACT_NEXT_PREFERENCE:
            v->preference_cursor = (v->preference_cursor + 1) % 3;
            break;
        case BADGE_ACT_VOLUME:
            v->screen = BADGE_SCREEN_VOLUME;
            break;
        case BADGE_ACT_VOLUME_UP:
            v->speaker_volume = v->speaker_volume > 95 ? 100 : v->speaker_volume + 5;
            break;
        case BADGE_ACT_VOLUME_DOWN:
            v->speaker_volume = v->speaker_volume < 5 ? 0 : v->speaker_volume - 5;
            break;
        case BADGE_ACT_LIVE:
            v->recording = false; v->sending = true;
            v->retry_send = v->reply_unread = false;
            v->live_speaking = false;
            v->live_waiting = false;
            v->live_ready = v->live_muted = false;
            v->live_failed = false;
            v->screen = BADGE_SCREEN_LIVE;
            copy_cap(v->reply, sizeof(v->reply), "正在连接实时语音");
            break;
        case BADGE_ACT_END_LIVE:
        case BADGE_ACT_LIVE_RECONNECT:
            v->cancel_pending = true; v->restart_after_cancel = false;
            v->screen = BADGE_SCREEN_CANCELLING;
            break;
        case BADGE_ACT_LIVE_MUTE:
        case BADGE_ACT_LIVE_UNMUTE:
            v->live_muted = key.action == BADGE_ACT_LIVE_MUTE;
            break;
        case BADGE_ACT_NETWORKS:
            v->screen = BADGE_SCREEN_NETS;
            v->cursor = s->connected >= 0 && (size_t)s->connected < s->count ? s->connected : 0;
            break;
        case BADGE_ACT_AGENTS:
            v->screen = BADGE_SCREEN_SETTINGS;
            v->cursor = s->active_agent >= 0 && (size_t)s->active_agent < s->agent_count ? s->active_agent : 0;
            break;
        case BADGE_ACT_RECORD:
        case BADGE_ACT_CONTINUE:
        case BADGE_ACT_RERECORD:
            v->retry_send = v->reply_unread = false;
            v->cancel_pending = v->restart_after_cancel = false;
            memset(v->waveform, 0, sizeof(v->waveform));
            v->recording = true; v->sending = false;
            v->screen = BADGE_SCREEN_CAPTURE;
            break;
        case BADGE_ACT_DISCARD:
        case BADGE_ACT_CANCEL_RECORD:
            (void)badge_finish_cancel(v,"本次操作已取消");
            break;
        case BADGE_ACT_SEND:
        case BADGE_ACT_RETRY:
            v->recording = false; v->sending = true;
            v->screen = BADGE_SCREEN_PROGRESS;
            break;
        case BADGE_ACT_NEXT_NETWORK:
            v->cursor = (v->cursor + 1) % (int)(s->count + 1);
            break;
        case BADGE_ACT_OPEN_NETWORK:
            v->screen = BADGE_SCREEN_NET_MENU;
            break;
        case BADGE_ACT_ADD_NETWORK:
        case BADGE_ACT_CONFIGURE:
            if (v->screen != BADGE_SCREEN_ADD) v->setup_parent = v->screen;
            v->screen = BADGE_SCREEN_ADD;
            break;
        case BADGE_ACT_NEXT_AGENT:
            v->cursor = (v->cursor + 1) % (int)s->agent_count;
            break;
        case BADGE_ACT_CANCEL_SEND:
        case BADGE_ACT_RESTART_SEND:
            v->cancel_pending=true;v->restart_after_cancel=key.action==BADGE_ACT_RESTART_SEND;
            v->screen=BADGE_SCREEN_CANCELLING;
            break;
        case BADGE_ACT_BACK:
            if (v->screen==BADGE_SCREEN_PROGRESS || (v->screen==BADGE_SCREEN_TALK && v->sending)) {
                v->cancel_pending=true;v->restart_after_cancel=false;
                v->screen=BADGE_SCREEN_CANCELLING;break;
            }
            v->screen = v->screen == BADGE_SCREEN_NET_MENU ? BADGE_SCREEN_NETS
                : v->screen == BADGE_SCREEN_ADD ? ((v->setup_parent == BADGE_SCREEN_SETTINGS || v->setup_parent == BADGE_SCREEN_NET_MENU || v->setup_parent == BADGE_SCREEN_NETS) ? v->setup_parent : BADGE_SCREEN_HOME)
                : v->screen == BADGE_SCREEN_NETS || v->screen == BADGE_SCREEN_SETTINGS || v->screen == BADGE_SCREEN_VOLUME ? BADGE_SCREEN_PREFERENCES
                : BADGE_SCREEN_HOME;
            break;
        case BADGE_ACT_PREV_PAGE: v->reply_page--; break;
        case BADGE_ACT_NEXT_PAGE: v->reply_page++; break;
        case BADGE_ACT_PROGRESS: v->screen = BADGE_SCREEN_PROGRESS; break;
        case BADGE_ACT_READ_REPLY: v->screen = BADGE_SCREEN_TALK; v->reply_unread = false; break;
        default: break; /* Owner executes persistence and radio side effects. */
    }
    return key.action;
}

void badge_offer_retry(badge_view_t *v, const char *text) {
    v->recording = v->sending = v->reply_unread = false;
    v->retry_send = true;
    v->reply_page = 0;
    copy_cap(v->reply, sizeof(v->reply), text);
    v->screen = BADGE_SCREEN_FAILURE;
}

bool badge_finish_cancel(badge_view_t *v, const char *text) {
    bool restart=v->restart_after_cancel;
    v->recording=v->sending=v->retry_send=v->reply_unread=v->cancel_pending=v->restart_after_cancel=false;
    v->reply_page=0;v->screen=BADGE_SCREEN_HOME;
    copy_cap(v->reply,sizeof(v->reply),text);
    return restart;
}

void badge_finish_send(badge_view_t *v, bool success, bool retained_clip, const char *text) {
    if (!v->sending || v->cancel_pending) return;
    v->sending = false;
    v->retry_send = !success && retained_clip;
    copy_cap(v->reply, sizeof(v->reply), text);
    if (v->screen!=BADGE_SCREEN_TALK) v->reply_page = 0;
    v->reply_unread = success;
    if (!success && v->screen == BADGE_SCREEN_LIVE) {
        v->live_failed = true;
        v->live_ready = v->live_speaking = v->live_waiting = false;
        memset(v->waveform, 0, sizeof(v->waveform));
        return;
    }
    if (v->screen == BADGE_SCREEN_HOME || v->screen == BADGE_SCREEN_PROGRESS || v->screen == BADGE_SCREEN_TALK || v->screen == BADGE_SCREEN_LIVE) {
        if (success && v->screen==BADGE_SCREEN_TALK) { v->reply_unread=false;return; }
        if (success) badge_set_reply(v, text);
        else v->screen = retained_clip ? BADGE_SCREEN_FAILURE : BADGE_SCREEN_HOME;
    }
}
