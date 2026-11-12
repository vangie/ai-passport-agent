#include "passport_ble_session.h"

#include <assert.h>
#include <stdio.h>

int main(void) {
    passport_ble_session_t state = {0};
    uint32_t previous = 999;
    uint32_t phone = passport_ble_connected(&state, 0, 100);
    assert(phone && passport_ble_count(&state) == 1);
    assert(!passport_ble_connected(&state, 0, 100));
    assert(!passport_ble_claim(&state, phone, true, false, &previous));
    passport_ble_peer(&state, 0)->subscribed = true;
    assert(!passport_ble_claim(&state, phone, false, false, &previous));
    assert(!passport_ble_claim(&state, phone, true, true, &previous));
    assert(passport_ble_claim(&state, phone, true, false, &previous) && !previous);
    uint32_t browser = passport_ble_connected(&state, 1, 200);
    assert(browser && browser != phone && passport_ble_owned(&state, phone));
    assert(passport_ble_count(&state) == 2 && !passport_ble_connected(&state, 2, 200));
    passport_ble_peer(&state, 1)->subscribed = true;
    assert(!passport_ble_claim(&state, browser, true, true, &previous));
    assert(passport_ble_owned(&state, phone));
    assert(passport_ble_claim(&state, browser, true, false, &previous) && previous == phone);
    assert(!passport_ble_claim(&state, phone, true, false, &previous));
    passport_ble_disconnected(&state, 0);
    assert(passport_ble_owned(&state, browser) && passport_ble_count(&state) == 1);
    assert(!passport_ble_serial(&state, phone));
    uint32_t next_phone = passport_ble_connected(&state, 0, 300);
    assert(next_phone != phone && next_phone != browser);
    assert(!passport_ble_claim(&state, phone, true, false, &previous));
    assert(!passport_ble_expired(&state, 120299));
    assert(passport_ble_expired(&state, 120300) == next_phone);
    passport_ble_disconnected(&state, 0);
    passport_ble_disconnected(&state, 1);
    assert(!state.owner && !passport_ble_count(&state));
    state.next_serial = UINT32_MAX;
    uint32_t wrapped = passport_ble_connected(&state, 2, UINT32_MAX - 1000);
    assert(wrapped && passport_ble_expired(&state, 119000) == wrapped);
    puts("BLE ownership, encrypted handshake, deferred takeover, stale handles and expiry: PASS");
    return 0;
}
