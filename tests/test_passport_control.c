#include "passport_control.h"
#include <assert.h>

int main(void) {
    unsigned expected = 0, desired = 0;
    assert(passport_control_volume("VOLUME 75 100", &expected, &desired));
    assert(expected == 75 && desired == 100);
    assert(passport_control_volume("VOLUME 100 0", &expected, &desired));
    const char *invalid[] = {"VOLUME", "VOLUME ", "VOLUME -1 50", "VOLUME 101 50", "VOLUME 50 101",
        "VOLUME 50 9999999999999", "VOLUME 50 10x", "VOLUME  50 10", "VOLUME 50", "VOLUME 50 10 extra"};
    for (unsigned index = 0; index < sizeof(invalid) / sizeof(invalid[0]); index++)
        assert(!passport_control_volume(invalid[index], &expected, &desired));
    unsigned power[4] = {0};
    assert(passport_control_power("POWERCFG 60 1800 120 0", power));
    assert(power[0] == 60 && power[1] == 1800 && power[2] == 120 && power[3] == 0);
    assert(passport_control_power("POWERCFG 15 0 600 7200", power) && power[2] == 600 && power[3] == 7200);
    const char *bad[] = {"POWERCFG", "POWERCFG ", "POWERCFG 60 1800 120", "POWERCFG 60 1800 120 0 1",
        "POWERCFG 60 1800 120 -1", "POWERCFG 60 1800 12x 0", "POWERCFG 60  1800 120 0",
        "POWERCFG 60 1800 120 999999", "POWER 60 1800 120 0", "POWERCFG 60 1800 120 0 "};
    for (unsigned index = 0; index < sizeof(bad) / sizeof(bad[0]); index++)
        assert(!passport_control_power(bad[index], power));
    char line[160];
    passport_control_status(line, sizeof(line), 65, -1, true, false, false, true, 60, 1800);
    assert(!strcmp(line, "DEVICE volume=65 battery=-1 wifi=1 busy=0 sleeping=0 usb_host=1 idle_s=60 poweroff_s=1800"));
    passport_control_status(line, sizeof(line), 0, 50, false, true, true, false, 300, 0);
    assert(!strcmp(line, "DEVICE volume=0 battery=50 wifi=0 busy=1 sleeping=1 usb_host=0 idle_s=300 poweroff_s=0"));
}
