#!/usr/bin/env python3
"""Exercise saved-network workers and radio rollback using the actual C functions."""

import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


def function_source(source, name):
    match = re.search(rf"(?:static\s+)?(?:const\s+char\s*\*|bool|void|int|size_t|uint16_t|passport_network_state_t)\s*{name}\([^;]*?\)\s*\{{", source)
    if not match:
        raise AssertionError(f"Missing function: {name}")
    depth, end = 1, match.end()
    while depth:
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
        end += 1
    return source[match.start():end]


COMMON = r"""
#include "passport_network.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define pdTRUE 1
#define pdMS_TO_TICKS(value) (value)
#define portMAX_DELAY (-1)
#define ESP_OK 0
typedef struct {
    struct {
        unsigned char ssid[32], password[64];
        struct { int authmode; } threshold;
        struct { bool capable, required; } pmf_cfg;
        int scan_method, failure_retry_cnt, sae_pwe_h2e;
        bool bssid_set;
    } sta;
} wifi_config_t;
#define WIFI_IF_STA 0
static bool s_online;
static wifi_config_t current;
static int esp_wifi_get_config(int interface, wifi_config_t *config) {
    (void)interface; *config=current; return ESP_OK;
}
static bool passport_wifi_is_online(void) { return s_online; }
"""

QUEUE = r"""
static void *s_lines=(void *)1, *s_lock=(void *)2;
static passport_store_t fixture_store, *s_store=&fixture_store;
static atomic_uint s_configuration_pending, s_configuration_activity;
static atomic_uint s_async_configuration;
typedef struct { char *line; uint32_t session; } passport_link_command_t;
static atomic_bool s_configuration_connected;
static atomic_int s_network_state;
static _Atomic(char *) s_network_command;
static int s_network_index;
static uint32_t s_network_generation;
static char *queued;
static bool queue_full, allocation_fails, lock_fails;
static unsigned power_commands, joins, saves, handled;
static const char *join_failure;
static void *fixture_malloc(size_t length) { return allocation_fails?NULL:malloc(length); }
#define malloc fixture_malloc
static void passport_power_command_begin(void) { power_commands++; }
static void passport_power_command_end(void) { assert(power_commands); power_commands--; }
static int xQueueSend(void *queue, const passport_link_command_t *command, int timeout) {
    (void)queue; (void)timeout;
    if (queue_full) return 0;
    assert(!queued && !command->session); queued=command->line; return pdTRUE;
}
static int xSemaphoreTake(void *lock, int timeout) { (void)lock; (void)timeout; return !lock_fails; }
static void xSemaphoreGive(void *lock) { (void)lock; }
static const char *passport_wifi_join(const char *ssid, const char *pass, bool keep) {
    assert(!strcmp(ssid,"office") && !strcmp(pass,"saved-password") && keep);
    assert(power_commands && atomic_load(&s_configuration_pending));
    assert(atomic_load(&s_network_state)==PASSPORT_NETWORK_CONNECTING);
    joins++; return join_failure;
}
static bool passport_nvs_save(const passport_store_t *store) {
    assert(store->connected==1); saves++; return true;
}
static void handle_line(const char *line) { assert(line[0]); handled++; }
"""

