#pragma once
#include "badge.h"
#include "esp_err.h"

typedef enum {
    BADGE_SEND_IDLE, BADGE_SEND_CONNECTING, BADGE_SEND_UPLOAD,
    BADGE_SEND_WAITING, BADGE_SEND_PARTIAL, BADGE_SEND_DONE, BADGE_SEND_FAILED, BADGE_SEND_CANCELLED,
    BADGE_SEND_LISTENING, BADGE_SEND_SPEAKING,
} badge_send_state_t;

/* Main-task ownership of start/poll; the network worker never calls LVGL. */
esp_err_t badge_gateway_send(badge_store_t *store);
/* Start connecting and authenticating while recording. The worker waits for
 * badge_gateway_send (upload) or badge_gateway_cancel (abandon). */
esp_err_t badge_gateway_preconnect(void);
esp_err_t badge_gateway_live(bool chinese);
esp_err_t badge_gateway_live_test(void);
void badge_gateway_drop_preconnect(void);
esp_err_t badge_gateway_probe(void);
bool badge_gateway_has_pending_reply(void);
bool badge_gateway_cancel(void);
bool badge_gateway_abandon(void);
esp_err_t badge_gateway_resume_reply(const char *run_id);
esp_err_t badge_gateway_history_probe(void);
badge_send_state_t badge_gateway_poll(char *message, size_t cap);
bool badge_gateway_active(void);
void badge_gateway_maintain(bool enabled);
bool badge_gateway_quiescent(void);
void badge_gateway_identity(char out[65]);
void badge_gateway_diagnostic(char *out, size_t cap);
void badge_gateway_timing_diagnostic(char *out, size_t cap);
