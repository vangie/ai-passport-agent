#include "passport.h"
#include "esp_wifi.h"
#include "passport_line.h"
#include "passport_ble_session.h"
#include "passport_memory.h"
#include "passport_power.h"
#include "passport_network.h"
#include <stdatomic.h>
#include "bsp_display.h"
#include "lvgl.h"
#include "esp_heap_caps.h"

#include "driver/usb_serial_jtag.h"
#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include "host/ble_gap.h"
#include "host/ble_att.h"
#include "host/ble_hs.h"
#include "host/ble_hs_id.h"
#include "host/ble_sm.h"
#include "host/ble_store.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "passport_link";
void ble_store_config_init(void);
static const ble_uuid128_t k_service = BLE_UUID128_INIT(
    0x6b, 0x1c, 0x8a, 0x2e, 0x4d, 0x10, 0x4a, 0x7e,
    0x9c, 0x33, 0x51, 0x70, 0x2a, 0x8e, 0xb4, 0xd1);
static const ble_uuid128_t k_rx = BLE_UUID128_INIT(
    0x6b, 0x1c, 0x8a, 0x2e, 0x4d, 0x10, 0x4a, 0x7e,
    0x9c, 0x33, 0x51, 0x70, 0x2a, 0x8e, 0xb4, 0xd2);
static const ble_uuid128_t k_tx = BLE_UUID128_INIT(
    0x6b, 0x1c, 0x8a, 0x2e, 0x4d, 0x10, 0x4a, 0x7e,
    0x9c, 0x33, 0x51, 0x70, 0x2a, 0x8e, 0xb4, 0xd3);

static passport_store_t *s_store;
static passport_scan_t s_scan[PASSPORT_SCAN_MAX];
static size_t s_scan_count;

int passport_wifi_scan(passport_scan_t *out, size_t cap,
                       int (*found)(void *, const passport_scan_t *), void *ctx);
const char *passport_wifi_join(const char *ssid, const char *pass, bool keep);
bool passport_nvs_save(const passport_store_t *store);
void passport_nvs_revert(passport_store_t *store);

bool passport_wifi_is_online(void);
static const char *join_wifi(void *ctx, const char *ssid, const char *pass, bool keep) {
    (void)ctx;
    /* Rejoining the network that already has an address only interrupts it.
     * Proxy and credential changes are stored by the caller either way. */
    /* Skip only when nothing about the join would change. A different
     * password must still be proven, or a typo is saved while the old
     * session stays up and the passport cannot reconnect later. */
    if (keep && passport_wifi_is_online()) {
        wifi_config_t current;
        if (esp_wifi_get_config(WIFI_IF_STA, &current) == ESP_OK &&
            !strncmp((const char *)current.sta.ssid, ssid, sizeof(current.sta.ssid)) &&
            !strncmp((const char *)current.sta.password, pass, sizeof(current.sta.password))) return NULL;
    }
    return passport_wifi_join(ssid, pass, keep);
}
static uint16_t s_tx_handle;
static uint8_t s_addr_type;
static char s_ble_name[17];
#define CMD_LINE_MAX PASSPORT_CONFIG_LINE_MAX
static passport_ble_session_t s_ble_sessions;
static passport_line_t s_ble_lines[PASSPORT_BLE_PEERS];
static portMUX_TYPE s_ble_state_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_command_session;
typedef struct {
    char *line;
    uint32_t session;
} passport_link_command_t;
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
static atomic_uint s_configuration_pending;
static atomic_uint s_configuration_activity;
static atomic_bool s_configuration_connected;
static atomic_int s_network_state;
static _Atomic(char *) s_network_command;
static int s_network_index;
static uint32_t s_network_generation;
static SemaphoreHandle_t s_ble_transmit_lock;
static SemaphoreHandle_t s_ble_transmitted;
static atomic_int s_ble_transmit_status;
static atomic_uint_least16_t s_ble_transmitting = BLE_HS_CONN_HANDLE_NONE;
static atomic_uint s_async_configuration;

