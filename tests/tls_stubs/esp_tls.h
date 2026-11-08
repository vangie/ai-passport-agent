#pragma once
#include "esp_err.h"
#include <stdbool.h>
#define CONFIG_ESP_TLS_CLIENT_SESSION_TICKETS 1
typedef struct { int identity; } esp_tls_client_session_t;
typedef struct { int fd; int step; } esp_tls_t;
typedef enum { ESP_TLS_VER_ANY, ESP_TLS_VER_TLS_1_2, ESP_TLS_VER_TLS_1_3 } esp_tls_proto_ver_t;
typedef struct {
    int timeout_ms;
    bool non_block;
    const char *common_name;
    esp_tls_client_session_t *client_session;
    esp_err_t (*crt_bundle_attach)(void *);
    const unsigned char *cacert_buf;
    const unsigned char *clientcert_buf;
    const unsigned char *clientkey_buf;
    const char **alpn_protos;
    const int *ciphersuites_list;
    esp_tls_proto_ver_t tls_version;
    bool use_global_ca_store;
    bool skip_common_name;
    bool is_plain_tcp;
    bool use_secure_element;
    void *ds_data;
} esp_tls_cfg_t;
typedef enum { ESP_TLS_INIT, ESP_TLS_CONNECTING, ESP_TLS_HANDSHAKE, ESP_TLS_FAIL, ESP_TLS_DONE } esp_tls_conn_state_t;
typedef void *esp_tls_error_handle_t;
int esp_tls_conn_new_async(const char *,int,int,const esp_tls_cfg_t *,esp_tls_t *);
esp_err_t esp_tls_get_conn_sockfd(esp_tls_t *,int *);
esp_err_t esp_tls_get_conn_state(esp_tls_t *,esp_tls_conn_state_t *);
void *esp_tls_get_ssl_context(esp_tls_t *);
esp_tls_client_session_t *esp_tls_get_client_session(esp_tls_t *);
void esp_tls_free_client_session(esp_tls_client_session_t *);