QUEUE_TEST = r"""
static void drain(void) { char *line=queued; queued=NULL; process_line(line); }
int main(void) {
    passport_store_init(s_store);
    assert(!passport_link_configuration_busy() && !passport_link_configuration_pending());
    atomic_store(&s_configuration_connected,true);
    assert(passport_link_configuration_busy() && !passport_link_configuration_pending());
    atomic_store(&s_configuration_pending,1);
    assert(passport_link_configuration_pending());
    atomic_store(&s_configuration_pending,0);
    atomic_store(&s_configuration_connected,false);
    assert(passport_store_add(s_store,"Home","home","home-password"));
    assert(passport_store_add(s_store,"Office","office","saved-password"));
    s_store->connected=0;
    unsigned generation=s_store->generation;
    assert(!passport_link_connect_network(-1,generation));
    allocation_fails=true;
    assert(!passport_link_connect_network(1,generation));
    assert(!queued && !power_commands && !atomic_load(&s_network_command));
    allocation_fails=false; queue_full=true;
    assert(!passport_link_connect_network(1,generation));
    assert(!queued && !power_commands && !atomic_load(&s_configuration_pending));
    assert(passport_link_network_result()==PASSPORT_NETWORK_IDLE);
    queue_full=false;
    assert(passport_link_connect_network(1,generation));
    assert(queued && !joins && passport_link_configuration_busy());
    assert(!passport_link_connect_network(0,generation));
    assert(passport_link_network_result()==PASSPORT_NETWORK_CONNECTING);
    drain();
    assert(joins==1 && saves==1 && !handled);
    assert(!power_commands && !atomic_load(&s_configuration_pending));
    assert(!atomic_load(&s_network_command));
    assert(passport_link_network_result()==PASSPORT_NETWORK_CONNECTED);
    assert(passport_link_network_result()==PASSPORT_NETWORK_IDLE);
    assert(!passport_link_configuration_busy());
    assert(passport_link_connect_network(1,generation));
    drain();
    assert(passport_link_network_result()==PASSPORT_NETWORK_STALE && joins==1);
    generation=s_store->generation;
    lock_fails=true;
    assert(passport_link_connect_network(1,generation)); drain();
    assert(passport_link_network_result()==PASSPORT_NETWORK_BUSY && joins==1);
    lock_fails=false;
    join_failure="timeout";
    assert(passport_link_connect_network(1,generation)); drain();
    assert(passport_link_network_result()==PASSPORT_NETWORK_TIMEOUT && joins==2 && saves==1);
    join_failure=NULL;
    s_online=true;
    strcpy((char *)current.sta.ssid,"office");
    strcpy((char *)current.sta.password,"saved-password");
    assert(passport_link_connect_network(1,generation)); drain();
    assert(passport_link_network_result()==PASSPORT_NETWORK_CONNECTED && joins==2 && saves==2);
    assert(passport_link_connect_network(1,s_store->generation));
    char *external=malloc(32); strcpy(external,"WIFI_SELECT 1 0");
    assert(configuration_line(external));
    passport_power_command_begin(); atomic_fetch_add(&s_configuration_pending,1);
    process_line(external);
    assert(handled==1 && joins==2);
    assert(passport_link_network_result()==PASSPORT_NETWORK_CONNECTING);
    drain(); assert(passport_link_network_result()==PASSPORT_NETWORK_CONNECTED);
    assert(!power_commands && !atomic_load(&s_configuration_pending));
    memset(current.sta.ssid,'W',sizeof(current.sta.ssid));
    memset(current.sta.password,'a',sizeof(current.sta.password));
    char full_ssid[33], full_password[65];
    memset(full_ssid,'W',32); full_ssid[32]=0;
    memset(full_password,'a',64); full_password[64]=0;
    assert(!join_wifi(NULL,full_ssid,full_password,true) && joins==2);
    puts("Asynchronous Wi-Fi worker: queue/OOM cleanup, serialization, generation, lock, result ownership, same-network skip PASS");
}
"""

RADIO = r"""
#define WIFI_AUTH_WPA_WPA2_PSK 1
#define WIFI_AUTH_OPEN 0
#define WIFI_ALL_CHANNEL_SCAN 1
#define WPA3_SAE_PWE_BOTH 1
#define ESP_LOGI(...) ((void)0)
static void *s_operation_lock=(void *)1;
static atomic_uint s_join_waiters;
static unsigned takes, gives, attempts, leaves, restores, target_attempts, missing_attempts;
static int64_t radio_now;
static int64_t esp_timer_get_time(void) { return radio_now; }
static void vTaskDelay(int delay) { radio_now += (int64_t)delay * 1000; }
static const char *failure;
static wifi_config_t requested;
static int xSemaphoreTake(void *lock, int timeout) {
    assert(lock && timeout==portMAX_DELAY && atomic_load(&s_join_waiters)==1); takes++; return pdTRUE;
}
static void xSemaphoreGive(void *lock) { assert(lock && !atomic_load(&s_join_waiters)); gives++; }
static int start_once(void) { return ESP_OK; }
static void leave(void) { leaves++; }
static int esp_wifi_set_config(int interface, const wifi_config_t *config) {
    (void)interface; current=*config; restores++; return ESP_OK;
}
static const char *attempt(const wifi_config_t *config) {
    attempts++;
    radio_now += 3000000;
    if (memcmp(config,&current,sizeof(current))) {
        requested=*config; target_attempts++;
        return target_attempts <= missing_attempts ? "notfound" : failure;
    }
    assert(!memcmp(config,&current,sizeof(current))); return NULL;
}
"""

