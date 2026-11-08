#include "badge.h"
#include "badge_pair.h"

#include <stdio.h>
#include <string.h>

static int g_failed;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_failed++;
    }
}

static void test_store_order(void) {
    badge_store_t store;
    bool visible[4] = {true, true, false, false};
    int rssi[4] = {-70, -40, 0, 0};
    badge_store_init(&store);
    expect(badge_store_add(&store, "家里", "home", "a"), "add home");
    expect(badge_store_add(&store, "手机热点", "phone", "b"), "add phone");
    expect(badge_store_add(&store, "家里", "home", "changed"), "update home");
    expect(store.count == 2, "dedupe ssid");
    {
        badge_store_t full;
        char ssid[8];
        badge_store_init(&full);
        for (int i = 0; i < BADGE_NET_MAX; i++) {
            snprintf(ssid, sizeof(ssid), "n%d", i);
            badge_store_add(&full, ssid, ssid, "p");
        }
        expect(!badge_store_add(&full, "extra", "extra", "p"), "full list refuses new");
        expect(badge_store_add(&full, "n3", "n3", "q"), "full list still updates");
        expect(strcmp(full.nets[3].pass, "q") == 0, "full list password changed");
    }
    expect(strcmp(store.nets[0].pass, "changed") == 0, "password replaced");
    expect(badge_store_pick(&store, visible, rssi, 2) == 0, "priority beats rssi");
    expect(badge_store_promote(&store, 1), "promote phone");
    expect(strcmp(store.nets[0].ssid, "phone") == 0, "phone is first");
    expect(badge_store_pick(&store, visible, rssi, 2) == 0, "promoted network wins");
    visible[0] = false;
    expect(badge_store_pick(&store, visible, rssi, 2) == 1, "skip hidden network");
    expect(badge_store_forget(&store, 0), "forget phone");
    expect(store.count == 1, "one network left");
    expect(strcmp(store.nets[0].ssid, "home") == 0, "home remains");
    expect(!badge_store_add(&store, "", "", ""), "reject empty ssid");
}

static void test_navigation(void) {
    badge_store_t store;
    badge_view_t v;
    badge_store_init(&store);
    badge_store_add(&store, "家里", "home", "a");
    badge_store_add(&store, "热点", "phone", "b");
    badge_agent_upsert(&store, "助手", "openclaw", "wss://main.example", "", NULL, NULL, NULL);
    badge_agent_upsert(&store, "办公室", "openclaw", "wss://work.example", "", NULL, NULL, NULL);
    badge_view_init(&v);
    v.retry_send=true;
    expect(badge_key_binding(&v,&store,BADGE_BTN_OK).action==BADGE_ACT_RETRY,"home keeps the pending clip reachable");
    expect(!badge_key_binding(&v,&store,BADGE_BTN_DOWN).enabled,"Live is unavailable while a clip waits");
    badge_view_init(&v);
    expect(badge_key_binding(&v,&store,BADGE_BTN_UP).action == BADGE_ACT_PREFERENCES, "home up opens settings");
    badge_on_key(&v,&store,BADGE_BTN_UP,BADGE_CLICK);
    badge_on_key(&v,&store,BADGE_BTN_DOWN,BADGE_CLICK);
    badge_on_key(&v,&store,BADGE_BTN_UP,BADGE_CLICK);
    badge_on_key(&v,&store,BADGE_BTN_UP,BADGE_CLICK);
    expect(v.cursor == 2, "cycle reaches add row");
    expect(badge_on_key(&v,&store,BADGE_BTN_DOWN,BADGE_CLICK) == BADGE_ACT_ADD_NETWORK, "add emits radio configuration action");
    expect(v.screen == BADGE_SCREEN_ADD, "add opens setup");
    badge_on_key(&v,&store,BADGE_BTN_OK,BADGE_CLICK);
    expect(v.screen == BADGE_SCREEN_NETS, "setup returns to its parent");
    v.cursor = 1;
    badge_on_key(&v,&store,BADGE_BTN_DOWN,BADGE_CLICK);
    expect(v.screen == BADGE_SCREEN_NET_MENU, "open displays network detail");
    expect(badge_on_key(&v,&store,BADGE_BTN_UP,BADGE_CLICK) == BADGE_ACT_PRIORITIZE, "priority is an explicit persistence action");
    expect(!strcmp(store.nets[0].ssid,"home"), "UI does not mutate configuration before persistence");
    expect(badge_key_binding(&v,&store,BADGE_BTN_DOWN).action == BADGE_ACT_CONFIGURE,
           "network detail offers configuration, not a per-Wi-Fi proxy");
    badge_on_key(&v,&store,BADGE_BTN_OK,BADGE_CLICK);
    expect(v.screen == BADGE_SCREEN_NETS, "detail returns to network selection");
    badge_on_key(&v,&store,BADGE_BTN_OK,BADGE_CLICK);
    expect(v.screen == BADGE_SCREEN_PREFERENCES, "network returns to settings");

    badge_on_key(&v,&store,BADGE_BTN_UP,BADGE_CLICK);
    badge_on_key(&v,&store,BADGE_BTN_DOWN,BADGE_CLICK);
    expect(v.screen == BADGE_SCREEN_SETTINGS, "down opens assistants");
    expect(badge_key_binding(&v,&store,BADGE_BTN_DOWN).action == BADGE_ACT_CONFIGURE, "active assistant exposes setup");
    badge_on_key(&v,&store,BADGE_BTN_UP,BADGE_CLICK);
    expect(badge_on_key(&v,&store,BADGE_BTN_DOWN,BADGE_CLICK) == BADGE_ACT_SELECT_AGENT, "candidate emits selection action");
    expect(store.active_agent == 0, "owner must persist agent selection");
    badge_on_key(&v,&store,BADGE_BTN_OK,BADGE_CLICK);
    expect(v.screen == BADGE_SCREEN_PREFERENCES, "assistant returns to settings");
    badge_on_key(&v,&store,BADGE_BTN_OK,BADGE_CLICK);

    badge_on_key(&v,&store,BADGE_BTN_OK,BADGE_CLICK);
    expect(v.recording && v.screen == BADGE_SCREEN_CAPTURE, "recording has a dedicated screen");
    expect(badge_key_binding(&v,&store,BADGE_BTN_UP).action == BADGE_ACT_CANCEL_RECORD, "capture has explicit cancel");
    expect(badge_on_key(&v,&store,BADGE_BTN_DOWN,BADGE_CLICK) == BADGE_ACT_RERECORD && v.recording, "capture can restart");
    badge_on_key(&v,&store,BADGE_BTN_UP,BADGE_CLICK);
    expect(!v.recording && !v.sending && v.screen == BADGE_SCREEN_HOME, "cancel does not send");
    badge_on_key(&v,&store,BADGE_BTN_OK,BADGE_CLICK);
    badge_on_key(&v,&store,BADGE_BTN_OK,BADGE_CLICK);
    expect(!v.recording && v.sending && v.screen == BADGE_SCREEN_PROGRESS, "second OK sends once");
    expect(badge_key_binding(&v,&store,BADGE_BTN_UP).action==BADGE_ACT_CANCEL_SEND,"sending exposes cancel rather than settings");
    expect(badge_key_binding(&v,&store,BADGE_BTN_DOWN).action==BADGE_ACT_RESTART_SEND,"sending exposes a restart action");
    badge_on_key(&v,&store,BADGE_BTN_OK,BADGE_CLICK);
    expect(v.cancel_pending && v.screen==BADGE_SCREEN_CANCELLING && v.sending,"back ends the operation and waits for safe cleanup");
    badge_finish_send(&v,true,false,"late reply");
    expect(v.cancel_pending && !v.reply_unread,"late reply cannot override pending cancellation");
    expect(!badge_finish_cancel(&v,"已取消"),"ordinary cancel does not restart");
    expect(v.screen==BADGE_SCREEN_HOME && !v.sending && !v.retry_send && !v.reply_unread,"cancel returns to a genuinely idle home");
    expect(badge_key_binding(&v,&store,BADGE_BTN_OK).action==BADGE_ACT_RECORD,"a new recording is immediately reachable");
    badge_finish_send(&v,true,false,"old result");
    expect(v.screen==BADGE_SCREEN_HOME && !v.reply_unread,"abandoned task cannot pop up later");
    v.sending=true;v.screen=BADGE_SCREEN_PROGRESS;
    badge_on_key(&v,&store,BADGE_BTN_DOWN,BADGE_CLICK);
    expect(v.cancel_pending && v.restart_after_cancel && !v.recording,"re-record waits for network resources to be released");
    expect(badge_finish_cancel(&v,"已取消"),"re-record requests capture after cleanup");
    badge_on_key(&v,&store,BADGE_BTN_OK,BADGE_CLICK);
    expect(v.recording && v.screen==BADGE_SCREEN_CAPTURE,"new capture starts after cleanup");
    badge_on_key(&v,&store,BADGE_BTN_UP,BADGE_CLICK);

    v.sending=true;v.screen=BADGE_SCREEN_PROGRESS;
    badge_finish_send(&v,false,true,"发送失败");
    expect(v.retry_send && v.screen==BADGE_SCREEN_FAILURE,"failed send has its own retry page");
    expect(badge_on_key(&v,&store,BADGE_BTN_UP,BADGE_CLICK)==BADGE_ACT_RETRY && v.sending,"failure can retry");
    badge_finish_send(&v,false,true,"发送失败");
    expect(badge_key_binding(&v,&store,BADGE_BTN_OK).action==BADGE_ACT_DISCARD,"failure offers cancel instead of trapping a retained clip");
    badge_on_key(&v,&store,BADGE_BTN_OK,BADGE_CLICK);
    expect(v.screen==BADGE_SCREEN_HOME && !v.retry_send,"cancel discards the pending retry state");
    expect(badge_key_binding(&v,&store,BADGE_BTN_OK).action==BADGE_ACT_RECORD,"home talks again after a failure is cancelled");
    badge_set_reply(&v,"之前的回复");v.reply_unread=true;
    badge_offer_retry(&v,"录音已恢复，可重试");
    expect(v.screen==BADGE_SCREEN_FAILURE && v.retry_send && !v.reply_unread,"restored status is not a chat reply");
    badge_on_key(&v,&store,BADGE_BTN_OK,BADGE_CLICK);
    expect(!v.retry_send && !v.reply_unread && v.screen==BADGE_SCREEN_HOME,"restored recordings can be abandoned with one click");
    for (int screen = BADGE_SCREEN_HOME; screen <= BADGE_SCREEN_CANCELLING; screen++) {
        badge_view_init(&v);v.screen = (badge_screen_t)screen;
        badge_screen_t original = v.screen;
        for (int key = BADGE_BTN_UP; key <= BADGE_BTN_OK; key++) {
            expect(badge_on_key(&v,&store,(badge_btn_t)key,BADGE_HOLD) == BADGE_ACT_NONE, "hold never selects a hidden action");
            expect(badge_on_key(&v,&store,(badge_btn_t)key,BADGE_RELEASE) == BADGE_ACT_NONE, "raw release never duplicates click");
            expect(v.screen == original, "non-click events preserve page");
        }
    }
    for (int screen = BADGE_SCREEN_NETS; screen <= BADGE_SCREEN_SETTINGS; screen++) {
        badge_view_init(&v);v.screen = (badge_screen_t)screen;v.setup_parent=BADGE_SCREEN_NET_MENU;
        for (int steps=0;steps<4 && v.screen!=BADGE_SCREEN_HOME;steps++)
            badge_on_key(&v,&store,v.screen==BADGE_SCREEN_TALK ? BADGE_BTN_UP : BADGE_BTN_OK,BADGE_CLICK);
        expect(v.screen==BADGE_SCREEN_HOME,"every subpage has a bounded single-click path home");
    }
    badge_store_init(&store);badge_view_init(&v);
    badge_on_key(&v,&store,BADGE_BTN_UP,BADGE_CLICK);
    badge_on_key(&v,&store,BADGE_BTN_UP,BADGE_CLICK);
    badge_on_key(&v,&store,BADGE_BTN_DOWN,BADGE_CLICK);
    expect(!badge_key_binding(&v,&store,BADGE_BTN_UP).enabled,"empty assistant list disables cycling");
    expect(badge_on_key(&v,&store,BADGE_BTN_DOWN,BADGE_CLICK)==BADGE_ACT_CONFIGURE,"empty assistant page can open setup");
    v.bluetooth_ready=true;
    expect(badge_on_key(&v,&store,BADGE_BTN_DOWN,BADGE_CLICK)==BADGE_ACT_CLOSE_CONFIG,"setup has a visible close action");
    badge_on_key(&v,&store,BADGE_BTN_OK,BADGE_CLICK);
    badge_on_key(&v,&store,BADGE_BTN_OK,BADGE_CLICK);
    badge_on_key(&v,&store,BADGE_BTN_OK,BADGE_CLICK);
    expect(v.screen==BADGE_SCREEN_HOME,"empty configuration can always return home");
}

