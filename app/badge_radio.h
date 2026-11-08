#pragma once
#include <stdbool.h>
#include <stdint.h>
/* One policy for BLE memory ownership. Wi-Fi recovery cannot turn BLE back
 * on while I2S or the network worker still owns internal RAM. */
bool badge_radio_wants_ble(uint64_t since_boot_ms, uint64_t now_ms,
                           uint64_t config_until_ms, bool online,
                           bool saved_networks, bool busy);
