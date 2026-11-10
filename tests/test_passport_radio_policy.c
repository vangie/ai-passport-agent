#include "passport_radio.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    /* Boot with a saved network: allow Wi-Fi to finish before provisioning. */
    assert(!passport_radio_wants_ble(5000,false,true,false,false,false));
    assert(passport_radio_wants_ble(10000,false,true,false,false,false));
    assert(passport_radio_wants_ble(0,false,false,false,false,false));
    assert(passport_radio_wants_ble(0,true,true,false,false,false));
    assert(passport_radio_wants_ble(0,false,true,true,false,false));
    assert(passport_radio_wants_ble(0,false,true,false,true,false));
    assert(passport_radio_wants_ble(20000,true,true,false,false,false));
    assert(passport_radio_wants_ble(140000,true,true,false,false,false));
    assert(passport_radio_wants_ble(150000,true,true,true,false,false));
    assert(passport_radio_wants_ble(150000,false,true,true,false,false));
    /* Losing Wi-Fi in the middle of capture/transfer must never restore BLE. */
    for (int online=0;online<2;online++) for (int saved=0;saved<2;saved++) {
        assert(!passport_radio_wants_ble(500000,online,saved,true,false,true));
        assert(!passport_radio_wants_ble(500000,online,saved,false,false,true));
        assert(!passport_radio_wants_ble(500000,online,saved,true,true,true));
    }
    assert(passport_radio_wants_ble(510000,true,true,false,false,false));
    puts("Radio ownership: startup grace, default-on standby, no expiry and exclusive audio/network memory PASS");
}
