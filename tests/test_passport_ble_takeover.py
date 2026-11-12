import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_passport_network_runtime import function_source


ROOT = Path(__file__).resolve().parents[1]

HARNESS = r'''
#include "passport_ble_session.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define BLE_HS_CONN_HANDLE_NONE UINT16_MAX
#define BLE_ERR_REM_USER_CONN_TERM 19
#define PASSPORT_NETWORK_CONNECTING 1
#define pdMS_TO_TICKS(value) (value)
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
static int s_ble_state_lock;
static passport_ble_session_t s_ble_sessions;
static uint32_t s_command_session;
static atomic_uint s_async_configuration, s_configuration_pending;
static atomic_int s_network_state;
typedef struct { char *line; uint32_t session; } passport_link_command_t;
struct ble_gap_conn_desc { struct { bool encrypted; } sec_state; };
static bool encrypted = true, complete_async = true;
static unsigned processed, releases, notifications, terminations, delays;
static uint16_t terminated;
static bool configuration_line(const char *line) { return strcmp(line, "HELLO") != 0; }
static void passport_power_command_end(void) { releases++; }
static int ble_gap_conn_find(uint16_t handle, struct ble_gap_conn_desc *description) {
    description->sec_state.encrypted = encrypted;
    return passport_ble_peer(&s_ble_sessions, handle) ? 0 : -1;
}
static int ble_gap_terminate(uint16_t handle, int reason) {
    assert(reason == BLE_ERR_REM_USER_CONN_TERM);
    terminated = handle;
    terminations++;
    return 0;
}
static int bluetooth_reply(uint32_t serial, const char *out, size_t length, unsigned timeout) {
    (void)timeout;
    assert(strlen(out) == length && serial);
    if (!strcmp(out, "SESSION TAKEN\n")) notifications++;
    else assert(!strcmp(out, "ERR busy\n"));
    return 0;
}
static void vTaskDelay(unsigned ticks) {
    assert(ticks == 100);
    assert(s_ble_sessions.owner == (complete_async ? 1 : 2));
    delays++;
    if (complete_async) atomic_store(&s_async_configuration, 0);
}
static void process_line(char *line) {
    assert(passport_ble_owned(&s_ble_sessions, s_command_session));
    if (configuration_line(line)) atomic_fetch_sub(&s_configuration_pending, 1);
    processed++;
    passport_power_command_end();
    free(line);
}
'''

ACCEPTANCE = r'''
static void command(uint32_t session, const char *text) {
    if (configuration_line(text)) atomic_fetch_add(&s_configuration_pending, 1);
    process_command((passport_link_command_t){.line = strdup(text), .session = session});
    assert(!s_command_session);
}
int main(void) {
    uint32_t phone = passport_ble_connected(&s_ble_sessions, 10, 0);
    passport_ble_peer(&s_ble_sessions, 10)->subscribed = true;
    command(phone, "HELLO");
    assert(processed == 1 && !notifications && s_ble_sessions.owner == phone);
    uint32_t browser = passport_ble_connected(&s_ble_sessions, 20, 0);
    command(browser, "SAVE 1 Test secret");
    assert(processed == 1 && s_ble_sessions.owner == phone && !s_configuration_pending);
    command(browser, "HELLO");
    assert(processed == 1 && s_ble_sessions.owner == phone);
    passport_ble_peer(&s_ble_sessions, 20)->subscribed = true;
    encrypted = false;
    command(browser, "HELLO");
    assert(processed == 1 && s_ble_sessions.owner == phone);
    encrypted = true;
    atomic_store(&s_async_configuration, 1);
    command(browser, "HELLO");
    assert(delays == 1 && processed == 2 && notifications == 1);
    assert(terminated == 10 && terminations == 1 && s_ble_sessions.owner == browser);
    command(phone, "SAVE 1 Test secret");
    command(phone, "HELLO");
    assert(processed == 2 && notifications == 1 && s_ble_sessions.owner == browser);
    passport_ble_disconnected(&s_ble_sessions, 10);
    assert(s_ble_sessions.owner == browser);
    uint32_t recycled = passport_ble_connected(&s_ble_sessions, 10, 0);
    assert(recycled != phone);
    command(phone, "LANG 1 en");
    command(browser, "LANG 1 zh");
    assert(processed == 3 && !s_configuration_pending && releases == 9);
    assert(session_handle(phone) == BLE_HS_CONN_HANDLE_NONE);
    passport_ble_peer(&s_ble_sessions, 10)->subscribed = true;
    atomic_store(&s_async_configuration, 1);
    complete_async = false;
    command(recycled, "HELLO");
    assert(delays == 601 && processed == 3 && s_ble_sessions.owner == browser);
    atomic_store(&s_async_configuration, 0);
    atomic_store(&s_network_state, PASSPORT_NETWORK_CONNECTING);
    command(recycled, "HELLO");
    assert(processed == 3 && s_ble_sessions.owner == browser);
    atomic_store(&s_network_state, 0);
    command(recycled, "HELLO");
    assert(processed == 4 && notifications == 2 && terminated == 20);
    puts("Real BLE command worker: encrypted subscribed handshake, deferred handoff, retirement and recycled handles PASS");
    return 0;
}
'''


class TakeoverTests(unittest.TestCase):
    def test_real_command_worker(self):
        source = (ROOT / "main/passport_link.c").read_text()
        functions = "\n".join(function_source(source, name) for name in
                              ["session_handle", "terminate_session", "process_command"])
        with tempfile.TemporaryDirectory() as directory:
            program, executable = Path(directory) / "takeover.c", Path(directory) / "test"
            program.write_text(HARNESS + functions + ACCEPTANCE)
            subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-D_POSIX_C_SOURCE=200809L",
                            "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "app"), str(program),
                            str(ROOT / "app/passport_ble_session.c"), "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
