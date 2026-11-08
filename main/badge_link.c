#include "badge.h"
#include "esp_wifi.h"
#include "badge_line.h"
#include "badge_memory.h"
#include "bsp_display.h"
#include "lvgl.h"
#include "esp_heap_caps.h"

#include "driver/usb_serial_jtag.h"
#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "badge_link";
static const ble_uuid128_t k_service = BLE_UUID128_INIT(
    0x6b, 0x1c, 0x8a, 0x2e, 0x4d, 0x10, 0x4a, 0x7e,
    0x9c, 0x33, 0x51, 0x70, 0x2a, 0x8e, 0xb4, 0xd1);
static const ble_uuid128_t k_rx = BLE_UUID128_INIT(
    0x6b, 0x1c, 0x8a, 0x2e, 0x4d, 0x10, 0x4a, 0x7e,
    0x9c, 0x33, 0x51, 0x70, 0x2a, 0x8e, 0xb4, 0xd2);
static const ble_uuid128_t k_tx = BLE_UUID128_INIT(
    0x6b, 0x1c, 0x8a, 0x2e, 0x4d, 0x10, 0x4a, 0x7e,
    0x9c, 0x33, 0x51, 0x70, 0x2a, 0x8e, 0xb4, 0xd3);

static badge_store_t *s_store;
static badge_scan_t s_scan[BADGE_SCAN_MAX];
static size_t s_scan_count;

size_t badge_wifi_scan(badge_scan_t *out, size_t cap);
const char *badge_wifi_join(const char *ssid, const char *pass, bool keep);
bool badge_nvs_save(const badge_store_t *store);
void badge_nvs_revert(badge_store_t *store);

bool badge_wifi_is_online(void);
static const char *join_wifi(void *ctx, const char *ssid, const char *pass, bool keep) {
    (void)ctx;
    /* Rejoining the network that already has an address only interrupts it.
     * Proxy and credential changes are stored by the caller either way. */
    /* Skip only when nothing about the join would change. A different
     * password must still be proven, or a typo is saved while the old
     * session stays up and the badge cannot reconnect later. */
    if (keep && badge_wifi_is_online()) {
        wifi_config_t current;
        if (esp_wifi_get_config(WIFI_IF_STA, &current) == ESP_OK &&
            !strcmp((const char *)current.sta.ssid, ssid) &&
            !strcmp((const char *)current.sta.password, pass)) return NULL;
    }
    return badge_wifi_join(ssid, pass, keep);
}
static uint16_t s_tx_handle;
static uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;
static uint8_t s_addr_type;
#define CMD_LINE_MAX BADGE_CONFIG_LINE_MAX
static badge_line_t s_ble_line;
static SemaphoreHandle_t s_lock;
/* 试连 Wi-Fi 要十几秒，命令交给工作任务，不堵蓝牙协议栈。 */
static QueueHandle_t s_lines;
static bool s_ble_ready;
static bool s_ble_initialized;
static bool s_ble_paused;
static bool s_ble_stopping;
static TaskHandle_t s_host_task;
static TaskHandle_t s_command_task;
static SemaphoreHandle_t s_host_stopped;
static char s_firmware_id[17];

static bool queue_line(char *line) {
    if (xQueueSend(s_lines, &line, 0) != pdTRUE) {
        free(line);
        return false;
    }
    return true;
}

static int reply(void *ctx, const char *line) {
    // A successful mutation is acknowledged only after NVS has committed it.
    if (ctx != NULL && strncmp(line, "OK ", 3) == 0) {
        snprintf((char *)ctx, 32, "%s", line);
        return 0;
    }
    char out[768];
    snprintf(out, sizeof(out), "%s\n", line);
    /* The USB driver's 256-byte TX ring rejects a larger atomic write. */
    size_t length=strlen(out);
    for (size_t at=0;at<length;) {
        size_t chunk=length-at < 64 ? length-at : 64;
        int n=usb_serial_jtag_write_bytes(out+at,chunk,pdMS_TO_TICKS(50));
        if (n<=0) break;
        at+=(size_t)n;
    }
    if (length>192) (void)usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(50));
    if (s_conn == BLE_HS_CONN_HANDLE_NONE) {
        return 0;
    }
    struct os_mbuf *om = ble_hs_mbuf_from_flat(line, strlen(line));
    if (om == NULL) {
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    os_mbuf_append(om, "\n", 1);
    return ble_gatts_notify_custom(s_conn, s_tx_handle, om);
}

bool badge_app_diagnostic_request(bool audio_test);
bool badge_app_battery_request(void);
bool badge_app_resume_reply_request(const char *run_id);
bool badge_app_restore_clip_request(const char *arguments);
bool badge_app_gateway_test(void);
bool badge_app_live_test(void);
bool badge_app_memory_test(bool history, bool enable);
bool badge_app_key_request(badge_btn_t key, badge_press_t press);