static void test_navigation_sequences(void) {
    badge_view_t view;
    badge_store_t store;
    badge_store_init(&store);
    badge_store_add(&store,"Home","home","p");
    badge_store_add(&store,"Hotspot","hotspot","p");
    badge_agent_upsert(&store,"Main","openclaw","wss://main.example","",NULL,NULL,NULL);
    badge_agent_upsert(&store,"Office","openclaw","wss://office.example","",NULL,NULL,NULL);
    badge_view_init(&view);
    unsigned random = 12345;
    for (int i=0;i<2000;i++) {
        random = random * 1664525u + 1013904223u;
        badge_btn_t button = (badge_btn_t)((random >> 8) % 3);
        badge_key_binding_t binding = badge_key_binding(&view,&store,button);
        expect(badge_action_label(binding.action,true)[0]!=0,"every visible key has a caption");
        badge_action_t action=badge_on_key(&view,&store,button,BADGE_CLICK);
        expect(action==(binding.enabled ? binding.action : BADGE_ACT_NONE),"caption binding and executed action cannot disagree");
        if (action==BADGE_ACT_CONFIGURE || action==BADGE_ACT_ENABLE_CONFIG || action==BADGE_ACT_ADD_NETWORK) view.bluetooth_ready=true;
        if (action==BADGE_ACT_CLOSE_CONFIG) view.bluetooth_ready=false;
        if (view.cancel_pending && (random & 3)==0) {
            bool restart=badge_finish_cancel(&view,"cancelled");
            if (restart) badge_on_key(&view,&store,BADGE_BTN_OK,BADGE_CLICK);
        } else if (view.sending && !view.cancel_pending && (random & 7)==0) badge_finish_send(&view,(random & 16)!=0,true,"test reply");
        for(int key=0;key<3;key++) {
            badge_action_t bound=badge_key_binding(&view,&store,(badge_btn_t)key).action;
            expect((bound!=BADGE_ACT_NETWORKS && bound!=BADGE_ACT_AGENTS) || view.screen==BADGE_SCREEN_PREFERENCES,"network and assistants exist only in settings");
        }
        expect(!(view.recording && view.sending),"recording and sending remain mutually exclusive");
        expect(view.screen!=BADGE_SCREEN_CAPTURE || view.recording,"capture page always owns a recording");
        expect(view.screen!=BADGE_SCREEN_PROGRESS || view.sending,"progress page always has a task");
    }
}

