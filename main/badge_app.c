#include "badge.h"

#include "bsp_button.h"
#include "bsp_pins.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_battery.h"
#include "badge_audio.h"
#include "badge_clip.h"
#include "badge_gateway.h"
#include "badge_memory.h"
#include "badge_radio.h"
#include "badge_reply.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "nvs_flash.h"
#include <stdlib.h>

static const char *TAG = "badge";

static badge_store_t s_store;
static badge_view_t s_view;
static QueueHandle_t s_inputs;
typedef enum { INPUT_KEY, INPUT_DIAG, INPUT_BATTERY, INPUT_AUDIO_TEST, INPUT_GATEWAY_TEST, INPUT_BLE_ON, INPUT_BLE_OFF, INPUT_HISTORY_TEST, INPUT_RESUME_REPLY, INPUT_RESTORE_CLIP, INPUT_LIVE_TEST } input_kind_t;
typedef struct {
    input_kind_t kind;
    badge_input_t key;
    char *run_id;
} app_input_t;
static int64_t s_audio_test_end;
static int64_t s_battery_next_us;
static bsp_battery_reading_t s_battery_reading;
static bool s_audio_test_active;
static bool s_gateway_probe_ui;
static bool s_restart_live;
static uint8_t s_saved_volume;
static bool s_ble_config_requested;
static bool s_ble_config_suppressed;
static int64_t s_ble_config_deadline_us;
static int64_t s_boot_us;
static int s_polled_button = -1;
static int64_t s_polled_button_down_us;


void badge_screen_open(void);
void badge_screen_show(const badge_view_t *view, const badge_store_t *store);
void badge_link_start(badge_store_t *store);
bool badge_link_is_bluetooth_ready(void);
bool badge_link_is_bluetooth_active(void);
esp_err_t badge_link_pause_bluetooth(void);
esp_err_t badge_link_resume_bluetooth(void);
bool badge_nvs_load(badge_store_t *store);
bool badge_nvs_save(const badge_store_t *store);
uint8_t badge_nvs_volume_load(void);
bool badge_nvs_volume_save(uint8_t volume);
bool badge_wifi_is_online(void);
esp_err_t badge_wifi_gateway_performance(bool enabled);
esp_err_t badge_wifi_prepare(void);
void badge_wifi_autoconnect(badge_store_t *store);
void badge_link_diagnostic_reply(const char *line);
bool badge_link_apply_selection(badge_action_t action, int index, uint32_t expected_generation);

bool badge_app_diagnostic_request(bool audio_test) {
    const app_input_t event = { .kind = audio_test ? INPUT_AUDIO_TEST : INPUT_DIAG };
    return s_inputs != NULL && xQueueSend(s_inputs, &event, 0) == pdTRUE;
}

bool badge_app_battery_request(void) {
    const app_input_t event = { .kind = INPUT_BATTERY };
    return s_inputs != NULL && xQueueSend(s_inputs, &event, 0) == pdTRUE;
}

bool badge_app_gateway_test(void) {
    const app_input_t event = { .kind = INPUT_GATEWAY_TEST };
    return s_inputs && xQueueSend(s_inputs, &event, 0) == pdTRUE;
}
bool badge_app_live_test(void) {
    const app_input_t event = { .kind = INPUT_LIVE_TEST };
    return s_inputs && xQueueSend(s_inputs, &event, 0) == pdTRUE;
}

bool badge_app_memory_test(bool history, bool enable) {
    const app_input_t event = { .kind=history ? INPUT_HISTORY_TEST : enable ? INPUT_BLE_ON : INPUT_BLE_OFF };
    return s_inputs && xQueueSend(s_inputs,&event,0)==pdTRUE;
}

bool badge_app_key_request(badge_btn_t key, badge_press_t press) {
    const app_input_t event = { .kind = INPUT_KEY, .key = { .btn = key, .press = press } };
    return s_inputs && xQueueSend(s_inputs, &event, 0) == pdTRUE;
}

bool badge_app_resume_reply_request(const char *run_id) {
    if (!s_inputs || !run_id || !*run_id || strlen(run_id)>=80) return false;
    char *copy=strdup(run_id);if (!copy) return false;
    const app_input_t event={.kind=INPUT_RESUME_REPLY,.run_id=copy};
    if (xQueueSend(s_inputs,&event,0)!=pdTRUE) { free(copy);return false; }
    return true;
}

