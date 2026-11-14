#pragma once
#include <stdbool.h>
#include <stdint.h>
/* One policy for BLE memory ownership. Bluetooth and a gateway connection do
 * not fit in internal RAM together, so Bluetooth is manual: on when the user
 * turns it on in Settings, or while a setup client is connected. A device
 * that is still offline this long after boot turns it on by itself, so it can
 * always be set up. Wi-Fi recovery cannot turn BLE back on while I2S or the
 * network worker still owns the memory. */
#define PASSPORT_OFFLINE_BLE_MS 10000
bool passport_radio_wants_ble(uint64_t since_boot_ms, bool online,
                           bool saved_networks, bool explicitly_enabled,
                           bool configuring, bool busy);