static void test_live_navigation(void) {
    badge_store_t store;
    badge_view_t view;
    badge_store_init(&store); badge_view_init(&view);
    expect(!view.reply[0],"home starts without a synthetic reply or a removed mode prompt");
    const char *home_labels[]={"设置","通话","提问"};
    const char *home_labels_en[]={"Setup","Call","Ask"};
    for (int button=BADGE_BTN_UP;button<=BADGE_BTN_OK;button++) {
        badge_key_binding_t binding=badge_key_binding(&view,&store,(badge_btn_t)button);
        expect(!strcmp(badge_action_label(binding.action,true),home_labels[button]),"home captions match the three product entries");
        expect(!strcmp(badge_action_label(binding.action,false),home_labels_en[button]),"English home captions match the same actions");
    }
    expect(badge_on_key(&view,&store,BADGE_BTN_UP,BADGE_CLICK)==BADGE_ACT_PREFERENCES &&
           view.screen==BADGE_SCREEN_PREFERENCES,"settings do not introduce another conversation menu");
    expect(view.speaker_volume == 55, "speaker defaults to the previous call volume");
    badge_on_key(&view,&store,BADGE_BTN_UP,BADGE_CLICK);
    badge_on_key(&view,&store,BADGE_BTN_UP,BADGE_CLICK);
    expect(badge_on_key(&view,&store,BADGE_BTN_DOWN,BADGE_CLICK)==BADGE_ACT_VOLUME && view.screen==BADGE_SCREEN_VOLUME,
           "volume is reachable without starting a call");
    expect(badge_on_key(&view,&store,BADGE_BTN_UP,BADGE_CLICK)==BADGE_ACT_VOLUME_UP && view.speaker_volume==60,
           "speaker volume changes in five-percent steps");
    for (int step=0;step<30;step++) badge_on_key(&view,&store,BADGE_BTN_UP,BADGE_CLICK);
    expect(view.speaker_volume==100 && !badge_key_binding(&view,&store,BADGE_BTN_UP).enabled,"volume stops at maximum");
    for (int step=0;step<30;step++) badge_on_key(&view,&store,BADGE_BTN_DOWN,BADGE_CLICK);
    expect(view.speaker_volume==0 && !badge_key_binding(&view,&store,BADGE_BTN_DOWN).enabled && !view.live_muted,"silent speaker does not mute microphone");
    view.sending=true;
    expect(!badge_key_binding(&view,&store,BADGE_BTN_UP).enabled,"no codec control while a call owns audio");
    view.sending=false;
    badge_on_key(&view,&store,BADGE_BTN_OK,BADGE_CLICK);
    expect(view.screen==BADGE_SCREEN_PREFERENCES && view.preference_cursor==2,"volume returns to its selected settings row");
    expect(badge_on_key(&view,&store,BADGE_BTN_OK,BADGE_CLICK)==BADGE_ACT_BACK &&
           view.screen==BADGE_SCREEN_HOME,"settings return directly to the two voice entries");
    expect(badge_on_key(&view,&store,BADGE_BTN_OK,BADGE_CLICK)==BADGE_ACT_RECORD && view.recording,"home starts a recording");
    expect(view.screen==BADGE_SCREEN_CAPTURE,"talk owns the capture screen");
    badge_on_key(&view,&store,BADGE_BTN_UP,BADGE_CLICK);
    expect(badge_on_key(&view,&store,BADGE_BTN_DOWN,BADGE_CLICK)==BADGE_ACT_LIVE,"Live has its own entry");
    expect(view.screen==BADGE_SCREEN_LIVE && view.sending && !view.recording,"Live owns the audio session, not a clip");
    expect(!badge_key_binding(&view,&store,BADGE_BTN_UP).enabled,"microphone controls wait for ready");
    view.live_ready=true;
    expect(badge_on_key(&view,&store,BADGE_BTN_UP,BADGE_CLICK)==BADGE_ACT_LIVE_MUTE && view.live_muted,"Live exposes a mute action");
    expect(badge_on_key(&view,&store,BADGE_BTN_UP,BADGE_CLICK)==BADGE_ACT_LIVE_UNMUTE && !view.live_muted,"Live can unmute");
    for (int button=1;button<3;button++) {
        badge_view_t active=view;
        expect(badge_on_key(&active,&store,(badge_btn_t)button,BADGE_CLICK)==(button==1?BADGE_ACT_LIVE_RECONNECT:BADGE_ACT_END_LIVE),"Live can reconnect or end");
        expect(active.cancel_pending && active.screen==BADGE_SCREEN_CANCELLING,"Live exit waits for cleanup");
    }
    badge_finish_send(&view,false,false,"Realtime unavailable");
    expect(view.screen==BADGE_SCREEN_LIVE && view.live_failed && !view.sending && !view.live_ready,"failed Live retains its reason and reconnect controls");
    expect(!badge_key_binding(&view,&store,BADGE_BTN_UP).enabled,"disconnected Call does not offer a working microphone");
    expect(badge_key_binding(&view,&store,BADGE_BTN_DOWN).enabled && badge_key_binding(&view,&store,BADGE_BTN_OK).enabled,"disconnected Call can reconnect or end");
    expect(badge_on_key(&view,&store,BADGE_BTN_DOWN,BADGE_CLICK)==BADGE_ACT_LIVE_RECONNECT && view.cancel_pending,"failed Call requests reconnect through cleanup");
    badge_finish_cancel(&view,"Cancelled");
    badge_on_key(&view,&store,BADGE_BTN_DOWN,BADGE_CLICK);
    expect(view.screen==BADGE_SCREEN_LIVE && !view.live_failed && !view.live_muted,"new Call clears disconnected state");
}

static void test_persist_and_portal(void) {
    badge_store_t store;
    badge_store_t loaded;
    uint8_t blob[1024];
    char qr[96];
    badge_portal_form_t form;
    badge_store_init(&store);
    badge_store_add(&store, "家里", "home", "secret");
    expect(badge_agent_upsert(&store, "家里的 OpenClaw", "openclaw", "wss://gw.example", "boot",
                              NULL, "cf-id", "cf-secret"),
           "add openclaw");
    store.connected = 0;
    int n = badge_store_save(&store, blob, sizeof(blob));
    expect(n > 16, "blob written");
    badge_store_init(&loaded);
    expect(badge_store_load(&loaded, blob, (size_t)n), "blob loads");
    expect(loaded.count == 1, "loaded count");
    expect(strcmp(loaded.nets[0].pass, "secret") == 0, "loaded password");
    expect(loaded.agent_count == 1, "loaded agent");
    expect(strcmp(badge_agent_active(&loaded)->url, "wss://gw.example") == 0, "loaded url");
    expect(strcmp(badge_agent_active(&loaded)->bootstrap, "boot") == 0, "loaded bootstrap");
    expect(strcmp(badge_agent_active(&loaded)->access_secret, "cf-secret") == 0, "loaded access");
    expect(badge_lang_zh(&loaded), "loaded language");
    blob[0] ^= 0xff;
    expect(!badge_store_load(&loaded, blob, (size_t)n), "bad magic rejected");

    badge_setup_wifi_qr(qr, sizeof(qr));
    expect(strcmp(qr, "WIFI:T:WPA;S:" BADGE_AP_SSID ";P:" BADGE_AP_PASS ";;") == 0,
           "setup qr");

    memset(&form, 0, sizeof(form));
    snprintf(form.ssid, sizeof(form.ssid), "Cafe");
    snprintf(form.pass, sizeof(form.pass), "latte");
    snprintf(form.gateway_url, sizeof(form.gateway_url), "https://nope");
    expect(strcmp(badge_portal_apply(&store, &form), "url") == 0, "reject https");
    expect(store.count == 1, "failed form keeps list");
    snprintf(form.agent_name, sizeof(form.agent_name), "家里的 OpenClaw");
    snprintf(form.gateway_url, sizeof(form.gateway_url), "wss://gw.example/ws");
    snprintf(form.bootstrap, sizeof(form.bootstrap), "next");
    expect(strcmp(badge_portal_apply(&store, &form), "ok") == 0, "portal save");
    expect(store.count == 2, "cafe added");
    expect(store.connected == 1, "cafe selected");
    expect(strcmp(badge_agent_active(&store)->bootstrap, "next") == 0, "bootstrap updated");
    expect(strcmp(badge_agent_active(&store)->access_secret, "cf-secret") == 0,
           "same name keeps the access secret");
    expect(badge_agent_upsert(&store, "公司", "openclaw", "wss://work.example", "wk", NULL, NULL,
                              NULL),
           "add second openclaw");
    expect(store.agent_count == 2, "two agents");
    expect(store.active_agent == 0, "first agent stays current");
    expect(badge_agent_select(&store, 1), "select second");
    expect(strcmp(badge_agent_active(&store)->name, "公司") == 0, "second active");
    expect(!badge_agent_upsert(&store, "别的", "hermes", "https://hermes.example", "x", NULL, NULL,
                              NULL),
           "only openclaw");
}

