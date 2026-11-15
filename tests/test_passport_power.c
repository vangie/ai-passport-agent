#include <assert.h>
#include <stdio.h>
#include "passport_power.h"

static void check_limits(void) {
    unsigned standby = 0, poweroff = 0;
    passport_power_limits(&standby, &poweroff);
    assert(standby == PASSPORT_STANDBY_MIN_S && poweroff == 0);
    standby = 100000; poweroff = 1;
    passport_power_limits(&standby, &poweroff);
    assert(standby == PASSPORT_STANDBY_MAX_S && poweroff == PASSPORT_STANDBY_MAX_S + PASSPORT_POWEROFF_GAP_S);
    standby = 300; poweroff = 300;
    passport_power_limits(&standby, &poweroff);
    assert(standby == 300 && poweroff == 360);
    standby = 60; poweroff = 100000;
    passport_power_limits(&standby, &poweroff);
    assert(poweroff == PASSPORT_POWEROFF_MAX_S);
    for (unsigned s = 0; s <= 700; s += 5) for (unsigned p = 0; p <= 8000; p += 20) {
        unsigned a = s, b = p;
        passport_power_limits(&a, &b);
        assert(a >= PASSPORT_STANDBY_MIN_S && a <= PASSPORT_STANDBY_MAX_S);
        assert(b == 0 || (b >= a + PASSPORT_POWEROFF_GAP_S && b <= PASSPORT_POWEROFF_MAX_S));
        assert((b == 0) == (p == 0) && passport_power_valid(a, b));
    }
    assert(passport_power_valid(60, 1800) && passport_power_valid(60, 0) && passport_power_valid(600, 660));
    assert(!passport_power_valid(60, 60) && !passport_power_valid(60, 119) && !passport_power_valid(10, 0));
    assert(!passport_power_valid(601, 0) && !passport_power_valid(60, 7201) && !passport_power_valid(600, 600));
}

static void check_power_off(void) {
    passport_power_policy_t policy = {0};
    passport_power_configure(&policy, 60, 300);
    assert(policy.standby_s == 60 && policy.poweroff_s == 300);
    assert(!passport_power_off_due(&policy, 400000, false, false));
    assert(passport_power_due(&policy, 60000, false, false));
    policy.sleeping = true;
    assert(!passport_power_off_due(&policy, 299999, false, false));
    assert(passport_power_off_due(&policy, 300000, false, false));
    assert(!passport_power_off_due(&policy, 300000, true, false));
    assert(!passport_power_off_due(&policy, 300000, false, true));
    assert(passport_power_key(&policy, 300001, true));
    assert(!passport_power_off_due(&policy, 300002, false, false));
    policy.sleeping = false; policy.consume_key = false;
    assert(!passport_power_due(&policy, 360000, false, false));
    assert(passport_power_due(&policy, 360001, false, false));
    policy.sleeping = true;
    assert(!passport_power_off_due(&policy, 600000, false, false));
    assert(passport_power_off_due(&policy, 600001, false, false));
    passport_power_configure(&policy, 60, 0);
    assert(!passport_power_off_due(&policy, UINT32_MAX / 2, false, false));
    passport_power_configure(&policy, 600, 5);
    assert(policy.standby_s == 600 && policy.poweroff_s == 660);
    policy.sleeping = false;
    passport_power_configure(&policy, 15, 75);
    passport_power_activity(&policy, UINT32_MAX - 10000);
    assert(!passport_power_due(&policy, 4998, false, false));
    assert(passport_power_due(&policy, 4999, false, false));
    policy.sleeping = true;
    assert(!passport_power_off_due(&policy, 64998, false, false));
    assert(passport_power_off_due(&policy, 64999, false, false));
}

int main(void) {
    check_limits();
    check_power_off();
    passport_power_policy_t policy = {0};
    passport_power_configure(&policy, PASSPORT_STANDBY_DEFAULT_S, PASSPORT_POWEROFF_DEFAULT_S);
    assert(!passport_power_due(&policy, 59999, false, false));
    assert(passport_power_due(&policy, 60000, false, false));
    assert(!passport_power_due(&policy, 120000, true, false));
    assert(!passport_power_due(&policy, 179999, false, false));
    assert(passport_power_due(&policy, 180000, false, false));
    policy.sleeping = true;
    assert(!passport_power_due(&policy, 200000, false, false));
    assert(passport_power_key(&policy, 200001, true));
    policy.sleeping = false;
    assert(passport_power_key(&policy, 200050, true));
    assert(passport_power_key(&policy, 200100, false));
    assert(!passport_power_key(&policy, 200200, true));
    assert(!passport_power_key(&policy, 200250, false));
    passport_power_activity(&policy, UINT32_MAX - 10000);
    assert(!passport_power_due(&policy, 49998, false, false));
    assert(passport_power_due(&policy, 49999, false, false));
    assert(!passport_power_due(&policy, 100000, false, true));
    assert(!passport_power_due(&policy, 200000, false, true));
    assert(!passport_power_due(&policy, 259999, false, false));
    assert(passport_power_due(&policy, 260000, false, false));
    for (unsigned cycle = 0; cycle < 100; cycle++) {
        policy.sleeping = true;
        assert(passport_power_key(&policy, cycle, true));
        policy.sleeping = false;
        assert(passport_power_key(&policy, cycle, false));
        assert(!passport_power_key(&policy, cycle, false));
    }
    puts("Passport standby policy tests: PASS");
}