bool badge_app_restore_clip_request(const char *arguments) {
    if (!s_inputs || !arguments || strlen(arguments)>=96) return false;
    char *copy=strdup(arguments);if (!copy) return false;
    const app_input_t event={.kind=INPUT_RESTORE_CLIP,.run_id=copy};
    if (xQueueSend(s_inputs,&event,0)!=pdTRUE) { free(copy);return false; }
    return true;
}

static void audio_diagnostic_reply(const char *prefix) {
    badge_audio_diagnostics_t audio;
    badge_audio_diagnostics(&audio);
    static char line[640];
    char *send_message = s_view.heard;
    char identity[65];
    badge_send_state_t send = badge_gateway_poll(send_message, sizeof(s_view.heard));
    badge_gateway_identity(identity);
    snprintf(line, 640,
             "%s phase=%s error=%s chunks=%lu peak=%lu worker=%d recording=%d "
             "screen=%d retry=%d unread=%d ok_action=%d volume=%u free=%u dma_free=%u dma_largest=%u adc_mv=%d wifi=%d ble=%d send=%d device=%s message=%.200s",
             prefix, audio.phase, esp_err_to_name(audio.error),
             (unsigned long)audio.chunks, (unsigned long)audio.peak,
             audio.worker_owned, s_view.recording, s_view.screen, s_view.retry_send, s_view.reply_unread,
             badge_key_binding(&s_view,&s_store,BADGE_BTN_OK).action,
             (unsigned)s_view.speaker_volume,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA),
             bsp_button_read_mv(), badge_wifi_is_online(), badge_link_is_bluetooth_ready(), send, identity, send_message);
    badge_link_diagnostic_reply(line);
    snprintf(line, sizeof(line), "PCM mean_square=%lu dc=%ld clipped=%lu dropped=%lu played=%lu voice=%lu silence=%lu suppressed=%lu response=%d muted=%d rms=%lu threshold=%lu voice_dropped=%lu",
             (unsigned long)audio.mean_square, (long)audio.dc, (unsigned long)audio.clipped,
             (unsigned long)badge_audio_live_dropped(), (unsigned long)audio.played,
             (unsigned long)audio.voice_chunks, (unsigned long)audio.silence_chunks,
             (unsigned long)audio.input_suppressed, audio.response, audio.muted,
             (unsigned long)audio.input_rms, (unsigned long)audio.input_threshold,
             (unsigned long)audio.voice_dropped);
    badge_link_diagnostic_reply(line);
    snprintf(line, sizeof(line), "CAPTURE rate=%u bits=16 channels=1 samples=%lu ms=%lu read_gap_ms=%lu clipped_total=%lu queue_max=%lu",
             (unsigned)audio.sample_rate,
             (unsigned long)audio.chunks * 256ul, (unsigned long)audio.capture_ms,
             (unsigned long)audio.read_gap_ms, (unsigned long)audio.clipped_total,
             (unsigned long)audio.input_high_water);
    badge_link_diagnostic_reply(line);
    badge_gateway_diagnostic(line, sizeof(line));
    badge_link_diagnostic_reply(line);
    badge_gateway_timing_diagnostic(line, sizeof(line));
    badge_link_diagnostic_reply(line);
    snprintf(line, sizeof(line), "INPUT muted_chunks=%lu response_chunks=%lu echo_chunks=%lu clipped=%lu play_stalls=%lu play_gap_ms=%lu",
        (unsigned long)audio.mute_chunks, (unsigned long)audio.response_chunks,
        (unsigned long)audio.echo_chunks, (unsigned long)audio.input_clipped,
        (unsigned long)audio.play_stalls, (unsigned long)audio.play_gap_ms);
    badge_link_diagnostic_reply(line);
    badge_memory_report("app_diag");
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
    badge_link_diagnostic_reply(line);
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
    if (!bsp_lvgl_lock(50)) {
        return;
    }
    badge_screen_show(&s_view, &s_store);
    bsp_lvgl_unlock();
}