static int capture_reply(void *ctx, const char *line) {
    char *buf = ctx;
    size_t used = strlen(buf);
    snprintf(buf + used, 1024 - used, "%s\n", line);
    return 0;
}

static int g_join_calls;

static bool g_join_keep;

static const char *fake_join(void *ctx, const char *ssid, const char *pass, bool keep) {
    (void)ctx;
    g_join_calls++;
    g_join_keep = keep;
    if (strcmp(ssid, "Cafe") != 0) {
        return "notfound";
    }
    return strcmp(pass, "a b") == 0 ? NULL : "pass";
}

static void test_usb(void) {
    badge_store_t store;
    badge_scan_t scan[1];
    char log[1024];
    badge_usb_t usb;
    badge_store_init(&store);
    snprintf(scan[0].ssid, sizeof(scan[0].ssid), "Cafe");
    scan[0].rssi = -40;
    memset(&usb, 0, sizeof(usb));
    usb.store = &store;
    usb.scan = scan;
    usb.scan_count = 1;
    usb.reply = capture_reply;
    usb.reply_ctx = log;
    usb.join = fake_join;
    log[0] = '\0';
    badge_usb_line(&usb, "HELLO");
    expect(strstr(log, "HELLO ai-passport-openclaw 1 0 0 zh") != NULL, "hello empty");
    log[0] = '\0';
    badge_usb_line(&usb, "SCAN");
    expect(strstr(log, "NET -40 Cafe") != NULL, "scan row");
    expect(strstr(log, "SCAN END") != NULL, "scan end");
    log[0] = '\0';
    badge_usb_line(&usb, "SAVE 1 Cafe a%20b");
    expect(g_join_calls == 1, "join attempted");
    expect(g_join_keep, "save keeps the network");
    expect(strstr(log, "OK saved") != NULL, "save ok");
    expect(store.count == 1, "usb added network");
    expect(strcmp(store.nets[0].pass, "a b") == 0, "password decoded");
    expect(store.agent_count == 0, "wifi alone adds no agent");
    log[0] = '\0';
    uint32_t generation = store.generation;
    char command[160];
    /* {"url":"wss://work.example","bootstrapToken":"wk"} */
    snprintf(command, sizeof(command),
             "AGENT %u %%E5%%85%%AC%%E5%%8F%%B8 eyJ1cmwiOiJ3c3M6Ly93b3JrLmV4YW1wbGUiLCJib290c3RyYXBUb2tlbiI6IndrIn0",
             generation);
    badge_usb_line(&usb, command);
    expect(strstr(log, "OK agent") != NULL, "agent saved");
    expect(store.agent_count == 1, "usb added the paired agent");
    expect(strcmp(badge_agent_active(&store)->url, "wss://work.example") == 0, "pair url");
    expect(strcmp(badge_agent_active(&store)->bootstrap, "wk") == 0, "pair bootstrap stored");
    expect(store.generation == generation + 1, "generation advanced");
    snprintf(command, sizeof(command), "USE %u 1", generation);
    badge_usb_line(&usb, command);
    expect(strstr(log, "ERR stale") != NULL, "stale use rejected");
    snprintf(command, sizeof(command), "USE %u 0", store.generation);
    badge_usb_line(&usb, command);
    expect(strcmp(badge_agent_active(&store)->name, "公司") == 0, "usb selected the paired one");
    log[0] = '\0';
    badge_usb_line(&usb, "AGENTS");
    expect(strstr(log, "wss%3A%2F%2Fwork.example") != NULL, "agents include url");
    expect(strstr(log, " pending") != NULL, "agents report pending, not the secret");
    expect(strstr(log, " wk") == NULL, "agents never return the bootstrap");
    expect(strstr(log, "SAVED") == NULL, "agents are not networks");
    badge_usb_line(&usb, "NETS");
    expect(strstr(log, "SAVED 0") != NULL, "saved network listed");
    log[0] = '\0';
    uint32_t before = store.generation;
    snprintf(command, sizeof(command), "LANG %u en", store.generation);
    badge_usb_line(&usb, command);
    expect(!badge_lang_zh(&store), "language switched");
    expect(store.generation == before + 1, "language bumps generation");
    log[0] = '\0';
    before = store.generation;
    badge_usb_line(&usb, "TRY Cafe a%20b");
    expect(strstr(log, "TRY ok") != NULL, "try ok");
    expect(!g_join_keep, "try goes back");
    badge_usb_line(&usb, "TRY Cafe");
    expect(strstr(log, "TRY ok\nTRY ok") != NULL, "try reuses saved password");
    badge_usb_line(&usb, "TRY Cafe -");
    expect(strstr(log, "TRY ok\nTRY ok\nTRY ok") != NULL, "try treats - as the saved password");
    badge_usb_line(&usb, "TRY Cafe nope");
    expect(strstr(log, "TRY fail pass") != NULL, "try wrong password");
    badge_usb_line(&usb, "TRY Far x");
    expect(strstr(log, "TRY fail notfound") != NULL, "try unknown network");
    expect(store.generation == before && store.count == 1, "try saves nothing");
    log[0] = '\0';
    snprintf(command, sizeof(command), "SAVE %u Cafe nope", store.generation);
    badge_usb_line(&usb, command);
    expect(strstr(log, "ERR join pass") != NULL, "save reports reason");
    expect(strcmp(store.nets[0].pass, "a b") == 0, "failed save keeps password");
    log[0] = '\0';
    snprintf(command, sizeof(command), "SAVE %u Cafe -   off - 0", store.generation);
    badge_usb_line(&usb, command);
    expect(strstr(log, "OK saved") != NULL && strcmp(store.nets[0].pass, "a b") == 0,
           "save keeps the stored password for -");
    log[0] = '\0';
    snprintf(command, sizeof(command), "FORGET %u 0", store.generation - 1);
    badge_usb_line(&usb, command);
    expect(strstr(log, "ERR stale") != NULL && store.count == 1, "stale forget rejected");
    snprintf(command, sizeof(command), "FORGET %u 5", store.generation);
    badge_usb_line(&usb, command);
    expect(strstr(log, "ERR net") != NULL, "forget out of range");
    before = store.generation;
    snprintf(command, sizeof(command), "FORGET %u 0", store.generation);
    badge_usb_line(&usb, command);
    expect(strstr(log, "OK forget") != NULL, "forget ok");
    expect(store.count == 0 && store.connected == -1, "forgot current network");
    expect(store.generation == before + 1, "forget bumps generation");
    snprintf(command, sizeof(command), "SAVE %u Cafe a%%20b", store.generation);
    badge_usb_line(&usb, command);
    expect(store.count == 1, "network added back");
    badge_usb_line(&usb, "NOPE");
    expect(strstr(log, "ERR command") != NULL, "unknown command");
    log[0] = '\0';
    /* {"url":"wss://home.example","bootstrapToken":"hk"} */
    snprintf(command, sizeof(command),
             "SAVE %u Cafe  eyJ1cmwiOiJ3c3M6Ly9ob21lLmV4YW1wbGUiLCJib290c3RyYXBUb2tlbiI6ImhrIn0 %%E5%%AE%%B6",
             store.generation);
    badge_usb_line(&usb, command);
    expect(strstr(log, "OK saved") != NULL, "save keeps known password");
    expect(strcmp(store.nets[0].pass, "a b") == 0, "blank password reuses saved one");
    expect(strcmp(badge_agent_active(&store)->name, "家") == 0, "saved agent becomes current");
    expect(strcmp(badge_agent_active(&store)->url, "wss://home.example") == 0, "saved agent url");
    expect(strcmp(badge_agent_active(&store)->bootstrap, "hk") == 0, "saved bootstrap");
    {
        badge_pair_t pair;
        const char *why = badge_pair_parse(
            "eyJ1cmwi\nOiJ3c3M6Ly93b3JrLmV4YW1wbGUiLCJib290c3RyYXBUb2tlbiI6IndrIn0", &pair);
        expect(why == NULL, "wrapped setup code still parses");
        expect(strcmp(pair.url, "wss://work.example") == 0, "wrapped code keeps the url");
    }
    log[0] = '\0';
    snprintf(command, sizeof(command), "AGENT %u nope", store.generation);
    badge_usb_line(&usb, command);
    expect(strstr(log, "ERR code") != NULL, "bad setup code rejected");
    expect(store.agent_count == 2, "bad code changes nothing");
}

