#include "badge_radio.h"
bool badge_radio_wants_ble(uint64_t since_boot_ms, uint64_t now_ms,
                           uint64_t config_until_ms, bool online,
                           bool saved_networks, bool busy) {
    if (busy) return false;
    if (config_until_ms > now_ms) return true;
    return !online && (!saved_networks || since_boot_ms >= 10000);
}