/* UI and USB mutations share the same transaction lock and rollback buffer. */
bool badge_link_apply_selection(badge_action_t action, int index, uint32_t expected_generation) {
    if (index < 0 || !s_store || !s_lock || xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    if (s_store->generation != expected_generation) { xSemaphoreGive(s_lock); return false; }
    bool ok = action == BADGE_ACT_PRIORITIZE ? badge_store_promote(s_store, (size_t)index)
            : action == BADGE_ACT_SELECT_AGENT && badge_agent_select(s_store, (size_t)index);
    if (ok) { badge_store_bump(s_store); ok = badge_nvs_save(s_store); }
    if (!ok) badge_nvs_revert(s_store);
    xSemaphoreGive(s_lock);
    return ok;
}

bool badge_link_agent_snapshot(badge_agent_t *out) {
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    const badge_agent_t *agent = badge_agent_active(s_store);
    if (agent) *out = *agent;
    xSemaphoreGive(s_lock);
    return agent != NULL;
}

bool badge_link_store_token(const badge_agent_t *expected, const char *token) {
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool ok = false;
    for (size_t i = 0; i < s_store->agent_count; i++) {
        badge_agent_t *agent = &s_store->agents[i];
        if (strcmp(agent->url, expected->url) || strcmp(agent->bootstrap, expected->bootstrap) ||
            strcmp(agent->device_token, expected->device_token)) continue;
        snprintf(agent->device_token, sizeof(agent->device_token), "%s", token);
        memset(agent->bootstrap, 0, sizeof(agent->bootstrap));
        badge_store_bump(s_store);
        ok = badge_nvs_save(s_store);
        if (!ok) badge_nvs_revert(s_store);
        break;
    }
    xSemaphoreGive(s_lock);
    return ok;
}

void badge_link_diagnostic_reply(const char *line) {
    reply(NULL, line);
}

static void handle_line(const char *line) {
    if (!strcmp(line, "LIVE_TEST")) {
        reply(NULL, badge_app_live_test() ? "LIVE_TEST synthetic queued" : "ERR busy");
        return;
    }
    if (!strcmp(line,"BLE_TEST ON") || !strcmp(line,"BLE_TEST OFF") || !strcmp(line,"HISTORY_TEST")) {
        bool history=!strcmp(line,"HISTORY_TEST");
        reply(NULL,badge_app_memory_test(history,!strcmp(line,"BLE_TEST ON")) ? "MEM_TEST queued" : "ERR busy");
        return;
    }

    if (!strcmp(line,"MEM") || !strcmp(line,"MEM RESET")) {
        if (!strcmp(line,"MEM RESET")) badge_memory_failures_reset();
        char out[512]; badge_memory_format(out,sizeof(out),"command"); reply(NULL,out);
        if (bsp_lvgl_lock(50)) {
            lv_mem_monitor_t mem; lv_mem_monitor(&mem); bsp_lvgl_unlock();
            snprintf(out,sizeof(out),"MEM lv_total=%u lv_free=%u lv_max_used=%u lv_largest=%u",
                     (unsigned)mem.total_size,(unsigned)mem.free_size,(unsigned)mem.max_used,(unsigned)mem.free_biggest_size);
            reply(NULL,out);
        }
        reply(NULL,heap_caps_check_integrity_all(false) ? "MEM integrity=ok" : "MEM integrity=failed");
        snprintf(out,sizeof(out),"MEM command_stack_free=%u",(unsigned)uxTaskGetStackHighWaterMark(s_command_task));
        reply(NULL,out);
        return;
    }

    if (!strncmp(line,"RESTORE_CLIP ",13)) {
        reply(NULL,badge_app_restore_clip_request(line+13) ? "RESTORE_CLIP queued" : "ERR busy or metadata");
        return;
    }
    if (!strncmp(line,"RESUME_REPLY ",13)) {
        reply(NULL,badge_app_resume_reply_request(line+13) ? "RESUME_REPLY queued" : "ERR busy or run id");
        return;
    }
    if (!strcmp(line, "KEY OK") || !strcmp(line, "KEY UP") ||
        !strcmp(line, "KEY DOWN") || !strcmp(line, "KEY BACK")) {
        badge_btn_t key = !strcmp(line, "KEY UP") ? BADGE_BTN_UP
                         : !strcmp(line, "KEY DOWN") ? BADGE_BTN_DOWN : BADGE_BTN_OK;
        badge_press_t press = BADGE_CLICK; /* Legacy BACK alias now presses the visible OK action. */
        reply(NULL, badge_app_key_request(key, press) ? "KEY queued" : "ERR busy");
        return;
    }
    if (strcmp(line, "GATEWAY_TEST") == 0) {
        reply(NULL, badge_app_gateway_test() ? "GATEWAY_TEST queued" : "ERR busy");
        return;
    }
    if (strcmp(line, "DIAG") == 0 || strcmp(line, "AUDIO_TEST") == 0) {
        if (!badge_app_diagnostic_request(strcmp(line, "AUDIO_TEST") == 0)) {
            reply(NULL, "ERR busy");
        }
        return;
    }
    if (strcmp(line, "BATT") == 0) {
        if (!badge_app_battery_request()) reply(NULL, "ERR busy");
        return;
    }
    badge_usb_t usb;
    memset(&usb, 0, sizeof(usb));
    usb.store = s_store;
    usb.build = s_firmware_id;
    usb.scan = s_scan;
    usb.scan_count = s_scan_count;
    usb.reply = reply;
    char pending_reply[32] = {0};
    usb.reply_ctx = pending_reply;
    usb.join = join_wifi;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (strcmp(line, "SCAN") == 0) {
        s_scan_count = badge_wifi_scan(s_scan, BADGE_SCAN_MAX);
        usb.scan_count = s_scan_count;
    }
    uint32_t generation = s_store->generation;
    badge_usb_line(&usb, line);
    if (s_store->generation != generation) {
        if (!badge_nvs_save(s_store)) {
            badge_nvs_revert(s_store);
            snprintf(pending_reply, sizeof(pending_reply), "ERR storage");
        }
    }
    if (pending_reply[0] != '\0') { reply(NULL, pending_reply); }
    xSemaphoreGive(s_lock);
    char cmd[8] = {0};
    sscanf(line, "%7s", cmd);
    ESP_LOGI(TAG, "cmd %s", cmd);
}

/* 配置和日志共用 USB。这里只收一整行命令，不走控制台。 */
static void usb_task(void *arg) {
    (void)arg;
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    if (usb_serial_jtag_driver_install(&cfg) != ESP_OK) {
        vTaskDelete(NULL);
        return;
    }
    badge_line_t buffer = {0};
    uint8_t byte = 0;
    while (1) {
        int n = usb_serial_jtag_read_bytes(&byte, 1, pdMS_TO_TICKS(200));
        if (n != 1) {
            continue;
        }
        char *line = NULL;
        badge_line_result_t result = badge_line_push(&buffer, byte, CMD_LINE_MAX, &line);
        if (result == BADGE_LINE_READY) {
            badge_memory_report("usb_line");
            if (!queue_line(line)) reply(NULL,"ERR busy");
        }
        else if (result == BADGE_LINE_TOO_LONG) { reply(NULL, "ERR too_long"); }
        else if (result == BADGE_LINE_NO_MEMORY) { reply(NULL, "ERR busy"); }
    }
}

static void line_task(void *arg) {
    (void)arg;
    char *line;
    while (1) {
        if (xQueueReceive(s_lines, &line, portMAX_DELAY) == pdTRUE) {
            handle_line(line);
            free(line);
        }
    }
}

static int gap_event(struct ble_gap_event *event, void *arg);

static int advertise(void) {
    if (s_ble_paused) return 0;
    struct ble_hs_adv_fields fields = {0};
    const char *name = "Badge";
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (const uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;
    fields.uuids128 = (ble_uuid128_t *)&k_service;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        return rc;
    }
    struct ble_gap_adv_params params = {0};
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    return ble_gap_adv_start(s_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
}

static int gap_event(struct ble_gap_event *event, void *arg) {
    (void)arg;
    if (event->type == BLE_GAP_EVENT_CONNECT) {
        if (event->connect.status == 0) {
            s_conn = event->connect.conn_handle;
            badge_line_reset(&s_ble_line);
        } else {
            advertise();
        }
    } else if (event->type == BLE_GAP_EVENT_DISCONNECT) {
        s_conn = BLE_HS_CONN_HANDLE_NONE;
        badge_line_reset(&s_ble_line);
        advertise();
    } else if (event->type == BLE_GAP_EVENT_ADV_COMPLETE) {
        advertise();
    }
    return 0;
}

static int on_rx(uint16_t conn_handle, uint16_t attr_handle,
                 struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    uint8_t buf[256];
    if (len > sizeof(buf)) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    ble_hs_mbuf_to_flat(ctxt->om, buf, len, NULL);
    for (uint16_t i = 0; i < len; i++) {
        char *line = NULL;
        badge_line_result_t result = badge_line_push(&s_ble_line, buf[i], CMD_LINE_MAX, &line);
        if (result == BADGE_LINE_READY) {
            badge_memory_report("usb_line");
            if (!queue_line(line)) reply(NULL,"ERR busy");
        }
        else if (result == BADGE_LINE_TOO_LONG) { reply(NULL, "ERR too_long"); }
        else if (result == BADGE_LINE_NO_MEMORY) { reply(NULL, "ERR busy"); }
    }
    return 0;
}

static const struct ble_gatt_svc_def k_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &k_service.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &k_rx.u,
                .access_cb = on_rx,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = &k_tx.u,
                .val_handle = &s_tx_handle,
                .flags = BLE_GATT_CHR_F_NOTIFY,
            },
            {0},
        },
    },
    {0},
};