static void test_independent_configuration(void) {
    badge_store_t store;
    badge_store_init(&store);
    badge_agent_upsert(&store, "existing", "openclaw", "wss://existing.example",
                       "", "existing-device-token", "existing-access-id", "existing-access-secret");
    badge_agent_t original = store.agents[0];
    char log[1024] = {0}, command[256];
    badge_usb_t usb = { .store=&store, .reply=capture_reply, .reply_ctx=log, .join=fake_join };
    snprintf(command, sizeof(command), "SAVE %u Cafe a%%20b  ", store.generation);
    badge_usb_line(&usb, command);
    expect(strstr(log, "OK saved") != NULL, "Wi-Fi saves without a pairing code");
    expect(store.agent_count == 1 && store.active_agent == 0 &&
           memcmp(&original, &store.agents[0], sizeof(original)) == 0,
           "Wi-Fi-only save preserves agent credentials, address and selection");
    badge_net_t networks[BADGE_NET_MAX];
    memcpy(networks, store.nets, sizeof(networks));
    int connected = store.connected, joins = g_join_calls;
    snprintf(command, sizeof(command),
             "AGENT %u work eyJ1cmwiOiJ3c3M6Ly93b3JrLmV4YW1wbGUiLCJib290c3RyYXBUb2tlbiI6IndrIn0",
             store.generation);
    badge_usb_line(&usb, command);
    expect(strstr(log, "OK agent") != NULL && store.agent_count == 2,
           "OpenClaw saves independently of Wi-Fi");
    expect(store.active_agent == 1, "Independently written agent becomes current");
    expect(g_join_calls == joins && store.count == 1 && store.connected == connected &&
           memcmp(networks, store.nets, sizeof(networks)) == 0,
           "OpenClaw-only save never joins or modifies Wi-Fi");
    badge_store_init(&store);
    snprintf(command, sizeof(command),
             "AGENT %u work eyJ1cmwiOiJ3c3M6Ly93b3JrLmV4YW1wbGUiLCJib290c3RyYXBUb2tlbiI6IndrIn0",
             store.generation);
    badge_usb_line(&usb, command);
    expect(store.agent_count == 1 && store.count == 0 && g_join_calls == joins,
           "OpenClaw can be configured before any Wi-Fi exists");
}

static void test_agent_rename(void) {
    badge_store_t store;
    badge_store_init(&store);
    badge_store_add(&store,"Office","Office","fixture-wifi");
    badge_agent_upsert(&store,"old-cloud","openclaw","wss://cloud.example",
                       "bootstrap","device-credential","access-id","access-secret");
    badge_agent_upsert(&store,"old-sakura","openclaw","wss://sakura.example",
                       "","sakura-credential",NULL,NULL);
    store.active_agent=1;
    char log[1024]={0},command[256];
    badge_usb_t usb={.store=&store,.reply=capture_reply,.reply_ctx=log};
    badge_store_t expected=store;
    badge_usb_line(&usb,"RENAME 1 0 %E6%98%A5%E5%BA%AD%C2%B7Cloudflare");
    strcpy(expected.agents[0].name,"助手·Cloudflare");badge_store_bump(&expected);
    expect(strstr(log,"OK rename")!=NULL && !memcmp(&store,&expected,sizeof(store)),
           "rename changes only one name and generation, preserving every credential, network and selection");
    const char *invalid[]={"RENAME 1 1 stale", "RENAME 2 1 %E6%98%A5%E5%BA%AD%C2%B7Cloudflare",
        "RENAME 2 1 ", "RENAME 2 8 invalid", "RENAME 2 -1 invalid", "RENAME 2 invalid name",
        "RENAME 2 0x name", "RENAME 2  name", "RENAME 2 0 123456789012345678901234"};
    for(size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        log[0]=0;badge_usb_line(&usb,invalid[i]);
        expect(strstr(log,"ERR ")!=NULL && !memcmp(&store,&expected,sizeof(store)),
               "invalid or stale rename preserves all existing configuration");
    }
    snprintf(command,sizeof(command),"RENAME %u 1 %%E6%%98%%A5%%E5%%BA%%AD%%C2%%B7SakuraFrp",store.generation);
    log[0]=0;badge_usb_line(&usb,command);
    strcpy(expected.agents[1].name,"助手·SakuraFrp");badge_store_bump(&expected);
    expect(strstr(log,"OK rename")!=NULL && !memcmp(&store,&expected,sizeof(store)),
           "active assistant can be renamed without changing authentication or active index");
    log[0]=0;badge_usb_line(&usb,"AGENTS");
    expect(strstr(log,"Cloudflare") && strstr(log,"SakuraFrp") && !strstr(log,"credential") && !strstr(log,"access-secret"),
           "readback returns both route names without credentials");
    uint8_t blob[4096];badge_store_t loaded;
    int n=badge_store_save(&store,blob,sizeof(blob));
    expect(n>0 && badge_store_load(&loaded,blob,(size_t)n) &&
           !strcmp(loaded.agents[0].name,"助手·Cloudflare") &&
           !strcmp(loaded.agents[1].name,"助手·SakuraFrp") &&
           !strcmp(loaded.agents[1].device_token,"sakura-credential") && loaded.active_agent==1,
           "both renamed records and their credentials survive storage round-trip");
}

static void test_agent_endpoint_move(void) {
    badge_store_t store,expected,loaded;
    badge_store_init(&store);
    badge_store_add(&store,"Office","Office","fixture-password");
    badge_agent_upsert(&store,"助手·Cloudflare","openclaw","wss://cloud.example",
                       "","cloud-token","cloud-id","cloud-secret");
    badge_agent_upsert(&store,"助手·SakuraFrp","openclaw","wss://old.example:36054",
                       "","sakura-token",NULL,NULL);
    store.active_agent=1;expected=store;
    char log[2048]={0},command[1024];
    badge_usb_t usb={.store=&store,.reply=capture_reply,.reply_ctx=log};
    badge_usb_line(&usb,"ENDPOINT 1 1 wss%3A%2F%2Fold.example%3A36054 wss%3A%2F%2Fnew.example%3A26814");
    strcpy(expected.agents[1].url,"wss://new.example:26814");badge_store_bump(&expected);
    expect(strstr(log,"OK endpoint") && !memcmp(&store,&expected,sizeof(store)),
           "endpoint move preserves all credentials, Wi-Fi, names, selection and other agents");
    const char *invalid[]={
        "ENDPOINT 1 1 wss://new.example:26814 wss://stale.example",
        "ENDPOINT 2 1 wss://wrong.example wss://new.example",
        "ENDPOINT 2 invalid wss://new.example:26814 wss://new.example",
        "ENDPOINT 2 -1 wss://new.example:26814 wss://new.example",
        "ENDPOINT 2 8 wss://new.example:26814 wss://new.example",
        "ENDPOINT 2 1 wss://new.example:26814 ws://new.example",
        "ENDPOINT 2 1 wss://new.example:26814 wss://",
        "ENDPOINT 2 1 wss://new.example:26814 wss://user:pass@new.example",
        "ENDPOINT 2 1 wss://new.example:26814 wss://new.example:0",
        "ENDPOINT 2 1 wss://new.example:26814 wss://new.example:65536",
        "ENDPOINT 2 1 wss://new.example:26814 wss://new.example:99999999999999",
        "ENDPOINT 2 1 wss://new.example:26814 wss://new.example%0D%0AInjected",
        "ENDPOINT 2 1 wss://new.example:26814 wss://new.example?token=secret",
        "ENDPOINT 2 1 wss://new.example:26814 wss://new.example#fragment",
        "ENDPOINT 2 1 wss://new.example:26814 wss://new.example\\path",
        "ENDPOINT 2 1 wss://new.example:26814 "};
    for(size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        log[0]=0;badge_usb_line(&usb,invalid[i]);
        expect(strstr(log,"ERR ") && !memcmp(&store,&expected,sizeof(store)),
               "invalid, stale or downgraded endpoint move leaves settings untouched");
    }
    char large[BADGE_URL_MAX+1];memset(large,'a',sizeof(large)-1);large[sizeof(large)-1]=0;
    snprintf(command,sizeof(command),"ENDPOINT 2 1 wss://new.example:26814 wss://%s",large);
    log[0]=0;badge_usb_line(&usb,command);
    expect(strstr(log,"ERR url") && !memcmp(&store,&expected,sizeof(store)),"oversized endpoint is rejected without truncating");
    uint8_t blob[4096];int n=badge_store_save(&store,blob,sizeof(blob));
    expect(n>0 && badge_store_load(&loaded,blob,(size_t)n) &&
           !strcmp(loaded.agents[1].url,"wss://new.example:26814") &&
           !strcmp(loaded.agents[1].device_token,"sakura-token") && loaded.active_agent==1,
           "replacement URL and existing pairing survive storage round-trip");
    strcpy(store.agents[1].bootstrap,"pending-bootstrap");expected=store;
    log[0]=0;badge_usb_line(&usb,"ENDPOINT 2 1 wss://new.example:26814 wss://next.example");
    expect(strstr(log,"ERR url") && !memcmp(&store,&expected,sizeof(store)),"unconsumed bootstrap cannot be forwarded to a different endpoint");
}

