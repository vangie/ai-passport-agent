#include "passport.h"

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

void passport_view_init(passport_view_t *view) {
    memset(view, 0, sizeof(*view));
    view->link = PASSPORT_LINK_OFF;
    copy_cap(view->status, sizeof(view->status), "未联网");
    view->battery_soc = -1;
    view->network_target = -1;
    view->speaker_volume = PASSPORT_VOLUME_DEFAULT;
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
void passport_reply_set_glyph_width(int (*width)(uint32_t, uint32_t)) {
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

static const char *text_line(const char *start, const char **line_end, int body_width) {
    const char *p = start, *space = NULL, *previous = NULL;
    int pixels = 0;
    while (*p && *p != '\n' && *p != '\r') {
        int n = utf8_len((const unsigned char *)p);
        uint32_t cp = codepoint(p), next = codepoint(p+n);
        int width = s_glyph_width ? s_glyph_width(cp, next) : cp < 128 ? 8 : 16;
        if (width < 0) width = 0;
        if (pixels + width > body_width && p != start) {
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

static const char *reply_line(const char *start, const char **line_end) {
    return text_line(start, line_end, PASSPORT_REPLY_BODY_WIDTH);
}

static const char *reply_page(const char *p, char *out, size_t cap) {
    size_t used = 0;
    for (int row = 0; row < PASSPORT_REPLY_ROWS && *p; row++) {
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

static void chat_row(int page, int wanted, int row, const char *start, const char *end,
                     char *out, size_t cap, size_t *used, passport_chat_row_t *metadata,
                     bool assistant, bool caption) {
    if (!out || page != wanted || !cap) return;
    if (*used && *used + 1 < cap) out[(*used)++] = '\n';
    size_t offset = *used;
    while (start < end) {
        int bytes = utf8_len((const unsigned char *)start);
        if (*used + (size_t)bytes >= cap) break;
        memcpy(out + *used, start, (size_t)bytes);
        *used += (size_t)bytes;
        start += bytes;
    }
    out[*used] = 0;
    if (metadata) metadata[row] = (passport_chat_row_t){
        .offset = (uint16_t)offset, .length = (uint16_t)(*used - offset),
        .assistant = assistant, .caption = caption
    };
}

static void chat_caption(const passport_view_t *view, bool assistant, char *out, size_t cap) {
    const char *name = assistant ? (view->chat_name[0] ? view->chat_name : view->chat_zh ? "助手" : "Assistant")
                                 : view->chat_zh ? "你" : "You";
    const char *end;
    const char *next = text_line(name, &end, PASSPORT_CHAT_BODY_WIDTH - 32);
    size_t bytes = (size_t)(end - name);
    snprintf(out, cap, "%.*s%s%s", (int)bytes, name, *next ? "…" : "", view->chat_zh ? "：" : ":");
}

static int chat_pages(const passport_view_t *view, int wanted, char *out, size_t cap,
                      int *latest, passport_chat_row_t *metadata) {
    int page = 0, rows = 0;
    size_t used = 0;
    if (out && cap) out[0] = 0;
    if (metadata) memset(metadata, 0, sizeof(*metadata) * PASSPORT_REPLY_ROWS);
    if (latest) *latest = 0;
    for (int turn = 0; turn < view->chat_count; turn++) {
        bool first = true;
        for (int role = 0; role < 2; role++) {
            const char *body = role ? view->chat[turn].answer : view->chat[turn].question;
            if (!*body) continue;
            char caption[PASSPORT_NAME_MAX + 8];
            chat_caption(view, role != 0, caption, sizeof(caption));
            if (rows && rows + 3 > PASSPORT_REPLY_ROWS) { page++; rows = 0; }
            if (first && turn + 1 == view->chat_count && latest) *latest = page;
            first = false;
            if (rows) {
                chat_row(page, wanted, rows, "", "", out, cap, &used, metadata, role != 0, false);
                rows++;
            }
            chat_row(page, wanted, rows, caption, caption + strlen(caption), out, cap, &used, metadata, role != 0, true);
            rows++;
            while (*body) {
                if (rows == PASSPORT_REPLY_ROWS) {
                    page++; rows = 1;
                    chat_row(page, wanted, 0, caption, caption + strlen(caption), out, cap, &used, metadata, role != 0, true);
                }
                const char *end;
                const char *next = text_line(body, &end, PASSPORT_CHAT_BODY_WIDTH);
                chat_row(page, wanted, rows, body, end, out, cap, &used, metadata, role != 0, false);
                rows++;
                body = next;
            }
        }
    }
    return page + 1;
}

void passport_chat_page(const passport_view_t *view, char *out, size_t cap,
                     passport_chat_row_t rows[PASSPORT_REPLY_ROWS]) {
    (void)chat_pages(view, view->reply_page, out, cap, NULL, rows);
}

void passport_chat_sync(passport_view_t *view, const passport_store_t *store) {
    const passport_agent_t *agent = passport_agent_active(store);
    uint32_t scope = 2166136261u;
    if (agent) {
        const char *fields[] = {agent->name, agent->kind, agent->url};
        for (size_t field = 0; field < sizeof(fields) / sizeof(fields[0]); field++) {
            for (const unsigned char *cursor = (const unsigned char *)fields[field]; *cursor; cursor++)
                scope = (scope ^ *cursor) * 16777619u;
            scope = (scope ^ 255u) * 16777619u;
        }
    }
    if (scope != view->chat_scope) {
        memset(view->chat, 0, sizeof(view->chat));
        view->chat_count = 0;
        view->chat_pending = false;
        view->chat_evicted = false;
        if (view->chat_visible) {
            view->reply[0] = view->heard[0] = 0;
            view->reply_unread = false;
            view->reply_page = 0;
            if (view->screen == PASSPORT_SCREEN_TALK) view->screen = PASSPORT_SCREEN_HOME;
        }
        view->chat_visible = false;
        view->chat_scope = scope;
    }
    view->chat_zh = passport_lang_zh(store);
    copy_cap(view->chat_name, sizeof(view->chat_name), agent ? agent->name : "");
}

static void chat_discard_pending(passport_view_t *view) {
    if (view->chat_pending && view->chat_count) {
        memset(&view->chat[--view->chat_count], 0, sizeof(view->chat[0]));
        if (view->chat_evicted) {
            memmove(view->chat + 1, view->chat, sizeof(view->chat[0]) * view->chat_count);
            memcpy(view->chat, &view->chat[PASSPORT_CHAT_TURNS], sizeof(view->chat[0]));
            view->chat_count++;
        }
    }
    memset(&view->chat[PASSPORT_CHAT_TURNS], 0, sizeof(view->chat[0]));
    view->chat_evicted = false;
    view->chat_pending = false;
    view->chat_visible = view->chat_count > 0;
}

static void chat_begin(passport_view_t *view) {
    chat_discard_pending(view);
    if (view->chat_count == PASSPORT_CHAT_TURNS) {
        memcpy(&view->chat[PASSPORT_CHAT_TURNS], view->chat, sizeof(view->chat[0]));
        view->chat_evicted = true;
        memmove(view->chat, view->chat + 1, sizeof(view->chat[0]) * (PASSPORT_CHAT_TURNS - 1));
        view->chat_count--;
    }
    memset(&view->chat[view->chat_count++], 0, sizeof(view->chat[0]));
    view->chat_pending = view->chat_visible = true;
    view->reply_page = 0;
}

void passport_chat_update(passport_view_t *view, const char *answer) {
    if (!view->chat_pending || !view->chat_count) return;
    passport_chat_turn_t *turn = &view->chat[view->chat_count - 1];
    bool first_content = !turn->question[0] && !turn->answer[0];
    if (view->heard[0]) copy_cap(turn->question, sizeof(turn->question), view->heard);
    if (answer && *answer) copy_cap(turn->answer, sizeof(turn->answer), answer);
    if (first_content) (void)chat_pages(view, -1, NULL, 0, &view->reply_page, NULL);
}

int passport_reply_pages(const passport_view_t *view) {
    if (view && view->chat_visible && view->chat_count) return chat_pages(view, -1, NULL, 0, NULL, NULL);
    if (!view || !view->reply[0]) return 1;
    const char *p = view->reply;
    int pages = 0;
    do { p = reply_page(p, NULL, 0); pages++; } while (*p);
    return pages;
}

void passport_reply_page_text(const passport_view_t *view, char *out, size_t cap) {
    if (!out || !cap) return;
    out[0] = 0;
    if (!view) return;
    if (view->chat_visible && view->chat_count) {
        (void)chat_pages(view, view->reply_page, out, cap, NULL, NULL);
        return;
    }
    const char *p = view->reply;
    for (int i = 0; i < view->reply_page && *p; i++) p = reply_page(p, NULL, 0);
    (void)reply_page(p, out, cap);
}

void passport_set_reply(passport_view_t *view, const char *text) {
    view->chat_visible = false;
    copy_cap(view->reply, sizeof(view->reply), text);
    view->reply_page = 0;
    view->reply_unread = false;
    view->screen = PASSPORT_SCREEN_TALK;
}

void passport_set_status(passport_view_t *view, passport_link_t link, const char *text) {
    view->link = link;
    copy_cap(view->status, sizeof(view->status), text);
}

/* One binding table drives both on-screen labels and input handling. */
passport_key_binding_t passport_key_binding(const passport_view_t *v, const passport_store_t *s, passport_btn_t btn) {
    passport_key_binding_t key = { PASSPORT_ACT_NONE, false };
    if (!v || !s || btn < PASSPORT_BTN_UP || btn > PASSPORT_BTN_OK) return key;
    bool busy = v->recording || v->sending || v->network_state == PASSPORT_NETWORK_CONNECTING;
    key.enabled = true;
    switch (v->screen) {
        case PASSPORT_SCREEN_HOME:
            key.action = btn == PASSPORT_BTN_UP ? PASSPORT_ACT_PREFERENCES
                : btn == PASSPORT_BTN_DOWN ? PASSPORT_ACT_LIVE
                : v->sending ? PASSPORT_ACT_PROGRESS : v->retry_send ? PASSPORT_ACT_RETRY
                : v->reply_unread ? PASSPORT_ACT_READ_REPLY : PASSPORT_ACT_RECORD;
            key.enabled = !v->cancel_pending &&
                (btn != PASSPORT_BTN_DOWN || (!busy && !v->retry_send)) &&
                (btn == PASSPORT_BTN_UP || v->network_state != PASSPORT_NETWORK_CONNECTING);
            break;
        case PASSPORT_SCREEN_PREFERENCES:
            key.action = btn == PASSPORT_BTN_UP ? PASSPORT_ACT_NEXT_PREFERENCE
                : btn == PASSPORT_BTN_DOWN ? (v->preference_cursor == 2
                    ? (v->bluetooth_ready ? PASSPORT_ACT_CLOSE_CONFIG : PASSPORT_ACT_ENABLE_CONFIG)
                    : PASSPORT_ACT_OPEN_PREFERENCE) : PASSPORT_ACT_BACK;
            key.enabled = btn == PASSPORT_BTN_OK || !busy || v->network_state == PASSPORT_NETWORK_CONNECTING;
            if (btn == PASSPORT_BTN_DOWN && v->preference_cursor == 2)
                key.enabled = !busy && !v->bluetooth_busy;
            break;
        case PASSPORT_SCREEN_VOLUME:
            key.action = btn == PASSPORT_BTN_UP ? PASSPORT_ACT_VOLUME_UP
                : btn == PASSPORT_BTN_DOWN ? PASSPORT_ACT_VOLUME_DOWN : PASSPORT_ACT_BACK;
            key.enabled = btn == PASSPORT_BTN_OK || (!busy && !v->cancel_pending &&
                (btn == PASSPORT_BTN_UP ? v->speaker_volume < 100 : v->speaker_volume > 0));
            break;
        case PASSPORT_SCREEN_LIVE:
            key.action = btn == PASSPORT_BTN_UP ? (v->live_muted ? PASSPORT_ACT_LIVE_UNMUTE : PASSPORT_ACT_LIVE_MUTE)
                : btn == PASSPORT_BTN_DOWN ? (v->live_failed ? PASSPORT_ACT_LIVE_RECONNECT : PASSPORT_ACT_LIVE_INTERRUPT) : PASSPORT_ACT_END_LIVE;
            key.enabled = !v->cancel_pending && (btn == PASSPORT_BTN_OK ||
                (btn == PASSPORT_BTN_UP ? !v->live_failed : v->live_failed || (v->live_ready && (v->live_speaking || v->live_waiting))));
            break;
        case PASSPORT_SCREEN_FAILURE:
            key.action = btn == PASSPORT_BTN_UP ? PASSPORT_ACT_RETRY
                : btn == PASSPORT_BTN_DOWN ? PASSPORT_ACT_RERECORD : PASSPORT_ACT_DISCARD;
            break;
        case PASSPORT_SCREEN_CAPTURE:
            key.action = btn == PASSPORT_BTN_UP ? PASSPORT_ACT_CANCEL_RECORD
                : btn == PASSPORT_BTN_DOWN ? PASSPORT_ACT_RERECORD : PASSPORT_ACT_SEND;
            break;
        case PASSPORT_SCREEN_PROGRESS:
            key.action = btn == PASSPORT_BTN_UP ? PASSPORT_ACT_CANCEL_SEND
                : btn == PASSPORT_BTN_DOWN ? PASSPORT_ACT_RESTART_SEND : PASSPORT_ACT_BACK;
            break;
        case PASSPORT_SCREEN_CANCELLING:
            key.action = btn == PASSPORT_BTN_UP ? PASSPORT_ACT_CANCEL_SEND
                : btn == PASSPORT_BTN_DOWN ? PASSPORT_ACT_RESTART_SEND : PASSPORT_ACT_BACK;
            key.enabled = btn == PASSPORT_BTN_OK;
            break;
        case PASSPORT_SCREEN_TALK: {
            int pages = passport_reply_pages(v);
            key.action = btn == PASSPORT_BTN_UP ? (v->reply_page > 0 ? PASSPORT_ACT_PREV_PAGE : PASSPORT_ACT_BACK)
                : btn == PASSPORT_BTN_DOWN ? (v->reply_page + 1 < pages ? PASSPORT_ACT_NEXT_PAGE
                    : pages > 1 ? PASSPORT_ACT_BACK : PASSPORT_ACT_NONE)
                : v->sending ? PASSPORT_ACT_CANCEL_SEND : PASSPORT_ACT_CONTINUE;
            key.enabled = key.action != PASSPORT_ACT_NONE && !v->recording && !v->cancel_pending;
            break;
        }
        case PASSPORT_SCREEN_NETS:
            key.action = btn == PASSPORT_BTN_UP ? PASSPORT_ACT_NEXT_NETWORK
                : btn == PASSPORT_BTN_OK ? PASSPORT_ACT_BACK
                : v->cursor >= 0 && (size_t)v->cursor < s->count ? PASSPORT_ACT_OPEN_NETWORK : PASSPORT_ACT_ADD_NETWORK;
            key.enabled = (btn != PASSPORT_BTN_UP || s->count > 0) &&
                (btn != PASSPORT_BTN_DOWN || (v->cursor >= 0 && (size_t)v->cursor < s->count) || !busy);
            break;
        case PASSPORT_SCREEN_NET_MENU: {
            bool has_net = v->cursor >= 0 && (size_t)v->cursor < s->count;
            key.action = btn == PASSPORT_BTN_UP ? PASSPORT_ACT_CONNECT_NETWORK
                : btn == PASSPORT_BTN_OK ? PASSPORT_ACT_BACK : PASSPORT_ACT_CONFIGURE;
            key.enabled = btn == PASSPORT_BTN_OK || (!busy && !v->cancel_pending && has_net &&
                (btn != PASSPORT_BTN_UP || s->nets[v->cursor].enabled));
            break;
        }
        case PASSPORT_SCREEN_SETTINGS:
            key.action = btn == PASSPORT_BTN_UP ? PASSPORT_ACT_NEXT_AGENT : btn == PASSPORT_BTN_OK ? PASSPORT_ACT_BACK
                : s->agent_count && v->cursor >= 0 && (size_t)v->cursor < s->agent_count &&
                  v->cursor != s->active_agent ? PASSPORT_ACT_SELECT_AGENT : PASSPORT_ACT_CONFIGURE;
            key.enabled = btn == PASSPORT_BTN_UP ? s->agent_count > 1 : btn == PASSPORT_BTN_OK || !busy;
            break;
        case PASSPORT_SCREEN_ADD:
            key.action = btn == PASSPORT_BTN_UP ? PASSPORT_ACT_ENABLE_CONFIG
                : btn == PASSPORT_BTN_DOWN ? PASSPORT_ACT_CLOSE_CONFIG : PASSPORT_ACT_BACK;
            key.enabled = btn == PASSPORT_BTN_OK || (!busy && (btn != PASSPORT_BTN_DOWN || v->bluetooth_ready));
            break;
        default:
            key.action = PASSPORT_ACT_BACK;
            break;
    }
    return key;
}

const char *passport_action_label(passport_action_t action, bool zh) {
    static const char *const labels[][2] = {
        { "", "" }, { "网络", "Wi-Fi" }, { "助手", "Agt" }, { "提问", "Ask" },
        { "发送", "Send" }, { "重试", "Try" }, { "重录", "New" }, { "取消", "Stop" },
        { "切换", "Next" }, { "打开", "Open" }, { "添加", "Add" }, { "连接", "Join" },
        { "切换", "Next" }, { "使用", "Use" }, { "配置", "Set" }, { "关闭", "Off" },
        { "返回", "Back" }, { "上页", "Prev" }, { "下页", "Next" }, { "进度", "View" },
        { "查看", "Read" }, { "开启", "On" },
        { "取消", "Stop" }, { "取消", "Stop" }, { "重录", "New" },
        { "继续", "Talk" },
        { "设置", "Set" }, { "", "" }, { "", "" },
        { "通话", "Call" }, { "结束", "End" },
        { "静音", "Mute" }, { "收音", "Mic" }, { "重连", "Try" },
        { "切换", "Next" }, { "打开", "Open" }, { "调高", "Vol+" }, { "调低", "Vol-" },
        { "打开", "Open" },
        { "打断", "Stop" },
    };
    if ((unsigned)action >= sizeof(labels)/sizeof(labels[0])) return "";
    return labels[action][zh ? 0 : 1];
}

bool passport_capture_input_valid(const passport_view_t *view, const passport_store_t *store,
                               passport_btn_t btn, passport_action_t expected_action,
                               uint32_t expected_generation, uint32_t capture_generation) {
    if (!view || !store || !view->recording || view->screen != PASSPORT_SCREEN_CAPTURE ||
        view->cancel_pending || expected_generation != capture_generation) return false;
    if (expected_action != PASSPORT_ACT_SEND && expected_action != PASSPORT_ACT_CANCEL_RECORD &&
        expected_action != PASSPORT_ACT_RERECORD) return false;
    passport_key_binding_t key = passport_key_binding(view, store, btn);
    return key.enabled && key.action == expected_action;
}

passport_action_t passport_on_key(passport_view_t *v, passport_store_t *s, passport_btn_t btn, passport_press_t press) {
    if (!v || !s || press != PASSPORT_CLICK) return PASSPORT_ACT_NONE;
    passport_key_binding_t key = passport_key_binding(v, s, btn);
    if (!key.enabled) return PASSPORT_ACT_NONE;
    passport_action_t action = key.action;
    if (action == PASSPORT_ACT_OPEN_PREFERENCE) {
        action = v->preference_cursor == 0 ? PASSPORT_ACT_NETWORKS
            : v->preference_cursor == 1 ? PASSPORT_ACT_AGENTS
            : PASSPORT_ACT_VOLUME;
    }
    v->notice[0] = 0;
    switch (action) {
        case PASSPORT_ACT_PREFERENCES:
            v->screen = PASSPORT_SCREEN_PREFERENCES;
            break;
        case PASSPORT_ACT_NEXT_PREFERENCE:
            v->preference_cursor = (v->preference_cursor + 1) % 4;
            break;
        case PASSPORT_ACT_VOLUME:
            v->screen = PASSPORT_SCREEN_VOLUME;
            break;
        case PASSPORT_ACT_VOLUME_UP:
            v->speaker_volume = v->speaker_volume > 95 ? 100 : v->speaker_volume + 5;
            break;
        case PASSPORT_ACT_VOLUME_DOWN:
            v->speaker_volume = v->speaker_volume < 5 ? 0 : v->speaker_volume - 5;
            break;
        case PASSPORT_ACT_LIVE:
            chat_discard_pending(v);
            v->chat_visible = false;
            v->heard[0] = 0;
            v->recording = false; v->sending = true;
            v->retry_send = v->reply_unread = false;
            v->live_speaking = false;
            v->live_waiting = false;
            v->live_ready = v->live_muted = false;
            v->live_failed = false;
            v->screen = PASSPORT_SCREEN_LIVE;
            copy_cap(v->reply, sizeof(v->reply), "正在连接实时语音");
            break;
        case PASSPORT_ACT_END_LIVE:
        case PASSPORT_ACT_LIVE_RECONNECT:
            v->cancel_pending = true; v->restart_after_cancel = false;
            v->screen = PASSPORT_SCREEN_CANCELLING;
            break;
        case PASSPORT_ACT_LIVE_MUTE:
        case PASSPORT_ACT_LIVE_UNMUTE:
            v->live_muted = key.action == PASSPORT_ACT_LIVE_MUTE;
            break;
        case PASSPORT_ACT_LIVE_INTERRUPT:
            break;
        case PASSPORT_ACT_NETWORKS:
            v->screen = PASSPORT_SCREEN_NETS;
            v->cursor = s->connected >= 0 && (size_t)s->connected < s->count ? s->connected : 0;
            break;
        case PASSPORT_ACT_AGENTS:
            v->screen = PASSPORT_SCREEN_SETTINGS;
            v->cursor = s->active_agent >= 0 && (size_t)s->active_agent < s->agent_count ? s->active_agent : 0;
            break;
        case PASSPORT_ACT_RECORD:
        case PASSPORT_ACT_CONTINUE:
        case PASSPORT_ACT_RERECORD:
            passport_chat_sync(v, s);
            chat_begin(v);
            v->heard[0] = 0;
            v->retry_send = v->reply_unread = false;
            v->cancel_pending = v->restart_after_cancel = false;
            memset(v->waveform, 0, sizeof(v->waveform));
            v->recording = true; v->sending = false;
            v->screen = PASSPORT_SCREEN_CAPTURE;
            break;
        case PASSPORT_ACT_DISCARD:
        case PASSPORT_ACT_CANCEL_RECORD:
            (void)passport_finish_cancel(v,"本次操作已取消");
            break;
        case PASSPORT_ACT_SEND:
        case PASSPORT_ACT_RETRY:
            v->recording = false; v->sending = true;
            v->screen = PASSPORT_SCREEN_PROGRESS;
            break;
        case PASSPORT_ACT_NEXT_NETWORK:
            v->cursor = (v->cursor + 1) % (int)(s->count + 1);
            break;
        case PASSPORT_ACT_OPEN_NETWORK:
            v->screen = PASSPORT_SCREEN_NET_MENU;
            break;
        case PASSPORT_ACT_ADD_NETWORK:
        case PASSPORT_ACT_CONFIGURE:
            if (v->screen != PASSPORT_SCREEN_ADD) v->setup_parent = v->screen;
            v->screen = PASSPORT_SCREEN_ADD;
            break;
        case PASSPORT_ACT_NEXT_AGENT:
            v->cursor = (v->cursor + 1) % (int)s->agent_count;
            break;
        case PASSPORT_ACT_CANCEL_SEND:
        case PASSPORT_ACT_RESTART_SEND:
            v->cancel_pending=true;v->restart_after_cancel=key.action==PASSPORT_ACT_RESTART_SEND;
            v->screen=PASSPORT_SCREEN_CANCELLING;
            break;
        case PASSPORT_ACT_BACK:
            if (v->screen==PASSPORT_SCREEN_PROGRESS || (v->screen==PASSPORT_SCREEN_TALK && v->sending)) {
                v->cancel_pending=true;v->restart_after_cancel=false;
                v->screen=PASSPORT_SCREEN_CANCELLING;break;
            }
            v->screen = v->screen == PASSPORT_SCREEN_NET_MENU ? PASSPORT_SCREEN_NETS
                : v->screen == PASSPORT_SCREEN_ADD ? ((v->setup_parent == PASSPORT_SCREEN_PREFERENCES || v->setup_parent == PASSPORT_SCREEN_SETTINGS || v->setup_parent == PASSPORT_SCREEN_NET_MENU || v->setup_parent == PASSPORT_SCREEN_NETS) ? v->setup_parent : PASSPORT_SCREEN_HOME)
                : v->screen == PASSPORT_SCREEN_NETS || v->screen == PASSPORT_SCREEN_SETTINGS || v->screen == PASSPORT_SCREEN_VOLUME ? PASSPORT_SCREEN_PREFERENCES
                : PASSPORT_SCREEN_HOME;
            break;
        case PASSPORT_ACT_PREV_PAGE: v->reply_page--; break;
        case PASSPORT_ACT_NEXT_PAGE: v->reply_page++; break;
        case PASSPORT_ACT_PROGRESS: v->screen = PASSPORT_SCREEN_PROGRESS; break;
        case PASSPORT_ACT_READ_REPLY: v->screen = PASSPORT_SCREEN_TALK; v->reply_unread = false; break;
        default: break; /* Owner executes persistence and radio side effects. */
    }
    return key.action;
}

void passport_offer_retry(passport_view_t *v, const char *text) {
    v->recording = v->sending = v->reply_unread = false;
    v->retry_send = true;
    v->reply_page = 0;
    copy_cap(v->reply, sizeof(v->reply), text);
    v->screen = PASSPORT_SCREEN_FAILURE;
}

bool passport_finish_cancel(passport_view_t *v, const char *text) {
    bool restart=v->restart_after_cancel;
    chat_discard_pending(v);
    v->recording=v->sending=v->retry_send=v->reply_unread=v->cancel_pending=v->restart_after_cancel=false;
    v->live_ready=v->live_failed=v->live_speaking=v->live_waiting=v->live_muted=false;
    memset(v->waveform, 0, sizeof(v->waveform));
    v->reply_page=0;v->screen=PASSPORT_SCREEN_HOME;
    v->heard[0] = 0;
    copy_cap(v->reply,sizeof(v->reply),text);
    return restart;
}

void passport_finish_send(passport_view_t *v, bool success, bool retained_clip, const char *text) {
    if (!v->sending || v->cancel_pending) return;
    bool chat = v->screen != PASSPORT_SCREEN_LIVE && v->chat_pending;
    if (chat && success) {
        passport_chat_update(v, text);
        v->chat_pending = false;
        v->chat_evicted = false;
        memset(&v->chat[PASSPORT_CHAT_TURNS], 0, sizeof(v->chat[0]));
    }
    v->sending = false;
    v->retry_send = !success && retained_clip;
    copy_cap(v->reply, sizeof(v->reply), text);
    if (v->screen!=PASSPORT_SCREEN_TALK && !chat) v->reply_page = 0;
    v->reply_unread = success;
    if (success && v->screen == PASSPORT_SCREEN_LIVE) {
        v->live_failed = v->live_ready = v->live_speaking = v->live_waiting = v->live_muted = false;
        v->reply_unread = false;
        memset(v->waveform, 0, sizeof(v->waveform));
        v->screen = PASSPORT_SCREEN_HOME;
        return;
    }
    if (!success && v->screen == PASSPORT_SCREEN_LIVE) {
        v->live_failed = true;
        v->live_ready = v->live_speaking = v->live_waiting = false;
        memset(v->waveform, 0, sizeof(v->waveform));
        return;
    }
    if (v->screen == PASSPORT_SCREEN_HOME || v->screen == PASSPORT_SCREEN_PROGRESS || v->screen == PASSPORT_SCREEN_TALK || v->screen == PASSPORT_SCREEN_LIVE) {
        if (success && v->screen==PASSPORT_SCREEN_TALK) { v->reply_unread=false;return; }
        if (success && chat) { v->screen = PASSPORT_SCREEN_TALK; v->reply_unread = false; }
        else if (success) passport_set_reply(v, text);
        else v->screen = retained_clip ? PASSPORT_SCREEN_FAILURE : PASSPORT_SCREEN_HOME;
    }
}
