#include "passport_radio.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    /* Online: Bluetooth stays off unless asked for, however long it runs. */
    assert(!passport_radio_wants_ble(0,true,true,false,false,false));
    assert(!passport_radio_wants_ble(20000,true,true,false,false,false));
    assert(!passport_radio_wants_ble(86400000,true,true,false,false,false));
    /* Turned on in Settings, or a setup client is connected: on, online or not. */
    assert(passport_radio_wants_ble(0,true,true,true,false,false));
    assert(passport_radio_wants_ble(150000,true,true,true,false,false));
    assert(passport_radio_wants_ble(150000,false,true,true,false,false));
    assert(passport_radio_wants_ble(0,true,true,false,true,false));
    assert(passport_radio_wants_ble(0,false,true,false,true,false));
    /* A device that cannot get online turns it on by itself: at once with
     * nothing saved, after a grace period when a saved network is failing. */
    assert(passport_radio_wants_ble(0,false,false,false,false,false));
    assert(!passport_radio_wants_ble(PASSPORT_OFFLINE_BLE_MS-1,false,true,false,false,false));
    assert(passport_radio_wants_ble(PASSPORT_OFFLINE_BLE_MS,false,true,false,false,false));
    /* Capture and transfer own the memory: never on while busy. */
    for (int online=0;online<2;online++) for (int saved=0;saved<2;saved++) {
        assert(!passport_radio_wants_ble(500000,online,saved,true,false,true));
        assert(!passport_radio_wants_ble(500000,online,saved,false,false,true));
        assert(!passport_radio_wants_ble(500000,online,saved,true,true,true));
    }
    puts("Radio ownership: manual Bluetooth when online, automatic only when offline, setup clients kept, exclusive audio/network memory PASS");
}
