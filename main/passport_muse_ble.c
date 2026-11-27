#include "passport_muse.h"
#include "mg_link_setup.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mbedtls/platform_util.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

static const ble_uuid128_t service = BLE_UUID128_INIT(MGLS_SERVICE_UUID_LE);
static const ble_uuid128_t rx = BLE_UUID128_INIT(MGLS_RX_UUID_LE);
static const ble_uuid128_t tx = BLE_UUID128_INIT(MGLS_TX_UUID_LE);
static uint16_t tx_handle;
static SemaphoreHandle_t lock;
static mgls_t setup;
static mgls_config_t config;
static uint8_t address[6];
static char sdk_token[49];
static char name[30];
static atomic_uint state;
static atomic_uint connection = BLE_HS_CONN_HANDLE_NONE;
static atomic_uint generation;
static unsigned observed_generation;
static atomic_uint prompt;
static atomic_uint confirmed_prompt;
static unsigned next_prompt;
static bool paired;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
static void *borrow(void *ctx, size_t size) { return malloc(size); }
static void release(void *ctx, void *value) { free(value); }
static int random_bytes(void *ctx, uint8_t *out, size_t len) { esp_fill_random(out, len); return 0; }
static bool set_up(void *ctx) { return paired; }
static bool may_prompt(void *ctx, uint32_t elapsed) { return elapsed >= MGLS_RESETUP_PROMPT_MS; }
static const char *token(void *ctx, size_t *len) { *len = strlen(sdk_token); return sdk_token; }

static int notify(void *ctx, const uint8_t *data, size_t size) {
    unsigned handle = atomic_load(&connection);
    if (handle == BLE_HS_CONN_HANDLE_NONE) return -1;
    struct os_mbuf *packet = ble_hs_mbuf_from_flat(data, size);
    if (!packet) return MGLS_AGAIN;
    int result = ble_gatts_notify_custom(handle, tx_handle, packet);
    return result == 0 ? MGLS_OK : result == BLE_HS_ENOMEM ? MGLS_AGAIN : -1;
}

static void disconnect(void *ctx) {
    unsigned handle = atomic_load(&connection);
    if (handle != BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM);
}

static int commit(void *ctx, const mgls_credentials_t *in) {
    passport_muse_credentials_t *out = calloc(1, sizeof(*out));
    if (!out) return -1;
    memcpy(out->access, in->access_token, in->access_len);
    memcpy(out->refresh, in->refresh_token, in->refresh_len);
    memcpy(out->proof, in->k, sizeof(out->proof));
    bool saved = passport_muse_credentials_save(out);
    mbedtls_platform_zeroize(out, sizeof(*out));
    free(out);
    if (saved) paired = true;
    return saved ? 0 : -1;
}

static void event(void *ctx, enum mgls_event ev) {
    if (ev == MGLS_EV_CONFIRM_ARMED) {
        if (++next_prompt == 0) ++next_prompt;
        atomic_store(&prompt, next_prompt);
        atomic_store(&state, PASSPORT_MUSE_PAIR_CONFIRM);
        return;
    }
    if (ev == MGLS_EV_CONFIRMED || ev == MGLS_EV_CONFIRM_TIMEOUT || ev == MGLS_EV_SESSION_ENDED)
        atomic_store(&prompt, 0);
    if (ev == MGLS_EV_CONFIRMED) atomic_store(&state, PASSPORT_MUSE_PAIR_SAVING);
    else if (ev == MGLS_EV_PROVISIONED) atomic_store(&state, PASSPORT_MUSE_PAIR_DONE);
    else if (ev == MGLS_EV_STORAGE_FAILED) atomic_store(&state, PASSPORT_MUSE_PAIR_FAILED);
    else if ((ev == MGLS_EV_CONFIRM_TIMEOUT || ev == MGLS_EV_SESSION_ENDED) &&
             atomic_load(&state) != PASSPORT_MUSE_PAIR_DONE && atomic_load(&state) != PASSPORT_MUSE_PAIR_FAILED)
        atomic_store(&state, PASSPORT_MUSE_PAIR_WAITING);
}

unsigned passport_muse_pair_state(void) { return atomic_load(&state); }
unsigned passport_muse_pair_prompt(void) { return atomic_load(&prompt); }
void passport_muse_pair_confirm(unsigned id) {
    if (id && id == passport_muse_pair_prompt()) atomic_store(&confirmed_prompt, id);
}

