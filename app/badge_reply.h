#ifndef BADGE_REPLY_H
#define BADGE_REPLY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define BADGE_RUN_ID_MAX 80
#define BADGE_REPLY_SOFT_MS 180000ULL
#define BADGE_REPLY_MAX_MS 600000ULL
#define BADGE_REPLY_POLL_MS 15000ULL
#define BADGE_REPLY_POLL_TIMEOUT_MS 20000ULL
/* One accepted request. Retained through network-worker cleanup, bound to the
 * recording and gateway identity so retry cannot read another agent's run. */
typedef struct {
    char request_id[33];
    char run_id[BADGE_RUN_ID_MAX];
    uint8_t scope[32];
    bool bound, acknowledged, complete, poll_pending;
    uint64_t next_poll_ms, poll_sent_ms;
} badge_reply_tracker_t;
void badge_reply_bind(badge_reply_tracker_t *t, const char *request_id, const uint8_t scope[32]);
bool badge_reply_resume(badge_reply_tracker_t *t, const char *run_id, const uint8_t scope[32]);
bool badge_reply_ack(badge_reply_tracker_t *t, const char *run_id);
bool badge_reply_matches(const badge_reply_tracker_t *t, const char *run_id);
bool badge_reply_terminal_text(const badge_reply_tracker_t *t, const char *run_id,
                               const char *role, const char *stop_reason, bool running);
bool badge_reply_poll_due(const badge_reply_tracker_t *t, uint64_t now_ms);
void badge_reply_poll_sent(badge_reply_tracker_t *t, uint64_t now_ms);
/* Live reply text from OpenClaw `chat` delta frames: append `delta`, or
 * replace everything when `replace` is set. Never splits a UTF-8 character;
 * text beyond `cap` is dropped. Returns true when `out` changed. */
bool badge_reply_live_apply(char *out, size_t cap, const char *delta, bool replace);
bool badge_reply_live_tail(char *out, size_t cap, const char *delta, bool replace);
#endif
