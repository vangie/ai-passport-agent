#pragma once

#include <stdbool.h>
#include <stdint.h>

#define PASSPORT_IDLE_TIMEOUT_MS 60000U

typedef struct {
    uint32_t activity_ms;
    bool sleeping;
    bool consume_key;
} passport_power_policy_t;

static inline void passport_power_activity(passport_power_policy_t *policy, uint32_t now_ms) {
    policy->activity_ms = now_ms;
}

static inline bool passport_power_due(passport_power_policy_t *policy, uint32_t now_ms, bool busy, bool usb_host) {
    if (busy || usb_host) passport_power_activity(policy, now_ms);
    return !busy && !usb_host && !policy->sleeping &&
        (uint32_t)(now_ms - policy->activity_ms) >= PASSPORT_IDLE_TIMEOUT_MS;
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
