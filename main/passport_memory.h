#pragma once
#include <stddef.h>
void passport_memory_init(void);
void passport_memory_report(const char *phase);
void passport_memory_format(char *out, size_t cap, const char *phase);
void passport_memory_failures_reset(void);
