#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include "esp_err.h"
typedef void *TaskHandle_t;
typedef int portMUX_TYPE;
typedef void *esp_transport_handle_t;
typedef int nvs_handle_t;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(guard) ((void)(guard))
#define portEXIT_CRITICAL(guard) ((void)(guard))
#define pdMS_TO_TICKS(ms) (ms)
#define pdPASS 1
#define pdTRUE 1
#define NVS_READWRITE 1
#define SNTP_OPMODE_POLL 1
#define ESP_LOGI(tag,...) do { (void)(tag);if (0) fprintf(stderr,__VA_ARGS__); } while (0)
#define ESP_LOGW(tag,...) ESP_LOGI(tag,__VA_ARGS__)
#define ESP_LOGD(tag,...) ESP_LOGI(tag,__VA_ARGS__)
enum { WS_TRANSPORT_OPCODES_CONT=0, WS_TRANSPORT_OPCODES_TEXT=1,
       WS_TRANSPORT_OPCODES_CLOSE=8, WS_TRANSPORT_OPCODES_PING=9,
       WS_TRANSPORT_OPCODES_PONG=10, WS_TRANSPORT_OPCODES_FIN=128 };
typedef struct { const char *ws_path; } passport_transport_ws_config_t;
int64_t esp_timer_get_time(void);
void esp_fill_random(void *out, size_t size);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
void vTaskDelay(unsigned ticks);
void vTaskPrioritySet(TaskHandle_t task, unsigned priority);
int xTaskCreate(void (*entry)(void *), const char *name, unsigned stack, void *arg, unsigned priority, TaskHandle_t *out);
void xTaskNotifyGive(TaskHandle_t task);
unsigned ulTaskNotifyTake(int clear, unsigned timeout);
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *out);
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *name, void *out, size_t *size);
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *name, const void *data, size_t size);
esp_err_t nvs_commit(nvs_handle_t handle);
void nvs_close(nvs_handle_t handle);
bool esp_sntp_enabled(void);
void esp_sntp_setoperatingmode(int mode);
void esp_sntp_setservername(int index, const char *name);
void esp_sntp_init(void);
esp_err_t esp_crt_bundle_attach(void *conf);
esp_transport_handle_t esp_transport_ssl_init(void);
esp_transport_handle_t esp_transport_tcp_init(void);
void esp_transport_ssl_crt_bundle_attach(esp_transport_handle_t handle, esp_err_t (*attach)(void *));
esp_transport_handle_t passport_transport_ws_init(esp_transport_handle_t parent);
esp_err_t passport_transport_ws_set_config(esp_transport_handle_t handle, const passport_transport_ws_config_t *config);
esp_err_t passport_transport_ws_set_headers(esp_transport_handle_t handle, const char *headers);
int esp_transport_connect(esp_transport_handle_t handle, const char *host, int port, int timeout);
int esp_transport_get_socket(esp_transport_handle_t handle);
int esp_transport_read(esp_transport_handle_t handle, char *out, int size, int timeout);
int esp_transport_poll_read(esp_transport_handle_t handle, int timeout);
int passport_transport_ws_get_read_opcode(esp_transport_handle_t handle);
int64_t passport_transport_ws_last_receive_us(esp_transport_handle_t handle);
const char *passport_transport_ws_failure(esp_transport_handle_t handle);
int passport_transport_ws_get_upgrade_request_status(esp_transport_handle_t handle);
int passport_transport_ws_send_raw(esp_transport_handle_t handle, int opcode, const char *data, int size, int timeout);
int esp_transport_close(esp_transport_handle_t handle);
int esp_transport_destroy(esp_transport_handle_t handle);
