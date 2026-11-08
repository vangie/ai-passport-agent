#include "badge_reply.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
static bool valid_run(const char *id) {
    if (!id || !*id || strlen(id) >= BADGE_RUN_ID_MAX) return false;
    for (const unsigned char *p=(const unsigned char *)id; *p; p++)
        if (!isalnum(*p) && *p!='-' && *p!='_') return false;
    return true;
}
void badge_reply_bind(badge_reply_tracker_t *t, const char *request_id, const uint8_t scope[32]) {
    if (t->bound && !strcmp(t->request_id,request_id) && !memcmp(t->scope,scope,32)) return;
    memset(t,0,sizeof(*t));t->bound=true;
    snprintf(t->request_id,sizeof(t->request_id),"%s",request_id);memcpy(t->scope,scope,32);
}
bool badge_reply_ack(badge_reply_tracker_t *t, const char *run_id) {
    if (!valid_run(run_id)) return false;
    if (t->acknowledged && strcmp(t->run_id,run_id)) return false;
    snprintf(t->run_id,sizeof(t->run_id),"%s",run_id);t->acknowledged=true;return true;
}
bool badge_reply_resume(badge_reply_tracker_t *t, const char *run_id, const uint8_t scope[32]) {
    if (!valid_run(run_id)) return false;
    memset(t,0,sizeof(*t));t->bound=true;memcpy(t->scope,scope,32);
    return badge_reply_ack(t,run_id);
}
bool badge_reply_matches(const badge_reply_tracker_t *t, const char *run_id) {
    return t->acknowledged && run_id && *run_id && !strcmp(t->run_id,run_id);
}
bool badge_reply_terminal_text(const badge_reply_tracker_t *t, const char *run_id,
                               const char *role, const char *stop_reason, bool running) {
    return !running && badge_reply_matches(t,run_id) && role && !strcmp(role,"assistant") &&
           stop_reason && (!strcmp(stop_reason,"stop") || !strcmp(stop_reason,"length"));
}
bool badge_reply_poll_due(const badge_reply_tracker_t *t, uint64_t now_ms) {
    return t->acknowledged && now_ms>=t->next_poll_ms &&
           (!t->poll_pending || now_ms-t->poll_sent_ms>=BADGE_REPLY_POLL_TIMEOUT_MS);
}
void badge_reply_poll_sent(badge_reply_tracker_t *t, uint64_t now_ms) {
    t->poll_pending=true;t->poll_sent_ms=now_ms;t->next_poll_ms=now_ms+BADGE_REPLY_POLL_MS;
}
static size_t utf8_unit(const unsigned char *p) {
    return *p < 0x80 ? 1 : (*p & 0xe0) == 0xc0 ? 2 : (*p & 0xf0) == 0xe0 ? 3 : (*p & 0xf8) == 0xf0 ? 4 : 1;
}
static bool live_apply(char *out, size_t cap, const char *delta, bool replace, bool tail) {
    if (!out || !cap) return false;
    size_t used = replace ? 0 : strnlen(out, cap - 1);
    size_t before = strnlen(out,cap-1);
    bool changed = false;
    for (const unsigned char *p = (const unsigned char *)(delta ? delta : ""); *p;) {
        size_t n = utf8_unit(p);
        if ((*p>=0x80 && n==1) || (*p<0xc2 && n>1) || *p>0xf4) break;
        bool valid=true;
        for (size_t index=1;index<n;index++) if ((p[index]&0xc0)!=0x80) { valid=false;break; }
        if (!valid || (n==3 && ((*p==0xe0 && p[1]<0xa0) || (*p==0xed && p[1]>=0xa0))) ||
            (n==4 && ((*p==0xf0 && p[1]<0x90) || (*p==0xf4 && p[1]>=0x90)))) break;
        if (used + n >= cap) {
            if (!tail) break;
            if (n >= cap) { p += n; continue; }
            size_t discard = 0;
            while (used - discard + n >= cap) discard += utf8_unit((const unsigned char *)out + discard);
            memmove(out, out + discard, used - discard);
            used -= discard;
            changed = true;
        }
        if (replace && memcmp(out+used,p,n)) changed=true;
        memcpy(out + used, p, n);
        used += n; p += n;
    }
    if (replace && out[used]) changed=true;
    out[used] = 0;
    return changed || used != before;
}

bool badge_reply_live_apply(char *out, size_t cap, const char *delta, bool replace) {
    return live_apply(out, cap, delta, replace, false);
}

bool badge_reply_live_tail(char *out, size_t cap, const char *delta, bool replace) {
    return live_apply(out, cap, delta, replace, true);
}
