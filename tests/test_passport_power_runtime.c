#include <assert.h>
#include <stdio.h>
#include "../main/passport_power.c"

struct power_test_lock { unsigned count; };
static struct power_test_lock locks[2];

esp_err_t esp_pm_lock_create(int type, int unused, const char *name, esp_pm_lock_handle_t *handle) {
    assert(type >= 0 && type < 2 && unused == 0 && name);
    *handle = &locks[type];
    return ESP_OK;
}
esp_err_t esp_pm_lock_acquire(esp_pm_lock_handle_t handle) {
    assert(handle);
    handle->count++;
    return ESP_OK;
}
esp_err_t esp_pm_lock_release(esp_pm_lock_handle_t handle) {
    assert(handle && handle->count);
    handle->count--;
    return ESP_OK;
}
esp_err_t esp_pm_configure(const esp_pm_config_t *config) {
    assert(config->max_freq_mhz == 160 && config->min_freq_mhz == 40 && config->light_sleep_enable);
    assert(locks[0].count == 1 && locks[1].count == 1);
    return ESP_OK;
}
int main(void) {
    passport_power_init();
    for (unsigned cycle = 0; cycle < 100; cycle++) {
        passport_power_awake(true);
        assert(locks[0].count == 1 && locks[1].count == 1);
        passport_power_command_begin();
        passport_power_command_begin();
        passport_power_awake(false);
        passport_power_awake(false);
        assert(locks[0].count == 2 && locks[1].count == 2);
        passport_power_command_end();
        passport_power_command_end();
        assert(!locks[0].count && !locks[1].count);
        passport_power_command_begin();
        passport_power_awake(true);
        passport_power_command_end();
        assert(locks[0].count == 1 && locks[1].count == 1);
    }
    puts("Passport power lock ownership tests: PASS");
}
