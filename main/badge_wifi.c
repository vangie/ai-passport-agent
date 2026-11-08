#include "badge.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "badge_wifi";
static const int k_connected = BIT0;
static const int k_failed = BIT1;

static esp_netif_t *s_netif;
static EventGroupHandle_t s_events;
static bool s_started;
static bool s_online;
static volatile uint8_t s_reason;
static SemaphoreHandle_t s_operation_lock;
static badge_scan_t s_scan[BADGE_SCAN_MAX];
static size_t s_scan_count;

esp_err_t badge_wifi_prepare(void) {
    s_operation_lock = xSemaphoreCreateMutex();
    return s_operation_lock != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}

static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event = data;
        s_reason = event != NULL ? event->reason : 0;
        ESP_LOGW(TAG, "station disconnected reason=%u", (unsigned)s_reason);
        s_online = false;
        xEventGroupSetBits(s_events, k_failed);
    }
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_online = true;
        xEventGroupSetBits(s_events, k_connected);
    }
}

static const char *reason_text(uint8_t reason) {
    switch (reason) {
        case WIFI_REASON_NO_AP_FOUND:
        case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
            return "notfound";
        case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
        case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
            return "security";
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_MIC_FAILURE:
            return "pass";
        case WIFI_REASON_AUTH_EXPIRE:
            return "timeout";
        default:
            return "fail";
    }
}

/* 先离开当前网络，等它的断开事件过去，免得被当成这次试连失败。 */
static void leave(void) {
    xEventGroupClearBits(s_events, k_failed);
    /* A timed-out station may still be authenticating without an IP. Cancel
     * that attempt too, before changing config or starting another scan. */
    if (esp_wifi_disconnect() == ESP_OK) {
        xEventGroupWaitBits(s_events, k_failed, pdFALSE, pdFALSE, pdMS_TO_TICKS(2000));
    }
    s_online = false;
}

static const char *attempt(const wifi_config_t *cfg) {
    leave();
    xEventGroupClearBits(s_events, k_connected | k_failed);
    s_reason = 0;
    wifi_config_t copy = *cfg;
    if (esp_wifi_set_config(WIFI_IF_STA, &copy) != ESP_OK || esp_wifi_connect() != ESP_OK) {
        return "fail";
    }
    /* Leave the first DHCP exchange alone. Restarting it every second made
     * this AP drop the station before a lease could finish. */
    EventBits_t bits = xEventGroupWaitBits(s_events, k_connected | k_failed, pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(10000));
    if (bits & k_connected) return NULL;
    if (bits & k_failed) return reason_text(s_reason);
    if (s_netif) {
        esp_netif_dhcpc_stop(s_netif);
        esp_netif_dhcpc_start(s_netif);
    }
    bits = xEventGroupWaitBits(s_events, k_connected | k_failed, pdFALSE, pdFALSE,
                               pdMS_TO_TICKS(5000));
    if (bits & k_connected) return NULL;
    return (bits & k_failed) ? reason_text(s_reason) : "timeout";
}

static esp_err_t start_once(void) {
    if (s_started) {
        return ESP_OK;
    }
    if (s_events == NULL) { s_events = xEventGroupCreate(); }
    if (s_events == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) { return err; }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    bool initialized = false;
    bool wifi_handler = false;
    bool ip_handler = false;
    esp_netif_config_t netif = ESP_NETIF_DEFAULT_WIFI_STA();
    s_netif = esp_netif_new(&netif);
    if (s_netif == NULL) { return ESP_ERR_NO_MEM; }
    err = esp_netif_attach_wifi_station(s_netif);
    if (err != ESP_OK) { goto fail; }
    err = esp_wifi_set_default_wifi_sta_handlers();
    if (err != ESP_OK) { goto fail; }
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    // C3 has no PSRAM: leave room for BLE, LVGL and the USB command worker.
    cfg.static_rx_buf_num = 4;
    cfg.dynamic_rx_buf_num = 8;
    cfg.tx_buf_type = 1;
    cfg.dynamic_tx_buf_num = 8;
    cfg.cache_tx_buf_num = 0;
    cfg.mgmt_sbuf_num = 8;
    cfg.rx_ba_win = 4;
    cfg.nvs_enable = 0;
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) { goto fail; }
    initialized = true;
    err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi, NULL);
    if (err != ESP_OK) { goto fail; }
    wifi_handler = true;
    err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi, NULL);
    if (err != ESP_OK) { goto fail; }
    ip_handler = true;
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) { goto fail; }
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) { goto fail; }
    err = esp_wifi_start();
    if (err != ESP_OK) { goto fail; }
    s_started = true;
    return ESP_OK;
fail:
    if (ip_handler) { esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi); }
    if (wifi_handler) { esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi); }
    if (initialized) { esp_wifi_deinit(); }
    esp_netif_destroy_default_wifi(s_netif);
    s_netif = NULL;
    ESP_LOGE(TAG, "Wi-Fi initialization failed: %s", esp_err_to_name(err));
    return err;
}

size_t badge_wifi_scan(badge_scan_t *out, size_t cap) {
    if (s_operation_lock == NULL || out == NULL || cap == 0) {
        return 0;
    }
    xSemaphoreTake(s_operation_lock, portMAX_DELAY);
    if (start_once() != ESP_OK) {
        xSemaphoreGive(s_operation_lock);
        return 0;
    }
    if (esp_wifi_scan_start(NULL, true) != ESP_OK) {
        xSemaphoreGive(s_operation_lock);
        return s_scan_count;
    }
    wifi_ap_record_t records[BADGE_SCAN_MAX];
    uint16_t count = BADGE_SCAN_MAX;
    if (esp_wifi_scan_get_ap_records(&count, records) != ESP_OK) {
        xSemaphoreGive(s_operation_lock);
        return 0;
    }
    s_scan_count = count < cap ? count : cap;
    for (size_t i = 0; i < s_scan_count; i++) {
        snprintf(out[i].ssid, sizeof(out[i].ssid), "%s", (const char *)records[i].ssid);
        out[i].rssi = records[i].rssi;
        out[i].has_bssid = true;
        memcpy(out[i].bssid, records[i].bssid, sizeof(out[i].bssid));
        s_scan[i] = out[i];
    }
    ESP_LOGI(TAG, "scan %u", (unsigned)s_scan_count);
    xSemaphoreGive(s_operation_lock);
    return s_scan_count;
}