static void test_gateway_route(void) {
    badge_store_t store,loaded;
    badge_store_init(&store);
    badge_store_add(&store,"Cafe","Cafe","a b");
    badge_agent_upsert(&store,"Cloudflare","openclaw","wss://cf.example","","kept-token",NULL,NULL);
    badge_agent_upsert(&store,"SakuraFrp","openclaw","wss://cn.example:26814","","cn-token",NULL,NULL);
    badge_agent_t original=store.agents[1];
    char log[2048]={0},command[256];
    badge_usb_t usb={.store=&store,.reply=capture_reply,.reply_ctx=log,.join=fake_join};
    snprintf(command,sizeof(command),"ROUTE %u 0 ss proxy.example 443 s%%20ecret",store.generation);
    badge_usb_line(&usb,command);
    badge_proxy_t *route=&store.agents[0].proxy;
    expect(strstr(log,"OK route") && route->enabled && !strcmp(route->host,"proxy.example") &&
           route->port==443 && !strcmp(route->secret,"s ecret"),"Shadowsocks route is stored on its gateway");
    expect(!store.agents[1].proxy.enabled && !memcmp(&original,&store.agents[1],sizeof(original)),
           "another gateway stays direct and untouched");
    log[0]=0;badge_usb_line(&usb,"AGENTS");
    expect(strstr(log,"ROUTE 0 ss proxy.example 443 1 aes-256-cfb") && strstr(log,"ROUTE 1 direct - 0 0 aes-256-cfb") &&
           !strstr(log,"ecret"),"AGENTS reports routes without the password");
    snprintf(command,sizeof(command),"ROUTE %u 0 ss proxy.example 443 - chacha20-ietf-poly1305",store.generation);
    log[0]=0;badge_usb_line(&usb,command);
    expect(strstr(log,"ERR proxy") && route->method==BADGE_SS_AES_256_CFB,"unsupported method rejected");
    snprintf(command,sizeof(command),"ROUTE %u 0 ss proxy.example 443 - aes-256-cfb",store.generation);
    log[0]=0;badge_usb_line(&usb,command);
    snprintf(command,sizeof(command),"ROUTE %u 0 ss proxy.example 8388 -",store.generation);
    log[0]=0;badge_usb_line(&usb,command);
    expect(strstr(log,"OK route") && route->port==8388 && !strcmp(route->secret,"s ecret"),"- keeps the saved password");
    snprintf(command,sizeof(command),"ROUTE %u 0 direct",store.generation);
    log[0]=0;badge_usb_line(&usb,command);
    expect(strstr(log,"OK route") && !route->enabled && !strcmp(route->host,"proxy.example") &&
           !strcmp(route->secret,"s ecret"),"direct keeps the server for a later switch back");
    uint32_t generation=store.generation;badge_store_t before=store;
    const char *invalid[]={"ss proxy.example 0 pw","ss proxy.example 65536 pw","ss bad%0Ahost 443 pw",
                           "ss  443 pw","http 192.168.1.10 6152","ss proxy.example 443",
                           "ss proxy.example 443 pw rc4-md5"};
    for (size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        log[0]=0;snprintf(command,sizeof(command),"ROUTE %u 1 %s",store.generation,invalid[i]);
        badge_usb_line(&usb,command);
        expect(strstr(log,"ERR proxy") && store.generation==generation && !memcmp(&before,&store,sizeof(store)),
               "invalid route rejected without changes");
    }
    log[0]=0;snprintf(command,sizeof(command),"ROUTE %u 9 direct",store.generation);badge_usb_line(&usb,command);
    expect(strstr(log,"ERR proxy")!=NULL,"unknown gateway rejected");
    log[0]=0;snprintf(command,sizeof(command),"ROUTE %u 0 direct",store.generation-1);badge_usb_line(&usb,command);
    expect(strstr(log,"ERR stale")!=NULL,"stale route rejected");
    snprintf(command,sizeof(command),"ROUTE %u 0 ss proxy.example 443 -",store.generation);badge_usb_line(&usb,command);
    snprintf(command,sizeof(command),"SAVE %u Cafe a%%20b",store.generation);badge_usb_line(&usb,command);
    expect(store.agents[0].proxy.enabled,"saving Wi-Fi does not touch gateway routes");
    uint8_t blob[4096];int n=badge_store_save(&store,blob,sizeof(blob));
    expect(n>0 && badge_store_load(&loaded,blob,(size_t)n),"routes survive persistence");
    expect(!memcmp(&store.agents[0].proxy,&loaded.agents[0].proxy,sizeof(badge_proxy_t)) &&
           !loaded.agents[1].proxy.enabled,"routes persist exactly");
    store.agents[0].proxy.method=1;
    expect(badge_store_save(&store,blob,sizeof(blob))<0,"unknown method is never stored");
    store.agents[0].proxy.method=BADGE_SS_AES_256_CFB;n=badge_store_save(&store,blob,sizeof(blob));
    const uint8_t *mark=NULL;
    for (int i=0;i+4<n;i++) if (!memcmp(blob+i,"GSS2",4)) mark=blob+i;
    expect(mark!=NULL,"route extension is marked");
    size_t legacy=(size_t)(mark-blob);
    for (size_t cut=legacy+1;cut<(size_t)n;cut++)
        expect(!badge_store_load(&loaded,blob,cut),"truncated route extension rejected");
    expect(badge_store_load(&loaded,blob,legacy) && !loaded.agents[0].proxy.enabled &&
           !strcmp(loaded.agents[0].device_token,"kept-token"),"records without routes load as direct");
    /* GSS1 (no method byte) loads as aes-256-cfb. */
    {
        uint8_t v1[4096];memcpy(v1,blob,legacy);size_t at=legacy;
        memcpy(v1+at,"GSS1",4);at+=4;v1[at++]=2;
        v1[at++]=1;v1[at++]=0xbb;v1[at++]=0x01;memcpy(v1+at,"proxy.example",14);at+=14;memcpy(v1+at,"pw",3);at+=3;
        v1[at++]=0;v1[at++]=0;v1[at++]=0;v1[at++]=0;v1[at++]=0;
        expect(badge_store_load(&loaded,v1,at) && loaded.agents[0].proxy.enabled && loaded.agents[0].proxy.port==443 &&
               loaded.agents[0].proxy.method==BADGE_SS_AES_256_CFB && !loaded.agents[1].proxy.enabled,
               "GSS1 routes load as aes-256-cfb");
    }
    /* Old firmware kept per-Wi-Fi proxies in PXY2. They load, but drop. */
    uint8_t old_blob[4096];memcpy(old_blob,blob,legacy);
    /* PXY2, one network: kind=http, on, port 6152, host, empty method and secret. */
    const uint8_t pxy[]={'P','X','Y','2',1, 0,1,0x08,0x18,'1','9','2','.','1','6','8','.','1','.','2','8',0,0,0};
    memcpy(old_blob+legacy,pxy,sizeof(pxy));
    expect(badge_store_load(&loaded,old_blob,legacy+sizeof(pxy)) && !loaded.agents[0].proxy.enabled &&
           !strcmp(loaded.nets[0].pass,"a b") && !strcmp(loaded.agents[1].device_token,"cn-token"),
           "old Wi-Fi proxy records migrate to direct gateways");
    badge_agent_drop(&store,0);
    expect(!store.agents[0].proxy.enabled && !strcmp(store.agents[0].name,"SakuraFrp"),"route moves with its gateway");
}