RADIO_TEST = r"""
int main(void) {
    assert(!passport_wifi_is_online());
    strcpy((char *)current.sta.ssid,"home");
    strcpy((char *)current.sta.password,"home-password");
    assert(!strcmp(passport_wifi_join("","",true),"notfound") && !takes);
    for (int online=0;online<2;online++) {
        for (int keep=0;keep<2;keep++) {
            for (int failed=0;failed<2;failed++) {
                s_online=online; attempts=leaves=restores=target_attempts=0;
                failure=failed?"pass":NULL;
                const char *result=passport_wifi_join("office","saved-password",keep);
                assert(result==failure);
                assert(!strcmp((char *)requested.sta.ssid,"office"));
                assert(!strcmp((char *)requested.sta.password,"saved-password"));
                assert(requested.sta.pmf_cfg.capable && !requested.sta.pmf_cfg.required);
                if (keep && !failed) assert(attempts==1 && !leaves && !restores);
                else if (online) assert(attempts==2 && !leaves && !restores);
                else assert(attempts==1 && leaves==1 && restores==1);
                assert(takes==gives && !atomic_load(&s_join_waiters));
            }
        }
    }
    char full_ssid[33], full_password[65];
    memset(full_ssid,'W',32); full_ssid[32]=0;
    memset(full_password,'a',64); full_password[64]=0;
    attempts=0; failure=NULL;
    assert(!passport_wifi_join(full_ssid,full_password,true));
    assert(!memcmp(requested.sta.ssid,full_ssid,32));
    assert(!memcmp(requested.sta.password,full_password,64));
    s_online=true; attempts=leaves=restores=target_attempts=0; radio_now=0;
    missing_attempts=3; failure=NULL;
    assert(!passport_wifi_join("office","saved-password",true));
    assert(target_attempts==4 && attempts==4 && !leaves && !restores);
    assert(radio_now==15000000);
    attempts=leaves=restores=target_attempts=0; radio_now=0; missing_attempts=100;
    assert(!strcmp(passport_wifi_join("office","saved-password",true),"notfound"));
    assert(target_attempts==4 && attempts==5 && !leaves && !restores);
    attempts=leaves=restores=target_attempts=0; radio_now=0;
    assert(!strcmp(passport_wifi_join("office","saved-password",false),"notfound"));
    assert(target_attempts==1 && attempts==2);
    puts("Wi-Fi radio: successful switch, failed join restores previous network, offline cleanup PASS");
}
"""

CONTROLLER = r"""
static passport_store_t s_store;
static passport_view_t s_view;
static int s_network_index;
static uint32_t s_network_generation;
static int64_t s_network_started, now;
static bool s_network_queued, quiescent, enqueue_fails;
static unsigned requests, maintenance;
static passport_network_state_t result;
static void passport_gateway_maintain(bool enabled) { assert(!enabled); maintenance++; }
static bool passport_gateway_quiescent(void) { return quiescent; }
static int64_t esp_timer_get_time(void) { return now; }
static bool passport_link_connect_network(int index, uint32_t generation) {
    assert(index==1 && generation==7); requests++; return !enqueue_fails;
}
static passport_network_state_t passport_link_network_result(void) { return result; }
"""

