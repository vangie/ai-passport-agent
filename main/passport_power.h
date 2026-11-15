#pragma once

#include <stdbool.h>
#include <stdint.h>

#define PASSPORT_STANDBY_DEFAULT_S 60U
#define PASSPORT_STANDBY_MIN_S 15U
#define PASSPORT_STANDBY_MAX_S 600U
#define PASSPORT_POWEROFF_DEFAULT_S 1800U
#define PASSPORT_POWEROFF_MAX_S 7200U
#define PASSPORT_POWEROFF_GAP_S 60U

typedef struct {
    uint32_t activity_ms;
    uint16_t standby_s;
    uint16_t poweroff_s;
    bool sleeping;
    bool consume_key;
} passport_power_policy_t;

/* Power-off always comes after standby: 0 disables it, any other value is
 * raised to at least one minute past the standby time. */
static inline void passport_power_limits(unsigned *standby_s, unsigned *poweroff_s) {
    if (*standby_s < PASSPORT_STANDBY_MIN_S) *standby_s = PASSPORT_STANDBY_MIN_S;
    if (*standby_s > PASSPORT_STANDBY_MAX_S) *standby_s = PASSPORT_STANDBY_MAX_S;
    if (!*poweroff_s) return;
    if (*poweroff_s < *standby_s + PASSPORT_POWEROFF_GAP_S) *poweroff_s = *standby_s + PASSPORT_POWEROFF_GAP_S;
    if (*poweroff_s > PASSPORT_POWEROFF_MAX_S) *poweroff_s = PASSPORT_POWEROFF_MAX_S;
}

static inline bool passport_power_valid(unsigned standby_s, unsigned poweroff_s) {
    unsigned standby = standby_s, poweroff = poweroff_s;
    passport_power_limits(&standby, &poweroff);
    return standby == standby_s && poweroff == poweroff_s;
}

static inline void passport_power_configure(passport_power_policy_t *policy, unsigned standby_s, unsigned poweroff_s) {
    passport_power_limits(&standby_s, &poweroff_s);
    policy->standby_s = (uint16_t)standby_s;
    policy->poweroff_s = (uint16_t)poweroff_s;
}

static inline void passport_power_activity(passport_power_policy_t *policy, uint32_t now_ms) {
    policy->activity_ms = now_ms;
}

static inline bool passport_power_due(passport_power_policy_t *policy, uint32_t now_ms, bool busy, bool usb_host) {
    if (busy || usb_host) passport_power_activity(policy, now_ms);
    return !busy && !usb_host && !policy->sleeping &&
        (uint32_t)(now_ms - policy->activity_ms) >= (uint32_t)policy->standby_s * 1000U;
}

/* Only a device that is already in standby powers off, so the two can never
 * race: activity, a busy device or a USB host restart both timers. */
static inline bool passport_power_off_due(const passport_power_policy_t *policy, uint32_t now_ms, bool busy, bool usb_host) {
    return policy->poweroff_s && policy->sleeping && !busy && !usb_host &&
        (uint32_t)(now_ms - policy->activity_ms) >= (uint32_t)policy->poweroff_s * 1000U;
}

static inline bool passport_power_key(passport_power_policy_t *policy, uint32_t now_ms, bool pressed) {
    bool consumed = policy->sleeping || policy->consume_key;
    if (pressed) {
        passport_power_activity(policy, now_ms);
        if (policy->sleeping) policy->consume_key = true;
    } else {
        policy->consume_key = false;
    }
    return consumed;
}

void passport_power_init(void);
void passport_power_awake(bool awake);
void passport_power_command_begin(void);
void passport_power_command_end(void);
void passport_power_off(void);
