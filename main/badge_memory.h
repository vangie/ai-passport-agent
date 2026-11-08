#pragma once
#include <stddef.h>
void badge_memory_init(void);
void badge_memory_report(const char *phase);
void badge_memory_format(char *out, size_t cap, const char *phase);
void badge_memory_failures_reset(void);
