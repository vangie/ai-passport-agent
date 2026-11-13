#include "passport.h"

#include "bsp_button.h"
#include "bsp_pins.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_battery.h"
#include "passport_audio.h"
#include "passport_clip.h"
#include "passport_gateway.h"
#include "passport_memory.h"
#include "passport_radio.h"
#include "passport_reply.h"
#include "passport_power.h"
#include "passport_buttons.h"
#include "passport_control.h"
#include "passport_network.h"
#include "driver/usb_serial_jtag.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "nvs_flash.h"
#include <stdlib.h>
#include <stdatomic.h>

static const char *TAG = "passport";

static passport_store_t s_store;
static passport_view_t s_view;
static char s_gateway_message[PASSPORT_TEXT_MAX];
static QueueHandle_t s_inputs;
typedef enum { INPUT_KEY, INPUT_PHYSICAL_KEY, INPUT_DIAG, INPUT_BATTERY, INPUT_POWER, INPUT_AUDIO_TEST, INPUT_GATEWAY_TEST, INPUT_BLE_ON, INPUT_BLE_OFF, INPUT_HISTORY_TEST, INPUT_RESUME_REPLY, INPUT_RESTORE_CLIP, INPUT_LIVE_TEST, INPUT_AUTO_SEND, INPUT_DEVICE, INPUT_VOLUME, INPUT_CHECK, INPUT_CHECK_CANCEL } input_kind_t;
typedef struct {
    input_kind_t kind;
    passport_input_t key;
    char *run_id;
    uint32_t capture_generation;
    passport_action_t capture_action;
    unsigned expected_volume;
    unsigned desired_volume;
} app_input_t;
static int64_t s_audio_test_end;
static int64_t s_battery_next_us;
static bsp_battery_reading_t s_battery_reading;
static bool s_audio_test_active;
static uint32_t s_capture_generation;
static bool s_auto_send_queued;
static bool s_gateway_probe_ui;
static bool s_restart_live;
static uint8_t s_saved_volume;
static bool s_ble_config_requested;
static bool s_ble_config_suppressed;
static int64_t s_boot_us;
static int s_polled_button = -1;
static passport_buttons_t s_buttons;
static passport_action_t s_polled_capture_action;
static uint32_t s_polled_capture_generation;
static passport_power_policy_t s_power;
static atomic_bool s_device_busy = ATOMIC_VAR_INIT(true);
static bool s_device_check;
static int s_network_index;
static uint32_t s_network_generation;
static int64_t s_network_started;
static bool s_network_queued;


void passport_screen_open(void);
void passport_screen_show(const passport_view_t *view, const passport_store_t *store);
void passport_link_start(passport_store_t *store);
bool passport_link_is_bluetooth_ready(void);
bool passport_link_is_bluetooth_connected(void);
bool passport_link_configuration_pending(void);
bool passport_link_is_bluetooth_active(void);
esp_err_t passport_link_pause_bluetooth(void);
esp_err_t passport_link_resume_bluetooth(void);
bool passport_nvs_load(passport_store_t *store);
bool passport_nvs_save(const passport_store_t *store);
uint8_t passport_nvs_volume_load(void);
bool passport_nvs_volume_save(uint8_t volume);
bool passport_wifi_is_online(void);
esp_err_t passport_wifi_gateway_performance(bool enabled);
esp_err_t passport_wifi_prepare(void);
void passport_wifi_autoconnect(passport_store_t *store);
void passport_link_diagnostic_reply(const char *line);
void passport_link_control_complete(void);
bool passport_link_apply_selection(passport_action_t action, int index, uint32_t expected_generation);
bool passport_link_connect_network(int index, uint32_t generation);
passport_network_state_t passport_link_network_result(void);
bool passport_link_configuration_busy(void);
unsigned passport_link_configuration_activity(void);

bool passport_app_device_busy(void) { return atomic_load(&s_device_busy); }

bool passport_app_control_request(const char *line) {
    app_input_t event = {0};
    if (!strcmp(line, "DEVICE")) event.kind = INPUT_DEVICE;
    else if (!strcmp(line, "CHECK")) event.kind = INPUT_CHECK;
    else if (!strcmp(line, "CHECK_CANCEL")) event.kind = INPUT_CHECK_CANCEL;
    else {
        event.kind = INPUT_VOLUME;
        if (!passport_control_volume(line, &event.expected_volume, &event.desired_volume)) return false;
    }
    return s_inputs && xQueueSend(s_inputs, &event, 0) == pdTRUE;
}

static bool device_busy(void) {
    return s_view.recording || s_view.sending || s_view.cancel_pending || s_audio_test_active ||
        s_view.screen == PASSPORT_SCREEN_LIVE || passport_gateway_active() ||
        s_view.network_state == PASSPORT_NETWORK_CONNECTING;
}

static void device_status(void) {
    char line[224];
    passport_control_status(line, sizeof(line), s_view.speaker_volume, s_view.battery_soc,
        passport_wifi_is_online(), device_busy(), s_power.sleeping, usb_serial_jtag_is_connected());
    size_t used = strlen(line);
    snprintf(line + used, sizeof(line) - used, " generation=%lu network=%d agent=%d",
        (unsigned long)s_store.generation, s_store.connected, s_store.active_agent);
    passport_link_diagnostic_reply(line);
}

bool passport_app_power_request(void) {
    const app_input_t event = { .kind = INPUT_POWER };
    return s_inputs && xQueueSend(s_inputs, &event, 0) == pdTRUE;
}

