#include "badge_radio.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    /* Boot with a saved network: allow Wi-Fi to finish before provisioning. */
    assert(!badge_radio_wants_ble(5000,5000,0,false,true,false));
    assert(badge_radio_wants_ble(10000,10000,0,false,true,false));
    assert(badge_radio_wants_ble(0,0,0,false,false,false));
    assert(!badge_radio_wants_ble(20000,20000,0,true,true,false));
    /* Manual configuration is available online and expires after 2 minutes. */
    assert(badge_radio_wants_ble(20000,20000,140000,true,true,false));
    assert(!badge_radio_wants_ble(140000,140000,140000,true,true,false));
    /* Losing Wi-Fi in the middle of capture/transfer must never restore BLE. */
    for (int net=0;net<2;net++) for (int saved=0;saved<2;saved++) {
        assert(!badge_radio_wants_ble(500000,500000,620000,net,saved,true));
        assert(!badge_radio_wants_ble(500000,500000,0,net,saved,true));
    }
    assert(badge_radio_wants_ble(510000,510000,620000,true,true,false));
    puts("Radio ownership: startup, offline recovery, manual expiry and exclusive audio/network memory PASS");
}
