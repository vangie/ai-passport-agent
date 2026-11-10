#pragma once
#include "passport.h"
#include "esp_err.h"

typedef enum {
    PASSPORT_SEND_IDLE, PASSPORT_SEND_CONNECTING, PASSPORT_SEND_UPLOAD,
    PASSPORT_SEND_WAITING, PASSPORT_SEND_PARTIAL, PASSPORT_SEND_DONE, PASSPORT_SEND_FAILED, PASSPORT_SEND_CANCELLED,
    PASSPORT_SEND_LISTENING, PASSPORT_SEND_SPEAKING,
} passport_send_state_t;

/* Main-task ownership of start/poll; the network worker never calls LVGL. */
esp_err_t passport_gateway_send(passport_store_t *store);
/* Start connecting and authenticating while recording. The worker waits for
 * passport_gateway_send (upload) or passport_gateway_cancel (abandon). */
esp_err_t passport_gateway_preconnect(void);
esp_err_t passport_gateway_live(bool chinese);
esp_err_t passport_gateway_live_test(void);
void passport_gateway_drop_preconnect(void);
esp_err_t passport_gateway_probe(void);
typedef struct {
    bool gateway;
    bool ask;
    unsigned plugin;
    unsigned call;
} passport_gateway_check_t;
esp_err_t passport_gateway_check(void);
void passport_gateway_check_result(passport_gateway_check_t *result);
bool passport_gateway_has_pending_reply(void);
bool passport_gateway_cancel(void);
bool passport_gateway_abandon(void);
esp_err_t passport_gateway_resume_reply(const char *run_id);
esp_err_t passport_gateway_history_probe(void);
passport_send_state_t passport_gateway_poll(char *message, size_t cap);
void passport_gateway_transcript(char *text, size_t cap);
bool passport_gateway_active(void);
void passport_gateway_maintain(bool enabled);
bool passport_gateway_quiescent(void);
void passport_gateway_identity(char out[65]);
void passport_gateway_diagnostic(char *out, size_t cap);
void passport_gateway_timing_diagnostic(char *out, size_t cap);