static bool configuration_line(const char *line) {
    return strcmp(line, "HELLO") && strcmp(line, "DIAG") && strcmp(line, "MEM") &&
        strcmp(line, "MEM RESET") && strcmp(line, "BATT") && strcmp(line, "POWER") &&
        strncmp(line, "KEY ", 4);
}

bool passport_link_configuration_busy(void) {
    return atomic_load(&s_configuration_pending) || atomic_load(&s_async_configuration) || atomic_load(&s_configuration_connected) ||
        atomic_load(&s_network_state) == PASSPORT_NETWORK_CONNECTING;
}

bool passport_link_configuration_pending(void) {
    return atomic_load(&s_configuration_pending) || atomic_load(&s_async_configuration) ||
        atomic_load(&s_network_state) == PASSPORT_NETWORK_CONNECTING;
}

unsigned passport_link_configuration_activity(void) {
    return atomic_load(&s_configuration_activity);
}

static bool queue_command(char *line, uint32_t session) {
    passport_power_command_begin();
    bool configuring = configuration_line(line);
    if (configuring) {
        atomic_fetch_add(&s_configuration_pending, 1);
        atomic_fetch_add(&s_configuration_activity, 1);
    }
    passport_link_command_t command = { .line = line, .session = session };
    if (xQueueSend(s_lines, &command, 0) != pdTRUE) {
        if (configuring) atomic_fetch_sub(&s_configuration_pending, 1);
        passport_power_command_end();
        char *expected = line;
        (void)atomic_compare_exchange_strong(&s_network_command, &expected, NULL);
        free(line);
        return false;
    }
    return true;
}

static bool queue_line(char *line) { return queue_command(line, 0); }

static uint16_t session_handle(uint32_t serial) {
    portENTER_CRITICAL(&s_ble_state_lock);
    passport_ble_peer_t *peer = passport_ble_serial(&s_ble_sessions, serial);
    uint16_t handle = peer ? peer->handle : BLE_HS_CONN_HANDLE_NONE;
    portEXIT_CRITICAL(&s_ble_state_lock);
    return handle;
}