CONTROLLER_TEST = r"""
int main(void) {
    (void)passport_wifi_is_online;
    (void)esp_wifi_get_config;
    s_network_index=1; s_network_generation=7; s_store.generation=7;
    s_view.screen=PASSPORT_SCREEN_NET_MENU; s_view.cursor=1;
    s_view.network_target=1; s_view.network_state=PASSPORT_NETWORK_CONNECTING;
    maintain_network_selection();
    assert(!requests && maintenance==1 && s_view.network_state==PASSPORT_NETWORK_CONNECTING);
    now=5000000;
    maintain_network_selection();
    assert(!requests && s_view.network_state==PASSPORT_NETWORK_BUSY);
    s_view.network_state=PASSPORT_NETWORK_CONNECTING; quiescent=true; enqueue_fails=true;
    maintain_network_selection();
    assert(requests==1 && s_view.network_state==PASSPORT_NETWORK_BUSY && !s_network_queued);
    s_view.network_state=PASSPORT_NETWORK_CONNECTING; enqueue_fails=false;
    result=PASSPORT_NETWORK_CONNECTING;
    maintain_network_selection(); maintain_network_selection();
    assert(requests==2 && s_network_queued && s_view.network_state==PASSPORT_NETWORK_CONNECTING);
    s_view.screen=PASSPORT_SCREEN_NETS; s_view.cursor=0;
    result=PASSPORT_NETWORK_CONNECTED; s_store.generation=8;
    maintain_network_selection();
    assert(s_view.screen==PASSPORT_SCREEN_NETS && s_view.cursor==0 && s_view.network_target==1);
    assert(s_view.network_state==PASSPORT_NETWORK_CONNECTED && s_network_generation==8);
    maintain_network_selection();
    assert(s_view.network_state==PASSPORT_NETWORK_CONNECTED);
    s_store.generation++;
    maintain_network_selection();
    assert(s_view.network_state==PASSPORT_NETWORK_IDLE && s_view.network_target==-1);
    puts("Wi-Fi controller: gateway shutdown, bounded wait, queue failure, one request, navigation independence, stale feedback PASS");
}
"""

BLUETOOTH_CONTROLLER = r"""
#include "passport_radio.h"
typedef int esp_err_t;
static passport_store_t s_store;
static passport_view_t s_view;
static int64_t now, s_boot_us;
static bool s_ble_config_requested, s_ble_config_suppressed;
static bool configuring, pending, active, gateway_active, gateway_resources;
static int audio_release_result, resume_result, pause_result;
static unsigned starts, stops;
static int64_t esp_timer_get_time(void) { return now; }
static bool passport_link_configuration_busy(void) { return configuring; }
static bool passport_link_configuration_pending(void) { return pending; }
static bool passport_gateway_active(void) { return gateway_active; }
static void passport_gateway_maintain(bool enabled) { assert(!enabled || !configuring); }
static bool passport_gateway_quiescent(void) { return !gateway_active && !gateway_resources; }
static bool passport_link_is_bluetooth_active(void) { return active; }
static int passport_link_pause_bluetooth(void) { stops++; if (!pause_result) active=false; return pause_result; }
static int passport_link_resume_bluetooth(void) { starts++; if (!resume_result) active=true; return resume_result; }
static int passport_audio_release(void) { return audio_release_result; }
"""