static uint32_t power_now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static bool power_wake(void) {
    passport_power_awake(true);
    if (s_power.sleeping) {
        esp_err_t error = bsp_lvgl_set_sleep(false);
        if (error != ESP_OK) {
            ESP_LOGE(TAG, "display wake failed: %s", esp_err_to_name(error));
            return false;
        }
        (void)bsp_button_set_polling(true);
        s_power.sleeping = false;
        ESP_LOGI(TAG, "standby wake");
    }
    passport_power_activity(&s_power, power_now_ms());
    return true;
}

static void power_maintain(void) {
    static unsigned last_configuration;
    unsigned configuration = passport_link_configuration_activity();
    if (configuration != last_configuration || passport_link_configuration_busy()) {
        last_configuration = configuration;
        (void)power_wake();
    }
    passport_audio_diagnostics_t audio;
    passport_audio_diagnostics(&audio);
    bool busy = s_view.recording || s_view.sending || s_view.cancel_pending ||
        s_view.screen == PASSPORT_SCREEN_LIVE || s_audio_test_active || audio.worker_owned ||
        s_view.screen == PASSPORT_SCREEN_PREFERENCES || s_view.screen == PASSPORT_SCREEN_NETS ||
        s_view.screen == PASSPORT_SCREEN_NET_MENU || s_view.screen == PASSPORT_SCREEN_ADD ||
        s_view.screen == PASSPORT_SCREEN_SETTINGS || s_view.screen == PASSPORT_SCREEN_VOLUME ||
        s_polled_button >= 0 || passport_link_configuration_busy() ||
        usb_serial_jtag_is_connected() || s_view.network_state == PASSPORT_NETWORK_CONNECTING;
    if (s_power.sleeping && (busy || passport_gateway_active())) (void)power_wake();
    if (!passport_power_due(&s_power, power_now_ms(), busy || passport_gateway_active(),
                         usb_serial_jtag_is_connected())) return;
    esp_err_t error = passport_audio_release();
    if (error == ESP_OK) error = bsp_lvgl_set_sleep(true);
    if (error == ESP_OK) {
        (void)bsp_button_set_polling(false);
        (void)passport_wifi_gateway_performance(false);
        s_power.sleeping = true;
        passport_power_awake(false);
        ESP_LOGI(TAG, "standby entered; key wakes without action");
    } else {
        passport_power_activity(&s_power, power_now_ms());
        ESP_LOGW(TAG, "standby deferred: %s", esp_err_to_name(error));
    }
}

bool passport_app_diagnostic_request(bool audio_test) {
    const app_input_t event = { .kind = audio_test ? INPUT_AUDIO_TEST : INPUT_DIAG };
    return s_inputs != NULL && xQueueSend(s_inputs, &event, 0) == pdTRUE;
}

bool passport_app_battery_request(void) {
    const app_input_t event = { .kind = INPUT_BATTERY };
    return s_inputs != NULL && xQueueSend(s_inputs, &event, 0) == pdTRUE;
}

bool passport_app_gateway_test(void) {
    const app_input_t event = { .kind = INPUT_GATEWAY_TEST };
    return s_inputs && xQueueSend(s_inputs, &event, 0) == pdTRUE;
}
bool passport_app_live_test(void) {
    const app_input_t event = { .kind = INPUT_LIVE_TEST };
    return s_inputs && xQueueSend(s_inputs, &event, 0) == pdTRUE;
}

bool passport_app_memory_test(bool history, bool enable) {
    const app_input_t event = { .kind=history ? INPUT_HISTORY_TEST : enable ? INPUT_BLE_ON : INPUT_BLE_OFF };
    return s_inputs && xQueueSend(s_inputs,&event,0)==pdTRUE;
}

bool passport_app_key_request(passport_btn_t key, passport_press_t press) {
    const app_input_t event = { .kind = INPUT_KEY, .key = { .btn = key, .press = press } };
    return s_inputs && xQueueSend(s_inputs, &event, 0) == pdTRUE;
}

bool passport_app_resume_reply_request(const char *run_id) {
    if (!s_inputs || !run_id || !*run_id || strlen(run_id)>=80) return false;
    char *copy=strdup(run_id);if (!copy) return false;
    const app_input_t event={.kind=INPUT_RESUME_REPLY,.run_id=copy};
    if (xQueueSend(s_inputs,&event,0)!=pdTRUE) { free(copy);return false; }
    return true;
}

bool passport_app_restore_clip_request(const char *arguments) {
    if (!s_inputs || !arguments || strlen(arguments)>=96) return false;
    char *copy=strdup(arguments);if (!copy) return false;
    const app_input_t event={.kind=INPUT_RESTORE_CLIP,.run_id=copy};
    if (xQueueSend(s_inputs,&event,0)!=pdTRUE) { free(copy);return false; }
    return true;
}

