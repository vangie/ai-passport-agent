#!/usr/bin/env python3
"""Exercise the firmware's BLE startup functions with a simulated NimBLE host."""

import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


def function_source(source, name):
    match = re.search(rf"(?:static\s+)?(?:void|int|esp_err_t)\s+{name}\([^;]*?\)\s*\{{", source)
    if not match:
        raise AssertionError(f"Missing function: {name}")
    depth = 1
    end = match.end()
    while depth:
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
        end += 1
    return source[match.start():end]


HARNESS = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "passport_ble_session.h"
#include "passport_line.h"
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
static int s_ble_state_lock;
static passport_ble_session_t s_ble_sessions;
static passport_line_t s_ble_lines[PASSPORT_BLE_PEERS];
static bool s_configuration_connected;
#define atomic_store(target, value) (*(target) = (value))

typedef int esp_err_t;
typedef void *TaskHandle_t;
typedef void *SemaphoreHandle_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_TIMEOUT 3
#define BLE_HS_ENOMEM 6
#define BLE_HS_ENOENT 7
#define BLE_HS_CONN_HANDLE_NONE 65535
#define BLE_ERR_REM_USER_CONN_TERM 1
#define BLE_SM_IO_CAP_NO_IO 3
#define BLE_SM_PAIR_KEY_DIST_ENC 1
#define BLE_SM_PAIR_KEY_DIST_ID 2
#define BLE_HS_ADV_F_DISC_GEN 2
#define BLE_HS_ADV_F_BREDR_UNSUP 4
#define BLE_GAP_CONN_MODE_UND 2
#define BLE_GAP_DISC_MODE_GEN 2
#define BLE_HS_FOREVER 2147483647
#define pdPASS 1
#define pdTRUE 1
#define pdMS_TO_TICKS(value) (value)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)

static bool s_ble_initialized, s_ble_stopping, s_ble_paused, s_ble_ready;
static uint16_t s_tx_handle;
static uint8_t s_addr_type;
static char s_ble_name[17];
typedef int ble_uuid128_t;
static const ble_uuid128_t k_service = 1;
struct ble_hs_adv_fields {
    uint8_t flags;
    const uint8_t *name;
    uint8_t name_len, name_is_complete;
    ble_uuid128_t *uuids128;
    uint8_t num_uuids128, uuids128_is_complete;
};
struct ble_gap_adv_params { int conn_mode, disc_mode; };
struct ble_gap_event { int type; };
static int gap_event(struct ble_gap_event *event, void *argument) {
    (void)event; (void)argument; return 0;
}
static TaskHandle_t s_host_task;
static SemaphoreHandle_t s_host_stopped;
static const int k_svcs[] = {1, 0};
static struct {
    void (*sync_cb)(void);
    int sm_io_cap, sm_bonding, sm_mitm, sm_sc, sm_our_key_dist, sm_their_key_dist;
    int (*store_status_cb)(void);
} ble_hs_cfg;
static unsigned fixture_store_inits;
static void ble_store_config_init(void) { fixture_store_inits++; }
static int ble_store_util_status_rr(void) { return 0; }
static int fixture_init_result, fixture_name_result, fixture_count_result;
static int fixture_add_result, fixture_deinit_result, fixture_address_result;
static int fixture_infer_result, fixture_advertise_result;
static int fixture_copy_result, fixture_response_result;
static uint8_t fixture_address[6] = {0x34, 0x12, 0x56, 0x78, 0x9a, 0x02};
static char fixture_gap_name[17], fixture_advertised_name[17];
static bool fixture_task_fails, fixture_counted, fixture_queued, fixture_host_started;
static unsigned fixture_deinits, fixture_adverts, fixture_creates;