static int bluetooth_reply(uint32_t serial, const char *out, size_t length, unsigned timeout_ms) {
    if (!serial || !s_ble_transmit_lock ||
        xSemaphoreTake(s_ble_transmit_lock, pdMS_TO_TICKS(2000)) != pdTRUE) return 0;
    uint16_t connection = session_handle(serial);
    struct ble_gap_conn_desc description;
    int result = connection == BLE_HS_CONN_HANDLE_NONE ? BLE_HS_ENOTCONN : ble_gap_conn_find(connection, &description);
    if (result == 0 && !description.sec_state.encrypted) result = BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    if (result == 0) {
        atomic_store(&s_ble_transmitting, connection);
        uint16_t mtu = ble_att_mtu(connection);
        size_t payload = mtu > 3 ? mtu - 3 : 20;
        for (size_t offset = 0; offset < length;) {
            size_t count = length - offset < payload ? length - offset : payload;
            struct os_mbuf *packet = ble_hs_mbuf_from_flat(out + offset, count);
            if (!packet) { result = BLE_ATT_ERR_INSUFFICIENT_RES; break; }
            (void)xSemaphoreTake(s_ble_transmitted, 0);
            atomic_store(&s_ble_transmit_status, BLE_HS_ETIMEOUT);
            result = ble_gatts_indicate_custom(connection, s_tx_handle, packet);
            if (result != 0) break;
            if (xSemaphoreTake(s_ble_transmitted, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
                result = BLE_HS_ETIMEOUT;
                break;
            }
            result = atomic_load(&s_ble_transmit_status);
            if (result != 0 || session_handle(serial) != connection) break;
            offset += count;
        }
        atomic_store(&s_ble_transmitting, BLE_HS_CONN_HANDLE_NONE);
    }
    xSemaphoreGive(s_ble_transmit_lock);
    if (result != 0 && connection != BLE_HS_CONN_HANDLE_NONE)
        (void)ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
    return result;
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
    portENTER_CRITICAL(&s_ble_state_lock);
    uint32_t serial = s_ble_sessions.owner;
    portEXIT_CRITICAL(&s_ble_state_lock);
    if (xTaskGetCurrentTaskHandle() == s_command_task && s_command_session) serial = s_command_session;
    return bluetooth_reply(serial, out, length, 3000);
}

bool passport_app_diagnostic_request(bool audio_test);
bool passport_app_battery_request(void);
bool passport_app_power_request(void);
bool passport_app_resume_reply_request(const char *run_id);
bool passport_app_restore_clip_request(const char *arguments);
bool passport_app_gateway_test(void);
bool passport_app_live_test(void);
bool passport_app_memory_test(bool history, bool enable);
bool passport_app_key_request(passport_btn_t key, passport_press_t press);
bool passport_app_control_request(const char *line);
bool passport_app_device_busy(void);

bool passport_link_connect_network(int index, uint32_t generation) {
    if (!s_lines || index < 0 || index >= PASSPORT_NET_MAX) return false;
    int expected = PASSPORT_NETWORK_IDLE;
    if (!atomic_compare_exchange_strong(&s_network_state, &expected, PASSPORT_NETWORK_CONNECTING)) return false;
    char *line = malloc(1);
    if (line) {
        line[0] = '\0';
        atomic_store(&s_network_command, line);
        s_network_index = index;
        s_network_generation = generation;
        if (queue_line(line)) return true;
    }
    atomic_store(&s_network_command, NULL);
    atomic_store(&s_network_state, PASSPORT_NETWORK_IDLE);
    return false;
}

passport_network_state_t passport_link_network_result(void) {
    int state = atomic_load(&s_network_state);
    if (state != PASSPORT_NETWORK_CONNECTING)
        atomic_compare_exchange_strong(&s_network_state, &state, PASSPORT_NETWORK_IDLE);
    return (passport_network_state_t)state;
}

static bool save_network(void *context, const passport_store_t *store) {
    (void)context;
    return passport_nvs_save(store);
}

/* UI and USB mutations share the same transaction lock and rollback buffer. */
bool passport_link_apply_selection(passport_action_t action, int index, uint32_t expected_generation) {
    if (index < 0 || !s_store || !s_lock || xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    if (s_store->generation != expected_generation) { xSemaphoreGive(s_lock); return false; }
    bool ok = action == PASSPORT_ACT_SELECT_AGENT && passport_agent_select(s_store, (size_t)index);
    if (ok) { passport_store_bump(s_store); ok = passport_nvs_save(s_store); }
    if (!ok) passport_nvs_revert(s_store);
    xSemaphoreGive(s_lock);
    return ok;
}

bool passport_link_agent_snapshot(passport_agent_t *out) {
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    const passport_agent_t *agent = passport_agent_active(s_store);
    if (agent) *out = *agent;
    xSemaphoreGive(s_lock);
    return agent != NULL;
}

bool passport_link_store_token(const passport_agent_t *expected, const char *token, const char *kind) {
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool ok = false;
    for (size_t i = 0; i < s_store->agent_count; i++) {
        passport_agent_t *agent = &s_store->agents[i];
        if (strcmp(agent->url, expected->url) || strcmp(agent->bootstrap, expected->bootstrap) ||
            strcmp(agent->device_token, expected->device_token)) continue;
        snprintf(agent->device_token, sizeof(agent->device_token), "%s", token);
        snprintf(agent->kind, sizeof(agent->kind), "%s", kind);
        memset(agent->bootstrap, 0, sizeof(agent->bootstrap));
        passport_store_bump(s_store);
        ok = passport_nvs_save(s_store);
        if (!ok) passport_nvs_revert(s_store);
        break;
    }
    xSemaphoreGive(s_lock);
    return ok;
}

void passport_link_diagnostic_reply(const char *line) {
    reply(NULL, line);
}

void passport_link_control_complete(void) {
    if (atomic_load(&s_async_configuration)) atomic_fetch_sub(&s_async_configuration, 1);
}

static void handle_line(const char *line) {
    if (!strcmp(line, "DEVICE") || !strcmp(line, "CHECK") || !strcmp(line, "CHECK_CANCEL") ||
        !strncmp(line, "VOLUME ", 7)) {
        bool deferred = !strcmp(line, "DEVICE") || !strncmp(line, "VOLUME ", 7);
        if (deferred) atomic_fetch_add(&s_async_configuration, 1);
        if (!passport_app_control_request(line)) {
            if (deferred) atomic_fetch_sub(&s_async_configuration, 1);
            reply(NULL, "ERR control");
        }
        return;
    }
    if (!strcmp(line, "POWER")) {
        if (!passport_app_power_request()) reply(NULL, "ERR busy");
        return;
    }
    if (!strcmp(line, "LIVE_TEST")) {
        reply(NULL, passport_app_live_test() ? "LIVE_TEST synthetic queued" : "ERR busy");
        return;
    }
    if (!strcmp(line,"BLE_TEST ON") || !strcmp(line,"BLE_TEST OFF") || !strcmp(line,"HISTORY_TEST")) {
        bool history=!strcmp(line,"HISTORY_TEST");
        reply(NULL,passport_app_memory_test(history,!strcmp(line,"BLE_TEST ON")) ? "MEM_TEST queued" : "ERR busy");
        return;
    }

    if (!strcmp(line,"MEM") || !strcmp(line,"MEM RESET")) {
        if (!strcmp(line,"MEM RESET")) passport_memory_failures_reset();
        char out[512]; passport_memory_format(out,sizeof(out),"command"); reply(NULL,out);
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
        reply(NULL,passport_app_restore_clip_request(line+13) ? "RESTORE_CLIP queued" : "ERR busy or metadata");
        return;
    }
    if (!strncmp(line,"RESUME_REPLY ",13)) {
        reply(NULL,passport_app_resume_reply_request(line+13) ? "RESUME_REPLY queued" : "ERR busy or run id");
        return;
    }
    if (!strcmp(line, "KEY OK") || !strcmp(line, "KEY UP") ||
        !strcmp(line, "KEY DOWN") || !strcmp(line, "KEY BACK")) {
        passport_btn_t key = !strcmp(line, "KEY UP") ? PASSPORT_BTN_UP
                         : !strcmp(line, "KEY DOWN") ? PASSPORT_BTN_DOWN : PASSPORT_BTN_OK;
        passport_press_t press = PASSPORT_CLICK; /* Legacy BACK alias now presses the visible OK action. */
        reply(NULL, passport_app_key_request(key, press) ? "KEY queued" : "ERR busy");
        return;
    }
    if (strcmp(line, "GATEWAY_TEST") == 0) {
        reply(NULL, passport_app_gateway_test() ? "GATEWAY_TEST queued" : "ERR busy");
        return;
    }
    if (strcmp(line, "DIAG") == 0 || strcmp(line, "AUDIO_TEST") == 0) {
        portENTER_CRITICAL(&s_ble_state_lock);
        unsigned peers = passport_ble_count(&s_ble_sessions);
        bool owned = s_ble_sessions.owner != 0;
        portEXIT_CRITICAL(&s_ble_state_lock);
        char state[160];
        snprintf(state, sizeof(state), "BLE ready=%d advertising=%d peers=%u owner=%d capacity=%d name=%s",
                 s_ble_ready, s_ble_ready && ble_gap_adv_active(), peers, owned,
                 CONFIG_BT_NIMBLE_MAX_CONNECTIONS, s_ble_name);
        reply(NULL, state);
        if (!passport_app_diagnostic_request(strcmp(line, "AUDIO_TEST") == 0)) {
            reply(NULL, "ERR busy");
        }
        return;
    }
    if (strcmp(line, "BATT") == 0) {
        if (!passport_app_battery_request()) reply(NULL, "ERR busy");
        return;
    }
    if (passport_app_device_busy() && strcmp(line, "HELLO") && strcmp(line, "NETS") && strcmp(line, "AGENTS")) {
        reply(NULL, "ERR busy");
        return;
    }
    passport_usb_t usb;
    memset(&usb, 0, sizeof(usb));
    usb.store = s_store;
    usb.build = s_firmware_id;
    usb.capabilities = "device-control ble-secure ble-takeover scan-live";
    usb.scan = s_scan;
    usb.scan_count = s_scan_count;
    usb.reply = reply;
    char pending_reply[32] = {0};
    usb.reply_ctx = pending_reply;
    usb.join = join_wifi;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool live_scan = strcmp(line, "SCAN LIVE") == 0;
    if (live_scan || strcmp(line, "SCAN") == 0) {
        int result = live_scan ? reply(NULL, "SCAN LIVE") : 0;
        if (result == 0) result = passport_wifi_scan(s_scan, PASSPORT_SCAN_MAX,
                                                    live_scan ? passport_usb_scan_row : NULL, &usb);
        s_scan_count = result < 0 ? 0 : (size_t)result;
        usb.scan_count = s_scan_count;
        if (live_scan) reply(NULL, result < 0 ? "ERR scan" : "SCAN END");
    }
    uint32_t generation = s_store->generation;
    if (!live_scan) passport_usb_line(&usb, line);
    if (s_store->generation != generation) {
        if (!passport_nvs_save(s_store)) {
            passport_nvs_revert(s_store);
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
    passport_line_t buffer = {0};
    uint8_t byte = 0;
    while (1) {
        int n = usb_serial_jtag_read_bytes(&byte, 1, pdMS_TO_TICKS(200));
        if (n != 1) {
            continue;
        }
        char *line = NULL;
        passport_line_result_t result = passport_line_push(&buffer, byte, CMD_LINE_MAX, &line);
        if (result == PASSPORT_LINE_READY) {
            passport_memory_report("usb_line");
            if (!queue_line(line)) reply(NULL,"ERR busy");
        }
        else if (result == PASSPORT_LINE_TOO_LONG) { reply(NULL, "ERR too_long"); }
        else if (result == PASSPORT_LINE_NO_MEMORY) { reply(NULL, "ERR busy"); }
    }
}

static void process_line(char *line) {
    bool configuring = configuration_line(line);
    bool network = line == atomic_load(&s_network_command);
    passport_network_state_t result = PASSPORT_NETWORK_BUSY;
    if (network) {
        if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
            const passport_network_io_t io = { .join = join_wifi, .save = save_network };
            result = passport_network_connect(s_store, s_network_index, s_network_generation, &io);
            xSemaphoreGive(s_lock);
        }
    } else handle_line(line);
    if (configuring) atomic_fetch_sub(&s_configuration_pending, 1);
    passport_power_command_end();
    if (network) atomic_store(&s_network_command, NULL);
    free(line);
    if (network) {
        atomic_store(&s_network_state, result);
    }
}

static void terminate_session(uint32_t serial) {
    uint16_t handle = session_handle(serial);
    if (handle != BLE_HS_CONN_HANDLE_NONE) (void)ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM);
}

static void process_command(passport_link_command_t command) {
    s_command_session = command.session;
    if (command.session) {
        uint32_t previous = 0;
        bool handshake = !strcmp(command.line, "HELLO");
        bool allowed = false;
        struct ble_gap_conn_desc description;
        uint16_t handle = session_handle(command.session);
        bool encrypted = handle != BLE_HS_CONN_HANDLE_NONE && ble_gap_conn_find(handle, &description) == 0 &&
            description.sec_state.encrypted;
        if (handshake) {
            for (unsigned attempt = 0; atomic_load(&s_async_configuration) && attempt < 600; attempt++)
                vTaskDelay(pdMS_TO_TICKS(100));
        }
        portENTER_CRITICAL(&s_ble_state_lock);
        if (passport_ble_owned(&s_ble_sessions, command.session)) allowed = true;
        else if (handshake) {
            allowed = passport_ble_claim(&s_ble_sessions, command.session, encrypted,
                atomic_load(&s_async_configuration) ||
                atomic_load(&s_network_state) == PASSPORT_NETWORK_CONNECTING, &previous);
        }
        portEXIT_CRITICAL(&s_ble_state_lock);
        if (!allowed) {
            (void)bluetooth_reply(command.session, "ERR busy\n", 9, 500);
            if (configuration_line(command.line)) atomic_fetch_sub(&s_configuration_pending, 1);
            passport_power_command_end();
            free(command.line);
        } else {
            if (previous && previous != command.session) {
                const char notification[] = "SESSION TAKEN\n";
                (void)bluetooth_reply(previous, notification, sizeof(notification) - 1, 500);
                terminate_session(previous);
            }
            process_line(command.line);
        }
    } else process_line(command.line);
    s_command_session = 0;
}

static void line_task(void *arg) {
    (void)arg;
    passport_link_command_t command;
    while (1) {
        if (xQueueReceive(s_lines, &command, pdMS_TO_TICKS(250)) == pdTRUE) process_command(command);
        portENTER_CRITICAL(&s_ble_state_lock);
        uint32_t expired = passport_ble_expired(&s_ble_sessions, (uint32_t)(esp_timer_get_time() / 1000));
        portEXIT_CRITICAL(&s_ble_state_lock);
        if (expired) terminate_session(expired);
    }
}

static int gap_event(struct ble_gap_event *event, void *arg);

static int advertise(void) {
    if (s_ble_paused) return 0;
    portENTER_CRITICAL(&s_ble_state_lock);
    bool full = passport_ble_count(&s_ble_sessions) >= PASSPORT_BLE_PEERS;
    portEXIT_CRITICAL(&s_ble_state_lock);
    if (full || ble_gap_adv_active()) return 0;
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t *)&k_service;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        return rc;
    }
    struct ble_hs_adv_fields response = {0};
    response.name = (const uint8_t *)s_ble_name;
    response.name_len = strlen(s_ble_name);
    response.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&response);
    if (rc != 0) return rc;
    struct ble_gap_adv_params params = {0};
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    return ble_gap_adv_start(s_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
}

