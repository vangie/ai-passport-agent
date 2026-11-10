#include <assert.h>
#include <stdio.h>
#include "passport_power.h"

int main(void) {
    passport_power_policy_t policy = {0};
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