/* keep 为 false，或者没连上时，回到原来的网络。返回 NULL 表示连上。 */
const char *badge_wifi_join(const char *ssid, const char *pass, bool keep) {
    if (ssid == NULL || ssid[0] == '\0') {
        return "notfound";
    }
    if (s_operation_lock == NULL) {
        return "fail";
    }
    xSemaphoreTake(s_operation_lock, portMAX_DELAY);
    if (start_once() != ESP_OK) {
        xSemaphoreGive(s_operation_lock);
        return "fail";
    }
    wifi_config_t old = {0};
    bool was_online = s_online;
    esp_wifi_get_config(WIFI_IF_STA, &old);

    wifi_config_t cfg = {0};
    snprintf((char *)cfg.sta.ssid, sizeof(cfg.sta.ssid), "%s", ssid);
    snprintf((char *)cfg.sta.password, sizeof(cfg.sta.password), "%s", pass != NULL ? pass : "");
    // The AP may advertise WPA2/WPA3 transition or another supported mode.
    // The password still authenticates the connection; this threshold only
    // prevents rejecting a valid AP before the handshake.
    cfg.sta.threshold.authmode = pass != NULL && pass[0] != '\0'
        ? WIFI_AUTH_WPA_WPA2_PSK : WIFI_AUTH_OPEN;
    cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    cfg.sta.failure_retry_cnt = 5;
    cfg.sta.pmf_cfg.capable = true;
    cfg.sta.pmf_cfg.required = false;
    cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    // Do not pin a BSSID: this SSID is provided by multiple APs and may roam
    // between them. Let the station choose the compatible BSSID.
    cfg.sta.bssid_set = false;
    const char *why = attempt(&cfg);
    ESP_LOGI(TAG, "join %s %s", ssid, why != NULL ? why : "ok");

    if (keep && why == NULL) {
        xSemaphoreGive(s_operation_lock);
        return NULL;
    }
    if (was_online) {
        attempt(&old);
    } else {
        leave();
        esp_wifi_set_config(WIFI_IF_STA, &old);
    }
    xSemaphoreGive(s_operation_lock);
    return why;
}

bool badge_wifi_is_online(void) {
    return s_online;
}

esp_err_t badge_wifi_gateway_performance(bool enabled) {
    if (!s_started) return ESP_ERR_INVALID_STATE;
    esp_err_t err = esp_wifi_set_ps(enabled ? WIFI_PS_NONE : WIFI_PS_MIN_MODEM);
    ESP_LOGI(TAG, "gateway Wi-Fi power mode=%s result=%s",
             enabled ? "performance" : "power-save", esp_err_to_name(err));
    return err;
}

static void autoconnect_task(void *arg) {
    badge_store_t *store = arg;
    while (store != NULL) {
        /* Own the radio for the whole pass. badge_wifi_join takes the same
         * lock, so calling it here would deadlock; use attempt() directly. */
        if (!s_online && store->count > 0 &&
            xSemaphoreTake(s_operation_lock, pdMS_TO_TICKS(1000)) == pdTRUE) {
            size_t count = store->count;
            int start_index = store->connected >= 0 && (size_t)store->connected < count ? store->connected : 0;
            for (size_t offset = 0; offset < count && !s_online; offset++) {
                size_t index = ((size_t)start_index + offset) % count;
                if (index >= store->count || !store->nets[index].enabled) continue;
                wifi_config_t cfg = {0};
                memcpy(cfg.sta.ssid, store->nets[index].ssid, sizeof(cfg.sta.ssid) - 1);
                memcpy(cfg.sta.password, store->nets[index].pass, sizeof(cfg.sta.password) - 1);
                cfg.sta.threshold.authmode = cfg.sta.password[0] ? WIFI_AUTH_WPA_WPA2_PSK : WIFI_AUTH_OPEN;
                cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
                cfg.sta.failure_retry_cnt = 5;
                cfg.sta.pmf_cfg.capable = true;
                cfg.sta.pmf_cfg.required = false;
                cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
                cfg.sta.bssid_set = false;
                if (start_once() != ESP_OK) break;
                const char *why = attempt(&cfg);
                ESP_LOGI(TAG, "join %s %s", (const char *)cfg.sta.ssid, why ? why : "ok");
                if (!why) {
                    if (index < store->count && !strcmp(store->nets[index].ssid, (const char *)cfg.sta.ssid))
                        store->connected = (int)index;
                    break;
                }
                ESP_LOGW(TAG, "saved network %s failed: %s; retrying later", (const char *)cfg.sta.ssid, why);
            }
            xSemaphoreGive(s_operation_lock);
        }
        vTaskDelay(pdMS_TO_TICKS(s_online ? 2000 : 5000));
    }
    vTaskDelete(NULL);
}

void badge_wifi_autoconnect(badge_store_t *store) {
    if (store == NULL || store->count == 0) {
        return;
    }
    if (xTaskCreate(autoconnect_task, "badge_wifi", 4096, store, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "saved Wi-Fi task failed");
    }
}
