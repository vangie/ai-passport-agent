#ifndef PASSPORT_REPLY_H
#define PASSPORT_REPLY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define PASSPORT_RUN_ID_MAX 80
#define PASSPORT_REPLY_SOFT_MS 180000ULL
#define PASSPORT_REPLY_MAX_MS 600000ULL
#define PASSPORT_REPLY_POLL_MS 15000ULL
#define PASSPORT_REPLY_POLL_TIMEOUT_MS 20000ULL
/* One accepted request. Retained through network-worker cleanup, bound to the
 * recording and gateway identity so retry cannot read another agent's run. */
typedef struct {
    char request_id[33];
    char run_id[PASSPORT_RUN_ID_MAX];
    uint8_t scope[32];
    bool bound, acknowledged, complete, poll_pending;
    uint64_t next_poll_ms, poll_sent_ms;
} passport_reply_tracker_t;
void passport_reply_bind(passport_reply_tracker_t *t, const char *request_id, const uint8_t scope[32]);
bool passport_reply_resume(passport_reply_tracker_t *t, const char *run_id, const uint8_t scope[32]);
bool passport_reply_ack(passport_reply_tracker_t *t, const char *run_id);
bool passport_reply_matches(const passport_reply_tracker_t *t, const char *run_id);
bool passport_reply_terminal_text(const passport_reply_tracker_t *t, const char *run_id,
                               const char *role, const char *stop_reason, bool running);
bool passport_reply_poll_due(const passport_reply_tracker_t *t, uint64_t now_ms);
void passport_reply_poll_sent(passport_reply_tracker_t *t, uint64_t now_ms);
/* Live reply text from OpenClaw `chat` delta frames: append `delta`, or
 * replace everything when `replace` is set. Never splits a UTF-8 character;
 * text beyond `cap` is dropped. Returns true when `out` changed. */
bool passport_reply_live_apply(char *out, size_t cap, const char *delta, bool replace);
bool passport_reply_live_tail(char *out, size_t cap, const char *delta, bool replace);
#endif