static void test_encoded_code_limit(void) {
    badge_store_t store;
    badge_store_init(&store);
    char log[1024] = {0}, line[5200];
    badge_usb_t usb = { .store=&store,.reply=capture_reply,.reply_ctx=log };
    /* More than 1400 encoded bytes is still a valid decoded field size. */
    strcpy(line,"AGENT 1 test ");
    for (int i=0;i<450;i++) strcat(line,"%41");
    badge_usb_line(&usb,line);
    expect(store.generation==1 && store.agent_count==0,"invalid decoded code preserves state");
    expect(strstr(log,"ERR")!=NULL,"invalid decoded code rejected");
    memset(log,0,sizeof(log)); strcpy(line,"SAVE 1 test pass ");
    for (int i=0;i<1450;i++) strcat(line,"%41");
    badge_usb_line(&usb,line);
    expect(strstr(log,"ERR code")!=NULL,"oversized decoded code rejected without a large stack buffer");
    expect(store.generation==1 && store.count==0,"oversized code leaves Wi-Fi untouched");
}

static int wide_latin(uint32_t cp, uint32_t next) {
    (void)next;
    return cp == 'W' ? 16 : cp < 128 ? 8 : 16;
}

static void test_continue_conversation(void) {
    badge_store_t store;
    badge_view_t view;
    badge_store_init(&store);
    badge_agent_upsert(&store,"助手","openclaw","wss://main.example","","device-credential",NULL,NULL);
    badge_store_t saved = store;
    badge_view_init(&view);
    badge_on_key(&view,&store,BADGE_BTN_OK,BADGE_CLICK);
    for (int turn=0;turn<4;turn++) {
        expect(view.recording && !view.sending && !view.retry_send,"each turn records fresh audio");
        expect(badge_on_key(&view,&store,BADGE_BTN_OK,BADGE_CLICK)==BADGE_ACT_SEND,
               "second OK sends the new recording, never a previous retry");
        badge_finish_send(&view,true,false,"这是当前回复。\n1\n2\n3\n4\n5\n6\n7\n8\n下一页");
        expect(view.screen==BADGE_SCREEN_TALK && !view.sending,"completed turn opens reply");
        if (turn%2) badge_on_key(&view,&store,BADGE_BTN_DOWN,BADGE_CLICK);
        expect(badge_key_binding(&view,&store,BADGE_BTN_OK).action==BADGE_ACT_CONTINUE,
               "continue is available on every reply page");
        expect(!strcmp(badge_action_label(BADGE_ACT_CONTINUE,true),"继续"),"continue has a visible Chinese caption");
        memset(view.waveform,99,sizeof(view.waveform));
        expect(badge_on_key(&view,&store,BADGE_BTN_OK,BADGE_CLICK)==BADGE_ACT_CONTINUE,"one OK starts the next turn");
        for (size_t i=0;i<sizeof(view.waveform);i++) expect(view.waveform[i]==0,"new capture clears stale waveform");
        expect(!view.reply_unread && !view.cancel_pending,"new turn has no stale reply or cancellation");
        badge_finish_send(&view,true,false,"late previous reply");
        expect(view.screen==BADGE_SCREEN_CAPTURE && view.recording,"late completion cannot interrupt next capture");
        expect(!memcmp(&store,&saved,sizeof(store)),"continuing preserves selected gateway and device identity configuration");
    }
    badge_on_key(&view,&store,BADGE_BTN_OK,BADGE_CLICK);
    badge_finish_send(&view,false,true,"网络中断");
    expect(badge_on_key(&view,&store,BADGE_BTN_UP,BADGE_CLICK)==BADGE_ACT_RETRY,"a follow-up failure can retry its own audio");
    badge_finish_send(&view,true,false,"恢复后的回复");
    expect(badge_on_key(&view,&store,BADGE_BTN_UP,BADGE_CLICK)==BADGE_ACT_BACK,"reply can return home without recording");
    expect(!view.recording && !view.sending && !view.retry_send,"home exit does not upload or restart audio");
    badge_set_reply(&view,"继续测试");
    badge_on_key(&view,&store,BADGE_BTN_OK,BADGE_CLICK);
    badge_on_key(&view,&store,BADGE_BTN_UP,BADGE_CLICK);
    expect(view.screen==BADGE_SCREEN_HOME && !view.recording && !view.sending,"follow-up recording can be cancelled");
}