static SemaphoreHandle_t xSemaphoreCreateBinary(void) { return (void *)1; }
static int xSemaphoreTake(SemaphoreHandle_t semaphore, int timeout) {
    (void)semaphore; (void)timeout; return pdTRUE;
}
static void xSemaphoreGive(SemaphoreHandle_t semaphore) { (void)semaphore; }
static int nimble_port_init(void) {
    fixture_host_started = fixture_counted = fixture_queued = false;
    return fixture_init_result;
}
static int nimble_port_deinit(void) { fixture_deinits++; return fixture_deinit_result; }
static int nimble_port_stop(void) { return 0; }
static void nimble_port_run(void) {}
static void ble_svc_gap_init(void) {}
static void ble_svc_gatt_init(void) {}
static int ble_svc_gap_device_name_set(const char *name) {
    assert(strlen(name) == 16 && !strncmp(name, "AI Passport ", 12));
    snprintf(fixture_gap_name, sizeof(fixture_gap_name), "%s", name);
    return fixture_name_result;
}
static int ble_gatts_count_cfg(const int *services) {
    assert(services == k_svcs && !fixture_host_started);
    fixture_counted = true; return fixture_count_result;
}
static int ble_gatts_add_svcs(const int *services) {
    assert(services == k_svcs && fixture_counted && !fixture_host_started);
    fixture_queued = !fixture_add_result; return fixture_add_result;
}
static int ble_hs_util_ensure_addr(int prefer_random) {
    (void)prefer_random; return fixture_address_result;
}
static int ble_hs_id_infer_auto(int privacy, uint8_t *address_type) {
    (void)privacy; *address_type = 0; return fixture_infer_result;
}
static int ble_hs_id_copy_addr(uint8_t address_type, uint8_t *address, int *is_nrpa) {
    assert(address_type == s_addr_type && is_nrpa == NULL);
    memcpy(address, fixture_address, sizeof(fixture_address));
    return fixture_copy_result;
}
static int ble_gap_adv_set_fields(const struct ble_hs_adv_fields *fields) {
    assert(fields->flags == (BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP));
    assert(fields->num_uuids128 == 1 && fields->uuids128 == &k_service);
    assert(fields->uuids128_is_complete && !fields->name_len);
    assert(3 + 2 + 16 * fields->num_uuids128 <= 31);
    return fixture_advertise_result;
}
static int ble_gap_adv_rsp_set_fields(const struct ble_hs_adv_fields *fields) {
    assert(fields->name_is_complete && fields->name_len == 16);
    assert(2 + fields->name_len <= 31 && !fields->num_uuids128);
    snprintf(fixture_advertised_name, sizeof(fixture_advertised_name), "%.*s",
             fields->name_len, fields->name);
    return fixture_response_result;
}
static int ble_gap_adv_start(uint8_t address_type, const void *peer, int duration,
                             const struct ble_gap_adv_params *params,
                             int (*callback)(struct ble_gap_event *, void *), void *argument) {
    assert(address_type == s_addr_type && peer == NULL && duration == BLE_HS_FOREVER);
    assert(params->conn_mode == BLE_GAP_CONN_MODE_UND && params->disc_mode == BLE_GAP_DISC_MODE_GEN);
    assert(callback == gap_event && argument == NULL);
    assert(fixture_queued && fixture_host_started && s_tx_handle != 0);
    fixture_adverts++;
    return 0;
}
static int ble_gap_adv_stop(void) { return 0; }
static int ble_gap_adv_active(void) { return 0; }
static void terminate_session(uint32_t serial) { (void)serial; }
static void passport_memory_report(const char *label) { (void)label; }
static void vTaskSuspend(TaskHandle_t task) { (void)task; }
static void vTaskDelete(TaskHandle_t task) { (void)task; }
static int xTaskCreate(void (*entry)(void *), const char *name, int stack,
                       void *argument, int priority, TaskHandle_t *task) {
    (void)entry; (void)name; (void)stack; (void)argument; (void)priority;
    assert(fixture_queued && !fixture_host_started && !s_tx_handle);
    fixture_creates++;
    if (fixture_task_fails) return 0;
    fixture_host_started = true;
    s_tx_handle = 42;
    *task = (void *)2;
    ble_hs_cfg.sync_cb();
    return pdPASS;
}
"""

ACCEPTANCE = r"""
int main(void) {
    for (unsigned cycle = 0; cycle < 3; cycle++) {
        assert(passport_link_resume_bluetooth() == ESP_OK);
        assert(s_ble_initialized && s_ble_ready && s_tx_handle == 42);
        assert(!strcmp(fixture_gap_name, "AI Passport 1234"));
        assert(!strcmp(fixture_advertised_name, fixture_gap_name));
        assert(ble_hs_cfg.sm_sc == 1 && ble_hs_cfg.sm_mitm == 0);
        assert(ble_hs_cfg.sm_bonding == 1 && fixture_store_inits == cycle + 1);
        assert(ble_hs_cfg.sm_our_key_dist == (BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID));
        assert(ble_hs_cfg.sm_their_key_dist == ble_hs_cfg.sm_our_key_dist);
        assert(ble_hs_cfg.store_status_cb == ble_store_util_status_rr);
        unsigned connected_adverts = fixture_adverts;
        assert(passport_ble_connected(&s_ble_sessions, 1, 0));
        assert(advertise() == 0 && fixture_adverts == connected_adverts + 1);
        assert(passport_ble_connected(&s_ble_sessions, 2, 0));
        assert(advertise() == 0 && fixture_adverts == connected_adverts + 1);
        passport_ble_disconnected(&s_ble_sessions, 1);
        passport_ble_disconnected(&s_ble_sessions, 2);
        on_sync();
        unsigned creates = fixture_creates;
        assert(passport_link_resume_bluetooth() == ESP_OK && fixture_creates == creates);
        assert(passport_link_pause_bluetooth() == ESP_OK);
        assert(!s_ble_initialized && !s_ble_ready && !s_host_task);
    }
    unsigned creates = fixture_creates;
    fixture_init_result = ESP_ERR_NO_MEM;
    assert(passport_link_resume_bluetooth() == ESP_ERR_NO_MEM && !s_ble_initialized);
    fixture_init_result = 0;
    fixture_count_result = BLE_HS_ENOMEM;
    assert(passport_link_resume_bluetooth() == ESP_ERR_NO_MEM && !s_ble_initialized);
    fixture_count_result = 0;
    fixture_add_result = 9;
    assert(passport_link_resume_bluetooth() == ESP_FAIL && !s_ble_initialized);
    fixture_deinit_result = ESP_FAIL;
    assert(passport_link_resume_bluetooth() == ESP_FAIL && s_ble_initialized);
    assert(!s_ble_ready && fixture_creates == creates);
    fixture_deinit_result = fixture_add_result = 0;
    assert(passport_link_pause_bluetooth() == ESP_OK && !s_ble_initialized);
    fixture_task_fails = true;
    assert(passport_link_resume_bluetooth() == ESP_ERR_NO_MEM && !s_ble_initialized);
    fixture_task_fails = false;
    fixture_address_result = 9;
    unsigned adverts = fixture_adverts;
    assert(passport_link_resume_bluetooth() == ESP_OK && !s_ble_ready);
    assert(fixture_adverts == adverts);
    fixture_address_result = 0;
    fixture_infer_result = 9;
    on_sync();
    assert(!s_ble_ready && fixture_adverts == adverts);
    fixture_infer_result = 0;
    fixture_copy_result = 9;
    on_sync();
    assert(!s_ble_ready && fixture_adverts == adverts);
    fixture_copy_result = 0;
    fixture_name_result = 9;
    on_sync();
    assert(!s_ble_ready && fixture_adverts == adverts);
    fixture_name_result = 0;
    s_tx_handle = 0;
    on_sync();
    assert(!s_ble_ready && fixture_adverts == adverts);
    s_tx_handle = 42;
    fixture_advertise_result = 9;
    on_sync();
    assert(!s_ble_ready);
    fixture_advertise_result = 0;
    fixture_response_result = 9;
    on_sync();
    assert(!s_ble_ready && fixture_adverts == adverts);
    fixture_response_result = 0;
    on_sync();
    assert(s_ble_ready);
    fixture_address[0] = 0x0f;
    fixture_address[1] = 0x01;
    on_sync();
    assert(s_ble_ready && !strcmp(fixture_gap_name, "AI Passport 010F"));
    assert(!strcmp(fixture_advertised_name, fixture_gap_name));
    assert(passport_link_pause_bluetooth() == ESP_OK);
    adverts = fixture_adverts;
    assert(advertise() == 0 && fixture_adverts == adverts);
    assert(fixture_deinits > 0);
    puts("BLE service registration, resync and startup rollback: PASS");
    return 0;
}
"""


class BLEStartupTests(unittest.TestCase):
    def test_bonds_persist_across_restarts(self):
        configuration = (ROOT / "sdkconfig.defaults").read_text()
        self.assertIn("CONFIG_BT_NIMBLE_NVS_PERSIST=y", configuration)
        self.assertIn("CONFIG_BT_NIMBLE_MAX_BONDS=3", configuration)
        self.assertIn("CONFIG_BT_NIMBLE_MAX_CONNECTIONS=2", configuration)
        self.assertIn("CONFIG_BT_CTRL_BLE_MAX_ACT=3", configuration)

    def test_real_startup_functions(self):
        source = (ROOT / "main/passport_link.c").read_text()
        names = ["advertise", "on_sync", "host_task", "passport_link_resume_bluetooth", "passport_link_pause_bluetooth"]
        functions = "\n".join(function_source(source, name) for name in names)
        with tempfile.TemporaryDirectory(prefix="passport-ble-startup-") as directory:
            program = Path(directory) / "startup.c"
            executable = Path(directory) / "startup"
            program.write_text(HARNESS + functions + ACCEPTANCE)
            subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "app"),
                            str(program), str(ROOT / "app/passport_ble_session.c"), str(ROOT / "app/passport_line.c"), "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
