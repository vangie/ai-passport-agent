#pragma once
#include <assert.h>
#include <stdbool.h>
typedef int esp_err_t;
typedef struct power_test_lock *esp_pm_lock_handle_t;
typedef struct {
    int max_freq_mhz;
    int min_freq_mhz;
    bool light_sleep_enable;
} esp_pm_config_t;
#define ESP_PM_CPU_FREQ_MAX 0
#define ESP_PM_NO_LIGHT_SLEEP 1
#define ESP_OK 0
#define ESP_ERROR_CHECK(result) assert((result) == ESP_OK)
esp_err_t esp_pm_lock_create(int, int, const char *, esp_pm_lock_handle_t *);
esp_err_t esp_pm_lock_acquire(esp_pm_lock_handle_t);
esp_err_t esp_pm_lock_release(esp_pm_lock_handle_t);
esp_err_t esp_pm_configure(const esp_pm_config_t *);
