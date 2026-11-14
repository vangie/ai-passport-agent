#include "passport_radio.h"
bool passport_radio_wants_ble(uint64_t since_boot_ms, bool online,
                           bool saved_networks, bool explicitly_enabled,
                           bool configuring, bool busy) {
    if (busy) return false;
    if (configuring || explicitly_enabled) return true;
    /* Off by default once the device is online, so the memory goes to a
     * standing gateway connection. It still comes up on its own when the
     * device has no way to get online, because Bluetooth is how that is
     * fixed: nothing saved, or a saved network that did not connect. */
    if (online) return false;
    return !saved_networks || since_boot_ms >= PASSPORT_OFFLINE_BLE_MS;
}