static void test_pages(void) {
    badge_view_t view;
    badge_store_t store;
    char page[BADGE_PAGE_TEXT_MAX], text[BADGE_TEXT_MAX] = "";
    badge_store_init(&store);
    badge_view_init(&view);
    for (int i = 0; i < 110; i++) strcat(text, "春");
    badge_set_reply(&view, text);
    expect(view.screen == BADGE_SCREEN_TALK, "reply opens reader");
    expect(badge_reply_pages(&view) == 2, "reader fills nine lines before paginating");
    badge_reply_page_text(&view, page, sizeof(page));
    int chinese = 0, breaks = 0;
    for (size_t i = 0; i < strlen(page); i++) {
        if (page[i] == '\n') breaks++;
        if ((unsigned char)page[i] == 0xe6) chinese++;
    }
    expect(chinese == 99 && breaks == 8, "first page contains ninety-nine complete Han glyphs");
    expect(badge_key_binding(&view, &store, BADGE_BTN_UP).action == BADGE_ACT_BACK &&
           badge_key_binding(&view, &store, BADGE_BTN_DOWN).action == BADGE_ACT_NEXT_PAGE,
           "first of two pages: up is Back, down is Next page");
    expect(!strcmp(badge_action_label(BADGE_ACT_BACK, true), "返回"), "Back has its Chinese caption");
    expect(badge_on_key(&view, &store, BADGE_BTN_UP, BADGE_CLICK) == BADGE_ACT_BACK,
           "up leaves the first reply page");
    expect(view.screen == BADGE_SCREEN_HOME, "first reply page has an exit action");
    badge_set_reply(&view, text);
    badge_on_key(&view, &store, BADGE_BTN_DOWN, BADGE_CLICK);
    expect(view.reply_page == 1, "down moves to page two");
    badge_reply_page_text(&view, page, sizeof(page));
    expect(strlen(page) == 33, "last page contains the remaining eleven glyphs");
    expect(badge_key_binding(&view, &store, BADGE_BTN_UP).action == BADGE_ACT_PREV_PAGE &&
           badge_key_binding(&view, &store, BADGE_BTN_DOWN).action == BADGE_ACT_BACK,
           "last page: up is Previous page, down is Back");
    badge_on_key(&view, &store, BADGE_BTN_UP, BADGE_CLICK);
    expect(view.reply_page == 0, "up returns to the first page");
    badge_on_key(&view, &store, BADGE_BTN_DOWN, BADGE_CLICK);
    expect(badge_on_key(&view, &store, BADGE_BTN_DOWN, BADGE_CLICK) == BADGE_ACT_BACK &&
           view.screen == BADGE_SCREEN_HOME && !view.recording && !view.sending,
           "down on the last page returns home without recording");
    badge_set_reply(&view, "一页");
    expect(badge_key_binding(&view, &store, BADGE_BTN_UP).action == BADGE_ACT_BACK &&
           badge_key_binding(&view, &store, BADGE_BTN_DOWN).action == BADGE_ACT_BACK &&
           badge_key_binding(&view, &store, BADGE_BTN_DOWN).enabled,
           "a single-page reply shows Back on both paging keys");
    badge_set_reply(&view, text);
    expect(badge_on_key(&view, &store, BADGE_BTN_OK, BADGE_CLICK) == BADGE_ACT_CONTINUE,
           "OK continues the same conversation");
    expect(view.screen == BADGE_SCREEN_CAPTURE && view.recording,
           "OK starts the next voice turn");
    badge_on_key(&view, &store, BADGE_BTN_UP, BADGE_CLICK);

    badge_set_reply(&view,text);
    view.sending=true;
    expect(badge_key_binding(&view,&store,BADGE_BTN_DOWN).enabled &&
           badge_key_binding(&view,&store,BADGE_BTN_OK).action==BADGE_ACT_CANCEL_SEND,
           "live reader permits paging and cancellation, not a concurrent recording");
    badge_on_key(&view,&store,BADGE_BTN_DOWN,BADGE_CLICK);
    expect(view.reply_page==1,"live reply can be paged before completion");
    badge_finish_send(&view,true,false,text);
    expect(view.reply_page==1 && view.screen==BADGE_SCREEN_TALK && !view.sending &&
           badge_key_binding(&view,&store,BADGE_BTN_OK).action==BADGE_ACT_CONTINUE,
           "final reply keeps reading position and enables the next turn");
    view.sending=true;
    expect(badge_on_key(&view,&store,BADGE_BTN_DOWN,BADGE_CLICK)==BADGE_ACT_BACK && view.cancel_pending &&
           view.screen==BADGE_SCREEN_CANCELLING,"last-page Back cancels a live run rather than leaving an orphan");
    badge_finish_cancel(&view,"cancelled");
    badge_set_reply(&view,text);view.sending=true;
    badge_finish_send(&view,false,true,"interrupted");
    expect(view.screen==BADGE_SCREEN_FAILURE && view.retry_send,"live failure offers recovery instead of a stuck reader");
    badge_finish_cancel(&view,"cancelled");

    badge_set_reply(&view, "1\n2\n3\n4\n5\n6\n7\n8\n9\n10");
    expect(badge_reply_pages(&view) == 2, "explicit newlines consume display rows");
    view.reply_page = 1;
    badge_reply_page_text(&view, page, sizeof(page));
    expect(!strcmp(page, "10"), "paragraph page boundary resumes without losing content");
    badge_set_reply(&view, "a\r\nb\n\nc");
    badge_reply_page_text(&view, page, sizeof(page));
    expect(!strcmp(page, "a\nb\n\nc"), "CRLF and blank paragraphs remain readable");
    badge_set_reply(&view, "hello world readable words wrap without splitting them");
    badge_reply_page_text(&view, page, sizeof(page));
    expect(strstr(page, "hello world readable\nwords") != NULL, "English wraps at word boundaries");
    badge_reply_set_glyph_width(wide_latin);
    memset(text, 'W', 110); text[110] = 0;
    badge_set_reply(&view, text);
    expect(badge_reply_pages(&view) == 2, "wide Latin glyphs use actual font advances");
    badge_reply_set_glyph_width(NULL);
    expect(badge_reply_pages(&view) == 1, "narrow glyph advances allow more content");
    badge_set_reply(&view, "一二三四五六七八九十甲，继续");
    badge_reply_page_text(&view, page, sizeof(page));
    expect(strstr(page, "十\n甲，继续") != NULL, "closing punctuation cannot start a line");
    badge_set_reply(&view, "一二三四五六七八九十（继续");
    badge_reply_page_text(&view, page, sizeof(page));
    expect(strstr(page, "十\n（继续") != NULL, "opening punctuation moves with its following text");
    static const char *const punctuation_pairs[][2] = {
        {"“","”"}, {"‘","’"}, {"「","」"}, {"『","』"}, {"〝","〞"}, {"〝","〟"},
        {"（","）"}, {"［","］"}, {"｛","｝"}, {"｟","｠"}, {"〔","〕"}, {"【","】"},
        {"〖","〗"}, {"〘","〙"}, {"〚","〛"}, {"《","》"}, {"〈","〉"},
        {"﹙","﹚"}, {"﹛","﹜"}, {"﹝","﹞"}
    };
    for (size_t index = 0; index < sizeof(punctuation_pairs)/sizeof(punctuation_pairs[0]); index++) {
        char reply[128], expected[32];
        snprintf(reply,sizeof(reply),"一二三四五六七八九十%s继续",punctuation_pairs[index][0]);
        badge_set_reply(&view,reply);badge_reply_page_text(&view,page,sizeof(page));
        snprintf(expected,sizeof(expected),"十\n%s继续",punctuation_pairs[index][0]);
        expect(strstr(page,expected) != NULL,"opening quotation marks and brackets stay with their text");
        snprintf(reply,sizeof(reply),"一二三四五六七八九十甲%s继续",punctuation_pairs[index][1]);
        badge_set_reply(&view,reply);badge_reply_page_text(&view,page,sizeof(page));
        snprintf(expected,sizeof(expected),"十\n甲%s继续",punctuation_pairs[index][1]);
        expect(strstr(page,expected) != NULL,"closing quotation marks and brackets do not start a line");
    }
    char oversized[481] = "";
    for (int i = 0; i < 160; i++) strcat(oversized, "春");
    badge_set_reply(&view, oversized);
    expect(strlen(view.reply) == 477, "stored reply truncation preserves complete UTF-8 glyphs");
    char small[5];
    badge_set_reply(&view, "中文内容");
    badge_reply_page_text(&view, small, sizeof(small));
    expect(!strcmp(small, "中"), "small output buffers preserve UTF-8 boundaries");
    badge_on_key(&view, &store, BADGE_BTN_OK, BADGE_HOLD);
    expect(view.screen == BADGE_SCREEN_TALK, "hold OK has no alternate action");
    expect(badge_on_key(&view, &store, BADGE_BTN_OK, BADGE_CLICK) == BADGE_ACT_CONTINUE,
           "OK starts another turn after a reply");
    expect(view.screen == BADGE_SCREEN_CAPTURE && view.recording,
           "reply reader transitions directly to recording");
}

static void test_frame(void) {
    char buf[512];
    badge_turn_t turn;
    memset(&turn, 0, sizeof(turn));
    turn.has_audio = true;
    snprintf(turn.audio_format, sizeof(turn.audio_format), "audio/ogg");
    int n = badge_openclaw_chat_send(buf, sizeof(buf), &turn);
    expect(n > 0 && (size_t)n < sizeof(buf), "frame fits");
    expect(strstr(buf, "\"method\":\"chat.send\"") != NULL, "method");
    expect(strstr(buf, "agent:main:passport") != NULL, "session");
    expect(strstr(buf, "audio/ogg") != NULL, "audio attachment");
    expect(strstr(buf, "\"message\":\"\"") != NULL, "empty message with audio");

    memset(&turn, 0, sizeof(turn));
    snprintf(turn.transcript, sizeof(turn.transcript), "hello");
    badge_openclaw_chat_send(buf, sizeof(buf), &turn);
    expect(strstr(buf, "\"message\":\"hello\"") != NULL, "text fallback");
    expect(strstr(buf, "attachments") == NULL, "text has no attachment");
}

int main(void) {
    test_store_order();
    test_navigation();
    test_navigation_sequences();
    test_live_navigation();
    test_persist_and_portal();
    test_usb();
    test_independent_configuration();
    test_agent_rename();
    test_agent_endpoint_move();
    test_gateway_route();
    test_encoded_code_limit();
    test_continue_conversation();
    test_pages();
    test_frame();
    if (g_failed != 0) {
        fprintf(stderr, "%d assertion(s) failed\n", g_failed);
        return 1;
    }
    printf("badge tests passed\n");
    return 0;
}