static void on_sync(void) {
    ble_hs_util_ensure_addr(0);
    ble_hs_id_infer_auto(0, &s_addr_type);
    ble_gatts_count_cfg(k_svcs);
    ble_gatts_add_svcs(k_svcs);
    s_ble_ready = true;
    advertise();
}

static void host_task(void *arg) {
    (void)arg;
    nimble_port_run();
    badge_memory_report("ble_host_stopped");
    xSemaphoreGive(s_host_stopped);
    vTaskSuspend(NULL);
}

esp_err_t badge_link_resume_bluetooth(void) {
    if (s_ble_initialized) return s_ble_stopping ? ESP_ERR_INVALID_STATE : ESP_OK;
    if (s_host_stopped == NULL) s_host_stopped = xSemaphoreCreateBinary();
    if (!s_host_stopped) return ESP_ERR_NO_MEM;
    (void)xSemaphoreTake(s_host_stopped, 0);
    ESP_LOGI(TAG, "BLE resume: controller init");
    esp_err_t err = nimble_port_init();
    ESP_LOGI(TAG, "BLE resume: controller result %d", err);
    if (err != ESP_OK) return err;
    s_ble_initialized = true;
    s_ble_paused = false;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ESP_LOGI(TAG, "BLE resume: services ready");
    ble_svc_gap_device_name_set("Badge");
    ble_hs_cfg.sync_cb = on_sync;
    if (xTaskCreate(host_task, "nimble", 6144, NULL, 5, &s_host_task) != pdPASS) {
        s_host_task = NULL;
        err = nimble_port_deinit();
        if (err == ESP_OK) s_ble_initialized = false;
        return ESP_ERR_NO_MEM;
    }
    badge_memory_report("ble_on");
    ESP_LOGI(TAG, "BLE resume: host started");
    return ESP_OK;
}