BLUETOOTH_CONTROLLER_TEST = r"""
int main(void) {
    (void)esp_wifi_get_config;
    /* A saved network that has not connected yet: wait out the grace
     * period, then turn Bluetooth on so the device can be set up. */
    s_store.count=1; now=5000000;
    maintain_bluetooth_mode();
    assert(!active && !starts);
    now=10000000;
    maintain_bluetooth_mode();
    assert(active && starts==1 && !stops);
    /* Still offline much later: it stays on, there is no expiry. */
    now=200000000;
    maintain_bluetooth_mode();
    assert(active && !stops && !s_ble_config_requested);
    /* Wi-Fi comes up with nobody connected: Bluetooth hands the memory
     * back so the gateway can hold a connection. */
    s_online=true; now=400000000;
    maintain_bluetooth_mode();
    assert(!active && stops==1 && !s_ble_config_requested);
    /* Online, it stays off however long the device runs. */
    now=800000000;
    maintain_bluetooth_mode();
    assert(!active && starts==1 && stops==1);
    /* Turned on in Settings: on, and it stays on while online. */
    request_bluetooth_config(); maintain_bluetooth_mode();
    assert(active && starts==2);
    now=1000000000;
    maintain_bluetooth_mode();
    assert(active && stops==1 && s_ble_config_requested);
    /* A setup client is connected and saving: Off is refused until the
     * save finishes, then honoured. */
    configuring=true; pending=true;
    close_bluetooth_config();
    maintain_bluetooth_mode();
    assert(active && stops==1 && !s_ble_config_suppressed);
    /* The save is done and the client has disconnected. */
    pending=false; configuring=false;
    close_bluetooth_config();
    maintain_bluetooth_mode();
    assert(!active && stops==2 && s_ble_config_suppressed && !s_ble_config_requested);
    maintain_bluetooth_mode();
    assert(!active && starts==2);
    /* On again, then recording, sending and the gateway each take the
     * memory; Bluetooth returns only when all of them have let go. */
    request_bluetooth_config(); maintain_bluetooth_mode();
    assert(active && starts==3);
    s_view.recording=true;
    maintain_bluetooth_mode();
    assert(!active && stops==3);
    s_view.recording=false; s_view.sending=true;
    maintain_bluetooth_mode();
    assert(!active && starts==3);
    s_view.sending=false; gateway_active=true;
    maintain_bluetooth_mode();
    assert(!active && starts==3);
    gateway_active=false; gateway_resources=true;
    maintain_bluetooth_mode();
    assert(!active && starts==3);
    gateway_resources=false;
    maintain_bluetooth_mode();
    assert(active && starts==4);
    /* A failed stop or start is retried after a pause, not in a loop. */
    s_view.recording=true; pause_result=1;
    maintain_bluetooth_mode();
    assert(active && stops==4);
    now+=1000000; pause_result=0;
    maintain_bluetooth_mode();
    assert(!active && stops==5);
    s_view.recording=false; audio_release_result=1;
    maintain_bluetooth_mode();
    assert(!active && starts==4);
    now+=1000000; audio_release_result=0; resume_result=1;
    maintain_bluetooth_mode();
    assert(!active && starts==5);
    now+=1000000; resume_result=0;
    maintain_bluetooth_mode();
    assert(active && starts==6);
    /* Switching networks from the device never toggles Bluetooth. */
    s_view.network_state=PASSPORT_NETWORK_CONNECTING;
    maintain_bluetooth_mode();
    assert(active && starts==6 && stops==5);
    s_view.network_state=PASSPORT_NETWORK_IDLE;
    close_bluetooth_config();
    maintain_bluetooth_mode();
    assert(!active && stops==6);
    /* Turned off by hand, then the device loses every network: it stays
     * off, because the user said so. A fresh start with nothing saved
     * turns it on again. */
    s_online=false;
    maintain_bluetooth_mode();
    assert(!active && starts==6);
    s_store.count=0; s_boot_us=now;
    s_ble_config_suppressed=false;
    maintain_bluetooth_mode();
    assert(active && starts==7);
    close_bluetooth_config();
    maintain_bluetooth_mode();
    assert(!active && stops==7);
    s_store.count=1;
    request_bluetooth_config();
    maintain_bluetooth_mode();
    assert(active && starts==8);
    puts("Bluetooth manual when online, automatic when offline, no expiry, save protection, audio release, gateway cleanup and failure retries PASS");
}
"""


SCAN = r"""
#include "passport_hotspot.h"
typedef int esp_err_t;
typedef int wifi_vendor_ie_type_t;
typedef struct { uint8_t element_id, length, payload[10]; } vendor_ie_data_t;
typedef struct { uint8_t ssid[33], bssid[6]; int8_t rssi; } wifi_ap_record_t;
typedef struct { uint8_t schan, nchan; } wifi_country_t;
typedef struct { uint8_t channel; bool coex_background_scan; } wifi_scan_config_t;
#define WIFI_VND_IE_TYPE_BEACON 0
#define WIFI_VND_IE_TYPE_PROBE_RESP 1
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
#define ESP_LOGI(...) ((void)0)
static void *s_operation_lock=(void *)1;
static int s_hotspot_lock;
static uint8_t s_hotspots[PASSPORT_SCAN_MAX][6];
static size_t s_hotspot_count;
static bool s_scanning, fail_start, fail_country, fail_emit;
static unsigned records, cursor, clears, locks, scans, emissions, fail_channel, channel;
static wifi_country_t fixture_country = {3, 3};
static int start_once(void) { return fail_start ? -1 : ESP_OK; }
static int esp_wifi_get_country(wifi_country_t *country) {
    *country=fixture_country;
    return fail_country ? -1 : ESP_OK;
}
static int xSemaphoreTake(void *lock, int timeout) { (void)lock; (void)timeout; locks++; return pdTRUE; }
static void xSemaphoreGive(void *lock) { (void)lock; assert(locks); locks--; }
static void on_vendor_ie(void *, wifi_vendor_ie_type_t, const uint8_t[6], const vendor_ie_data_t *, int);
static int esp_wifi_scan_start(const wifi_scan_config_t *config, bool blocking) {
    assert(blocking && s_scanning && locks && config->coex_background_scan);
    assert(config->channel == fixture_country.schan + scans);
    channel=config->channel; scans++; cursor=0;
    const vendor_ie_data_t hotspot={0xdd,0x0a,{0x00,0x17,0xf2,0x06,0x01,0x01,0x03,0x01,0x00,0x00}};
    for (unsigned index=0; channel==4 && index<40; index++) {
        uint8_t address[6]={0,0,0,0,0,(uint8_t)index};
        on_vendor_ie(NULL, WIFI_VND_IE_TYPE_BEACON, address, &hotspot, -50);
        on_vendor_ie(NULL, WIFI_VND_IE_TYPE_PROBE_RESP, address, &hotspot, -50);
    }
    return channel==fail_channel ? -1 : ESP_OK;
}
static int esp_wifi_scan_get_ap_record(wifi_ap_record_t *record) {
    if (cursor>=records) return -1;
    memset(record,0,sizeof(*record));
    if (cursor==0) strcpy((char *)record->ssid,"bad\nname");
    else if (cursor>1) snprintf((char *)record->ssid,sizeof(record->ssid),"Network %u",cursor);
    record->bssid[5]=(uint8_t)cursor++;
    record->rssi=channel==4 ? -40 : channel==3 ? -50 : -60;
    return ESP_OK;
}
static void esp_wifi_clear_ap_list(void) { clears++; }
static int found(void *ctx, const passport_scan_t *network) {
    assert(ctx == &emissions && locks && !s_scanning);
    assert(scans < fixture_country.nchan);
    assert(network->ssid[0]);
    emissions++;
    return fail_emit ? -1 : 0;
}
static void reset_scan(void) { scans=emissions=clears=0; }
"""