static void audio_diagnostic_reply(const char *prefix) {
    passport_audio_diagnostics_t audio;
    passport_audio_diagnostics(&audio);
    static char line[640];
    char *send_message = s_gateway_message;
    char identity[65];
    passport_send_state_t send = passport_gateway_poll(send_message, sizeof(s_gateway_message));
    passport_gateway_identity(identity);
    snprintf(line, 640,
             "%s phase=%s error=%s chunks=%lu peak=%lu worker=%d recording=%d "
             "screen=%d retry=%d unread=%d ok_action=%d volume=%u free=%u dma_free=%u dma_largest=%u adc_mv=%d wifi=%d ble=%d send=%d device=%s message=%.200s",
             prefix, audio.phase, esp_err_to_name(audio.error),
             (unsigned long)audio.chunks, (unsigned long)audio.peak,
             audio.worker_owned, s_view.recording, s_view.screen, s_view.retry_send, s_view.reply_unread,
             passport_key_binding(&s_view,&s_store,PASSPORT_BTN_OK).action,
             (unsigned)s_view.speaker_volume,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA),
             bsp_button_read_mv(), passport_wifi_is_online(), passport_link_is_bluetooth_ready(), send, identity, send_message);
    passport_link_diagnostic_reply(line);
    snprintf(line, sizeof(line), "PCM mean_square=%lu dc=%ld clipped=%lu dropped=%lu played=%lu voice=%lu silence=%lu suppressed=%lu response=%d muted=%d rms=%lu threshold=%lu voice_dropped=%lu",
             (unsigned long)audio.mean_square, (long)audio.dc, (unsigned long)audio.clipped,
             (unsigned long)passport_audio_live_dropped(), (unsigned long)audio.played,
             (unsigned long)audio.voice_chunks, (unsigned long)audio.silence_chunks,
             (unsigned long)audio.input_suppressed, audio.response, audio.muted,
             (unsigned long)audio.input_rms, (unsigned long)audio.input_threshold,
             (unsigned long)audio.voice_dropped);
    passport_link_diagnostic_reply(line);
    snprintf(line, sizeof(line), "CAPTURE rate=%u bits=16 channels=1 samples=%lu ms=%lu read_gap_ms=%lu clipped_total=%lu queue_max=%lu",
             (unsigned)audio.sample_rate,
             (unsigned long)audio.chunks * 256ul, (unsigned long)audio.capture_ms,
             (unsigned long)audio.read_gap_ms, (unsigned long)audio.clipped_total,
             (unsigned long)audio.input_high_water);
    passport_link_diagnostic_reply(line);
    passport_gateway_diagnostic(line, sizeof(line));
    passport_link_diagnostic_reply(line);
    passport_gateway_timing_diagnostic(line, sizeof(line));
    passport_link_diagnostic_reply(line);
    snprintf(line, sizeof(line), "INPUT muted_chunks=%lu response_chunks=%lu echo_chunks=%lu clipped=%lu play_stalls=%lu play_gap_ms=%lu",
        (unsigned long)audio.mute_chunks, (unsigned long)audio.response_chunks,
        (unsigned long)audio.echo_chunks, (unsigned long)audio.input_clipped,
        (unsigned long)audio.play_stalls, (unsigned long)audio.play_gap_ms);
    passport_link_diagnostic_reply(line);
    passport_memory_report("app_diag");
}

static void battery_diagnostic_reply(void) {
    bsp_battery_reading_t reading;
    char line[128];
    esp_err_t err = bsp_battery_read(&reading);
    if (err == ESP_OK) {
        snprintf(line, sizeof(line), "BATT soc=%d.%02d%% raw=%u mv=%d",
                 reading.soc_percent, (reading.soc_raw & 0xff) * 100 / 256,
                 (unsigned)reading.soc_raw, reading.voltage_mv);
    } else {
        snprintf(line, sizeof(line), "BATT error=%s", esp_err_to_name(err));
    }
    passport_link_diagnostic_reply(line);
}

static void battery_refresh(void) {
    int64_t now = esp_timer_get_time();
    if (now < s_battery_next_us) return;
    if (bsp_battery_read(&s_battery_reading) == ESP_OK)
        s_view.battery_soc = s_battery_reading.soc_percent;
    else
        s_view.battery_soc = -1;
    s_battery_next_us = now + 5LL * 1000 * 1000;
}

static void redraw(void) {
    if (s_power.sleeping) return;
    if (!bsp_lvgl_lock(50)) {
        return;
    }
    passport_screen_show(&s_view, &s_store);
    bsp_lvgl_unlock();
}

static void request_bluetooth_config(void) {
    s_ble_config_suppressed = false;
    s_ble_config_requested = true;
    passport_gateway_maintain(false);
}

static void close_bluetooth_config(void) {
    if (passport_link_configuration_pending()) return;
    s_ble_config_suppressed = true;
    s_ble_config_requested = false;
}

static void maintain_network_selection(void) {
    if (s_view.network_state != PASSPORT_NETWORK_CONNECTING) {
        if (s_view.network_state != PASSPORT_NETWORK_IDLE && s_store.generation != s_network_generation) {
            s_view.network_state = PASSPORT_NETWORK_IDLE;
            s_view.network_target = -1;
        }
        return;
    }
    passport_gateway_maintain(false);
    if (!s_network_queued) {
        if (!passport_gateway_quiescent()) {
            if (esp_timer_get_time() - s_network_started >= 5000000)
                s_view.network_state = PASSPORT_NETWORK_BUSY;
            return;
        }
        if (!passport_link_connect_network(s_network_index, s_network_generation)) {
            s_view.network_state = PASSPORT_NETWORK_BUSY;
            return;
        }
        s_network_queued = true;
    }
    passport_network_state_t result = passport_link_network_result();
    if (result != PASSPORT_NETWORK_CONNECTING && result != PASSPORT_NETWORK_IDLE) {
        s_view.network_state = result;
        s_network_generation = s_store.generation;
    }
}

