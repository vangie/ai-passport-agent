#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PASSPORT_BLE_PEERS 2
#define PASSPORT_BLE_NO_HANDLE UINT16_MAX

typedef struct {
    uint16_t handle;
    uint32_t serial;
    uint32_t connected_ms;
    bool subscribed;
    bool retired;
} passport_ble_peer_t;

typedef struct {
    passport_ble_peer_t peers[PASSPORT_BLE_PEERS];
    uint32_t next_serial;
    uint32_t owner;
} passport_ble_session_t;

uint32_t passport_ble_connected(passport_ble_session_t *state, uint16_t handle, uint32_t now_ms);
void passport_ble_disconnected(passport_ble_session_t *state, uint16_t handle);
passport_ble_peer_t *passport_ble_peer(passport_ble_session_t *state, uint16_t handle);
passport_ble_peer_t *passport_ble_serial(passport_ble_session_t *state, uint32_t serial);
size_t passport_ble_count(const passport_ble_session_t *state);
bool passport_ble_claim(passport_ble_session_t *state, uint32_t serial, bool encrypted,
                        bool operation_pending, uint32_t *previous);
bool passport_ble_owned(const passport_ble_session_t *state, uint32_t serial);
uint32_t passport_ble_expired(const passport_ble_session_t *state, uint32_t now_ms);
