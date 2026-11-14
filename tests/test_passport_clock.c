#include <assert.h>
#include <stdio.h>
#include <string.h>
#define time fixture_time
#include "../main/passport_clock.c"
#undef time

static time_t current_time;
static unsigned starts, preparations, delays, sync_after;
static bool cancelled, fail_prepare;
time_t fixture_time(time_t *out) {
    if (out) *out = current_time;
    return current_time;
}
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *config) {
    preparations++;
    assert(!config->start && !config->wait_for_sync && config->num_of_servers == 3);
    assert(!strcmp(config->servers[0], "ntp.aliyun.com"));
    return fail_prepare ? ESP_ERR_NO_MEM : ESP_OK;
}
esp_err_t esp_netif_sntp_start(void) { starts++;return ESP_OK; }
bool passport_tls_cancelled(void) { return cancelled; }
void vTaskDelay(unsigned ticks) {
    assert(ticks == 100);
    delays++;
    if (sync_after && delays >= sync_after) current_time = 1800000000;
}
int main(void) {
    passport_clock_start();assert(!starts);
    fail_prepare = true;
    assert(passport_clock_prepare() == ESP_ERR_NO_MEM);
    fail_prepare = false;
    assert(passport_clock_prepare() == ESP_OK);
    assert(passport_clock_prepare() == ESP_OK && preparations == 2);
    passport_clock_start();assert(starts == 1 && !delays);
    sync_after = 3;
    assert(passport_clock_wait() && delays == 3);
    passport_clock_start();assert(starts == 1);
    delays = 0;assert(passport_clock_wait() && !delays);
    current_time = 0;sync_after = 0;
    passport_clock_start();assert(starts == 2);
    assert(!passport_clock_wait() && delays == 100);
    cancelled = true;delays = 0;
    assert(!passport_clock_wait() && !delays);
    puts("Passport clock: early start, sync, reconnect, timeout and cancellation PASS");
    return 0;
}
