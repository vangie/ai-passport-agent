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

/* "POWERCFG <expected standby> <expected power-off> <standby> <power-off>", all in
 * seconds. The expected pair is the value the client last read, so two clients
 * cannot overwrite each other silently. Range and ordering are checked by the
 * power policy, not here. */
static inline bool passport_control_power(const char *line, unsigned values[4]) {
    if (strncmp(line, "POWERCFG ", 9)) return false;
    const char *cursor = line + 9;
    for (unsigned index = 0; index < 4; index++) {
        unsigned digits = 0;
        values[index] = 0;
        while (*cursor >= '0' && *cursor <= '9') {
            if (++digits > 5) return false;
            values[index] = values[index] * 10 + (unsigned)(*cursor++ - '0');
        }
        if (!digits) return false;
        if (index < 3 && *cursor++ != ' ') return false;
    }
    return !*cursor;
}

static inline void passport_control_status(char *out, size_t capacity, unsigned volume, int battery,
                                       bool wifi, bool busy, bool sleeping, bool usb_host,
                                       unsigned standby_s, unsigned poweroff_s) {
    snprintf(out, capacity, "DEVICE volume=%u battery=%d wifi=%d busy=%d sleeping=%d usb_host=%d idle_s=%u poweroff_s=%u",
             volume, battery, wifi, busy, sleeping, usb_host, standby_s, poweroff_s);
}