static void request_bluetooth_config(void) {
    s_ble_config_suppressed = false;
    s_ble_config_requested = true;
    s_ble_config_deadline_us = esp_timer_get_time() + 120LL * 1000 * 1000;
    badge_gateway_maintain(false);
}

static void maintain_bluetooth_mode(void) {
    static int64_t retry_after;
    int64_t now = esp_timer_get_time();
    if (now < retry_after) return;
    if (s_ble_config_requested && now >= s_ble_config_deadline_us) s_ble_config_requested = false;
    bool online = badge_wifi_is_online();
    bool want = badge_radio_wants_ble((uint64_t)(now-s_boot_us)/1000, (uint64_t)now/1000,
        s_ble_config_requested ? (uint64_t)s_ble_config_deadline_us/1000 : 0,
        online,s_store.count>0,
        s_view.recording || s_view.sending || badge_gateway_active());
    /* A saved network that is still offline needs the DMA heap to rejoin.
     * BLE can return once Wi-Fi has an address, or when configuration is
     * explicitly requested. */
    if (!online && s_store.count > 0) want = false;
    if (s_ble_config_suppressed) want = false;
    if (want) {
        badge_gateway_maintain(false);
        if (!badge_gateway_quiescent()) return;
    }
    esp_err_t err = ESP_OK;
    if (!want && badge_link_is_bluetooth_active()) err=badge_link_pause_bluetooth();
    else if (want && !badge_link_is_bluetooth_active()) {
        err=badge_audio_release();
        if (err==ESP_OK) err=badge_link_resume_bluetooth();
    }
    if (err!=ESP_OK) retry_after=now+1000000;
    if (!want) badge_gateway_maintain(online && err==ESP_OK);
}

static void enqueue_input(badge_btn_t btn, badge_press_t press) {
    if (s_inputs == NULL) {
        return;
    }
    const app_input_t input = { .kind = INPUT_KEY, .key = { .btn = btn, .press = press } };
    (void)xQueueSend(s_inputs, &input, 0);
}

static void finish_cancelled_operation(void) {
    if (!badge_gateway_abandon()) return;
    badge_clip_discard();
    bool restart=badge_finish_cancel(&s_view,"本次操作已取消");
    s_gateway_probe_ui=false;
    if (s_restart_live) {
        s_restart_live = false;
        s_view.screen = BADGE_SCREEN_HOME;
        enqueue_input(BADGE_BTN_DOWN,BADGE_CLICK);
    } else if (restart) {
        s_view.screen=BADGE_SCREEN_TALK;
        enqueue_input(BADGE_BTN_OK,BADGE_CLICK);
    }
}

static int decode_button_mv(int mv) {
    static const uint16_t windows[BSP_BTN_COUNT][2] = BSP_BTN_MV_TABLE;
    if (mv < 0) return -1;
    for (int i = 0; i < BSP_BTN_COUNT; i++) {
        if (mv >= windows[i][0] && mv < windows[i][1]) return i;
    }
    return -1;
}

static void poll_button(void) {
    int mv = bsp_button_read_mv();
    int button = decode_button_mv(mv);
    int64_t now = esp_timer_get_time();
    if (button >= 0) {
        if (s_polled_button < 0) {
            s_polled_button = button;
            s_polled_button_down_us = now;
        }
        return;
    }
    if (s_polled_button >= 0) {
        /* Exactly one action on release, independent of hold duration. */
        if (now - s_polled_button_down_us >= 30000)
            enqueue_input((badge_btn_t)s_polled_button, BADGE_CLICK);
        s_polled_button = -1;
    }
}