static int gap_event(struct ble_gap_event *event, void *arg) {
    (void)arg;
    if (event->type == BLE_GAP_EVENT_CONNECT) {
        if (event->connect.status == 0) {
            portENTER_CRITICAL(&s_ble_state_lock);
            uint32_t serial = passport_ble_connected(&s_ble_sessions, event->connect.conn_handle,
                (uint32_t)(esp_timer_get_time() / 1000));
            portEXIT_CRITICAL(&s_ble_state_lock);
            if (!serial) { (void)ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM); return 0; }
            atomic_store(&s_configuration_connected, true);
            atomic_fetch_add(&s_configuration_activity, 1);
            advertise();
        } else {
            advertise();
        }
    } else if (event->type == BLE_GAP_EVENT_DISCONNECT) {
        ESP_LOGI(TAG, "BLE disconnected: reason=%d pending=%u", event->disconnect.reason,
                 atomic_load(&s_configuration_pending));
        portENTER_CRITICAL(&s_ble_state_lock);
        passport_ble_peer_t *peer = passport_ble_peer(&s_ble_sessions, event->disconnect.conn.conn_handle);
        int index = peer ? (int)(peer - s_ble_sessions.peers) : -1;
        passport_ble_disconnected(&s_ble_sessions, event->disconnect.conn.conn_handle);
        bool connected = passport_ble_count(&s_ble_sessions) != 0;
        portEXIT_CRITICAL(&s_ble_state_lock);
        atomic_store(&s_configuration_connected, connected);
        if (atomic_load(&s_ble_transmitting) == event->disconnect.conn.conn_handle) {
            atomic_store(&s_ble_transmit_status, BLE_HS_ENOTCONN);
            if (s_ble_transmitted) xSemaphoreGive(s_ble_transmitted);
        }
        if (index >= 0) passport_line_reset(&s_ble_lines[index]);
        advertise();
    } else if (event->type == BLE_GAP_EVENT_SUBSCRIBE && event->subscribe.attr_handle == s_tx_handle) {
        portENTER_CRITICAL(&s_ble_state_lock);
        passport_ble_peer_t *peer = passport_ble_peer(&s_ble_sessions, event->subscribe.conn_handle);
        if (peer) peer->subscribed = event->subscribe.cur_indicate;
        portEXIT_CRITICAL(&s_ble_state_lock);
    } else if (event->type == BLE_GAP_EVENT_NOTIFY_TX &&
               event->notify_tx.indication && event->notify_tx.attr_handle == s_tx_handle &&
               event->notify_tx.conn_handle == atomic_load(&s_ble_transmitting) &&
               event->notify_tx.status != 0) {
        atomic_store(&s_ble_transmit_status,
                     event->notify_tx.status == BLE_HS_EDONE ? 0 : event->notify_tx.status);
        if (s_ble_transmitted) xSemaphoreGive(s_ble_transmitted);
    } else if (event->type == BLE_GAP_EVENT_ADV_COMPLETE) {
        advertise();
    }
    return 0;
}