void passport_muse_pair_open(void) {
    if (!lock) lock = xSemaphoreCreateMutex();
    if (!lock) { atomic_store(&state, PASSPORT_MUSE_PAIR_FAILED); return; }
    xSemaphoreTake(lock, portMAX_DELAY);
    bool ready = esp_read_mac(address, ESP_MAC_BT) == ESP_OK && passport_muse_token_load(sdk_token);
    (void)mgls_format_name(name, sizeof(name), "Passport", address);
    paired = passport_muse_paired();
    config = (mgls_config_t){
        .bd_addr = address, .firmware_version = "1.0.0",
        .borrow = borrow, .release = release, .notify = notify, .disconnect = disconnect,
        .random = random_bytes, .commit = commit, .sdk_token = token, .event = event,
        .set_up = set_up, .may_prompt = may_prompt,
    };
    atomic_store(&prompt, 0);
    atomic_store(&confirmed_prompt, 0);
    atomic_store(&state, ready && mgls_init(&setup, &config) == MGLS_OK
        ? PASSPORT_MUSE_PAIR_WAITING : PASSPORT_MUSE_PAIR_FAILED);
    xSemaphoreGive(lock);
}

void passport_muse_pair_close(void) {
    atomic_store(&state, PASSPORT_MUSE_PAIR_OFF);
    if (!lock) return;
    xSemaphoreTake(lock, portMAX_DELAY);
    atomic_store(&prompt, 0);
    atomic_store(&connection, BLE_HS_CONN_HANDLE_NONE);
    atomic_fetch_add(&generation, 1);
    mgls_abort(&setup);
    mbedtls_platform_zeroize(sdk_token, sizeof(sdk_token));
    atomic_store(&state, PASSPORT_MUSE_PAIR_OFF);
    xSemaphoreGive(lock);
}

void passport_muse_ble_connection(uint16_t handle, bool connected) {
    if (connected) atomic_store(&connection, handle);
    else if (atomic_load(&connection) == handle) atomic_store(&connection, BLE_HS_CONN_HANDLE_NONE);
    else return;
    atomic_fetch_add(&generation, 1);
}

static void sync_connection(void) {
    unsigned current = atomic_load(&generation);
    if (observed_generation == current) return;
    observed_generation = current;
    atomic_store(&prompt, 0);
    atomic_store(&confirmed_prompt, 0);
    mgls_disconnected(&setup);
    if (passport_muse_pair_state() == PASSPORT_MUSE_PAIR_DONE) return;
    atomic_store(&state, PASSPORT_MUSE_PAIR_WAITING);
    if (atomic_load(&connection) != BLE_HS_CONN_HANDLE_NONE) mgls_connected(&setup, now_ms());
}

void passport_muse_ble_poll(void) {
    if (!passport_muse_pair_state() || !lock || !xSemaphoreTake(lock, 0)) return;
    if (passport_muse_pair_state() != PASSPORT_MUSE_PAIR_FAILED) {
        sync_connection();
        unsigned id = atomic_exchange(&confirmed_prompt, 0);
        if (id && id == passport_muse_pair_prompt()) (void)mgls_confirm_gesture(&setup);
        mgls_poll(&setup, now_ms());
    }
    xSemaphoreGive(lock);
}

static int access_cb(uint16_t handle, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *ctx) {
    if (!passport_muse_pair_state() || passport_muse_pair_state() == PASSPORT_MUSE_PAIR_FAILED ||
        handle != atomic_load(&connection)) return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    if (!xSemaphoreTake(lock, 0)) return BLE_ATT_ERR_INSUFFICIENT_RES;
    sync_connection();
    int result = 0;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        char status[32];
        size_t len = mgls_read_status(&setup, status, sizeof(status));
        if (os_mbuf_append(ctxt->om, status, len)) result = BLE_ATT_ERR_INSUFFICIENT_RES;
    } else {
        uint8_t data[256];
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        if (!len || len > sizeof(data) || ble_hs_mbuf_to_flat(ctxt->om, data, len, NULL))
            result = BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        else {
            mgls_mtu_changed(&setup, ble_att_mtu(handle));
            mgls_rx(&setup, data, len);
        }
    }
    xSemaphoreGive(lock);
    return result;
}

static const struct ble_gatt_svc_def services[] = {
    { .type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &service.u,
      .characteristics = (struct ble_gatt_chr_def[]){
          { .uuid = &rx.u, .access_cb = access_cb, .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP },
          { .uuid = &tx.u, .access_cb = access_cb, .val_handle = &tx_handle,
            .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY },
          {0} } }, {0}
};

int passport_muse_ble_register(void) {
    tx_handle = 0;
    int result = ble_gatts_count_cfg(services);
    return result ? result : ble_gatts_add_svcs(services);
}

bool passport_muse_ble_ready(void) { return tx_handle != 0; }
const char *passport_muse_ble_name(void) { return name; }

int passport_muse_ble_advertisement(void) {
    uint8_t adv[31];
    /* Explicit setup mode makes the gadget discoverable for a replacement pairing. */
    size_t len = mgls_adv_build(adv, sizeof(adv), false);
    int result = ble_gap_adv_set_data(adv, len);
    if (result) return result;
    struct ble_hs_adv_fields response = {
        .name = (const uint8_t *)name, .name_len = strlen(name), .name_is_complete = 1,
    };
    return ble_gap_adv_rsp_set_fields(&response);
}