esp_err_t badge_link_pause_bluetooth(void) {
    if (!s_ble_initialized) return ESP_OK;
    s_ble_paused = true;
    s_ble_ready = false;
    if (!s_ble_stopping) {
        (void)ble_gap_adv_stop();
        if (s_conn != BLE_HS_CONN_HANDLE_NONE) (void)ble_gap_terminate(s_conn, BLE_ERR_REM_USER_CONN_TERM);
        if (nimble_port_stop() != 0) return ESP_FAIL;
        s_ble_stopping = true;
    }
    if (s_host_task) {
        if (xSemaphoreTake(s_host_stopped, pdMS_TO_TICKS(2000)) != pdTRUE) return ESP_ERR_TIMEOUT;
        vTaskDelete(s_host_task);
        s_host_task = NULL;
    }
    esp_err_t err = nimble_port_deinit();
    if (err == ESP_OK) {
        s_ble_initialized = false; s_ble_stopping = false;
        s_conn = BLE_HS_CONN_HANDLE_NONE;
        badge_line_reset(&s_ble_line);
    }
    badge_memory_report("ble_off");
    return err;
}

void badge_link_start(badge_store_t *store) {
    s_store = store;
    const esp_app_desc_t *description = esp_app_get_description();
    for (size_t i = 0; i < 8; i++) {
        snprintf(s_firmware_id + i * 2, 3, "%02x", description->app_elf_sha256[i]);
    }
    s_lock = xSemaphoreCreateMutex();
    s_lines = xQueueCreate(4, sizeof(char *));
    if (s_lock == NULL || s_lines == NULL ||
        xTaskCreate(line_task, "badge_cmd", 12288, NULL, 4, &s_command_task) != pdPASS ||
        xTaskCreate(usb_task, "badge_usb", 4096, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "command task failed");
        return;
    }
    /* Wi-Fi is the runtime transport. Start BLE only when there is no saved
     * network; the app opens it on demand for phone configuration. */
    if (store == NULL || store->count == 0) {
        esp_err_t err = badge_link_resume_bluetooth();
        if (err != ESP_OK) ESP_LOGE(TAG, "nimble init failed: %s", esp_err_to_name(err));
    }
}

bool badge_link_is_bluetooth_ready(void) {
    return s_ble_ready;
}

/* True also during startup or a failed stop: policy must finish cleanup. */
bool badge_link_is_bluetooth_active(void) { return s_ble_initialized; }