static int on_rx(uint16_t conn_handle, uint16_t attr_handle,
                 struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)attr_handle;
    (void)arg;
    portENTER_CRITICAL(&s_ble_state_lock);
    passport_ble_peer_t *peer = passport_ble_peer(&s_ble_sessions, conn_handle);
    int index = peer ? (int)(peer - s_ble_sessions.peers) : -1;
    uint32_t serial = peer ? peer->serial : 0;
    portEXIT_CRITICAL(&s_ble_state_lock);
    if (index < 0) return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    uint8_t buf[256];
    if (len > sizeof(buf)) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    ble_hs_mbuf_to_flat(ctxt->om, buf, len, NULL);
    for (uint16_t i = 0; i < len; i++) {
        char *line = NULL;
        passport_line_result_t result = passport_line_push(&s_ble_lines[index], buf[i], CMD_LINE_MAX, &line);
        if (result == PASSPORT_LINE_READY) {
            if (!queue_command(line, serial)) return BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        else if (result == PASSPORT_LINE_TOO_LONG) { return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN; }
        else if (result == PASSPORT_LINE_NO_MEMORY) { return BLE_ATT_ERR_INSUFFICIENT_RES; }
    }
    return 0;
}

static int on_tx(uint16_t conn_handle, uint16_t attr_handle,
                 struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn_handle;
    (void)attr_handle;
    (void)ctxt;
    (void)arg;
    return BLE_ATT_ERR_READ_NOT_PERMITTED;
}