static void maintain_bluetooth_mode(void) {
    static int64_t retry_after;
    int64_t now = esp_timer_get_time();
    if (s_view.network_state == PASSPORT_NETWORK_CONNECTING) { passport_gateway_maintain(false); return; }
    if (now < retry_after) return;
    bool configuring = passport_link_is_bluetooth_active() && passport_link_configuration_busy() &&
        (!s_ble_config_suppressed || passport_link_configuration_pending());
    bool online = passport_wifi_is_online();
    bool want = passport_radio_wants_ble((uint64_t)(now-s_boot_us)/1000,
        online,s_store.count>0,s_ble_config_requested,configuring,
        s_view.recording || s_view.sending || passport_gateway_active());
    if (s_ble_config_suppressed && !configuring) want = false;
    if (want) {
        passport_gateway_maintain(false);
        if (!passport_gateway_quiescent()) return;
    }
    esp_err_t err = ESP_OK;
    if (!want && passport_link_is_bluetooth_active()) err=passport_link_pause_bluetooth();
    else if (want && !passport_link_is_bluetooth_active()) {
        err=passport_audio_release();
        if (err==ESP_OK) err=passport_link_resume_bluetooth();
    }
    if (err!=ESP_OK) retry_after=now+1000000;
    if (!want) passport_gateway_maintain(online && err==ESP_OK);
}

static void enqueue_input(passport_btn_t btn, passport_press_t press) {
    if (s_inputs == NULL) {
        return;
    }
    const app_input_t input = { .kind = INPUT_KEY, .key = { .btn = btn, .press = press } };
    (void)xQueueSend(s_inputs, &input, 0);
}

static void finish_cancelled_operation(void) {
    if (!passport_gateway_abandon()) return;
    passport_clip_discard();
    bool restart=passport_finish_cancel(&s_view,"本次操作已取消");
    s_gateway_probe_ui=false;
    if (s_restart_live) {
        s_restart_live = false;
        s_view.screen = PASSPORT_SCREEN_HOME;
        enqueue_input(PASSPORT_BTN_DOWN,PASSPORT_CLICK);
    } else if (restart) {
        s_view.screen=PASSPORT_SCREEN_TALK;
        enqueue_input(PASSPORT_BTN_OK,PASSPORT_CLICK);
    }
}

static void button_event(bsp_btn_t button, bsp_btn_ev_t event, void *user) {
    (void)user;
    if (!s_inputs || (event != BSP_BTN_PRESS && event != BSP_BTN_RELEASE)) return;
    const app_input_t input = { .kind = INPUT_PHYSICAL_KEY,
        .key = { .btn = (passport_btn_t)button, .press = event == BSP_BTN_PRESS ? PASSPORT_PRESS : PASSPORT_RELEASE } };
    (void)xQueueSend(s_inputs, &input, 0);
}

static void poll_button_wake(void) {
    if (!s_power.sleeping && !s_power.consume_key) return;
    int mv = bsp_button_read_mv();
    if (mv < 0) return;
    bool pressed = mv < BSP_BTN_MV_RELEASED_MIN;
    if (passport_power_key(&s_power, power_now_ms(), pressed) && pressed) (void)power_wake();
}

