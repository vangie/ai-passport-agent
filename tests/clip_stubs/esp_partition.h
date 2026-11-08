#pragma once
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
typedef struct { size_t size; } esp_partition_t;
#define ESP_PARTITION_TYPE_DATA 1
const esp_partition_t *esp_partition_find_first(int type, int subtype, const char *label);
esp_err_t esp_partition_erase_range(const esp_partition_t *, size_t, size_t);
esp_err_t esp_partition_write(const esp_partition_t *, size_t, const void *, size_t);
esp_err_t esp_partition_read(const esp_partition_t *, size_t, void *, size_t);