static const struct ble_gatt_svc_def k_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &k_service.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &k_rx.u,
                .access_cb = on_rx,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC,
                .min_key_size = 16,
            },
            {
                .uuid = &k_tx.u,
                .val_handle = &s_tx_handle,
                .access_cb = on_tx,
                .flags = BLE_GATT_CHR_F_INDICATE,
            },
            {0},
        },
    },
    {0},
};

static void on_sync(void) {
    s_ble_ready = false;
    int result = ble_hs_util_ensure_addr(0);
    if (result == 0) result = ble_hs_id_infer_auto(0, &s_addr_type);
    uint8_t address[6];
    if (result == 0) result = ble_hs_id_copy_addr(s_addr_type, address, NULL);
    if (result == 0) {
        snprintf(s_ble_name, sizeof(s_ble_name), "AI Passport %02X%02X",
                 (unsigned)address[1], (unsigned)address[0]);
        result = ble_svc_gap_device_name_set(s_ble_name);
    }
    if (result == 0 && s_tx_handle == 0) result = BLE_HS_ENOENT;
    if (result == 0) result = advertise();
    if (result != 0) {
        ESP_LOGE(TAG, "BLE sync failed: %d", result);
        return;
    }
    s_ble_ready = true;
}

static void host_task(void *arg) {
    (void)arg;
    nimble_port_run();
    passport_memory_report("ble_host_stopped");
    xSemaphoreGive(s_host_stopped);
    vTaskSuspend(NULL);
}