void app_main(void) {
    badge_memory_init();
    badge_memory_report("boot");
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

    badge_store_init(&s_store);
    badge_nvs_load(&s_store);
    s_boot_us = esp_timer_get_time();
    badge_view_init(&s_view);
    s_view.speaker_volume = s_saved_volume = badge_nvs_volume_load();
    (void)badge_audio_set_volume(s_saved_volume);
    if (!bsp_lvgl_lock(1000)) {
        return;
    }
    badge_screen_open();
    badge_screen_show(&s_view, &s_store);
    bsp_lvgl_unlock();

    s_inputs = xQueueCreate(8, sizeof(app_input_t));
    if (s_inputs == NULL) {
        ESP_LOGE(TAG, "button event queue allocation failed");
        return;
    }
    /* Poll the shared ADC ladder directly so a missed component CLICK event
     * cannot make the physical buttons appear dead. */
    ESP_ERROR_CHECK(bsp_button_init(NULL, NULL));
    ESP_ERROR_CHECK(badge_wifi_prepare());
    redraw();
    badge_link_start(&s_store);
    badge_wifi_autoconnect(&s_store);
    badge_memory_report("app_ready");
    ESP_LOGI(TAG, "badge ready, %u agents", (unsigned)s_store.agent_count);
    while (true) {
        app_input_t event;
        while (xQueueReceive(s_inputs, &event, 0) == pdTRUE) {
            if (event.kind == INPUT_RESTORE_CLIP) {
                unsigned bytes;unsigned long crc;char id[33],extra;
                esp_err_t restored=ESP_ERR_INVALID_ARG;
                if (!s_view.recording && !s_view.sending &&
                    sscanf(event.run_id,"%u %32s %lx %c",&bytes,id,&crc,&extra)==3)
                    restored=badge_clip_restore(bytes,id,(uint32_t)crc);
                free(event.run_id);
                if (restored==ESP_OK) {
                    badge_offer_retry(&s_view,"录音已恢复，可重试");
                    badge_link_diagnostic_reply("RESTORE_CLIP ok");
                } else badge_link_diagnostic_reply("RESTORE_CLIP failed");
                continue;
            }
            if (event.kind == INPUT_RESUME_REPLY) {
                if (!s_view.recording && !s_view.sending && badge_gateway_resume_reply(event.run_id)==ESP_OK) {
                    s_view.sending=true;s_view.screen=BADGE_SCREEN_PROGRESS;s_gateway_probe_ui=false;
                }
                free(event.run_id);continue;
            }
            if (event.kind == INPUT_BLE_ON || event.kind == INPUT_BLE_OFF) {
                if (s_view.recording || s_view.sending) { badge_link_diagnostic_reply("BLE_TEST busy"); continue; }
                if (event.kind == INPUT_BLE_ON) request_bluetooth_config();
                else { s_ble_config_suppressed=true; s_ble_config_requested=false; s_ble_config_deadline_us=0; (void)badge_link_pause_bluetooth(); }
                badge_link_diagnostic_reply(event.kind==INPUT_BLE_ON ? "BLE_TEST on" : "BLE_TEST off");
                continue;
            }
            if (event.kind == INPUT_GATEWAY_TEST || event.kind == INPUT_HISTORY_TEST || event.kind == INPUT_LIVE_TEST) {
                if (s_view.recording || s_view.sending) { badge_link_diagnostic_reply("GATEWAY_TEST busy");continue; }
                esp_err_t probe = event.kind == INPUT_LIVE_TEST ? badge_gateway_live_test() :
                    event.kind==INPUT_HISTORY_TEST ? badge_gateway_history_probe() : badge_gateway_probe();
                if (probe!=ESP_OK) { badge_link_diagnostic_reply("GATEWAY_TEST failed to start");continue; }
                s_gateway_probe_ui = true;
                s_view.sending = true;
                s_view.screen = event.kind == INPUT_LIVE_TEST ? BADGE_SCREEN_LIVE : BADGE_SCREEN_PROGRESS;
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
                if (s_view.recording || s_view.sending || s_view.retry_send || s_view.reply_unread || s_audio_test_end != 0 ||
                    s_view.screen != BADGE_SCREEN_HOME) {
                    badge_link_diagnostic_reply("AUDIO_TEST busy");
                    continue;
                }
                audio_diagnostic_reply("AUDIO_TEST before");
                event.key = (badge_input_t){ .btn = BADGE_BTN_OK, .press = BADGE_CLICK };
            }
            badge_input_t input = event.key;
            bool was_recording = s_view.recording;
            bool was_sending = s_view.sending;
            bool was_cancelling = s_view.cancel_pending;
            badge_screen_t was_screen = s_view.screen;
            int selected = s_view.cursor;
            uint32_t expected_generation = s_store.generation;
            uint8_t previous_volume = s_view.speaker_volume;
            badge_action_t action = badge_on_key(&s_view, &s_store, input.btn, input.press);
            if (action == BADGE_ACT_VOLUME_UP || action == BADGE_ACT_VOLUME_DOWN) {
                if (badge_audio_set_volume(s_view.speaker_volume) != ESP_OK) {
                    s_view.speaker_volume = previous_volume;
                    snprintf(s_view.notice, sizeof(s_view.notice), "请先结束通话");
                }
                continue;
            }
            if (was_screen == BADGE_SCREEN_VOLUME && action == BADGE_ACT_BACK && s_saved_volume != s_view.speaker_volume) {
                if (badge_nvs_volume_save(s_view.speaker_volume)) s_saved_volume = s_view.speaker_volume;
                else {
                    s_view.screen = BADGE_SCREEN_VOLUME;
                    snprintf(s_view.notice, sizeof(s_view.notice), "保存失败，请重试");
                }
            }
            ESP_LOGI(TAG, "key=%d action=%d screen=%d recording=%d sending=%d",
                     input.btn, action, s_view.screen, s_view.recording, s_view.sending);
            if (action == BADGE_ACT_LIVE) {
                if (!badge_gateway_abandon() || badge_gateway_live(badge_lang_zh(&s_store)) != ESP_OK) {
                    s_view.sending = false; s_view.screen = BADGE_SCREEN_HOME;
                    snprintf(s_view.reply, sizeof(s_view.reply), "实时语音启动失败，请检查网络");
                }
                continue;
            }
            if (action == BADGE_ACT_LIVE_MUTE || action == BADGE_ACT_LIVE_UNMUTE) {
                badge_audio_live_mute(s_view.live_muted);
                continue;
            }
            if (action == BADGE_ACT_LIVE_RECONNECT) s_restart_live = true;
            if (!was_cancelling && s_view.cancel_pending) {
                if (!badge_gateway_cancel()) finish_cancelled_operation();
                continue;
            }
            if (action==BADGE_ACT_DISCARD) {
                if (badge_gateway_abandon()) {
                    badge_clip_discard();(void)badge_finish_cancel(&s_view,"本次操作已取消");
                } else {
                    s_view.sending=true;s_view.cancel_pending=true;s_view.screen=BADGE_SCREEN_CANCELLING;
                    (void)badge_gateway_cancel();
                }
                continue;
            }
            if (was_screen == BADGE_SCREEN_ADD && s_view.screen != BADGE_SCREEN_ADD) {
                s_ble_config_requested = false;
                s_ble_config_suppressed = true;
                s_ble_config_deadline_us = 0;
            }
            if (action == BADGE_ACT_CONFIGURE || action == BADGE_ACT_ENABLE_CONFIG || action == BADGE_ACT_ADD_NETWORK) {
                request_bluetooth_config();
            } else if (action == BADGE_ACT_CLOSE_CONFIG) {
                s_ble_config_requested = false;
                s_ble_config_suppressed = true;
                s_ble_config_deadline_us = 0;
                (void)badge_link_pause_bluetooth();
                snprintf(s_view.notice, sizeof(s_view.notice), "蓝牙配置已关闭");
            } else if (action == BADGE_ACT_PRIORITIZE || action == BADGE_ACT_SELECT_AGENT) {
                if (badge_link_apply_selection(action, selected, expected_generation)) {
                    snprintf(s_view.notice, sizeof(s_view.notice), "%s",
                             action == BADGE_ACT_PRIORITIZE ? "已设为优先网络" : "已设为当前助手");
                    if (action == BADGE_ACT_PRIORITIZE) { s_view.cursor = 0; s_view.screen = BADGE_SCREEN_NETS; }
                } else snprintf(s_view.notice, sizeof(s_view.notice), "保存失败，请重试");
            }
            if (was_recording && (action == BADGE_ACT_CANCEL_RECORD || action == BADGE_ACT_RERECORD)) {
                badge_gateway_drop_preconnect();
                esp_err_t stopped = badge_audio_stop();
                if (stopped != ESP_OK) {
                    s_view.recording = true; s_view.sending = false;
                    s_view.screen = BADGE_SCREEN_CAPTURE;
                    snprintf(s_view.notice, sizeof(s_view.notice), "停止录音失败，请重试");
                    continue;
                }
                (void)badge_audio_release();
                badge_clip_discard();
                s_audio_test_active = false; s_audio_test_end = 0;
                was_recording = false;
            }
            if (!was_recording && s_view.recording) {
                if (!badge_gateway_abandon()) {
                    s_view.recording=false;s_view.sending=true;s_view.cancel_pending=true;
                    s_view.restart_after_cancel=true;s_view.screen=BADGE_SCREEN_CANCELLING;
                    (void)badge_gateway_cancel();continue;
                }
                /* I2S RX needs a contiguous DMA block. BLE can leave only a
                 * few KB of DMA-capable heap after a gateway probe, so give
                 * audio ownership of that memory for the whole capture. */
                if (badge_link_pause_bluetooth() != ESP_OK) {
                    s_view.recording = false;
                    s_view.sending = false;
                    s_view.screen = BADGE_SCREEN_HOME;
                    snprintf(s_view.reply, sizeof(s_view.reply), "设备忙，请重试");
                    badge_set_status(&s_view, BADGE_LINK_FAILED, "设备忙，请重试");
                    continue;
                }
                /* Diagnostic sampling discards PCM; physical recording retains
                 * a bounded clip for upload/retry. */
                esp_err_t start = event.kind == INPUT_AUDIO_TEST ? badge_audio_start() : badge_audio_capture();
                badge_memory_report("capture_start");
                /* Connect and authenticate while the user speaks; the upload
                 * starts as soon as recording stops. */
                if (start == ESP_OK && event.kind != INPUT_AUDIO_TEST) (void)badge_gateway_preconnect();
                if (start != ESP_OK) {
                    (void)badge_audio_release();
                    s_view.recording = false;
                    s_view.sending = false;
                    s_view.screen = BADGE_SCREEN_HOME;
                    snprintf(s_view.reply, sizeof(s_view.reply), "录音不可用");
                    badge_set_status(&s_view, BADGE_LINK_FAILED, "录音不可用");
                }
                if (event.kind == INPUT_AUDIO_TEST) {
                    if (s_view.recording) {
                        s_audio_test_active = true;
                        s_audio_test_end = esp_timer_get_time() + 1200000;
                    }
                    else audio_diagnostic_reply("AUDIO_TEST failed");
                }
            } else if (was_recording && !s_view.recording && s_view.sending) {
                if (badge_audio_stop() != ESP_OK) {
                    (void)badge_audio_release();
                    s_view.sending = false;
                    s_view.screen = BADGE_SCREEN_HOME;
                    snprintf(s_view.reply, sizeof(s_view.reply), "录音失败");
                    badge_set_status(&s_view, BADGE_LINK_FAILED, "录音失败");
                } else if (s_audio_test_active) {
                    (void)badge_audio_release();
                } else {
                    (void)badge_gateway_send(&s_store);
                }
                if (s_audio_test_active) {
                    s_view.sending = false;
                    s_view.screen = BADGE_SCREEN_HOME;
                    snprintf(s_view.reply, sizeof(s_view.reply), "录音测试完成");
                    s_audio_test_active = false;
                    s_audio_test_end = 0;
                    audio_diagnostic_reply("AUDIO_TEST done");
                }
            } else if (!was_sending && s_view.sending) {
                (void)badge_gateway_send(&s_store);
            }
        }
        if (s_audio_test_end != 0 && esp_timer_get_time() >= s_audio_test_end) {
            /* Same stop path as the physical second OK click, bounded to 1.2s.
             * This test consumes PCM for levels only, without storing/uploading it. */
            s_audio_test_end = 0;
            if (s_view.recording) enqueue_input(BADGE_BTN_OK, BADGE_CLICK);
        }
        if (s_view.recording && badge_audio_finished()) {
            if (badge_clip_full() && !s_audio_test_active) {
                enqueue_input(BADGE_BTN_OK, BADGE_CLICK);
            } else {
                (void)badge_audio_stop();
                s_view.recording = false;
                s_view.sending = false;
                s_view.screen = BADGE_SCREEN_HOME;
                (void)badge_audio_release();
                snprintf(s_view.reply, sizeof(s_view.reply), "录音失败");
                audio_diagnostic_reply("AUDIO_TEST read_failed");
                s_audio_test_active = false;
                s_audio_test_end = 0;
            }
        }
        /* Reap a completed worker even if the view changed earlier. */
        char *message = s_view.heard;
        badge_send_state_t send = badge_gateway_poll(message, sizeof(s_view.heard));
        if (s_view.recording && send == BADGE_SEND_FAILED) {
            /* The early connection failed; recording continues and the send
             * after it reconnects from scratch. */
            badge_gateway_drop_preconnect();
        }
        if (s_view.sending) {
            snprintf(s_view.reply, sizeof(s_view.reply), "%s", message);
            if (s_view.screen == BADGE_SCREEN_LIVE) {
                s_view.live_speaking = send == BADGE_SEND_SPEAKING;
                s_view.live_waiting = send == BADGE_SEND_WAITING;
                s_view.live_ready = send == BADGE_SEND_LISTENING || send == BADGE_SEND_SPEAKING || send == BADGE_SEND_WAITING;
            }
            if (send==BADGE_SEND_PARTIAL && !s_view.cancel_pending && s_view.screen==BADGE_SCREEN_PROGRESS) {
                s_view.screen=BADGE_SCREEN_TALK;
                s_view.reply_page=0;
            }
            if (s_view.reply_page>=badge_reply_pages(&s_view)) s_view.reply_page=badge_reply_pages(&s_view)-1;
            if (send==BADGE_SEND_CANCELLED || (s_view.cancel_pending && (send==BADGE_SEND_DONE || send==BADGE_SEND_FAILED))) {
                finish_cancelled_operation();
            } else if (send == BADGE_SEND_DONE) {
                if (!s_gateway_probe_ui) {
                    badge_clip_discard();
                    badge_finish_send(&s_view, true, false, message);
                } else {
                    s_view.sending = false;
                    if (s_view.screen == BADGE_SCREEN_PROGRESS || s_view.screen == BADGE_SCREEN_HOME || s_view.screen == BADGE_SCREEN_LIVE) {
                        if (badge_clip_size() > 0) badge_offer_retry(&s_view,"网关已连接，可重试");
                        else s_view.screen = BADGE_SCREEN_HOME;
                    }
                }
                s_gateway_probe_ui = false;
            } else if (send == BADGE_SEND_FAILED) {
                badge_finish_send(&s_view, false, s_view.screen != BADGE_SCREEN_LIVE &&
                    (badge_clip_size() > 0 || badge_gateway_has_pending_reply()), message);
                s_gateway_probe_ui = false;
            }
        }
        maintain_bluetooth_mode();
        battery_refresh();
        s_view.wifi_online = badge_wifi_is_online();
        s_view.bluetooth_ready = badge_link_is_bluetooth_ready();
        if (s_view.wifi_online && s_view.link != BADGE_LINK_GATEWAY) {
            badge_set_status(&s_view, BADGE_LINK_ONLINE, "已联网");
        } else if (!s_view.wifi_online && s_view.link != BADGE_LINK_OFF) {
            badge_set_status(&s_view, BADGE_LINK_OFF, "未联网");
        }
        if (s_view.recording || s_view.screen == BADGE_SCREEN_LIVE) {
            badge_audio_snapshot(s_view.waveform, BADGE_WAVEFORM_BARS);
        }
        poll_button();
        redraw();
        /* While recording and sending, the network worker (priority 3) runs
         * the TLS key exchange and then encrypts and uploads the clip, which
         * keeps the CPU busy for seconds. Keep this loop above it so the level
         * meter and the sending animation stay smooth; it only polls and
         * redraws, so it never holds the CPU for long. While sending, poll at
         * the spinner's rate so redraws do not slow the upload. */
        bool busy_ui = s_view.recording || s_view.sending;
        UBaseType_t want = busy_ui ? 4 : ESP_TASK_MAIN_PRIO;
        if (uxTaskPriorityGet(NULL) != want) vTaskPrioritySet(NULL, want);
        vTaskDelay(pdMS_TO_TICKS(s_view.recording ? 20 : s_view.sending ? 70 : 50));
    }
}
