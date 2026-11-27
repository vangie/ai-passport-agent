#pragma once
#include "passport_gateway.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char access[1025];
    char refresh[2049];
    uint8_t proof[32];
} passport_muse_credentials_t;

bool passport_muse_token_save(const char *token);
bool passport_muse_token_load(char token[49]);
bool passport_muse_credentials_load(passport_muse_credentials_t *credentials);
bool passport_muse_credentials_save(const passport_muse_credentials_t *credentials);
bool passport_muse_credentials_clear(void);
bool passport_muse_paired(void);
void passport_muse_node_id(char node[16]);

/* Token and pairing state are private to the device. */
unsigned passport_muse_pair_state(void);
void passport_muse_pair_open(void);
void passport_muse_pair_close(void);
unsigned passport_muse_pair_prompt(void);
void passport_muse_pair_confirm(unsigned prompt);
void passport_muse_ble_connection(uint16_t handle, bool connected);
void passport_muse_ble_poll(void);
int passport_muse_ble_register(void);
int passport_muse_ble_advertisement(void);
bool passport_muse_ble_ready(void);
const char *passport_muse_ble_name(void);

/* Only called by the existing gateway worker with BLE and its old socket released. */
void passport_muse_run(bool probe, void (*state)(passport_send_state_t, const char *),
                       void (*transcript)(const char *));

#ifdef __cplusplus
}
#endif