esp_err_t passport_link_resume_bluetooth(void) {
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
    s_ble_ready = false;
    s_tx_handle = 0;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    int result = ble_gatts_count_cfg(k_svcs);
    if (result == 0) result = ble_gatts_add_svcs(k_svcs);
    if (result != 0) {
        ESP_LOGE(TAG, "BLE service registration failed: %d", result);
        err = nimble_port_deinit();
        if (err == ESP_OK) s_ble_initialized = false;
        else ESP_LOGE(TAG, "BLE registration cleanup failed: %d", err);
        return result == BLE_HS_ENOMEM ? ESP_ERR_NO_MEM : ESP_FAIL;
    }
    ESP_LOGI(TAG, "BLE resume: services queued");
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_store_config_init();
    if (xTaskCreate(host_task, "nimble", 6144, NULL, 5, &s_host_task) != pdPASS) {
        s_host_task = NULL;
        err = nimble_port_deinit();
        if (err == ESP_OK) s_ble_initialized = false;
        return ESP_ERR_NO_MEM;
    }
    passport_memory_report("ble_on");
    ESP_LOGI(TAG, "BLE resume: host started");
    return ESP_OK;
}

esp_err_t passport_link_pause_bluetooth(void) {
    if (!s_ble_initialized) return ESP_OK;
    s_ble_paused = true;
    s_ble_ready = false;
    if (!s_ble_stopping) {
        (void)ble_gap_adv_stop();
        for (size_t index = 0; index < PASSPORT_BLE_PEERS; index++) {
            portENTER_CRITICAL(&s_ble_state_lock);
            uint32_t serial = s_ble_sessions.peers[index].serial;
            portEXIT_CRITICAL(&s_ble_state_lock);
            if (serial) terminate_session(serial);
        }
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
        for (size_t index = 0; index < PASSPORT_BLE_PEERS; index++) passport_line_reset(&s_ble_lines[index]);
        portENTER_CRITICAL(&s_ble_state_lock);
        for (size_t index = 0; index < PASSPORT_BLE_PEERS; index++)
            memset(&s_ble_sessions.peers[index], 0, sizeof(s_ble_sessions.peers[index]));
        s_ble_sessions.owner = 0;
        portEXIT_CRITICAL(&s_ble_state_lock);
        atomic_store(&s_configuration_connected, false);
    }
    passport_memory_report("ble_off");
    return err;
}

