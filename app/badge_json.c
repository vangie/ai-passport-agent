#include "badge_json.h"
#include <ctype.h>
#include <string.h>

enum { OBJECT = 1, ARRAY, KEY_OR_END, KEY_REQUIRED, COLON, VALUE_OR_END,
       VALUE_REQUIRED, COMMA_OR_END };

void badge_json_filter_init(badge_json_filter_t *f) { memset(f, 0, sizeof(*f)); }

static void emit(badge_json_filter_t *f, const char *p, size_t n) {
    if (n >= sizeof(f->output) - f->output_len) { f->failed = true; return; }
    memcpy(f->output + f->output_len, p, n);
    f->output_len += n;
    f->output[f->output_len] = 0;
}

static bool wanted(const badge_json_filter_t *f) {
    if (f->key_invalid) return false;
    static const char *const root[] = { "type", "event", "id", "ok", "error", "payload" };
    static const char *const nested[] = {
        "type", "auth", "policy", "protocol", "maxPayload", "attachments", "maxBytes",
        "role", "scopes", "deviceToken", "deviceTokens", "nonce", "ts", "details",
        "code", "reason", "message", "errorMessage", "runId", "sessionKey", "state",
        "status", "content", "text", "messages", "inFlightRun", "__openclaw", "stopReason",
        "sessionInfo", "hasActiveRun", "activeRunIds", "lastRunId", "deltaText", "replace",
        "tickIntervalMs", "subscribed", "key", "replayed", "seq",
        "sessionId", "relaySessionId", "transcriptionSessionId", "audio", "inputEncoding",
        "inputSampleRateHz", "outputEncoding", "outputSampleRateHz", "audioBase64",
        "final", "byteLength", "markName", "talkEvent", "turnId", "textMode", "transcriptId", "playbackSequence",
        "callId", "name", "args", "question", "context", "responseStyle", "agentSessionKey",
        "mode", "forced", "provider", "model", "phase", "passportSilence", "passportIma", "passportProtocol"
    };
    const char *const *keys = f->depth == 1 ? root : nested;
    size_t count = f->depth == 1 ? sizeof(root)/sizeof(root[0]) : sizeof(nested)/sizeof(nested[0]);
    for (size_t i = 0; i < count; i++) if (!strcmp(f->key, keys[i])) return true;
    return false;
}

static bool value_begin(badge_json_filter_t *f) {
    if (!f->depth) { f->started = true; return true; }
    unsigned i = f->depth - 1;
    bool keep = f->frames[i].keep;
    if (f->frames[i].kind == OBJECT) {
        if (f->frames[i].state != VALUE_REQUIRED) { f->failed = true; return false; }
        keep = keep && wanted(f);
    } else if (f->frames[i].state != VALUE_OR_END && f->frames[i].state != VALUE_REQUIRED) {
        f->failed = true; return false;
    }
    if (keep) {
        if (!f->frames[i].first) emit(f, ",", 1);
        f->frames[i].first = false;
        if (f->frames[i].kind == OBJECT) {
            emit(f, "\"", 1); emit(f, f->key, f->key_len); emit(f, "\":", 2);
        }
    }
    f->frames[i].state = COMMA_OR_END;
    return keep;
}

static bool valid_primitive(const char *p, size_t n) {
    if (!strcmp(p,"true") || !strcmp(p,"false") || !strcmp(p,"null")) return true;
    size_t i = 0;
    if (i < n && p[i] == '-') i++;
    if (i == n) return false;
    if (p[i] == '0') i++;
    else {
        if (p[i] < '1' || p[i] > '9') return false;
        while (i < n && isdigit((unsigned char)p[i])) i++;
    }
    if (i < n && p[i] == '.') {
        size_t start = ++i;
        while (i < n && isdigit((unsigned char)p[i])) i++;
        if (i == start) return false;
    }
    if (i < n && (p[i] == 'e' || p[i] == 'E')) {
        i++;
        if (i < n && (p[i] == '+' || p[i] == '-')) i++;
        size_t start = i;
        while (i < n && isdigit((unsigned char)p[i])) i++;
        if (i == start) return false;
    }
    return i == n;
}

static unsigned hex4(const char *p) {
    unsigned v = 0;
    for (int i=0; i<4; i++) {
        unsigned char c = (unsigned char)p[i];
        v = v * 16 + (c <= '9' ? c-'0' : (unsigned)(tolower(c)-'a'+10));
    }
    return v;
}

/* Accumulate one complete encoded character before emitting it. This also
 * prevents cutting between the two halves of an escaped surrogate pair. */
