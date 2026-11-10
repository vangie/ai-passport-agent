#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include "passport_proxy.h"
void passport_tls_set_proxy(const passport_proxy_t *proxy);
const char *passport_tls_proxy_error(void);
esp_err_t passport_tls_control_prepare(void);
void passport_tls_control_begin(void);
bool passport_tls_cancelled(void);
void passport_tls_request_cancel(bool allow_remote_abort);
bool passport_tls_cancel_can_write(void);
void passport_tls_set_writing(bool writing);
void passport_tls_track_socket(int fd);
void passport_tls_forget_socket(void);
