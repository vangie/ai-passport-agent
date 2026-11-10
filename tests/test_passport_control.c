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
    char line[160];
    passport_control_status(line, sizeof(line), 65, -1, true, false, false, true);
    assert(!strcmp(line, "DEVICE volume=65 battery=-1 wifi=1 busy=0 sleeping=0 usb_host=1 idle_s=60"));
}