static void string_byte(badge_json_filter_t *f, unsigned char c) {
    if (!f->unit_len && c == '"') {
        if (f->is_key) {
            f->key[f->key_len] = 0;
            f->frames[f->depth-1].state = COLON;
        } else if (f->keep_string) emit(f, "\"", 1);
        f->in_string = false;
        return;
    }
    if (!f->unit_len) {
        if (c < 0x20) { f->failed = true; return; }
        f->unit_need = c == '\\' ? 2 : c < 0x80 ? 1 :
                       c >= 0xc2 && c <= 0xdf ? 2 :
                       c >= 0xe0 && c <= 0xef ? 3 :
                       c >= 0xf0 && c <= 0xf4 ? 4 : 0;
        if (!f->unit_need) { f->failed = true; return; }
    }
    f->unit[f->unit_len++] = (char)c;
    if (f->unit[0] == '\\') {
        if (f->unit_len == 2) {
            if (c == 'u') f->unit_need = 6;
            else if (!c || !strchr("\"\\/bfnrt", c)) { f->failed = true; return; }
        }
        if (f->unit_need >= 6 && f->unit_len >= 3 && f->unit_len <= 6 && !isxdigit(c)) {
            f->failed = true; return;
        }
        if (f->unit_len == 6 && f->unit_need == 6) {
            unsigned u = hex4(f->unit+2);
            if (u >= 0xd800 && u <= 0xdbff) f->unit_need = 12;
            else if (u >= 0xdc00 && u <= 0xdfff) { f->failed = true; return; }
        }
        if ((f->unit_len == 7 && c != '\\') || (f->unit_len == 8 && c != 'u') ||
            (f->unit_len >= 9 && !isxdigit(c))) { f->failed = true; return; }
        if (f->unit_len == 12) {
            unsigned u = hex4(f->unit+8);
            if (u < 0xdc00 || u > 0xdfff) { f->failed = true; return; }
        }
    } else if (f->unit_len > 1) {
        unsigned char lead = (unsigned char)f->unit[0];
        if ((c & 0xc0) != 0x80 || (f->unit_len == 2 &&
            ((lead == 0xe0 && c < 0xa0) || (lead == 0xed && c >= 0xa0) ||
             (lead == 0xf0 && c < 0x90) || (lead == 0xf4 && c >= 0x90)))) {
            f->failed = true; return;
        }
    }
    if (f->unit_len != f->unit_need) return;
    if (f->is_key) {
        if (f->unit_len != 1 || c >= 0x80 || f->key_len+1 >= sizeof(f->key)) f->key_invalid = true;
        else if (!f->key_invalid) f->key[f->key_len++] = (char)c;
    } else if (f->keep_string && !f->truncated) {
        if (f->shorten_string && f->string_len + f->unit_len > 1536) f->truncated = true;
        else {
            emit(f, f->unit, f->unit_len);
            f->string_len += f->unit_len;
        }
    }
    f->unit_len = 0;
}

bool badge_json_filter_feed(badge_json_filter_t *f, const char *data, size_t size) {
    for (size_t at=0; at<size && !f->failed; at++) {
        unsigned char c = (unsigned char)data[at];
        if (f->done) { if (!isspace(c)) f->failed = true; continue; }
        if (f->in_string) { string_byte(f,c); continue; }
        if (f->in_primitive) {
            if (!isspace(c) && c != ',' && c != '}' && c != ']') {
                if (f->primitive_len+1 >= sizeof(f->primitive)) { f->failed = true; break; }
                f->primitive[f->primitive_len++] = (char)c;
                continue;
            }
            f->primitive[f->primitive_len] = 0;
            if (!valid_primitive(f->primitive,f->primitive_len)) { f->failed = true; break; }
            if (f->keep_primitive) emit(f,f->primitive,f->primitive_len);
            f->in_primitive = false;
        }
        if (isspace(c)) continue;
        if (!f->depth && (f->started || c != '{')) { f->failed = true; break; }
        unsigned i = f->depth ? f->depth-1 : 0;
        if (c == '}' || c == ']') {
            if (!f->depth || (c == '}' ? f->frames[i].kind != OBJECT : f->frames[i].kind != ARRAY) ||
                (f->frames[i].state != COMMA_OR_END && f->frames[i].state != KEY_OR_END &&
                 f->frames[i].state != VALUE_OR_END)) { f->failed = true; break; }
            if (f->frames[i].keep) { char close = (char)c; emit(f,&close,1); }
            f->depth--;
            if (!f->depth) f->done = true;
            continue;
        }
        if (c == ',') {
            if (!f->depth || f->frames[i].state != COMMA_OR_END) { f->failed = true; break; }
            f->frames[i].state = f->frames[i].kind == OBJECT ? KEY_REQUIRED : VALUE_REQUIRED;
            continue;
        }
        if (c == ':') {
            if (!f->depth || f->frames[i].state != COLON) { f->failed = true; break; }
            f->frames[i].state = VALUE_REQUIRED; continue;
        }
        if (c == '"' && f->depth && f->frames[i].kind == OBJECT &&
            (f->frames[i].state == KEY_OR_END || f->frames[i].state == KEY_REQUIRED)) {
            f->in_string = f->is_key = true;
            f->key_len = f->unit_len = 0; f->key_invalid = false;
            continue;
        }
        bool text = !strcmp(f->key,"text") || !strcmp(f->key,"deltaText");
        bool keep = value_begin(f);
        if (f->failed) break;
        if (c == '{' || c == '[') {
            if (f->depth == BADGE_JSON_DEPTH_MAX) { f->failed = true; break; }
            i = f->depth++;
            f->frames[i].kind = c == '{' ? OBJECT : ARRAY;
            f->frames[i].state = c == '{' ? KEY_OR_END : VALUE_OR_END;
            f->frames[i].keep = keep; f->frames[i].first = true;
            if (keep) { char open = (char)c; emit(f,&open,1); }
        } else if (c == '"') {
            f->in_string = true; f->is_key = false; f->keep_string = keep;
            f->shorten_string = text; f->truncated = false;
            f->string_len = f->unit_len = 0;
            if (keep) emit(f,"\"",1);
        } else {
            f->in_primitive = true; f->keep_primitive = keep;
            f->primitive[0] = (char)c; f->primitive_len = 1;
        }
    }
    return !f->failed;
}

int badge_json_filter_finish(const badge_json_filter_t *f, char *out, size_t cap) {
    if (f->failed || !f->done || f->in_string || f->depth || f->output_len+1 > cap) return -1;
    memcpy(out,f->output,f->output_len+1);
    return (int)f->output_len;
}
