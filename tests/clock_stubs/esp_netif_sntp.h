#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

#define ESP_SNTP_SERVER_LIST(...) { __VA_ARGS__ }
#define ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(count, list) \
    { .start = true, .wait_for_sync = true, .num_of_servers = (count), .servers = list }
typedef struct {
    bool start, wait_for_sync;
    size_t num_of_servers;
    const char *servers[3];
} esp_sntp_config_t;
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *config);
esp_err_t esp_netif_sntp_start(void);
