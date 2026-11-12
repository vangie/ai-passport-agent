import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_passport_network_runtime import function_source


ROOT = Path(__file__).resolve().parents[1]

HARNESS = r'''
#include "passport_ble_session.h"
#include "passport_line.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
#define ESP_LOGI(...) ((void)0)
#define BLE_HS_CONN_HANDLE_NONE UINT16_MAX
#define BLE_ERR_REM_USER_CONN_TERM 19
#define BLE_HS_ENOTCONN 7
#define BLE_HS_EDONE 8
#define BLE_GAP_EVENT_CONNECT 1
#define BLE_GAP_EVENT_DISCONNECT 2
#define BLE_GAP_EVENT_SUBSCRIBE 3
#define BLE_GAP_EVENT_NOTIFY_TX 4
#define BLE_GAP_EVENT_ADV_COMPLETE 5
#define BLE_ATT_ERR_INSUFFICIENT_AUTHEN 5
#define BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN 13
#define BLE_ATT_ERR_INSUFFICIENT_RES 17
#define CMD_LINE_MAX 2048
static int s_ble_state_lock;
static passport_ble_session_t s_ble_sessions;
static passport_line_t s_ble_lines[PASSPORT_BLE_PEERS];
static atomic_bool s_configuration_connected;
static atomic_uint s_configuration_activity;
static atomic_int s_ble_transmit_status;
static atomic_uint_least16_t s_ble_transmitting = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_tx_handle = 42;
static void *s_ble_transmitted = (void *)1;
static unsigned adverts, wakeups;
static uint32_t time_ms;
static char *queued[4];
static uint32_t sessions[4];
static size_t queued_count;
struct ble_gap_event {
    int type;
    struct { int status; uint16_t conn_handle; } connect;
    struct { int reason; struct { uint16_t conn_handle; } conn; } disconnect;
    struct { uint16_t attr_handle, conn_handle; bool cur_indicate; } subscribe;
    struct { bool indication; uint16_t attr_handle, conn_handle; int status; } notify_tx;
};
struct os_mbuf { const void *data; uint16_t length; };
struct ble_gatt_access_ctxt { struct os_mbuf *om; };
#define OS_MBUF_PKTLEN(buffer) ((buffer)->length)
static void ble_hs_mbuf_to_flat(struct os_mbuf *buffer, void *out, uint16_t length, void *unused) {
    (void)unused;
    memcpy(out, buffer->data, length);
}
static int64_t esp_timer_get_time(void) { return (int64_t)time_ms * 1000; }
static int advertise(void) {
    if (passport_ble_count(&s_ble_sessions) < PASSPORT_BLE_PEERS) adverts++;
    return 0;
}
static int ble_gap_terminate(uint16_t handle, int reason) { (void)handle; (void)reason; return 0; }
static void xSemaphoreGive(void *semaphore) { assert(semaphore == s_ble_transmitted); wakeups++; }
static bool queue_command(char *line, uint32_t serial) {
    assert(queued_count < 4);
    queued[queued_count] = line;
    sessions[queued_count++] = serial;
    return true;
}
'''

ACCEPTANCE = r'''
static void connect_peer(uint16_t handle) {
    struct ble_gap_event event = {.type = BLE_GAP_EVENT_CONNECT};
    event.connect.conn_handle = handle;
    gap_event(&event, NULL);
}
static void disconnect_peer(uint16_t handle) {
    struct ble_gap_event event = {.type = BLE_GAP_EVENT_DISCONNECT};
    event.disconnect.conn.conn_handle = handle;
    gap_event(&event, NULL);
}
static void input(uint16_t handle, const char *text) {
    struct os_mbuf buffer = {.data = text, .length = (uint16_t)strlen(text)};
    struct ble_gatt_access_ctxt context = {.om = &buffer};
    assert(on_rx(handle, 1, &context, NULL) == 0);
}
int main(void) {
    assert(!passport_link_is_bluetooth_connected());
    connect_peer(10);
    assert(passport_link_is_bluetooth_connected());
    assert(s_configuration_connected && adverts == 1);
    uint32_t phone = passport_ble_peer(&s_ble_sessions, 10)->serial;
    connect_peer(20);
    assert(passport_ble_count(&s_ble_sessions) == 2 && adverts == 1);
    uint32_t browser = passport_ble_peer(&s_ble_sessions, 20)->serial;
    input(10, "SAVE 1 Ho");
    input(20, "HE");
    input(10, "me password\n");
    input(20, "LLO\n");
    assert(queued_count == 2 && !strcmp(queued[0], "SAVE 1 Home password") && !strcmp(queued[1], "HELLO"));
    assert(sessions[0] == phone && sessions[1] == browser);
    struct ble_gap_event subscribed = {.type = BLE_GAP_EVENT_SUBSCRIBE};
    subscribed.subscribe.attr_handle = s_tx_handle;
    subscribed.subscribe.cur_indicate = true;
    subscribed.subscribe.conn_handle = 20;
    gap_event(&subscribed, NULL);
    uint32_t previous;
    assert(passport_ble_claim(&s_ble_sessions, browser, true, false, &previous));
    s_ble_transmitting = 20;
    input(10, "partial secret");
    disconnect_peer(10);
    assert(passport_link_is_bluetooth_connected());
    assert(!wakeups && s_configuration_connected && passport_ble_owned(&s_ble_sessions, browser));
    assert(adverts == 2 && !s_ble_lines[0].data);
    struct ble_gap_event ack = {.type = BLE_GAP_EVENT_NOTIFY_TX};
    ack.notify_tx.indication = true;
    ack.notify_tx.attr_handle = s_tx_handle;
    ack.notify_tx.conn_handle = 10;
    ack.notify_tx.status = BLE_HS_EDONE;
    gap_event(&ack, NULL);
    assert(!wakeups);
    ack.notify_tx.conn_handle = 20;
    gap_event(&ack, NULL);
    assert(wakeups == 1 && !s_ble_transmit_status);
    disconnect_peer(20);
    assert(!passport_link_is_bluetooth_connected());
    assert(wakeups == 2 && s_ble_transmit_status == BLE_HS_ENOTCONN && !s_configuration_connected);
    assert(!s_ble_sessions.owner && adverts == 3);
    for (size_t index = 0; index < queued_count; index++) free(queued[index]);
    puts("Real BLE GAP/RX: continued discovery, isolated fragments, late disconnect and ACK isolation PASS");
    return 0;
}
'''


class GapTests(unittest.TestCase):
    def test_connection_events_and_independent_rx(self):
        source = (ROOT / "main/passport_link.c").read_text()
        functions = "\n".join(function_source(source, name) for name in ["passport_link_is_bluetooth_connected", "gap_event", "on_rx"])
        with tempfile.TemporaryDirectory() as directory:
            program, executable = Path(directory) / "gap.c", Path(directory) / "test"
            program.write_text(HARNESS + functions + ACCEPTANCE)
            subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-I", str(ROOT / "app"), str(program), str(ROOT / "app/passport_ble_session.c"),
                            str(ROOT / "app/passport_line.c"), "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