void passport_link_start(passport_store_t *store) {
    s_store = store;
    const esp_app_desc_t *description = esp_app_get_description();
    for (size_t i = 0; i < 8; i++) {
        snprintf(s_firmware_id + i * 2, 3, "%02x", description->app_elf_sha256[i]);
    }
    s_lock = xSemaphoreCreateMutex();
    s_lines = xQueueCreate(4, sizeof(passport_link_command_t));
    s_ble_transmit_lock = xSemaphoreCreateMutex();
    s_ble_transmitted = xSemaphoreCreateBinary();
    if (s_lock == NULL || s_lines == NULL || !s_ble_transmit_lock || !s_ble_transmitted ||
        xTaskCreate(line_task, "passport_cmd", 12288, NULL, 4, &s_command_task) != pdPASS ||
        xTaskCreate(usb_task, "passport_usb", 4096, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "command task failed");
        return;
    }
    if (store == NULL || store->count == 0) {
        esp_err_t err = passport_link_resume_bluetooth();
        if (err != ESP_OK) ESP_LOGE(TAG, "nimble init failed: %s", esp_err_to_name(err));
    }
}

bool passport_link_is_bluetooth_ready(void) {
    return s_ble_ready;
}

bool passport_link_is_bluetooth_connected(void) {
    return atomic_load(&s_configuration_connected);
}

/* True also during startup or a failed stop: policy must finish cleanup. */
bool passport_link_is_bluetooth_active(void) { return s_ble_initialized; }
