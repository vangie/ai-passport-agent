#include "passport_ble_session.h"

#include <string.h>

passport_ble_peer_t *passport_ble_peer(passport_ble_session_t *state, uint16_t handle) {
    for (size_t index = 0; index < PASSPORT_BLE_PEERS; index++) {
        if (state->peers[index].serial && state->peers[index].handle == handle) return &state->peers[index];
    }
    return NULL;
}

passport_ble_peer_t *passport_ble_serial(passport_ble_session_t *state, uint32_t serial) {
    for (size_t index = 0; index < PASSPORT_BLE_PEERS; index++) {
        if (serial && state->peers[index].serial == serial) return &state->peers[index];
    }
    return NULL;
}

uint32_t passport_ble_connected(passport_ble_session_t *state, uint16_t handle, uint32_t now_ms) {
    if (handle == PASSPORT_BLE_NO_HANDLE || passport_ble_peer(state, handle)) return 0;
    for (size_t index = 0; index < PASSPORT_BLE_PEERS; index++) {
        passport_ble_peer_t *peer = &state->peers[index];
        if (peer->serial) continue;
        do { state->next_serial++; } while (!state->next_serial || passport_ble_serial(state, state->next_serial));
        *peer = (passport_ble_peer_t){ .handle = handle, .serial = state->next_serial, .connected_ms = now_ms };
        return peer->serial;
    }
    return 0;
}

void passport_ble_disconnected(passport_ble_session_t *state, uint16_t handle) {
    passport_ble_peer_t *peer = passport_ble_peer(state, handle);
    if (!peer) return;
    if (state->owner == peer->serial) state->owner = 0;
    memset(peer, 0, sizeof(*peer));
}

size_t passport_ble_count(const passport_ble_session_t *state) {
    size_t count = 0;
    for (size_t index = 0; index < PASSPORT_BLE_PEERS; index++) count += state->peers[index].serial != 0;
    return count;
}

bool passport_ble_owned(const passport_ble_session_t *state, uint32_t serial) {
    return serial && state->owner == serial;
}

bool passport_ble_claim(passport_ble_session_t *state, uint32_t serial, bool encrypted,
                        bool operation_pending, uint32_t *previous) {
    passport_ble_peer_t *peer = passport_ble_serial(state, serial);
    if (!peer || peer->retired || !peer->subscribed || !encrypted || operation_pending) return false;
    *previous = state->owner;
    passport_ble_peer_t *old = passport_ble_serial(state, state->owner);
    if (old && old != peer) old->retired = true;
    state->owner = serial;
    return true;
}

uint32_t passport_ble_expired(const passport_ble_session_t *state, uint32_t now_ms) {
    for (size_t index = 0; index < PASSPORT_BLE_PEERS; index++) {
        const passport_ble_peer_t *peer = &state->peers[index];
        if (peer->serial && peer->serial != state->owner && (uint32_t)(now_ms - peer->connected_ms) >= 120000)
            return peer->serial;
    }
    return 0;
}
