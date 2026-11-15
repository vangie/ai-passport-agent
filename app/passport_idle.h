#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The steps offered on the device and in the web studio for the two idle
 * times, in seconds. Power-off 0 means never and is the last step. Power-off
 * is always at least PASSPORT_IDLE_GAP_S later than screen-off. */
#define PASSPORT_IDLE_GAP_S 60U
#define PASSPORT_STANDBY_STEP_COUNT 6
#define PASSPORT_POWEROFF_STEP_COUNT 7

static const uint16_t passport_standby_steps[PASSPORT_STANDBY_STEP_COUNT] = { 15, 30, 60, 120, 300, 600 };
static const uint16_t passport_poweroff_steps[PASSPORT_POWEROFF_STEP_COUNT] = { 300, 600, 900, 1800, 3600, 7200, 0 };

static inline bool passport_idle_allowed(unsigned standby_s, unsigned poweroff_s) {
    return !poweroff_s || poweroff_s >= standby_s + PASSPORT_IDLE_GAP_S;
}

/* Position of a stored value on its scale. A value between two steps (set by
 * an older page or by hand) sits on the nearest step below it, so one press
 * in either direction lands on a real step. */
static inline unsigned passport_idle_index(const uint16_t *steps, size_t count, unsigned value) {
    unsigned index = 0;
    if (!value && !steps[count - 1]) return (unsigned)count - 1;
    for (size_t step = 0; step < count; step++)
        if (steps[step] && steps[step] <= value) index = (unsigned)step;
    return index;
}

static inline bool passport_idle_standby_can(unsigned standby_s, bool later) {
    unsigned index = passport_idle_index(passport_standby_steps, PASSPORT_STANDBY_STEP_COUNT, standby_s);
    return later ? index + 1 < PASSPORT_STANDBY_STEP_COUNT : index > 0;
}

/* Moves screen-off one step. A later screen-off pushes power-off to the first
 * step that keeps the gap; never stays never. */
static inline void passport_idle_standby_step(uint16_t *standby_s, uint16_t *poweroff_s, bool later) {
    if (!passport_idle_standby_can(*standby_s, later)) return;
    unsigned index = passport_idle_index(passport_standby_steps, PASSPORT_STANDBY_STEP_COUNT, *standby_s);
    *standby_s = passport_standby_steps[later ? index + 1 : index - 1];
    if (passport_idle_allowed(*standby_s, *poweroff_s)) return;
    for (size_t step = 0; step < PASSPORT_POWEROFF_STEP_COUNT; step++) {
        if (passport_idle_allowed(*standby_s, passport_poweroff_steps[step])) {
            *poweroff_s = passport_poweroff_steps[step];
            return;
        }
    }
}

/* Power-off cannot move to a step that would come before screen-off plus the gap. */
static inline bool passport_idle_poweroff_can(unsigned standby_s, unsigned poweroff_s, bool later) {
    unsigned index = passport_idle_index(passport_poweroff_steps, PASSPORT_POWEROFF_STEP_COUNT, poweroff_s);
    if (later) return index + 1 < PASSPORT_POWEROFF_STEP_COUNT;
    return index > 0 && passport_idle_allowed(standby_s, passport_poweroff_steps[index - 1]);
}

static inline void passport_idle_poweroff_step(uint16_t standby_s, uint16_t *poweroff_s, bool later) {
    if (!passport_idle_poweroff_can(standby_s, *poweroff_s, later)) return;
    unsigned index = passport_idle_index(passport_poweroff_steps, PASSPORT_POWEROFF_STEP_COUNT, *poweroff_s);
    *poweroff_s = passport_poweroff_steps[later ? index + 1 : index - 1];
}
