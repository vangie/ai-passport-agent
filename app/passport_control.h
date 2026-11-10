#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static inline bool passport_control_volume(const char *line, unsigned *expected, unsigned *desired) {
    if (strncmp(line, "VOLUME ", 7)) return false;
    const char *cursor = line + 7;
    unsigned values[2] = {0};
    for (unsigned index = 0; index < 2; index++) {
        unsigned digits = 0;
        while (*cursor >= '0' && *cursor <= '9') {
            if (++digits > 3) return false;
            values[index] = values[index] * 10 + (unsigned)(*cursor++ - '0');
        }
        if (!digits || values[index] > 100) return false;
        if (index == 0 && *cursor++ != ' ') return false;
    }
    if (*cursor) return false;
    *expected = values[0];
    *desired = values[1];
    return true;
}

static inline void passport_control_status(char *out, size_t capacity, unsigned volume, int battery,
                                       bool wifi, bool busy, bool sleeping, bool usb_host) {
    snprintf(out, capacity, "DEVICE volume=%u battery=%d wifi=%d busy=%d sleeping=%d usb_host=%d idle_s=60",
             volume, battery, wifi, busy, sleeping, usb_host);
}