SCAN_TEST = r"""
int main(void) {
    (void)esp_wifi_get_config; (void)passport_wifi_is_online;
    passport_scan_t output[PASSPORT_SCAN_MAX+1];
    assert(passport_wifi_scan(NULL,1,NULL,NULL)==-1 && passport_wifi_scan(output,0,NULL,NULL)==-1);
    fail_start=true;
    assert(passport_wifi_scan(output,1,NULL,NULL)==-1 && !locks && !s_scanning);
    fail_start=false; records=40;
    fail_country=true;
    assert(passport_wifi_scan(output,1,NULL,NULL)==-1 && !locks && !scans);
    fail_country=false; fixture_country.nchan=0;
    assert(passport_wifi_scan(output,1,NULL,NULL)==-1 && !locks && !scans);
    fixture_country.nchan=3;
    assert(passport_wifi_scan(output,PASSPORT_SCAN_MAX+1,found,&emissions)==PASSPORT_SCAN_MAX);
    assert(scans==3 && emissions==64 && clears==4 && !locks && !s_scanning);
    assert(!strcmp(output[0].ssid,"Network 2") && output[0].personal_hotspot && output[0].has_bssid);
    assert(output[0].rssi==-40);
    assert(!output[PASSPORT_SCAN_MAX-1].personal_hotspot);
    uint8_t address[6]={0};
    const vendor_ie_data_t hotspot={0xdd,0x0a,{0x00,0x17,0xf2,0x06,0x01,0x01,0x03,0x01,0,0}};
    s_hotspot_count=0;
    on_vendor_ie(NULL,WIFI_VND_IE_TYPE_BEACON,address,&hotspot,-50);
    assert(!s_hotspot_count);
    s_scanning=true;
    on_vendor_ie(NULL,99,address,&hotspot,-50);
    on_vendor_ie(NULL,WIFI_VND_IE_TYPE_BEACON,NULL,&hotspot,-50);
    on_vendor_ie(NULL,WIFI_VND_IE_TYPE_BEACON,address,NULL,-50);
    assert(!s_hotspot_count);
    s_scanning=false; reset_scan(); fail_channel=4;
    assert(passport_wifi_scan(output,1,found,&emissions)==-1);
    assert(scans==2 && emissions==1 && clears==2 && !locks && !s_scanning);
    reset_scan(); fail_channel=0;
    assert(passport_wifi_scan(output,1,NULL,NULL)==1 && scans==3 && clears==4 && !locks);
    reset_scan(); fail_emit=true;
    assert(passport_wifi_scan(output,1,found,&emissions)==-1);
    assert(scans==1 && emissions==1 && clears==1 && !locks && !s_scanning);
    reset_scan(); fail_emit=false; records=0;
    assert(passport_wifi_scan(output,1,found,&emissions)==0 && scans==3 && !emissions && !locks);
    puts("Progressive Wi-Fi scan, regulatory channels, bounded deduplication, hotspot upgrades and failure cleanup PASS");
}
"""


