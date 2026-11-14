#pragma once
#include "esp_err.h"
#include <stdbool.h>

esp_err_t passport_clock_prepare(void);
void passport_clock_start(void);
bool passport_clock_wait(void);
