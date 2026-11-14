#include "passport_clock.h"
#include "passport_tls.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <time.h>

static bool s_prepared;

esp_err_t passport_clock_prepare(void) {
    if (s_prepared) return ESP_OK;
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(3,
        ESP_SNTP_SERVER_LIST("ntp.aliyun.com", "pool.ntp.org", "time.cloudflare.com"));
    config.start = false;
    config.wait_for_sync = false;
    esp_err_t result = esp_netif_sntp_init(&config);
    if (result == ESP_OK) s_prepared = true;
    return result;
}

void passport_clock_start(void) {
    if (s_prepared && time(NULL) <= 1700000000) (void)esp_netif_sntp_start();
}

bool passport_clock_wait(void) {
    for (unsigned attempt = 0; attempt < 100 && time(NULL) <= 1700000000; attempt++) {
        if (passport_tls_cancelled()) return false;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return !passport_tls_cancelled() && time(NULL) > 1700000000;
}