void app_main(void) {
    passport_power_init();
    passport_power_activity(&s_power, power_now_ms());
    passport_memory_init();
    passport_memory_report("boot");
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(bsp_i2c_init());
    (void)bsp_battery_init();
    ESP_ERROR_CHECK(bsp_display_init());
    bsp_display_backlight(80);
    if (bsp_lvgl_init() == NULL) {
        ESP_LOGE(TAG, "lvgl init failed");
        return;
    }

    passport_store_init(&s_store);
    passport_nvs_load(&s_store);
    s_boot_us = esp_timer_get_time();
    passport_view_init(&s_view);
    s_view.speaker_volume = s_saved_volume = passport_nvs_volume_load();
    (void)passport_audio_set_volume(s_saved_volume);
    if (!bsp_lvgl_lock(1000)) {
        return;
    }
    passport_screen_open();
    passport_screen_show(&s_view, &s_store);
    bsp_lvgl_unlock();

    s_inputs = xQueueCreate(8, sizeof(app_input_t));
    if (s_inputs == NULL) {
        ESP_LOGE(TAG, "button event queue allocation failed");
        return;
    }
    ESP_ERROR_CHECK(bsp_button_init(button_event, NULL));
    ESP_ERROR_CHECK(passport_wifi_prepare());
    redraw();
    passport_link_start(&s_store);
    passport_wifi_autoconnect(&s_store);
    passport_memory_report("app_ready");
    ESP_LOGI(TAG, "passport ready, %u agents", (unsigned)s_store.agent_count);
    while (true) {
        poll_button_wake();
        if (s_view.recording || s_view.sending || passport_gateway_active())
            passport_power_activity(&s_power, power_now_ms());
        if (!s_view.recording && !s_view.sending) passport_chat_sync(&s_view, &s_store);
        app_input_t event;
        while (xQueueReceive(s_inputs, &event, 0) == pdTRUE) {
            if (event.kind == INPUT_PHYSICAL_KEY) {
                bool pressed = event.key.press == PASSPORT_PRESS;
                bool suppress = passport_power_key(&s_power, power_now_ms(), pressed);
                if (suppress && pressed) (void)power_wake();
                bool click = passport_buttons_event(&s_buttons, &event.key, suppress);
                s_polled_button = s_buttons.pressed ? (int)s_buttons.button : -1;
                if (!suppress && pressed) {
                    s_polled_capture_generation = s_capture_generation;
                    s_polled_capture_action = s_view.recording && s_view.screen == PASSPORT_SCREEN_CAPTURE
                        ? passport_key_binding(&s_view, &s_store, event.key.btn).action : PASSPORT_ACT_NONE;
                }
                if (!click) continue;
                event.kind = INPUT_KEY;
                event.capture_generation = s_polled_capture_generation;
                event.capture_action = s_polled_capture_action;
            }
            if (event.kind == INPUT_POWER) {
                char line[128];
                snprintf(line, sizeof(line), "POWER sleeping=%d idle_ms=%lu timeout_ms=%u usb_host=%d config=%d",
                    s_power.sleeping, (unsigned long)(power_now_ms() - s_power.activity_ms),
                    PASSPORT_IDLE_TIMEOUT_MS, usb_serial_jtag_is_connected(), passport_link_configuration_busy());
                passport_link_diagnostic_reply(line);
                continue;
            }
            if (event.kind == INPUT_DEVICE) { device_status(); passport_link_control_complete(); continue; }
            if (event.kind == INPUT_CHECK_CANCEL) {
                if (s_device_check) (void)passport_gateway_cancel();
                continue;
            }
            if (event.kind == INPUT_VOLUME) {
                const char *result = "OK volume";
                unsigned previous = s_view.speaker_volume;
                if (device_busy()) result = "ERR busy";
                else if (previous != event.expected_volume) result = "ERR stale";
                else if (passport_audio_set_volume(event.desired_volume) != ESP_OK) result = "ERR busy";
                else if (!passport_nvs_volume_save(event.desired_volume)) {
                    (void)passport_audio_set_volume(previous);
                    result = "ERR storage";
                } else s_view.speaker_volume = s_saved_volume = event.desired_volume;
                passport_link_diagnostic_reply(result);
                device_status();
                passport_link_control_complete();
                continue;
            }
            if (event.kind == INPUT_CHECK) {
                if (device_busy()) { passport_link_diagnostic_reply("CHECK busy"); continue; }
                if (passport_gateway_check() != ESP_OK) {
                    passport_link_diagnostic_reply("CHECK failed");
                    continue;
                }
                s_device_check = true;
                atomic_store(&s_device_busy, true);
                passport_link_diagnostic_reply("CHECK started");
                continue;
            }
            if (event.kind != INPUT_DIAG && event.kind != INPUT_BATTERY) {
                bool wake_only = s_power.sleeping && event.kind == INPUT_KEY;
                if (!power_wake()) {
                    free(event.run_id);
                    continue;
                }
                if (wake_only) continue;
            }
            if (event.kind == INPUT_AUTO_SEND) {
                if (s_audio_test_active || s_polled_button >= 0) {
                    s_auto_send_queued = false;
                    continue;
                }
                event.key = (passport_input_t){ .btn = PASSPORT_BTN_OK, .press = PASSPORT_CLICK };
            }
            if (event.capture_action != PASSPORT_ACT_NONE &&
                !passport_capture_input_valid(&s_view, &s_store, event.key.btn, event.capture_action,
                                          event.capture_generation, s_capture_generation)) continue;
            if (event.kind == INPUT_RESTORE_CLIP) {
                unsigned bytes;unsigned long crc;char id[33],extra;
                esp_err_t restored=ESP_ERR_INVALID_ARG;
                if (!device_busy() &&
                    sscanf(event.run_id,"%u %32s %lx %c",&bytes,id,&crc,&extra)==3)
                    restored=passport_clip_restore(bytes,id,(uint32_t)crc);
                free(event.run_id);
                if (restored==ESP_OK) {
                    passport_offer_retry(&s_view,"录音已恢复，可重试");
                    passport_link_diagnostic_reply("RESTORE_CLIP ok");
                } else passport_link_diagnostic_reply("RESTORE_CLIP failed");
                continue;
            }
            if (event.kind == INPUT_RESUME_REPLY) {
                if (!device_busy() && passport_gateway_resume_reply(event.run_id)==ESP_OK) {
                    s_view.sending=true;s_view.screen=PASSPORT_SCREEN_PROGRESS;s_gateway_probe_ui=false;
                }
                free(event.run_id);continue;
            }
            if (event.kind == INPUT_BLE_ON || event.kind == INPUT_BLE_OFF) {
                if (device_busy()) { passport_link_diagnostic_reply("BLE_TEST busy"); continue; }
                if (event.kind == INPUT_BLE_ON) request_bluetooth_config();
                else close_bluetooth_config();
                passport_link_diagnostic_reply(event.kind==INPUT_BLE_ON ? "BLE_TEST on" : "BLE_TEST off");
                continue;
            }
            if (event.kind == INPUT_GATEWAY_TEST || event.kind == INPUT_HISTORY_TEST || event.kind == INPUT_LIVE_TEST) {
                if (device_busy()) { passport_link_diagnostic_reply("GATEWAY_TEST busy");continue; }
                esp_err_t probe = event.kind == INPUT_LIVE_TEST ? passport_gateway_live_test() :
                    event.kind==INPUT_HISTORY_TEST ? passport_gateway_history_probe() : passport_gateway_probe();
                if (probe!=ESP_OK) { passport_link_diagnostic_reply("GATEWAY_TEST failed to start");continue; }
                s_gateway_probe_ui = true;
                s_view.sending = true;
                s_view.screen = event.kind == INPUT_LIVE_TEST ? PASSPORT_SCREEN_LIVE : PASSPORT_SCREEN_PROGRESS;
                continue;
            }
            if (event.kind == INPUT_DIAG) {
                audio_diagnostic_reply("DIAG");
                continue;
            }
            if (event.kind == INPUT_BATTERY) {
                battery_diagnostic_reply();
                continue;
            }
            if (event.kind == INPUT_AUDIO_TEST) {
                if (device_busy() || s_view.retry_send || s_view.reply_unread || s_audio_test_end != 0 ||
                    s_view.screen != PASSPORT_SCREEN_HOME) {
                    passport_link_diagnostic_reply("AUDIO_TEST busy");
                    continue;
                }
                audio_diagnostic_reply("AUDIO_TEST before");
                event.key = (passport_input_t){ .btn = PASSPORT_BTN_OK, .press = PASSPORT_CLICK };
            }
            if (s_device_check) { (void)passport_gateway_cancel(); continue; }
            passport_input_t input = event.key;
            bool was_recording = s_view.recording;
            bool was_sending = s_view.sending;
            bool was_cancelling = s_view.cancel_pending;
            passport_screen_t was_screen = s_view.screen;
            int selected = s_view.cursor;
            uint32_t expected_generation = s_store.generation;
            uint8_t previous_volume = s_view.speaker_volume;
            passport_action_t action = passport_on_key(&s_view, &s_store, input.btn, input.press);
            if (action == PASSPORT_ACT_VOLUME_UP || action == PASSPORT_ACT_VOLUME_DOWN) {
                if (passport_audio_set_volume(s_view.speaker_volume) != ESP_OK) {
                    s_view.speaker_volume = previous_volume;
                    snprintf(s_view.notice, sizeof(s_view.notice), "请先结束通话");
                }
                continue;
            }
            if (was_screen == PASSPORT_SCREEN_VOLUME && action == PASSPORT_ACT_BACK && s_saved_volume != s_view.speaker_volume) {
                if (passport_nvs_volume_save(s_view.speaker_volume)) s_saved_volume = s_view.speaker_volume;
                else {
                    s_view.screen = PASSPORT_SCREEN_VOLUME;
                    snprintf(s_view.notice, sizeof(s_view.notice), "保存失败，请重试");
                }
            }
            ESP_LOGI(TAG, "key=%d action=%d screen=%d recording=%d sending=%d",
                     input.btn, action, s_view.screen, s_view.recording, s_view.sending);
            if (action == PASSPORT_ACT_LIVE) {
                if (!passport_gateway_abandon() || passport_gateway_live(passport_lang_zh(&s_store)) != ESP_OK) {
                    s_view.sending = false; s_view.screen = PASSPORT_SCREEN_HOME;
                    snprintf(s_view.reply, sizeof(s_view.reply), "实时语音启动失败，请检查网络");
                }
                continue;
            }
            if (action == PASSPORT_ACT_LIVE_MUTE || action == PASSPORT_ACT_LIVE_UNMUTE) {
                passport_audio_live_mute(s_view.live_muted);
                continue;
            }
            if (action == PASSPORT_ACT_LIVE_RECONNECT) s_restart_live = true;
            if (action == PASSPORT_ACT_LIVE_INTERRUPT) {
                (void)passport_audio_live_interrupt();
                continue;
            }
            if (!was_cancelling && s_view.cancel_pending) {
                if (!passport_gateway_cancel()) finish_cancelled_operation();
                continue;
            }
            if (action==PASSPORT_ACT_DISCARD) {
                if (passport_gateway_abandon()) {
                    passport_clip_discard();(void)passport_finish_cancel(&s_view,"本次操作已取消");
                } else {
                    s_view.sending=true;s_view.cancel_pending=true;s_view.screen=PASSPORT_SCREEN_CANCELLING;
                    (void)passport_gateway_cancel();
                }
                continue;
            }
            if (action == PASSPORT_ACT_CONFIGURE || action == PASSPORT_ACT_ENABLE_CONFIG || action == PASSPORT_ACT_ADD_NETWORK) {
                request_bluetooth_config();
            } else if (action == PASSPORT_ACT_CLOSE_CONFIG) {
                close_bluetooth_config();
            } else if (action == PASSPORT_ACT_CONNECT_NETWORK) {
                s_view.network_target = selected;
                s_network_generation = expected_generation;
                if (device_busy() || passport_link_configuration_busy()) {
                    s_view.network_state = PASSPORT_NETWORK_BUSY;
                } else {
                    s_network_index = selected;
                    s_network_generation = expected_generation;
                    s_network_started = esp_timer_get_time();
                    s_network_queued = false;
                    s_view.network_state = PASSPORT_NETWORK_CONNECTING;
                    atomic_store(&s_device_busy, true);
                    passport_gateway_maintain(false);
                }
            } else if (action == PASSPORT_ACT_SELECT_AGENT) {
                if (passport_link_apply_selection(action, selected, expected_generation)) {
                    snprintf(s_view.notice, sizeof(s_view.notice), "已设为当前助手");
                } else snprintf(s_view.notice, sizeof(s_view.notice), "保存失败，请重试");
            }
            if (was_recording && (action == PASSPORT_ACT_CANCEL_RECORD || action == PASSPORT_ACT_RERECORD)) {
                passport_gateway_drop_preconnect();
                esp_err_t stopped = passport_audio_stop();
                if (stopped != ESP_OK) {
                    s_view.recording = true; s_view.sending = false;
                    s_view.screen = PASSPORT_SCREEN_CAPTURE;
                    snprintf(s_view.notice, sizeof(s_view.notice), "停止录音失败，请重试");
                    continue;
                }
                (void)passport_audio_release();
                passport_clip_discard();
                s_audio_test_active = false; s_audio_test_end = 0;
                was_recording = false;
            }
            if (!was_recording && s_view.recording) {
                if (!passport_gateway_abandon()) {
                    s_view.recording=false;s_view.sending=true;s_view.cancel_pending=true;
                    s_view.restart_after_cancel=true;s_view.screen=PASSPORT_SCREEN_CANCELLING;
                    (void)passport_gateway_cancel();continue;
                }
                /* I2S RX needs a contiguous DMA block. BLE can leave only a
                 * few KB of DMA-capable heap after a gateway probe, so give
                 * audio ownership of that memory for the whole capture. */
                if (passport_link_pause_bluetooth() != ESP_OK) {
                    s_view.recording = false;
                    s_view.sending = false;
                    s_view.screen = PASSPORT_SCREEN_HOME;
                    snprintf(s_view.reply, sizeof(s_view.reply), "设备忙，请重试");
                    passport_set_status(&s_view, PASSPORT_LINK_FAILED, "设备忙，请重试");
                    continue;
                }
                /* Diagnostic sampling discards PCM; physical recording retains
                 * a bounded clip for upload/retry. */
                esp_err_t start = event.kind == INPUT_AUDIO_TEST ? passport_audio_start() : passport_audio_capture();
                s_capture_generation++;
                s_auto_send_queued = false;
                passport_memory_report("capture_start");
                /* Connect and authenticate while the user speaks; the upload
                 * starts as soon as recording stops. */
                if (start == ESP_OK && event.kind != INPUT_AUDIO_TEST) (void)passport_gateway_preconnect();
                if (start != ESP_OK) {
                    (void)passport_audio_release();
                    s_view.recording = false;
                    s_view.sending = false;
                    s_view.screen = PASSPORT_SCREEN_HOME;
                    snprintf(s_view.reply, sizeof(s_view.reply), "录音不可用");
                    passport_set_status(&s_view, PASSPORT_LINK_FAILED, "录音不可用");
                }
                if (event.kind == INPUT_AUDIO_TEST) {
                    if (s_view.recording) {
                        s_audio_test_active = true;
                        s_audio_test_end = esp_timer_get_time() + 1200000;
                    }
                    else audio_diagnostic_reply("AUDIO_TEST failed");
                }
            } else if (was_recording && !s_view.recording && s_view.sending) {
                if (passport_audio_stop() != ESP_OK) {
                    (void)passport_audio_release();
                    s_view.sending = false;
                    s_view.screen = PASSPORT_SCREEN_HOME;
                    snprintf(s_view.reply, sizeof(s_view.reply), "录音失败");
                    passport_set_status(&s_view, PASSPORT_LINK_FAILED, "录音失败");
                } else if (s_audio_test_active) {
                    (void)passport_audio_release();
                } else if (!passport_audio_has_speech()) {
                    passport_gateway_drop_preconnect();
                    (void)passport_audio_release();
                    passport_clip_discard();
                    (void)passport_finish_cancel(&s_view, passport_lang_zh(&s_store)
                        ? "没有录音，请重新说话" : "No speech detected. Please try again.");
                } else {
                    (void)passport_gateway_send(&s_store);
                }
                if (s_audio_test_active) {
                    s_view.sending = false;
                    s_view.screen = PASSPORT_SCREEN_HOME;
                    snprintf(s_view.reply, sizeof(s_view.reply), "录音测试完成");
                    s_audio_test_active = false;
                    s_audio_test_end = 0;
                    audio_diagnostic_reply("AUDIO_TEST done");
                }
            } else if (!was_sending && s_view.sending) {
                (void)passport_gateway_send(&s_store);
            }
        }
        if (s_view.recording && !s_audio_test_active && !s_auto_send_queued && s_polled_button < 0 &&
            passport_audio_submit_ready()) {
            const app_input_t auto_send = { .kind = INPUT_AUTO_SEND,
                .capture_generation = s_capture_generation, .capture_action = PASSPORT_ACT_SEND };
            if (xQueueSend(s_inputs, &auto_send, 0) == pdTRUE) s_auto_send_queued = true;
        }
        if (s_audio_test_end != 0 && esp_timer_get_time() >= s_audio_test_end) {
            /* Same stop path as the physical second OK click, bounded to 1.2s.
             * This test consumes PCM for levels only, without storing/uploading it. */
            s_audio_test_end = 0;
            if (s_view.recording) enqueue_input(PASSPORT_BTN_OK, PASSPORT_CLICK);
        }
        if (s_view.recording && passport_audio_finished()) {
            if (passport_clip_full() && !s_audio_test_active) {
                enqueue_input(PASSPORT_BTN_OK, PASSPORT_CLICK);
            } else {
                (void)passport_audio_stop();
                s_view.recording = false;
                s_view.sending = false;
                s_view.screen = PASSPORT_SCREEN_HOME;
                (void)passport_audio_release();
                snprintf(s_view.reply, sizeof(s_view.reply), "录音失败");
                audio_diagnostic_reply("AUDIO_TEST read_failed");
                s_audio_test_active = false;
                s_audio_test_end = 0;
            }
        }
        /* Reap a completed worker even if the view changed earlier. */
        char *message = s_gateway_message;
        passport_send_state_t send = passport_gateway_poll(message, sizeof(s_gateway_message));
        if (s_device_check && !passport_gateway_active()) {
            passport_gateway_check_t result;
            passport_gateway_check_result(&result);
            char line[192];
            snprintf(line, sizeof(line), "CHECK %s gateway=%d ask=%d plugin=%u call=%u",
                send == PASSPORT_SEND_DONE ? "done" : send == PASSPORT_SEND_CANCELLED ? "cancelled" : "failed",
                result.gateway, result.ask, result.plugin, result.call);
            passport_link_diagnostic_reply(line);
            s_device_check = false;
            device_status();
        }
        if (s_view.recording && send == PASSPORT_SEND_FAILED) {
            /* The early connection failed; recording continues and the send
             * after it reconnects from scratch. */
            passport_gateway_drop_preconnect();
        }
        if (s_view.sending) {
            if (s_view.screen != PASSPORT_SCREEN_LIVE && !s_view.cancel_pending) {
                passport_gateway_transcript(s_view.heard, sizeof(s_view.heard));
                passport_chat_update(&s_view, send == PASSPORT_SEND_PARTIAL ? message : NULL);
                if (s_view.chat_pending && s_view.heard[0] && s_view.screen == PASSPORT_SCREEN_PROGRESS)
                    s_view.screen = PASSPORT_SCREEN_TALK;
            }
            snprintf(s_view.reply, sizeof(s_view.reply), "%s", message);
            if (s_view.screen == PASSPORT_SCREEN_LIVE) {
                s_view.live_speaking = send == PASSPORT_SEND_SPEAKING;
                s_view.live_waiting = send == PASSPORT_SEND_WAITING;
                s_view.live_ready = send == PASSPORT_SEND_LISTENING || send == PASSPORT_SEND_SPEAKING || send == PASSPORT_SEND_WAITING;
            }
            if (send==PASSPORT_SEND_PARTIAL && !s_view.cancel_pending && s_view.screen==PASSPORT_SCREEN_PROGRESS) {
                s_view.screen=PASSPORT_SCREEN_TALK;
                if (!s_view.chat_visible) s_view.reply_page=0;
            }
            if (s_view.reply_page>=passport_reply_pages(&s_view)) s_view.reply_page=passport_reply_pages(&s_view)-1;
            if (send==PASSPORT_SEND_CANCELLED || (s_view.cancel_pending && (send==PASSPORT_SEND_DONE || send==PASSPORT_SEND_FAILED))) {
                finish_cancelled_operation();
            } else if (send == PASSPORT_SEND_DONE) {
                if (!s_gateway_probe_ui) {
                    passport_clip_discard();
                    passport_finish_send(&s_view, true, false, message);
                } else {
                    s_view.sending = false;
                    if (s_view.screen == PASSPORT_SCREEN_PROGRESS || s_view.screen == PASSPORT_SCREEN_HOME || s_view.screen == PASSPORT_SCREEN_LIVE) {
                        if (passport_clip_size() > 0) passport_offer_retry(&s_view,"网关已连接，可重试");
                        else s_view.screen = PASSPORT_SCREEN_HOME;
                    }
                }
                s_gateway_probe_ui = false;
            } else if (send == PASSPORT_SEND_FAILED) {
                passport_finish_send(&s_view, false, s_view.screen != PASSPORT_SCREEN_LIVE &&
                    (passport_clip_size() > 0 || passport_gateway_has_pending_reply()), message);
                s_gateway_probe_ui = false;
            }
        }
        maintain_network_selection();
        maintain_bluetooth_mode();
        battery_refresh();
        s_view.wifi_online = passport_wifi_is_online();
        s_view.bluetooth_ready = passport_link_is_bluetooth_ready();
        s_view.bluetooth_connected = passport_link_is_bluetooth_connected();
        s_view.bluetooth_busy = passport_link_configuration_pending() ||
            (s_ble_config_requested && !s_view.bluetooth_ready) ||
            (s_ble_config_suppressed && passport_link_is_bluetooth_active());
        if (s_view.wifi_online && s_view.link != PASSPORT_LINK_GATEWAY) {
            passport_set_status(&s_view, PASSPORT_LINK_ONLINE, "已联网");
        } else if (!s_view.wifi_online && s_view.link != PASSPORT_LINK_OFF) {
            passport_set_status(&s_view, PASSPORT_LINK_OFF, "未联网");
        }
        if (s_view.recording || s_view.screen == PASSPORT_SCREEN_LIVE) {
            passport_audio_snapshot(s_view.waveform, PASSPORT_WAVEFORM_BARS);
        }
        power_maintain();
        atomic_store(&s_device_busy, device_busy());
        redraw();
        /* While recording and sending, the network worker (priority 3) runs
         * the TLS key exchange and then encrypts and uploads the clip, which
         * keeps the CPU busy for seconds. Keep this loop above it so the level
         * meter and the sending animation stay smooth; it only polls and
         * redraws, so it never holds the CPU for long. While sending, poll at
         * the spinner's rate so redraws do not slow the upload. */
        bool busy_ui = s_view.recording || s_view.sending;
        UBaseType_t want = busy_ui ? 5 : ESP_TASK_MAIN_PRIO;
        if (uxTaskPriorityGet(NULL) != want) vTaskPrioritySet(NULL, want);
        vTaskDelay(pdMS_TO_TICKS(s_view.recording || s_view.screen == PASSPORT_SCREEN_LIVE ? 20 : s_view.sending ? 70 : 50));
    }
}
