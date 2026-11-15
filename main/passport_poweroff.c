#include "passport_power.h"
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_wifi.h"

static void power_off_step(const char *step, esp_err_t error) {
    if (error != ESP_OK) ESP_LOGW("passport_power", "power-off continues: %s failed: %s", step, esp_err_to_name(error));
}

/* The board cannot cut its own supply, so power-off is deep sleep with every
 * peripheral stopped. Any function key restarts the application like a cold
 * boot. Order matters: the fuel gauge and codec share I2C and must be written
 * before the bus pins are released, and the key pin must go back to a digital
 * input before the low-level wake is armed or the device wakes at once. */
void passport_power_off(void) {
    passport_power_awake(true);
    (void)esp_wifi_stop();
    int level = 1;
    if (bsp_button_prepare_deep_sleep(&level) != ESP_OK) esp_restart();
    if (level != 1) esp_restart();
    power_off_step("key wake", esp_deep_sleep_enable_gpio_wakeup(1ULL << BSP_BTN_GPIO, ESP_GPIO_WAKEUP_GPIO_LOW));
    power_off_step("fuel gauge", bsp_battery_sleep());
    power_off_step("codec", bsp_audio_sleep());
    power_off_step("audio pins", bsp_audio_prepare_deep_sleep());
    power_off_step("shared bus", bsp_i2c_prepare_deep_sleep());
    if (!bsp_lvgl_lock(1000)) esp_restart();
    power_off_step("display", bsp_display_prepare_deep_sleep());
    esp_deep_sleep_start();
    esp_restart();
}