class NetworkRuntimeTests(unittest.TestCase):
    def run_harness(self, harness, source, names, acceptance, extra=()):
        functions = "\n".join(function_source(source, name) for name in names)
        with tempfile.TemporaryDirectory(prefix="passport-network-") as directory:
            program, executable = Path(directory) / "network.c", Path(directory) / "network"
            program.write_text(COMMON + harness + functions + acceptance)
            subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra", "-Werror",
                            "-I", str(ROOT / "app"), str(program), *map(str, extra), "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)

    def test_worker(self):
        self.run_harness(QUEUE, (ROOT / "main/passport_link.c").read_text(),
                         ["join_wifi", "configuration_line", "passport_link_configuration_busy", "passport_link_configuration_pending", "queue_command", "queue_line",
                          "passport_link_connect_network", "passport_link_network_result", "save_network", "process_line"],
                         QUEUE_TEST, [ROOT / "app" / name for name in
                                      ("passport_network.c", "passport_store.c", "passport_proxy.c")])

    def test_radio_rollback(self):
        self.run_harness(RADIO, (ROOT / "main/passport_wifi.c").read_text(), ["passport_wifi_join"], RADIO_TEST)

    def test_scan(self):
        self.run_harness(SCAN, (ROOT / "main/passport_wifi.c").read_text(),
                         ["on_vendor_ie", "passport_wifi_scan"], SCAN_TEST)

    def test_controller(self):
        self.run_harness(CONTROLLER, (ROOT / "main/passport_app.c").read_text(),
                         ["maintain_network_selection"], CONTROLLER_TEST)

    def test_bluetooth_configuration_lifetime(self):
        self.run_harness(BLUETOOTH_CONTROLLER, (ROOT / "main/passport_app.c").read_text(),
                         ["request_bluetooth_config", "close_bluetooth_config", "maintain_bluetooth_mode"], BLUETOOTH_CONTROLLER_TEST,
                         [ROOT / "app/passport_radio.c"])

    def test_call_refresh_stays_on_display_task(self):
        harness = r"""
static struct { bool sleeping; } s_power;
static passport_view_t s_view;
static passport_store_t s_store;
static bool locked, lock_fails;
static unsigned shown;
#define lv_refr_now(display) assert(!"Rendering must stay on the LVGL task")
#define lv_display_refr_timer(timer) assert(!"Rendering must stay on the LVGL task")
#define lv_timer_handler() assert(!"Rendering must stay on the LVGL task")
static bool bsp_lvgl_lock(int timeout) {
    assert(timeout==50 && !locked);
    if (lock_fails) return false;
    locked=true;return true;
}
static void bsp_lvgl_unlock(void) { assert(locked);locked=false; }
static void passport_screen_show(const passport_view_t *view, const passport_store_t *store) {
    assert(locked && view==&s_view && store==&s_store);shown++;
}
"""
        acceptance = r"""
int main(void) {
    (void)esp_wifi_get_config;(void)passport_wifi_is_online;
    s_view.screen=PASSPORT_SCREEN_HOME;redraw();assert(shown==1 && !locked);
    s_view.screen=PASSPORT_SCREEN_LIVE;
    lock_fails=true;redraw();assert(shown==1 && !locked);
    lock_fails=false;redraw();assert(shown==2 && !locked);
    redraw();assert(shown==3 && !locked);
    s_view.live_ready=true;redraw();assert(shown==4 && !locked);
    s_view.live_ready=false;redraw();assert(shown==5 && !locked);
    s_view.live_failed=true;redraw();assert(shown==6 && !locked);
    s_view.screen=PASSPORT_SCREEN_HOME;redraw();assert(shown==7 && !locked);
    unsigned before=shown;
    s_power.sleeping=true;s_view.screen=PASSPORT_SCREEN_LIVE;s_view.live_failed=false;
    redraw();assert(shown==before && !locked);
    s_power.sleeping=false;redraw();assert(shown==before+1 && !locked);
    return 0;
}
"""
        self.run_harness(harness, (ROOT / "main/passport_app.c").read_text(), ["redraw"], acceptance)


if __name__ == "__main__":
    unittest.main()
