#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include "badge_proxy.h"
void badge_tls_set_proxy(const badge_proxy_t *proxy);
const char *badge_tls_proxy_error(void);
esp_err_t badge_tls_control_prepare(void);
void badge_tls_control_begin(void);
bool badge_tls_cancelled(void);
void badge_tls_request_cancel(bool allow_remote_abort);
bool badge_tls_cancel_can_write(void);
void badge_tls_set_writing(bool writing);
void badge_tls_track_socket(int fd);
void badge_tls_forget_socket(void);
