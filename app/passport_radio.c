#include "passport_radio.h"
bool passport_radio_wants_ble(uint64_t since_boot_ms, bool online,
                           bool saved_networks, bool explicitly_enabled,
                           bool configuring, bool busy) {
    if (busy) return false;
    if (configuring || explicitly_enabled) return true;
    return online || !saved_networks || since_boot_ms >= 10000;
}
