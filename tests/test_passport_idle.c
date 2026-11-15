#include <assert.h>
#include <stdio.h>
#include "passport_idle.h"
#include "passport_power.h"

int main(void) {
    /* Every pair of steps the device can reach is one the firmware accepts. */
    for (size_t s = 0; s < PASSPORT_STANDBY_STEP_COUNT; s++) {
        for (size_t p = 0; p < PASSPORT_POWEROFF_STEP_COUNT; p++) {
            unsigned standby = passport_standby_steps[s], poweroff = passport_poweroff_steps[p];
            assert(passport_idle_allowed(standby, poweroff) == passport_power_valid(standby, poweroff));
        }
    }
    assert(PASSPORT_IDLE_GAP_S == PASSPORT_POWEROFF_GAP_S);
    assert(passport_standby_steps[0] == PASSPORT_STANDBY_MIN_S);
    assert(passport_standby_steps[PASSPORT_STANDBY_STEP_COUNT - 1] == PASSPORT_STANDBY_MAX_S);
    assert(passport_poweroff_steps[PASSPORT_POWEROFF_STEP_COUNT - 2] == PASSPORT_POWEROFF_MAX_S);
    assert(passport_poweroff_steps[PASSPORT_POWEROFF_STEP_COUNT - 1] == 0);

    /* Walking screen-off through every step keeps the pair valid at each press. */
    uint16_t standby = PASSPORT_STANDBY_DEFAULT_S, poweroff = PASSPORT_POWEROFF_DEFAULT_S;
    passport_idle_standby_step(&standby, &poweroff, true);
    assert(standby == 120 && poweroff == 1800);
    standby = 15; poweroff = 300;
    assert(!passport_idle_standby_can(standby, false) && passport_idle_standby_can(standby, true));
    for (unsigned press = 0; press < 5; press++) {
        passport_idle_standby_step(&standby, &poweroff, true);
        assert(passport_power_valid(standby, poweroff));
    }
    /* 5 minutes of screen-off pushed a 5-minute power-off to 10; 10 pushed it to 15. */
    assert(standby == 600 && poweroff == 900);
    assert(!passport_idle_standby_can(standby, true));
    passport_idle_standby_step(&standby, &poweroff, true);
    assert(standby == 600 && poweroff == 900);
    /* An earlier screen-off never moves power-off. */
    for (unsigned press = 0; press < 5; press++) passport_idle_standby_step(&standby, &poweroff, false);
    assert(standby == 15 && poweroff == 900);
    /* Never stays never however late screen-off goes. */
    poweroff = 0;
    for (unsigned press = 0; press < 5; press++) passport_idle_standby_step(&standby, &poweroff, true);
    assert(standby == 600 && poweroff == 0);

    /* Power-off stops at the first step that would crowd screen-off. */
    standby = 300; poweroff = 1800;
    passport_idle_poweroff_step(standby, &poweroff, false);
    assert(poweroff == 900);
    passport_idle_poweroff_step(standby, &poweroff, false);
    assert(poweroff == 600);
    assert(!passport_idle_poweroff_can(standby, poweroff, false));
    passport_idle_poweroff_step(standby, &poweroff, false);
    assert(poweroff == 600);
    standby = 600;
    assert(!passport_idle_poweroff_can(standby, 900, false) && passport_idle_poweroff_can(standby, 1800, false));
    /* Up to two hours, then never, and back. */
    standby = 60; poweroff = 3600;
    passport_idle_poweroff_step(standby, &poweroff, true);
    assert(poweroff == 7200);
    passport_idle_poweroff_step(standby, &poweroff, true);
    assert(poweroff == 0 && !passport_idle_poweroff_can(standby, poweroff, true));
    passport_idle_poweroff_step(standby, &poweroff, true);
    assert(poweroff == 0);
    passport_idle_poweroff_step(standby, &poweroff, false);
    assert(poweroff == 7200);
    standby = 15; poweroff = 300;
    assert(!passport_idle_poweroff_can(standby, poweroff, false));

    /* A stored value between two steps moves to a real step in one press. */
    standby = 45; poweroff = 1800;
    assert(passport_idle_index(passport_standby_steps, PASSPORT_STANDBY_STEP_COUNT, 45) == 1);
    passport_idle_standby_step(&standby, &poweroff, true);
    assert(standby == 60);
    standby = 45;
    passport_idle_standby_step(&standby, &poweroff, false);
    assert(standby == 15);
    standby = 60; poweroff = 1000;
    passport_idle_poweroff_step(standby, &poweroff, true);
    assert(poweroff == 1800);
    poweroff = 1000;
    passport_idle_poweroff_step(standby, &poweroff, false);
    assert(poweroff == 600);
    puts("Idle steps: ordering, pushed power-off, never, bounds and off-step values PASS");
}
