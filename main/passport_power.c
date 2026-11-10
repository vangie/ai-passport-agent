#include "passport_power.h"
#include "esp_pm.h"
#include "sdkconfig.h"

static esp_pm_lock_handle_t s_cpu_lock;
static esp_pm_lock_handle_t s_sleep_lock;
static bool s_awake;

void passport_power_command_begin(void) {
    ESP_ERROR_CHECK(esp_pm_lock_acquire(s_cpu_lock));
    ESP_ERROR_CHECK(esp_pm_lock_acquire(s_sleep_lock));
}

void passport_power_command_end(void) {
    ESP_ERROR_CHECK(esp_pm_lock_release(s_sleep_lock));
    ESP_ERROR_CHECK(esp_pm_lock_release(s_cpu_lock));
}

void passport_power_awake(bool awake) {
    if (s_awake == awake) return;
    if (awake) passport_power_command_begin();
    else passport_power_command_end();
    s_awake = awake;
}

void passport_power_init(void) {
    ESP_ERROR_CHECK(esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "passport", &s_cpu_lock));
    ESP_ERROR_CHECK(esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "passport-ui", &s_sleep_lock));
    passport_power_awake(true);
    const esp_pm_config_t config = {
        .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = 40,
        .light_sleep_enable = true,
    };
    ESP_ERROR_CHECK(esp_pm_configure(&config));
}
