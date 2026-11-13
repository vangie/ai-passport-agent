#include "passport.h"
#include "passport_hotspot.h"
#include "passport_pair.h"

#include <stdio.h>
#include <string.h>

static int g_failed;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_failed++;
    }
}

static void test_hotspot_detection(void) {
    uint8_t element[] = {0xdd, 0x0a, 0x00, 0x17, 0xf2, 0x06, 0x01, 0x01, 0x03, 0x03, 0x00, 0x00};
    expect(passport_hotspot_vendor_ie(element, sizeof(element)), "recognize hotspot signature");
    element[9] = 0x01;
    expect(passport_hotspot_vendor_ie(element, sizeof(element)), "accept observed hotspot feature variant");
    for (size_t size = 0; size < sizeof(element); size++) {
        expect(!passport_hotspot_vendor_ie(element, size), "reject truncated hotspot element");
    }
    expect(!passport_hotspot_vendor_ie(NULL, sizeof(element)), "reject null hotspot element");
    for (size_t index = 0; index < 9; index++) {
        element[index] ^= 0x01;
        expect(!passport_hotspot_vendor_ie(element, sizeof(element)), "reject unrelated vendor element");
        element[index] ^= 0x01;
    }
}

static void test_store_order(void) {
    passport_store_t store;
    bool visible[4] = {true, true, false, false};
    int rssi[4] = {-70, -40, 0, 0};
    passport_store_init(&store);
    expect(passport_store_add(&store, "家里", "home", "a"), "add home");
    expect(passport_store_add(&store, "手机热点", "phone", "b"), "add phone");
    expect(passport_store_add(&store, "家里", "home", "changed"), "update home");
    expect(store.count == 2, "dedupe ssid");
    {
        passport_store_t full;
        char ssid[8];
        passport_store_init(&full);
        for (int i = 0; i < PASSPORT_NET_MAX; i++) {
            snprintf(ssid, sizeof(ssid), "n%d", i);
            passport_store_add(&full, ssid, ssid, "p");
        }
        expect(!passport_store_add(&full, "extra", "extra", "p"), "full list refuses new");
        expect(passport_store_add(&full, "n3", "n3", "q"), "full list still updates");
        expect(strcmp(full.nets[3].pass, "q") == 0, "full list password changed");
    }
    expect(strcmp(store.nets[0].pass, "changed") == 0, "password replaced");
    expect(passport_store_pick(&store, visible, rssi, 2) == 0, "priority beats rssi");
    expect(passport_store_promote(&store, 1), "promote phone");
    expect(strcmp(store.nets[0].ssid, "phone") == 0, "phone is first");
    expect(passport_store_pick(&store, visible, rssi, 2) == 0, "promoted network wins");
    visible[0] = false;
    expect(passport_store_pick(&store, visible, rssi, 2) == 1, "skip hidden network");
    expect(passport_store_forget(&store, 0), "forget phone");
    expect(store.count == 1, "one network left");
    expect(strcmp(store.nets[0].ssid, "home") == 0, "home remains");
    expect(!passport_store_add(&store, "", "", ""), "reject empty ssid");
}

static void test_navigation(void) {
    passport_store_t store;
    passport_view_t v;
    passport_store_init(&store);
    passport_store_add(&store, "家里", "home", "a");
    passport_store_add(&store, "热点", "phone", "b");
    passport_agent_upsert(&store, "助手", "openclaw", "wss://main.example", "", NULL, NULL, NULL);
    passport_agent_upsert(&store, "办公室", "openclaw", "wss://work.example", "", NULL, NULL, NULL);
    passport_view_init(&v);
    v.retry_send=true;
    expect(passport_key_binding(&v,&store,PASSPORT_BTN_OK).action==PASSPORT_ACT_RETRY,"home keeps the pending clip reachable");
    expect(!passport_key_binding(&v,&store,PASSPORT_BTN_DOWN).enabled,"Live is unavailable while a clip waits");
    passport_view_init(&v);
    expect(passport_key_binding(&v,&store,PASSPORT_BTN_UP).action == PASSPORT_ACT_PREFERENCES, "home up opens settings");
    passport_on_key(&v,&store,PASSPORT_BTN_UP,PASSPORT_CLICK);
    passport_on_key(&v,&store,PASSPORT_BTN_DOWN,PASSPORT_CLICK);
    passport_on_key(&v,&store,PASSPORT_BTN_UP,PASSPORT_CLICK);
    passport_on_key(&v,&store,PASSPORT_BTN_UP,PASSPORT_CLICK);
    expect(v.cursor == 2, "cycle reaches add row");
    expect(passport_on_key(&v,&store,PASSPORT_BTN_DOWN,PASSPORT_CLICK) == PASSPORT_ACT_ADD_NETWORK, "add emits radio configuration action");
    expect(v.screen == PASSPORT_SCREEN_ADD, "add opens setup");
    passport_on_key(&v,&store,PASSPORT_BTN_OK,PASSPORT_CLICK);
    expect(v.screen == PASSPORT_SCREEN_NETS, "setup returns to its parent");
    v.cursor = 1;
    passport_on_key(&v,&store,PASSPORT_BTN_DOWN,PASSPORT_CLICK);
    expect(v.screen == PASSPORT_SCREEN_NET_MENU, "open displays network detail");
    expect(passport_on_key(&v,&store,PASSPORT_BTN_UP,PASSPORT_CLICK) == PASSPORT_ACT_CONNECT_NETWORK, "network detail requests a real connection");
    expect(!strcmp(store.nets[0].ssid,"home"), "UI does not mutate configuration before persistence");
    expect(passport_key_binding(&v,&store,PASSPORT_BTN_DOWN).action == PASSPORT_ACT_CONFIGURE,
           "network detail offers configuration, not a per-Wi-Fi proxy");
    passport_on_key(&v,&store,PASSPORT_BTN_OK,PASSPORT_CLICK);
    expect(v.screen == PASSPORT_SCREEN_NETS, "detail returns to network selection");
    passport_on_key(&v,&store,PASSPORT_BTN_OK,PASSPORT_CLICK);
    expect(v.screen == PASSPORT_SCREEN_PREFERENCES, "network returns to settings");

    passport_on_key(&v,&store,PASSPORT_BTN_UP,PASSPORT_CLICK);
    passport_on_key(&v,&store,PASSPORT_BTN_DOWN,PASSPORT_CLICK);
    expect(v.screen == PASSPORT_SCREEN_SETTINGS, "down opens assistants");
    expect(passport_key_binding(&v,&store,PASSPORT_BTN_DOWN).action == PASSPORT_ACT_CONFIGURE, "active assistant exposes setup");
    passport_on_key(&v,&store,PASSPORT_BTN_UP,PASSPORT_CLICK);
    expect(passport_on_key(&v,&store,PASSPORT_BTN_DOWN,PASSPORT_CLICK) == PASSPORT_ACT_SELECT_AGENT, "candidate emits selection action");
    expect(store.active_agent == 0, "owner must persist agent selection");
    passport_on_key(&v,&store,PASSPORT_BTN_OK,PASSPORT_CLICK);
    expect(v.screen == PASSPORT_SCREEN_PREFERENCES, "assistant returns to settings");
    passport_on_key(&v,&store,PASSPORT_BTN_UP,PASSPORT_CLICK);
    expect(passport_on_key(&v,&store,PASSPORT_BTN_DOWN,PASSPORT_CLICK) == PASSPORT_ACT_ENABLE_CONFIG,
           "settings enables Bluetooth without adding a network");
    expect(v.screen == PASSPORT_SCREEN_PREFERENCES, "Bluetooth toggles without leaving settings");
    passport_on_key(&v,&store,PASSPORT_BTN_UP,PASSPORT_CLICK);
    passport_on_key(&v,&store,PASSPORT_BTN_DOWN,PASSPORT_CLICK);
    expect(v.screen == PASSPORT_SCREEN_VOLUME, "volume remains reachable after Bluetooth");
    passport_on_key(&v,&store,PASSPORT_BTN_OK,PASSPORT_CLICK);
    passport_on_key(&v,&store,PASSPORT_BTN_UP,PASSPORT_CLICK);
    expect(v.preference_cursor == 0, "settings cycles through all four options");
    passport_on_key(&v,&store,PASSPORT_BTN_OK,PASSPORT_CLICK);

    passport_on_key(&v,&store,PASSPORT_BTN_OK,PASSPORT_CLICK);
    expect(v.recording && v.screen == PASSPORT_SCREEN_CAPTURE, "recording has a dedicated screen");
    expect(passport_key_binding(&v,&store,PASSPORT_BTN_UP).action == PASSPORT_ACT_CANCEL_RECORD, "capture has explicit cancel");
    expect(passport_on_key(&v,&store,PASSPORT_BTN_DOWN,PASSPORT_CLICK) == PASSPORT_ACT_RERECORD && v.recording, "capture can restart");
    passport_on_key(&v,&store,PASSPORT_BTN_UP,PASSPORT_CLICK);
    expect(!v.recording && !v.sending && v.screen == PASSPORT_SCREEN_HOME, "cancel does not send");
    passport_on_key(&v,&store,PASSPORT_BTN_OK,PASSPORT_CLICK);
    passport_on_key(&v,&store,PASSPORT_BTN_OK,PASSPORT_CLICK);
    expect(!v.recording && v.sending && v.screen == PASSPORT_SCREEN_PROGRESS, "second OK sends once");
    expect(passport_key_binding(&v,&store,PASSPORT_BTN_UP).action==PASSPORT_ACT_CANCEL_SEND,"sending exposes cancel rather than settings");
    expect(passport_key_binding(&v,&store,PASSPORT_BTN_DOWN).action==PASSPORT_ACT_RESTART_SEND,"sending exposes a restart action");
    passport_on_key(&v,&store,PASSPORT_BTN_OK,PASSPORT_CLICK);
    expect(v.cancel_pending && v.screen==PASSPORT_SCREEN_CANCELLING && v.sending,"back ends the operation and waits for safe cleanup");
    passport_finish_send(&v,true,false,"late reply");
    expect(v.cancel_pending && !v.reply_unread,"late reply cannot override pending cancellation");
    expect(!passport_finish_cancel(&v,"已取消"),"ordinary cancel does not restart");
    expect(v.screen==PASSPORT_SCREEN_HOME && !v.sending && !v.retry_send && !v.reply_unread,"cancel returns to a genuinely idle home");
    expect(passport_key_binding(&v,&store,PASSPORT_BTN_OK).action==PASSPORT_ACT_RECORD,"a new recording is immediately reachable");
    passport_finish_send(&v,true,false,"old result");
    expect(v.screen==PASSPORT_SCREEN_HOME && !v.reply_unread,"abandoned task cannot pop up later");
    v.sending=true;v.screen=PASSPORT_SCREEN_PROGRESS;
    passport_on_key(&v,&store,PASSPORT_BTN_DOWN,PASSPORT_CLICK);
    expect(v.cancel_pending && v.restart_after_cancel && !v.recording,"re-record waits for network resources to be released");
    expect(passport_finish_cancel(&v,"已取消"),"re-record requests capture after cleanup");
    passport_on_key(&v,&store,PASSPORT_BTN_OK,PASSPORT_CLICK);
    expect(v.recording && v.screen==PASSPORT_SCREEN_CAPTURE,"new capture starts after cleanup");
    passport_on_key(&v,&store,PASSPORT_BTN_UP,PASSPORT_CLICK);

    v.sending=true;v.screen=PASSPORT_SCREEN_PROGRESS;
    passport_finish_send(&v,false,true,"发送失败");
    expect(v.retry_send && v.screen==PASSPORT_SCREEN_FAILURE,"failed send has its own retry page");
    expect(passport_on_key(&v,&store,PASSPORT_BTN_UP,PASSPORT_CLICK)==PASSPORT_ACT_RETRY && v.sending,"failure can retry");
    passport_finish_send(&v,false,true,"发送失败");
    expect(passport_key_binding(&v,&store,PASSPORT_BTN_OK).action==PASSPORT_ACT_DISCARD,"failure offers cancel instead of trapping a retained clip");
    passport_on_key(&v,&store,PASSPORT_BTN_OK,PASSPORT_CLICK);
    expect(v.screen==PASSPORT_SCREEN_HOME && !v.retry_send,"cancel discards the pending retry state");
    expect(passport_key_binding(&v,&store,PASSPORT_BTN_OK).action==PASSPORT_ACT_RECORD,"home talks again after a failure is cancelled");
    passport_set_reply(&v,"之前的回复");v.reply_unread=true;
    passport_offer_retry(&v,"录音已恢复，可重试");
    expect(v.screen==PASSPORT_SCREEN_FAILURE && v.retry_send && !v.reply_unread,"restored status is not a chat reply");
    passport_on_key(&v,&store,PASSPORT_BTN_OK,PASSPORT_CLICK);
    expect(!v.retry_send && !v.reply_unread && v.screen==PASSPORT_SCREEN_HOME,"restored recordings can be abandoned with one click");
    for (int screen = PASSPORT_SCREEN_HOME; screen <= PASSPORT_SCREEN_CANCELLING; screen++) {
        passport_view_init(&v);v.screen = (passport_screen_t)screen;
        passport_screen_t original = v.screen;
        for (int key = PASSPORT_BTN_UP; key <= PASSPORT_BTN_OK; key++) {
            expect(passport_on_key(&v,&store,(passport_btn_t)key,PASSPORT_HOLD) == PASSPORT_ACT_NONE, "hold never selects a hidden action");
            expect(passport_on_key(&v,&store,(passport_btn_t)key,PASSPORT_RELEASE) == PASSPORT_ACT_NONE, "raw release never duplicates click");
            expect(v.screen == original, "non-click events preserve page");
        }
    }
    for (int screen = PASSPORT_SCREEN_NETS; screen <= PASSPORT_SCREEN_SETTINGS; screen++) {
        passport_view_init(&v);v.screen = (passport_screen_t)screen;v.setup_parent=PASSPORT_SCREEN_NET_MENU;
        for (int steps=0;steps<4 && v.screen!=PASSPORT_SCREEN_HOME;steps++)
            passport_on_key(&v,&store,v.screen==PASSPORT_SCREEN_TALK ? PASSPORT_BTN_UP : PASSPORT_BTN_OK,PASSPORT_CLICK);
        expect(v.screen==PASSPORT_SCREEN_HOME,"every subpage has a bounded single-click path home");
    }
    passport_store_init(&store);passport_view_init(&v);
    passport_on_key(&v,&store,PASSPORT_BTN_UP,PASSPORT_CLICK);
    passport_on_key(&v,&store,PASSPORT_BTN_UP,PASSPORT_CLICK);
    passport_on_key(&v,&store,PASSPORT_BTN_DOWN,PASSPORT_CLICK);
    expect(!passport_key_binding(&v,&store,PASSPORT_BTN_UP).enabled,"empty assistant list disables cycling");
    expect(passport_on_key(&v,&store,PASSPORT_BTN_DOWN,PASSPORT_CLICK)==PASSPORT_ACT_CONFIGURE,"empty assistant page can open setup");
    v.bluetooth_ready=true;
    expect(passport_on_key(&v,&store,PASSPORT_BTN_DOWN,PASSPORT_CLICK)==PASSPORT_ACT_CLOSE_CONFIG,"setup has a visible close action");
    passport_on_key(&v,&store,PASSPORT_BTN_OK,PASSPORT_CLICK);
    passport_on_key(&v,&store,PASSPORT_BTN_OK,PASSPORT_CLICK);
    passport_on_key(&v,&store,PASSPORT_BTN_OK,PASSPORT_CLICK);
    expect(v.screen==PASSPORT_SCREEN_HOME,"empty configuration can always return home");
}

static void test_network_navigation(void) {
    passport_store_t store;
    passport_view_t view;
    passport_store_init(&store);
    passport_store_add(&store, "Home", "home", "home-password");
    passport_store_add(&store, "Phone", "phone", "phone-password");
    passport_view_init(&view);
    expect(view.network_target == -1 && view.network_state == PASSPORT_NETWORK_IDLE, "no stale network feedback at boot");
    view.screen = PASSPORT_SCREEN_NET_MENU;
    view.cursor = 1;
    expect(passport_key_binding(&view, &store, PASSPORT_BTN_UP).enabled, "saved network can connect");
    store.nets[1].enabled = false;
    expect(!passport_key_binding(&view, &store, PASSPORT_BTN_UP).enabled, "disabled network cannot connect");
    store.nets[1].enabled = true;
    view.cursor = -1;
    expect(!passport_key_binding(&view, &store, PASSPORT_BTN_UP).enabled, "invalid network cannot connect");
    view.cursor = 1;
    view.network_target = 1;
    view.network_state = PASSPORT_NETWORK_CONNECTING;
    expect(passport_on_key(&view, &store, PASSPORT_BTN_UP, PASSPORT_CLICK) == PASSPORT_ACT_NONE, "duplicate join ignored");
    expect(passport_on_key(&view, &store, PASSPORT_BTN_DOWN, PASSPORT_CLICK) == PASSPORT_ACT_NONE, "cannot configure during join");
    expect(passport_on_key(&view, &store, PASSPORT_BTN_OK, PASSPORT_CLICK) == PASSPORT_ACT_BACK, "can leave pending join");
    expect(view.screen == PASSPORT_SCREEN_NETS && view.network_state == PASSPORT_NETWORK_CONNECTING, "back does not cancel join");
    passport_on_key(&view, &store, PASSPORT_BTN_UP, PASSPORT_CLICK);
    expect(view.cursor == 2, "can browse other networks during join");
    expect(!passport_key_binding(&view, &store, PASSPORT_BTN_DOWN).enabled, "adding Wi-Fi is disabled during join");
    passport_on_key(&view, &store, PASSPORT_BTN_OK, PASSPORT_CLICK);
    expect(view.screen == PASSPORT_SCREEN_PREFERENCES, "list can return to settings during join");
    passport_on_key(&view, &store, PASSPORT_BTN_OK, PASSPORT_CLICK);
    expect(view.screen == PASSPORT_SCREEN_HOME, "settings can return home during join");
    expect(!passport_key_binding(&view, &store, PASSPORT_BTN_OK).enabled, "recording blocked during join");
    expect(!passport_key_binding(&view, &store, PASSPORT_BTN_DOWN).enabled, "live blocked during join");
    expect(passport_key_binding(&view, &store, PASSPORT_BTN_UP).enabled, "settings remain reachable during join");
    passport_on_key(&view, &store, PASSPORT_BTN_UP, PASSPORT_CLICK);
    view.preference_cursor = 0;
    passport_on_key(&view, &store, PASSPORT_BTN_DOWN, PASSPORT_CLICK);
    expect(view.screen == PASSPORT_SCREEN_NETS, "network results remain reachable from home during join");
    view.screen = PASSPORT_SCREEN_VOLUME;
    expect(!passport_key_binding(&view, &store, PASSPORT_BTN_UP).enabled, "volume writes blocked during join");
    passport_on_key(&view, &store, PASSPORT_BTN_OK, PASSPORT_CLICK);
    expect(view.screen == PASSPORT_SCREEN_PREFERENCES, "volume always has an exit during join");
    view.screen = PASSPORT_SCREEN_HOME;
    view.network_state = PASSPORT_NETWORK_TIMEOUT;
    expect(passport_key_binding(&view, &store, PASSPORT_BTN_OK).enabled, "recording available after join ends");
    view.sending = true;
    expect(passport_key_binding(&view, &store, PASSPORT_BTN_OK).enabled, "upload progress remains reachable");
    expect(passport_on_key(&view, &store, PASSPORT_BTN_OK, PASSPORT_CLICK) == PASSPORT_ACT_PROGRESS, "home opens ongoing upload");
}

static void test_navigation_sequences(void) {
    passport_view_t view;
    passport_store_t store;
    passport_store_init(&store);
    passport_store_add(&store,"Home","home","p");
    passport_store_add(&store,"Hotspot","hotspot","p");
    passport_agent_upsert(&store,"Main","openclaw","wss://main.example","",NULL,NULL,NULL);
    passport_agent_upsert(&store,"Office","openclaw","wss://office.example","",NULL,NULL,NULL);
    passport_view_init(&view);
    unsigned random = 12345;
    for (int i=0;i<2000;i++) {
        random = random * 1664525u + 1013904223u;
        passport_btn_t button = (passport_btn_t)((random >> 8) % 3);
        passport_key_binding_t binding = passport_key_binding(&view,&store,button);
        expect(binding.action==PASSPORT_ACT_NONE ? !binding.enabled : passport_action_label(binding.action,true)[0]!=0,
               "every visible key has a caption and hidden keys are disabled");
        passport_action_t action=passport_on_key(&view,&store,button,PASSPORT_CLICK);
        expect(action==(binding.enabled ? binding.action : PASSPORT_ACT_NONE),"caption binding and executed action cannot disagree");
        if (action==PASSPORT_ACT_CONFIGURE || action==PASSPORT_ACT_ENABLE_CONFIG || action==PASSPORT_ACT_ADD_NETWORK) view.bluetooth_ready=true;
        if (action==PASSPORT_ACT_CLOSE_CONFIG) view.bluetooth_ready=false;
        if (view.cancel_pending && (random & 3)==0) {
            bool restart=passport_finish_cancel(&view,"cancelled");
            if (restart) passport_on_key(&view,&store,PASSPORT_BTN_OK,PASSPORT_CLICK);
        } else if (view.sending && !view.cancel_pending && (random & 7)==0) passport_finish_send(&view,(random & 16)!=0,true,"test reply");
        for(int key=0;key<3;key++) {
            passport_action_t bound=passport_key_binding(&view,&store,(passport_btn_t)key).action;
            expect((bound!=PASSPORT_ACT_NETWORKS && bound!=PASSPORT_ACT_AGENTS) || view.screen==PASSPORT_SCREEN_PREFERENCES,"network and assistants exist only in settings");
        }
        expect(!(view.recording && view.sending),"recording and sending remain mutually exclusive");
        expect(view.screen!=PASSPORT_SCREEN_CAPTURE || view.recording,"capture page always owns a recording");
        expect(view.screen!=PASSPORT_SCREEN_PROGRESS || view.sending,"progress page always has a task");
    }
}

static void test_preference_navigation(void) {
    passport_store_t store;
    passport_view_t view;
    passport_store_init(&store);
    passport_store_add(&store,"Home","home","fixture");
    passport_store_add(&store,"Office","office","fixture");
    store.connected=1;
    passport_agent_upsert(&store,"First","openclaw","wss://first.example","",NULL,NULL,NULL);
    passport_agent_upsert(&store,"Second","openclaw","wss://second.example","",NULL,NULL,NULL);
    passport_agent_select(&store,1);
    const passport_screen_t screens[]={PASSPORT_SCREEN_NETS,PASSPORT_SCREEN_SETTINGS,PASSPORT_SCREEN_PREFERENCES,PASSPORT_SCREEN_VOLUME};
    const char *labels[][3]={{"Next","Open","Back"},{"切换","打开","返回"}};
    for (int language=0;language<2;language++) {
        passport_lang_set(&store,language ? "zh" : "en");
        passport_view_init(&view);
        passport_on_key(&view,&store,PASSPORT_BTN_UP,PASSPORT_CLICK);
        for (int selection=0;selection<4;selection++) {
            expect(view.preference_cursor==selection,"settings cycle through the selected rows");
            for (int button=PASSPORT_BTN_UP;button<=PASSPORT_BTN_OK;button++) {
                passport_key_binding_t binding=passport_key_binding(&view,&store,(passport_btn_t)button);
                expect(binding.enabled,"idle settings expose all three buttons");
                const char *caption = selection==2 && button==PASSPORT_BTN_DOWN ? (language ? "开启" : "On") : labels[language][button];
                expect(!strcmp(passport_action_label(binding.action,language!=0),caption),
                       "settings caption describes opening pages or toggling Bluetooth");
            }
            passport_action_t selected_action=selection==2 ? PASSPORT_ACT_ENABLE_CONFIG : PASSPORT_ACT_OPEN_PREFERENCE;
            expect(passport_key_binding(&view,&store,PASSPORT_BTN_DOWN).action==selected_action,
                   "settings bind the second key to the selected row action");
            expect(passport_on_key(&view,&store,PASSPORT_BTN_DOWN,PASSPORT_HOLD)==PASSPORT_ACT_NONE &&
                   view.screen==PASSPORT_SCREEN_PREFERENCES,"opening settings requires a single click only");
            view.sending=true;
            expect(!passport_key_binding(&view,&store,PASSPORT_BTN_DOWN).enabled &&
                   passport_on_key(&view,&store,PASSPORT_BTN_DOWN,PASSPORT_CLICK)==PASSPORT_ACT_NONE &&
                   view.screen==PASSPORT_SCREEN_PREFERENCES,"busy settings cannot open another page");
            view.sending=false;
            expect(passport_on_key(&view,&store,PASSPORT_BTN_DOWN,PASSPORT_CLICK)==selected_action &&
                   view.screen==screens[selection],"open dispatches the correct selected settings destination");
            if (selection<2) expect(view.cursor==1,"network and assistant pages start at the active entry");
            if (selection==2) {
                view.bluetooth_busy=true;
                expect(!passport_key_binding(&view,&store,PASSPORT_BTN_DOWN).enabled &&
                       passport_on_key(&view,&store,PASSPORT_BTN_DOWN,PASSPORT_CLICK)==PASSPORT_ACT_NONE,
                       "transitioning Bluetooth cannot be toggled twice");
                view.bluetooth_ready=true;
                expect(!passport_key_binding(&view,&store,PASSPORT_BTN_DOWN).enabled,
                       "saving configuration prevents Bluetooth disconnection");
                view.bluetooth_busy=false;
                expect(passport_key_binding(&view,&store,PASSPORT_BTN_DOWN).action==PASSPORT_ACT_CLOSE_CONFIG &&
                       !strcmp(passport_action_label(PASSPORT_ACT_CLOSE_CONFIG,language!=0),language ? "关闭" : "Off"),
                       "enabled Bluetooth offers an explicit off action");
                expect(passport_on_key(&view,&store,PASSPORT_BTN_DOWN,PASSPORT_CLICK)==PASSPORT_ACT_CLOSE_CONFIG &&
                       view.screen==PASSPORT_SCREEN_PREFERENCES && view.preference_cursor==2 && view.bluetooth_ready,
                       "off waits for hardware acknowledgement without leaving the menu");
                view.bluetooth_ready=false;
            } else {
                expect(passport_on_key(&view,&store,PASSPORT_BTN_OK,PASSPORT_CLICK)==PASSPORT_ACT_BACK &&
                       view.screen==PASSPORT_SCREEN_PREFERENCES && view.preference_cursor==selection,
                       "returning from settings preserves the selected row");
            }
            passport_on_key(&view,&store,PASSPORT_BTN_UP,PASSPORT_CLICK);
        }
        expect(view.preference_cursor==0,"settings selection wraps to the first row");
        expect(passport_on_key(&view,&store,PASSPORT_BTN_OK,PASSPORT_CLICK)==PASSPORT_ACT_BACK &&
               view.screen==PASSPORT_SCREEN_HOME,"settings keep a consistent exit to home");
    }
}

static void test_live_navigation(void) {
    passport_store_t store;
    passport_view_t view;
    passport_store_init(&store); passport_view_init(&view);
    expect(!view.reply[0],"home starts without a synthetic reply or a removed mode prompt");
    const char *home_labels[]={"设置","通话","提问"};
    const char *home_labels_en[]={"Set","Call","Ask"};
    for (int button=PASSPORT_BTN_UP;button<=PASSPORT_BTN_OK;button++) {
        passport_key_binding_t binding=passport_key_binding(&view,&store,(passport_btn_t)button);
        expect(!strcmp(passport_action_label(binding.action,true),home_labels[button]),"home captions match the three product entries");
        expect(!strcmp(passport_action_label(binding.action,false),home_labels_en[button]),"English home captions match the same actions");
    }
    expect(passport_on_key(&view,&store,PASSPORT_BTN_UP,PASSPORT_CLICK)==PASSPORT_ACT_PREFERENCES &&
           view.screen==PASSPORT_SCREEN_PREFERENCES,"settings do not introduce another conversation menu");
    expect(view.speaker_volume == 55, "speaker defaults to the previous call volume");
    passport_on_key(&view,&store,PASSPORT_BTN_UP,PASSPORT_CLICK);
    passport_on_key(&view,&store,PASSPORT_BTN_UP,PASSPORT_CLICK);
    passport_on_key(&view,&store,PASSPORT_BTN_UP,PASSPORT_CLICK);
    expect(passport_on_key(&view,&store,PASSPORT_BTN_DOWN,PASSPORT_CLICK)==PASSPORT_ACT_OPEN_PREFERENCE && view.screen==PASSPORT_SCREEN_VOLUME,
           "volume is reachable without starting a call");
    expect(passport_on_key(&view,&store,PASSPORT_BTN_UP,PASSPORT_CLICK)==PASSPORT_ACT_VOLUME_UP && view.speaker_volume==60,
           "speaker volume changes in five-percent steps");
    for (int step=0;step<30;step++) passport_on_key(&view,&store,PASSPORT_BTN_UP,PASSPORT_CLICK);
    expect(view.speaker_volume==100 && !passport_key_binding(&view,&store,PASSPORT_BTN_UP).enabled,"volume stops at maximum");
    for (int step=0;step<30;step++) passport_on_key(&view,&store,PASSPORT_BTN_DOWN,PASSPORT_CLICK);
    expect(view.speaker_volume==0 && !passport_key_binding(&view,&store,PASSPORT_BTN_DOWN).enabled && !view.live_muted,"silent speaker does not mute microphone");
    view.sending=true;
    expect(!passport_key_binding(&view,&store,PASSPORT_BTN_UP).enabled,"no codec control while a call owns audio");
    view.sending=false;
    passport_on_key(&view,&store,PASSPORT_BTN_OK,PASSPORT_CLICK);
    expect(view.screen==PASSPORT_SCREEN_PREFERENCES && view.preference_cursor==3,"volume returns to its selected settings row");
    expect(passport_on_key(&view,&store,PASSPORT_BTN_OK,PASSPORT_CLICK)==PASSPORT_ACT_BACK &&
           view.screen==PASSPORT_SCREEN_HOME,"settings return directly to the two voice entries");
    expect(passport_on_key(&view,&store,PASSPORT_BTN_OK,PASSPORT_CLICK)==PASSPORT_ACT_RECORD && view.recording,"home starts a recording");
    expect(view.screen==PASSPORT_SCREEN_CAPTURE,"talk owns the capture screen");
    passport_on_key(&view,&store,PASSPORT_BTN_UP,PASSPORT_CLICK);
    expect(passport_on_key(&view,&store,PASSPORT_BTN_DOWN,PASSPORT_CLICK)==PASSPORT_ACT_LIVE,"Live has its own entry");
    expect(view.screen==PASSPORT_SCREEN_LIVE && view.sending && !view.recording,"Live owns the audio session, not a clip");
    expect(passport_key_binding(&view,&store,PASSPORT_BTN_UP).enabled,"microphone can be muted before the call is ready");
    expect(passport_on_key(&view,&store,PASSPORT_BTN_UP,PASSPORT_CLICK)==PASSPORT_ACT_LIVE_MUTE && view.live_muted,"connecting Call remembers mute");
    passport_view_t connecting=view;
    expect(passport_on_key(&connecting,&store,PASSPORT_BTN_OK,PASSPORT_CLICK)==PASSPORT_ACT_END_LIVE && connecting.cancel_pending,
           "connecting Call can end before ready");
    expect(passport_on_key(&view,&store,PASSPORT_BTN_UP,PASSPORT_CLICK)==PASSPORT_ACT_LIVE_UNMUTE && !view.live_muted,"connecting Call can unmute");
    view.live_ready=true;
    expect(passport_on_key(&view,&store,PASSPORT_BTN_UP,PASSPORT_CLICK)==PASSPORT_ACT_LIVE_MUTE && view.live_muted,"Live exposes a mute action");
    expect(passport_on_key(&view,&store,PASSPORT_BTN_UP,PASSPORT_CLICK)==PASSPORT_ACT_LIVE_UNMUTE && !view.live_muted,"Live can unmute");
    expect(!passport_key_binding(&view,&store,PASSPORT_BTN_DOWN).enabled,"listening does not offer reconnect");
    view.live_speaking=true;
    expect(passport_on_key(&view,&store,PASSPORT_BTN_DOWN,PASSPORT_CLICK)==PASSPORT_ACT_LIVE_INTERRUPT &&
           !view.cancel_pending && view.screen==PASSPORT_SCREEN_LIVE,"interrupt preserves the call");
    view.live_speaking=false;view.live_waiting=true;
    expect(passport_key_binding(&view,&store,PASSPORT_BTN_DOWN).enabled,"pending consult can be interrupted");
    passport_view_t active=view;
    expect(passport_on_key(&active,&store,PASSPORT_BTN_OK,PASSPORT_CLICK)==PASSPORT_ACT_END_LIVE && active.cancel_pending,
           "end still waits for cleanup");
    passport_finish_send(&view,false,false,"Realtime unavailable");
    expect(view.screen==PASSPORT_SCREEN_LIVE && view.live_failed && !view.sending && !view.live_ready,"failed Live retains its reason and reconnect controls");
    expect(!passport_key_binding(&view,&store,PASSPORT_BTN_UP).enabled,"disconnected Call does not offer a working microphone");
    expect(passport_key_binding(&view,&store,PASSPORT_BTN_DOWN).enabled && passport_key_binding(&view,&store,PASSPORT_BTN_OK).enabled,"disconnected Call can reconnect or end");
    expect(passport_on_key(&view,&store,PASSPORT_BTN_DOWN,PASSPORT_CLICK)==PASSPORT_ACT_LIVE_RECONNECT && view.cancel_pending,"failed Call requests reconnect through cleanup");
    passport_finish_cancel(&view,"Cancelled");
    expect(!view.live_muted && !view.live_failed && !view.live_ready,"ending clears all Call controls");
    passport_on_key(&view,&store,PASSPORT_BTN_DOWN,PASSPORT_CLICK);
    expect(view.screen==PASSPORT_SCREEN_LIVE && !view.live_failed && !view.live_muted,"new Call clears disconnected state");
    view.live_ready=view.live_speaking=view.live_waiting=view.live_muted=true;
    memset(view.waveform,7,sizeof(view.waveform));
    passport_finish_send(&view,true,false,"通话已结束");
    expect(view.screen==PASSPORT_SCREEN_HOME && !view.sending && !view.reply_unread,"completed Call returns home without an unread reply");
    expect(!view.live_ready && !view.live_speaking && !view.live_waiting && !view.live_muted && !view.live_failed,"completed Call clears live controls");
    for (unsigned index=0;index<PASSPORT_WAVEFORM_BARS;index++) expect(view.waveform[index]==0,"completed Call clears waveform");
}

static void test_persist_and_portal(void) {
    passport_store_t store;
    passport_store_t loaded;
    uint8_t blob[1024];
    char qr[96];
    passport_portal_form_t form;
    passport_store_init(&store);
    passport_store_add(&store, "家里", "home", "secret");
    expect(passport_agent_upsert(&store, "家里的 OpenClaw", "openclaw", "wss://gw.example", "boot",
                              NULL, "cf-id", "cf-secret"),
           "add openclaw");
    store.connected = 0;
    int n = passport_store_save(&store, blob, sizeof(blob));
    expect(n > 16, "blob written");
    expect(memcmp(blob, "PSP1", 4) == 0, "Passport settings format marker");
    passport_store_init(&loaded);
    expect(passport_store_load(&loaded, blob, (size_t)n), "blob loads");
    expect(loaded.count == 1, "loaded count");
    expect(strcmp(loaded.nets[0].pass, "secret") == 0, "loaded password");
    expect(loaded.agent_count == 1, "loaded agent");
    expect(strcmp(passport_agent_active(&loaded)->url, "wss://gw.example") == 0, "loaded url");
    expect(strcmp(passport_agent_active(&loaded)->bootstrap, "boot") == 0, "loaded bootstrap");
    expect(strcmp(passport_agent_active(&loaded)->access_secret, "cf-secret") == 0, "loaded access");
    expect(passport_lang_zh(&loaded), "loaded language");
    blob[0] ^= 0xff;
    expect(!passport_store_load(&loaded, blob, (size_t)n), "bad magic rejected");

    passport_setup_wifi_qr(qr, sizeof(qr));
    expect(strcmp(qr, "WIFI:T:WPA;S:" PASSPORT_AP_SSID ";P:" PASSPORT_AP_PASS ";;") == 0,
           "setup qr");

    memset(&form, 0, sizeof(form));
    snprintf(form.ssid, sizeof(form.ssid), "Cafe");
    snprintf(form.pass, sizeof(form.pass), "latte");
    snprintf(form.gateway_url, sizeof(form.gateway_url), "https://nope");
    expect(strcmp(passport_portal_apply(&store, &form), "url") == 0, "reject https");
    expect(store.count == 1, "failed form keeps list");
    snprintf(form.agent_name, sizeof(form.agent_name), "家里的 OpenClaw");
    snprintf(form.gateway_url, sizeof(form.gateway_url), "wss://gw.example/ws");
    snprintf(form.bootstrap, sizeof(form.bootstrap), "next");
    expect(strcmp(passport_portal_apply(&store, &form), "ok") == 0, "portal save");
    expect(store.count == 2, "cafe added");
    expect(store.connected == 1, "cafe selected");
    expect(strcmp(passport_agent_active(&store)->bootstrap, "next") == 0, "bootstrap updated");
    expect(strcmp(passport_agent_active(&store)->access_secret, "cf-secret") == 0,
           "same name keeps the access secret");
    expect(passport_agent_upsert(&store, "公司", "openclaw", "wss://work.example", "wk", NULL, NULL,
                              NULL),
           "add second openclaw");
    expect(store.agent_count == 2, "two agents");
    expect(store.active_agent == 0, "first agent stays current");
    expect(passport_agent_select(&store, 1), "select second");
    expect(strcmp(passport_agent_active(&store)->name, "公司") == 0, "second active");
    expect(!passport_agent_upsert(&store, "别的", "hermes", "https://hermes.example", "x", NULL, NULL,
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
    passport_store_t store;
    passport_scan_t scan[1] = {0};
    char log[1024];
    passport_usb_t usb;
    passport_store_init(&store);
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
    passport_usb_line(&usb, "HELLO");
    expect(strstr(log, "HELLO ai-passport-openclaw 1 0 0 zh") != NULL, "hello empty");
    usb.capabilities = "device-control";
    passport_usb_line(&usb, "HELLO");
    expect(strstr(log, "device-control") != NULL, "optional control capability");
    log[0] = '\0';
    passport_usb_line(&usb, "SCAN");
    expect(strstr(log, "NET -40 Cafe") != NULL, "scan row");
    expect(strstr(log, "SCAN END") != NULL, "scan end");
    expect(strstr(log, "NET_KIND") == NULL, "ordinary network has no hotspot marker");
    scan[0].personal_hotspot = true;
    log[0] = '\0';
    passport_usb_line(&usb, "SCAN");
    expect(strstr(log, "NET_KIND hotspot Cafe") != NULL, "hotspot metadata preserves legacy rows");
    snprintf(scan[0].ssid, sizeof(scan[0].ssid), "Phone %% Wi-Fi");
    log[0] = '\0';
    passport_usb_line(&usb, "SCAN");
    expect(strstr(log, "NET_KIND hotspot Phone%20%25%20Wi-Fi") != NULL, "hotspot metadata encodes SSID");
    log[0] = '\0';
    passport_usb_line(&usb, "SAVE 1 Cafe a%20b");
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
    passport_usb_line(&usb, command);
    expect(strstr(log, "OK agent") != NULL, "agent saved");
    expect(store.agent_count == 1, "usb added the paired agent");
    expect(strcmp(passport_agent_active(&store)->url, "wss://work.example") == 0, "pair url");
    expect(strcmp(passport_agent_active(&store)->bootstrap, "wk") == 0, "pair bootstrap stored");
    expect(store.generation == generation + 1, "generation advanced");
    snprintf(command, sizeof(command), "USE %u 1", generation);
    passport_usb_line(&usb, command);
    expect(strstr(log, "ERR stale") != NULL, "stale use rejected");
    snprintf(command, sizeof(command), "USE %u 0", store.generation);
    passport_usb_line(&usb, command);
    expect(strcmp(passport_agent_active(&store)->name, "公司") == 0, "usb selected the paired one");
    log[0] = '\0';
    passport_usb_line(&usb, "AGENTS");
    expect(strstr(log, "wss%3A%2F%2Fwork.example") != NULL, "agents include url");
    expect(strstr(log, " pending") != NULL, "agents report pending, not the secret");
    expect(strstr(log, " wk") == NULL, "agents never return the bootstrap");
    expect(strstr(log, "SAVED") == NULL, "agents are not networks");
    passport_usb_line(&usb, "NETS");
    expect(strstr(log, "SAVED 0") != NULL, "saved network listed");
    log[0] = '\0';
    uint32_t before = store.generation;
    snprintf(command, sizeof(command), "LANG %u en", store.generation);
    passport_usb_line(&usb, command);
    expect(!passport_lang_zh(&store), "language switched");
    expect(store.generation == before + 1, "language bumps generation");
    log[0] = '\0';
    before = store.generation;
    passport_usb_line(&usb, "TRY Cafe a%20b");
    expect(strstr(log, "TRY ok") != NULL, "try ok");
    expect(!g_join_keep, "try goes back");
    passport_usb_line(&usb, "TRY Cafe");
    expect(strstr(log, "TRY ok\nTRY ok") != NULL, "try reuses saved password");
    passport_usb_line(&usb, "TRY Cafe -");
    expect(strstr(log, "TRY ok\nTRY ok\nTRY ok") != NULL, "try treats - as the saved password");
    passport_usb_line(&usb, "TRY Cafe nope");
    expect(strstr(log, "TRY fail pass") != NULL, "try wrong password");
    passport_usb_line(&usb, "TRY Far x");
    expect(strstr(log, "TRY fail notfound") != NULL, "try unknown network");
    expect(store.generation == before && store.count == 1, "try saves nothing");
    log[0] = '\0';
    snprintf(command, sizeof(command), "SAVE %u Cafe nope", store.generation);
    passport_usb_line(&usb, command);
    expect(strstr(log, "ERR join pass") != NULL, "save reports reason");
    expect(strcmp(store.nets[0].pass, "a b") == 0, "failed save keeps password");
    log[0] = '\0';
    snprintf(command, sizeof(command), "SAVE %u Cafe -   off - 0", store.generation);
    passport_usb_line(&usb, command);
    expect(strstr(log, "OK saved") != NULL && strcmp(store.nets[0].pass, "a b") == 0,
           "save keeps the stored password for -");
    log[0] = '\0';
    snprintf(command, sizeof(command), "FORGET %u 0", store.generation - 1);
    passport_usb_line(&usb, command);
    expect(strstr(log, "ERR stale") != NULL && store.count == 1, "stale forget rejected");
    snprintf(command, sizeof(command), "FORGET %u 5", store.generation);
    passport_usb_line(&usb, command);
    expect(strstr(log, "ERR net") != NULL, "forget out of range");
    before = store.generation;
    snprintf(command, sizeof(command), "FORGET %u 0", store.generation);
    passport_usb_line(&usb, command);
    expect(strstr(log, "OK forget") != NULL, "forget ok");
    expect(store.count == 0 && store.connected == -1, "forgot current network");
    expect(store.generation == before + 1, "forget bumps generation");
    snprintf(command, sizeof(command), "SAVE %u Cafe a%%20b", store.generation);
    passport_usb_line(&usb, command);
    expect(store.count == 1, "network added back");
    passport_usb_line(&usb, "NOPE");
    expect(strstr(log, "ERR command") != NULL, "unknown command");
    log[0] = '\0';
    /* {"url":"wss://home.example","bootstrapToken":"hk"} */
    snprintf(command, sizeof(command),
             "SAVE %u Cafe  eyJ1cmwiOiJ3c3M6Ly9ob21lLmV4YW1wbGUiLCJib290c3RyYXBUb2tlbiI6ImhrIn0 %%E5%%AE%%B6",
             store.generation);
    passport_usb_line(&usb, command);
    expect(strstr(log, "OK saved") != NULL, "save keeps known password");
    expect(strcmp(store.nets[0].pass, "a b") == 0, "blank password reuses saved one");
    expect(strcmp(passport_agent_active(&store)->name, "家") == 0, "saved agent becomes current");
    expect(strcmp(passport_agent_active(&store)->url, "wss://home.example") == 0, "saved agent url");
    expect(strcmp(passport_agent_active(&store)->bootstrap, "hk") == 0, "saved bootstrap");
    {
        passport_pair_t pair;
        const char *why = passport_pair_parse(
            "eyJ1cmwi\nOiJ3c3M6Ly93b3JrLmV4YW1wbGUiLCJib290c3RyYXBUb2tlbiI6IndrIn0", &pair);
        expect(why == NULL, "wrapped setup code still parses");
        expect(strcmp(pair.url, "wss://work.example") == 0, "wrapped code keeps the url");
    }
    log[0] = '\0';
    snprintf(command, sizeof(command), "AGENT %u nope", store.generation);
    passport_usb_line(&usb, command);
    expect(strstr(log, "ERR code") != NULL, "bad setup code rejected");
    expect(store.agent_count == 2, "bad code changes nothing");
}

static void test_independent_configuration(void) {
    passport_store_t store;
    passport_store_init(&store);
    passport_agent_upsert(&store, "existing", "openclaw", "wss://existing.example",
                       "", "existing-device-token", "existing-access-id", "existing-access-secret");
    passport_agent_t original = store.agents[0];
    char log[1024] = {0}, command[256];
    passport_usb_t usb = { .store=&store, .reply=capture_reply, .reply_ctx=log, .join=fake_join };
    snprintf(command, sizeof(command), "SAVE %u Cafe a%%20b  ", store.generation);
    passport_usb_line(&usb, command);
    expect(strstr(log, "OK saved") != NULL, "Wi-Fi saves without a pairing code");
    expect(store.agent_count == 1 && store.active_agent == 0 &&
           memcmp(&original, &store.agents[0], sizeof(original)) == 0,
           "Wi-Fi-only save preserves agent credentials, address and selection");
    passport_net_t networks[PASSPORT_NET_MAX];
    memcpy(networks, store.nets, sizeof(networks));
    int connected = store.connected, joins = g_join_calls;
    snprintf(command, sizeof(command),
             "AGENT %u work eyJ1cmwiOiJ3c3M6Ly93b3JrLmV4YW1wbGUiLCJib290c3RyYXBUb2tlbiI6IndrIn0",
             store.generation);
    passport_usb_line(&usb, command);
    expect(strstr(log, "OK agent") != NULL && store.agent_count == 2,
           "OpenClaw saves independently of Wi-Fi");
    expect(store.active_agent == 1, "Independently written agent becomes current");
    expect(g_join_calls == joins && store.count == 1 && store.connected == connected &&
           memcmp(networks, store.nets, sizeof(networks)) == 0,
           "OpenClaw-only save never joins or modifies Wi-Fi");
    passport_store_init(&store);
    snprintf(command, sizeof(command),
             "AGENT %u work eyJ1cmwiOiJ3c3M6Ly93b3JrLmV4YW1wbGUiLCJib290c3RyYXBUb2tlbiI6IndrIn0",
             store.generation);
    passport_usb_line(&usb, command);
    expect(store.agent_count == 1 && store.count == 0 && g_join_calls == joins,
           "OpenClaw can be configured before any Wi-Fi exists");
}

static void test_agent_rename(void) {
    passport_store_t store;
    passport_store_init(&store);
    passport_store_add(&store,"Office","Office","fixture-wifi");
    passport_agent_upsert(&store,"old-cloud","openclaw","wss://cloud.example",
                       "bootstrap","device-credential","access-id","access-secret");
    passport_agent_upsert(&store,"old-sakura","openclaw","wss://sakura.example",
                       "","sakura-credential",NULL,NULL);
    store.active_agent=1;
    char log[1024]={0},command[256];
    passport_usb_t usb={.store=&store,.reply=capture_reply,.reply_ctx=log};
    passport_store_t expected=store;
    passport_usb_line(&usb,"RENAME 1 0 %E5%8A%A9%E6%89%8B%C2%B7Cloudflare");
    strcpy(expected.agents[0].name,"助手·Cloudflare");passport_store_bump(&expected);
    expect(strstr(log,"OK rename")!=NULL && !memcmp(&store,&expected,sizeof(store)),
           "rename changes only one name and generation, preserving every credential, network and selection");
    const char *invalid[]={"RENAME 1 1 stale", "RENAME 2 1 %E5%8A%A9%E6%89%8B%C2%B7Cloudflare",
        "RENAME 2 1 ", "RENAME 2 8 invalid", "RENAME 2 -1 invalid", "RENAME 2 invalid name",
        "RENAME 2 0x name", "RENAME 2  name", "RENAME 2 0 123456789012345678901234"};
    for(size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        log[0]=0;passport_usb_line(&usb,invalid[i]);
        expect(strstr(log,"ERR ")!=NULL && !memcmp(&store,&expected,sizeof(store)),
               "invalid or stale rename preserves all existing configuration");
    }
    snprintf(command,sizeof(command),"RENAME %u 1 %%E5%%8A%%A9%%E6%%89%%8B%%C2%%B7SakuraFrp",store.generation);
    log[0]=0;passport_usb_line(&usb,command);
    strcpy(expected.agents[1].name,"助手·SakuraFrp");passport_store_bump(&expected);
    expect(strstr(log,"OK rename")!=NULL && !memcmp(&store,&expected,sizeof(store)),
           "active assistant can be renamed without changing authentication or active index");
    log[0]=0;passport_usb_line(&usb,"AGENTS");
    expect(strstr(log,"Cloudflare") && strstr(log,"SakuraFrp") && !strstr(log,"credential") && !strstr(log,"access-secret"),
           "readback returns both route names without credentials");
    uint8_t blob[4096];passport_store_t loaded;
    int n=passport_store_save(&store,blob,sizeof(blob));
    expect(n>0 && passport_store_load(&loaded,blob,(size_t)n) &&
           !strcmp(loaded.agents[0].name,"助手·Cloudflare") &&
           !strcmp(loaded.agents[1].name,"助手·SakuraFrp") &&
           !strcmp(loaded.agents[1].device_token,"sakura-credential") && loaded.active_agent==1,
           "both renamed records and their credentials survive storage round-trip");
}

static void test_agent_endpoint_move(void) {
    passport_store_t store,expected,loaded;
    passport_store_init(&store);
    passport_store_add(&store,"Office","Office","fixture-password");
    passport_agent_upsert(&store,"助手·Cloudflare","openclaw","wss://cloud.example",
                       "","cloud-token","cloud-id","cloud-secret");
    passport_agent_upsert(&store,"助手·SakuraFrp","openclaw","wss://old.example:36054",
                       "","sakura-token",NULL,NULL);
    store.active_agent=1;expected=store;
    char log[2048]={0},command[1024];
    passport_usb_t usb={.store=&store,.reply=capture_reply,.reply_ctx=log};
    passport_usb_line(&usb,"ENDPOINT 1 1 wss%3A%2F%2Fold.example%3A36054 wss%3A%2F%2Fnew.example%3A26814");
    strcpy(expected.agents[1].url,"wss://new.example:26814");passport_store_bump(&expected);
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
        log[0]=0;passport_usb_line(&usb,invalid[i]);
        expect(strstr(log,"ERR ") && !memcmp(&store,&expected,sizeof(store)),
               "invalid, stale or downgraded endpoint move leaves settings untouched");
    }
    char large[PASSPORT_URL_MAX+1];memset(large,'a',sizeof(large)-1);large[sizeof(large)-1]=0;
    snprintf(command,sizeof(command),"ENDPOINT 2 1 wss://new.example:26814 wss://%s",large);
    log[0]=0;passport_usb_line(&usb,command);
    expect(strstr(log,"ERR url") && !memcmp(&store,&expected,sizeof(store)),"oversized endpoint is rejected without truncating");
    uint8_t blob[4096];int n=passport_store_save(&store,blob,sizeof(blob));
    expect(n>0 && passport_store_load(&loaded,blob,(size_t)n) &&
           !strcmp(loaded.agents[1].url,"wss://new.example:26814") &&
           !strcmp(loaded.agents[1].device_token,"sakura-token") && loaded.active_agent==1,
           "replacement URL and existing pairing survive storage round-trip");
    strcpy(store.agents[1].bootstrap,"pending-bootstrap");expected=store;
    log[0]=0;passport_usb_line(&usb,"ENDPOINT 2 1 wss://new.example:26814 wss://next.example");
    expect(strstr(log,"ERR url") && !memcmp(&store,&expected,sizeof(store)),"unconsumed bootstrap cannot be forwarded to a different endpoint");
}

static void test_gateway_route(void) {
    passport_store_t store,loaded;
    passport_store_init(&store);
    passport_store_add(&store,"Cafe","Cafe","a b");
    passport_agent_upsert(&store,"Cloudflare","openclaw","wss://cf.example","","kept-token",NULL,NULL);
    passport_agent_upsert(&store,"SakuraFrp","openclaw","wss://cn.example:26814","","cn-token",NULL,NULL);
    passport_agent_t original=store.agents[1];
    char log[2048]={0},command[256];
    passport_usb_t usb={.store=&store,.reply=capture_reply,.reply_ctx=log,.join=fake_join};
    snprintf(command,sizeof(command),"ROUTE %u 0 ss proxy.example 443 s%%20ecret",store.generation);
    passport_usb_line(&usb,command);
    passport_proxy_t *route=&store.agents[0].proxy;
    expect(strstr(log,"OK route") && route->enabled && !strcmp(route->host,"proxy.example") &&
           route->port==443 && !strcmp(route->secret,"s ecret"),"Shadowsocks route is stored on its gateway");
    expect(!store.agents[1].proxy.enabled && !memcmp(&original,&store.agents[1],sizeof(original)),
           "another gateway stays direct and untouched");
    log[0]=0;passport_usb_line(&usb,"AGENTS");
    expect(strstr(log,"ROUTE 0 ss proxy.example 443 1 aes-256-cfb") && strstr(log,"ROUTE 1 direct - 0 0 aes-256-cfb") &&
           !strstr(log,"ecret"),"AGENTS reports routes without the password");
    snprintf(command,sizeof(command),"ROUTE %u 0 ss proxy.example 443 - chacha20-ietf-poly1305",store.generation);
    log[0]=0;passport_usb_line(&usb,command);
    expect(strstr(log,"ERR proxy") && route->method==PASSPORT_SS_AES_256_CFB,"unsupported method rejected");
    snprintf(command,sizeof(command),"ROUTE %u 0 ss proxy.example 443 - aes-256-cfb",store.generation);
    log[0]=0;passport_usb_line(&usb,command);
    snprintf(command,sizeof(command),"ROUTE %u 0 ss proxy.example 8388 -",store.generation);
    log[0]=0;passport_usb_line(&usb,command);
    expect(strstr(log,"OK route") && route->port==8388 && !strcmp(route->secret,"s ecret"),"- keeps the saved password");
    snprintf(command,sizeof(command),"ROUTE %u 0 direct",store.generation);
    log[0]=0;passport_usb_line(&usb,command);
    expect(strstr(log,"OK route") && !route->enabled && !strcmp(route->host,"proxy.example") &&
           !strcmp(route->secret,"s ecret"),"direct keeps the server for a later switch back");
    uint32_t generation=store.generation;passport_store_t before=store;
    const char *invalid[]={"ss proxy.example 0 pw","ss proxy.example 65536 pw","ss bad%0Ahost 443 pw",
                           "ss  443 pw","http 192.168.1.10 6152","ss proxy.example 443",
                           "ss proxy.example 443 pw rc4-md5"};
    for (size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        log[0]=0;snprintf(command,sizeof(command),"ROUTE %u 1 %s",store.generation,invalid[i]);
        passport_usb_line(&usb,command);
        expect(strstr(log,"ERR proxy") && store.generation==generation && !memcmp(&before,&store,sizeof(store)),
               "invalid route rejected without changes");
    }
    log[0]=0;snprintf(command,sizeof(command),"ROUTE %u 9 direct",store.generation);passport_usb_line(&usb,command);
    expect(strstr(log,"ERR proxy")!=NULL,"unknown gateway rejected");
    log[0]=0;snprintf(command,sizeof(command),"ROUTE %u 0 direct",store.generation-1);passport_usb_line(&usb,command);
    expect(strstr(log,"ERR stale")!=NULL,"stale route rejected");
    snprintf(command,sizeof(command),"ROUTE %u 0 ss proxy.example 443 -",store.generation);passport_usb_line(&usb,command);
    snprintf(command,sizeof(command),"SAVE %u Cafe a%%20b",store.generation);passport_usb_line(&usb,command);
    expect(store.agents[0].proxy.enabled,"saving Wi-Fi does not touch gateway routes");
    uint8_t blob[4096];int n=passport_store_save(&store,blob,sizeof(blob));
    expect(n>0 && passport_store_load(&loaded,blob,(size_t)n),"routes survive persistence");
    expect(!memcmp(&store.agents[0].proxy,&loaded.agents[0].proxy,sizeof(passport_proxy_t)) &&
           !loaded.agents[1].proxy.enabled,"routes persist exactly");
    store.agents[0].proxy.method=1;
    expect(passport_store_save(&store,blob,sizeof(blob))<0,"unknown method is never stored");
    store.agents[0].proxy.method=PASSPORT_SS_AES_256_CFB;n=passport_store_save(&store,blob,sizeof(blob));
    const uint8_t *mark=NULL;
    for (int i=0;i+4<n;i++) if (!memcmp(blob+i,"GSS2",4)) mark=blob+i;
    expect(mark!=NULL,"route extension is marked");
    size_t legacy=(size_t)(mark-blob);
    for (size_t cut=legacy+1;cut<(size_t)n;cut++)
        expect(!passport_store_load(&loaded,blob,cut),"truncated route extension rejected");
    expect(passport_store_load(&loaded,blob,legacy) && !loaded.agents[0].proxy.enabled &&
           !strcmp(loaded.agents[0].device_token,"kept-token"),"records without routes load as direct");
    /* GSS1 (no method byte) loads as aes-256-cfb. */
    {
        uint8_t v1[4096];memcpy(v1,blob,legacy);size_t at=legacy;
        memcpy(v1+at,"GSS1",4);at+=4;v1[at++]=2;
        v1[at++]=1;v1[at++]=0xbb;v1[at++]=0x01;memcpy(v1+at,"proxy.example",14);at+=14;memcpy(v1+at,"pw",3);at+=3;
        v1[at++]=0;v1[at++]=0;v1[at++]=0;v1[at++]=0;v1[at++]=0;
        expect(passport_store_load(&loaded,v1,at) && loaded.agents[0].proxy.enabled && loaded.agents[0].proxy.port==443 &&
               loaded.agents[0].proxy.method==PASSPORT_SS_AES_256_CFB && !loaded.agents[1].proxy.enabled,
               "GSS1 routes load as aes-256-cfb");
    }
    /* Old firmware kept per-Wi-Fi proxies in PXY2. They load, but drop. */
    uint8_t old_blob[4096];memcpy(old_blob,blob,legacy);
    /* PXY2, one network: kind=http, on, port 6152, host, empty method and secret. */
    const uint8_t pxy[]={'P','X','Y','2',1, 0,1,0x08,0x18,'1','9','2','.','1','6','8','.','1','.','2','8',0,0,0};
    memcpy(old_blob+legacy,pxy,sizeof(pxy));
    expect(passport_store_load(&loaded,old_blob,legacy+sizeof(pxy)) && !loaded.agents[0].proxy.enabled &&
           !strcmp(loaded.nets[0].pass,"a b") && !strcmp(loaded.agents[1].device_token,"cn-token"),
           "old Wi-Fi proxy records migrate to direct gateways");
    passport_agent_drop(&store,0);
    expect(!store.agents[0].proxy.enabled && !strcmp(store.agents[0].name,"SakuraFrp"),"route moves with its gateway");
}

static void test_encoded_code_limit(void) {
    passport_store_t store;
    passport_store_init(&store);
    char log[1024] = {0}, line[5200];
    passport_usb_t usb = { .store=&store,.reply=capture_reply,.reply_ctx=log };
    /* More than 1400 encoded bytes is still a valid decoded field size. */
    strcpy(line,"AGENT 1 test ");
    for (int i=0;i<450;i++) strcat(line,"%41");
    passport_usb_line(&usb,line);
    expect(store.generation==1 && store.agent_count==0,"invalid decoded code preserves state");
    expect(strstr(log,"ERR")!=NULL,"invalid decoded code rejected");
    memset(log,0,sizeof(log)); strcpy(line,"SAVE 1 test pass ");
    for (int i=0;i<1450;i++) strcat(line,"%41");
    passport_usb_line(&usb,line);
    expect(strstr(log,"ERR code")!=NULL,"oversized decoded code rejected without a large stack buffer");
    expect(store.generation==1 && store.count==0,"oversized code leaves Wi-Fi untouched");
}

static int wide_latin(uint32_t cp, uint32_t next) {
    (void)next;
    return cp == 'W' ? 16 : cp < 128 ? 8 : 16;
}

static void test_continue_conversation(void) {
    passport_store_t store;
    passport_view_t view;
    passport_store_init(&store);
    passport_agent_upsert(&store,"助手","openclaw","wss://main.example","","device-credential",NULL,NULL);
    passport_store_t saved = store;
    passport_view_init(&view);
    passport_on_key(&view,&store,PASSPORT_BTN_OK,PASSPORT_CLICK);
    for (int turn=0;turn<4;turn++) {
        expect(view.recording && !view.sending && !view.retry_send,"each turn records fresh audio");
        expect(passport_on_key(&view,&store,PASSPORT_BTN_OK,PASSPORT_CLICK)==PASSPORT_ACT_SEND,
               "second OK sends the new recording, never a previous retry");
        passport_finish_send(&view,true,false,"这是当前回复。\n1\n2\n3\n4\n5\n6\n7\n8\n下一页");
        expect(view.screen==PASSPORT_SCREEN_TALK && !view.sending,"completed turn opens reply");
        if (turn%2) passport_on_key(&view,&store,PASSPORT_BTN_DOWN,PASSPORT_CLICK);
        expect(passport_key_binding(&view,&store,PASSPORT_BTN_OK).action==PASSPORT_ACT_CONTINUE,
               "continue is available on every reply page");
        expect(!strcmp(passport_action_label(PASSPORT_ACT_CONTINUE,true),"继续"),"continue has a visible Chinese caption");
        memset(view.waveform,99,sizeof(view.waveform));
        snprintf(view.heard,sizeof(view.heard),"上一次提问");
        expect(passport_on_key(&view,&store,PASSPORT_BTN_OK,PASSPORT_CLICK)==PASSPORT_ACT_CONTINUE,"one OK starts the next turn");
        for (size_t i=0;i<sizeof(view.waveform);i++) expect(view.waveform[i]==0,"new capture clears stale waveform");
        expect(!view.reply_unread && !view.cancel_pending,"new turn has no stale reply or cancellation");
        expect(!view.heard[0],"new recording clears the previous input transcript");
        passport_finish_send(&view,true,false,"late previous reply");
        expect(view.screen==PASSPORT_SCREEN_CAPTURE && view.recording,"late completion cannot interrupt next capture");
        expect(!memcmp(&store,&saved,sizeof(store)),"continuing preserves selected gateway and device identity configuration");
    }
    passport_on_key(&view,&store,PASSPORT_BTN_OK,PASSPORT_CLICK);
    passport_finish_send(&view,false,true,"网络中断");
    expect(passport_on_key(&view,&store,PASSPORT_BTN_UP,PASSPORT_CLICK)==PASSPORT_ACT_RETRY,"a follow-up failure can retry its own audio");
    passport_finish_send(&view,true,false,"恢复后的回复");
    view.reply_page = 0;
    expect(passport_on_key(&view,&store,PASSPORT_BTN_UP,PASSPORT_CLICK)==PASSPORT_ACT_BACK,"reply can return home without recording");
    expect(!view.recording && !view.sending && !view.retry_send,"home exit does not upload or restart audio");
    passport_set_reply(&view,"继续测试");
    passport_on_key(&view,&store,PASSPORT_BTN_OK,PASSPORT_CLICK);
    passport_on_key(&view,&store,PASSPORT_BTN_UP,PASSPORT_CLICK);
    expect(view.screen==PASSPORT_SCREEN_HOME && !view.recording && !view.sending,"follow-up recording can be cancelled");
}

static void test_chat_history(void) {
    passport_store_t store;
    passport_view_t view;
    char page[PASSPORT_PAGE_TEXT_MAX];
    passport_store_init(&store);
    passport_agent_upsert(&store,"春庭","openclaw","wss://chat.example","","fixture",NULL,NULL);
    passport_view_init(&view);
    passport_on_key(&view,&store,PASSPORT_BTN_OK,PASSPORT_CLICK);
    strcpy(view.heard,"林冲是谁？");
    passport_on_key(&view,&store,PASSPORT_BTN_OK,PASSPORT_CLICK);
    passport_chat_update(&view,NULL);
    passport_chat_update(&view,"林冲是");
    view.screen = PASSPORT_SCREEN_TALK;
    passport_reply_page_text(&view,page,sizeof(page));
    expect(strstr(page,"你：\n林冲是谁？\n\n春庭：\n林冲是")!=NULL,"question and streamed answer are consecutive role-labelled messages using the assistant name");
    passport_chat_row_t rows[PASSPORT_REPLY_ROWS];
    passport_chat_page(&view,page,sizeof(page),rows);
    expect(rows[0].caption && !rows[0].assistant && !rows[1].assistant &&
        rows[3].caption && rows[3].assistant && rows[4].assistant,"row metadata identifies speakers and captions independently of text");
    expect(!strncmp(page+rows[3].offset,"春庭：",rows[3].length),"assistant caption preserves its configured name");
    passport_finish_send(&view,true,false,"林冲是水浒传中的人物。");
    expect(view.chat_count==1 && !view.chat_pending,"completion commits exactly one turn");
    passport_on_key(&view,&store,PASSPORT_BTN_OK,PASSPORT_CLICK);
    expect(view.chat_count==2 && !strcmp(view.chat[0].question,"林冲是谁？"),"continue preserves the previous question");
    strcpy(view.heard,"他的绰号呢？");
    passport_on_key(&view,&store,PASSPORT_BTN_OK,PASSPORT_CLICK);
    passport_chat_update(&view,"豹子");
    passport_finish_send(&view,false,true,"发送失败");
    expect(!strcmp(view.chat[1].answer,"豹子"),"error status never becomes an assistant message");
    passport_on_key(&view,&store,PASSPORT_BTN_UP,PASSPORT_CLICK);
    passport_chat_update(&view,"豹子头。");
    passport_finish_send(&view,true,false,"豹子头。");
    expect(view.chat_count==2 && !strcmp(view.chat[1].answer,"豹子头。"),"retry updates its existing turn without duplication");
    expect(!strcmp(view.chat[0].answer,"林冲是水浒传中的人物。"),"a later answer cannot replace previous messages");
    passport_on_key(&view,&store,PASSPORT_BTN_OK,PASSPORT_CLICK);
    passport_on_key(&view,&store,PASSPORT_BTN_UP,PASSPORT_CLICK);
    expect(view.chat_count==2 && !view.chat_pending,"cancel removes only the unfinished turn");
    for (int turn=0;turn<5;turn++) {
        passport_on_key(&view,&store,PASSPORT_BTN_OK,PASSPORT_CLICK);
        snprintf(view.heard,sizeof(view.heard),"问题%d",turn);
        passport_on_key(&view,&store,PASSPORT_BTN_OK,PASSPORT_CLICK);
        passport_finish_send(&view,true,false,"回复");
    }
    expect(view.chat_count==PASSPORT_CHAT_TURNS && !strcmp(view.chat[0].question,"问题2"),"history evicts whole oldest turns within a fixed memory budget");
    passport_on_key(&view,&store,PASSPORT_BTN_OK,PASSPORT_CLICK);
    passport_on_key(&view,&store,PASSPORT_BTN_UP,PASSPORT_CLICK);
    expect(view.chat_count==PASSPORT_CHAT_TURNS && !strcmp(view.chat[0].question,"问题2"),"cancel at full capacity restores the oldest completed turn");
    passport_on_key(&view,&store,PASSPORT_BTN_OK,PASSPORT_CLICK);
    passport_on_key(&view,&store,PASSPORT_BTN_OK,PASSPORT_CLICK);
    expect(view.sending && view.chat_pending,"manual no-speech submission has an unfinished turn until capture is checked");
    passport_finish_cancel(&view,"No speech detected. Please try again.");
    expect(view.chat_count==PASSPORT_CHAT_TURNS && !view.chat_pending && !view.sending && !view.retry_send,
        "no-speech rejection removes the pending turn without enabling upload retry");
    expect(!strcmp(view.chat[0].question,"问题2") && view.screen==PASSPORT_SCREEN_HOME,
        "no-speech rejection restores completed history and reachable home controls");
    view.screen=PASSPORT_SCREEN_TALK;
    int pages = passport_reply_pages(&view);
    expect(view.reply_page>=0 && view.reply_page<pages,"new turn opens on a valid recent page");
    view.reply_page=0;
    passport_reply_page_text(&view,page,sizeof(page));
    expect(strstr(page,"问题2")!=NULL,"oldest retained question remains readable");
    strcpy(store.lang,"en");passport_chat_sync(&view,&store);
    passport_reply_page_text(&view,page,sizeof(page));
    expect(strstr(page,"You:") && strstr(page,"春庭:"),"chat language changes preserve the assistant's own name");
    strcpy(view.chat_name,"");passport_reply_page_text(&view,page,sizeof(page));
    expect(strstr(page,"Assistant:")!=NULL,"unnamed assistants have a localized fallback");
    passport_chat_sync(&view,&store);
    passport_on_key(&view,&store,PASSPORT_BTN_OK,PASSPORT_CLICK);
    passport_on_key(&view,&store,PASSPORT_BTN_OK,PASSPORT_CLICK);
    memset(view.heard,'W',sizeof(view.heard)-1);view.heard[sizeof(view.heard)-1]=0;
    passport_chat_update(&view,NULL);
    view.reply_page=0;
    passport_chat_update(&view,"streamed text");
    expect(view.reply_page==0,"streaming never jumps away from a manually selected page");
    for (int index=0;index<passport_reply_pages(&view);index++) {
        view.reply_page=index;
        passport_reply_page_text(&view,page,sizeof(page));
        int rows=1;
        for (const char *cursor=page;*cursor;cursor++) if (*cursor=='\n') rows++;
        expect(rows<=PASSPORT_REPLY_ROWS && page[0],"each chat page contains at most nine nonempty rows including role captions");
    }
    strcpy(store.agents[0].url,"wss://different.example");
    passport_chat_sync(&view,&store);
    expect(view.chat_count==0 && !view.chat_visible && !view.heard[0],"changing assistant endpoint clears private conversation history");
    view.sending=false;view.screen=PASSPORT_SCREEN_HOME;
    passport_on_key(&view,&store,PASSPORT_BTN_OK,PASSPORT_CLICK);
    strcpy(view.heard,"取消的提问");passport_chat_update(&view,NULL);
    passport_on_key(&view,&store,PASSPORT_BTN_UP,PASSPORT_CLICK);
    expect(view.chat_count==0,"cancelling before upload leaves no orphan user message");
    passport_on_key(&view,&store,PASSPORT_BTN_OK,PASSPORT_CLICK);
    passport_on_key(&view,&store,PASSPORT_BTN_OK,PASSPORT_CLICK);
    passport_chat_update(&view,"quick answer");view.screen=PASSPORT_SCREEN_TALK;
    strcpy(view.heard,"late transcript");passport_chat_update(&view,NULL);
    passport_finish_send(&view,true,false,"quick answer");
    expect(!strcmp(view.chat[0].question,"late transcript") && !strcmp(view.chat[0].answer,"quick answer"),"late transcript fills the same turn before final completion");
    expect(sizeof(view.chat)==3840,"chat allocation is fixed and independent of the number of turns");
}

static void test_pages(void) {
    passport_view_t view;
    passport_store_t store;
    char page[PASSPORT_PAGE_TEXT_MAX], text[PASSPORT_TEXT_MAX] = "";
    passport_store_init(&store);
    passport_view_init(&view);
    for (int i = 0; i < 110; i++) strcat(text, "春");
    passport_set_reply(&view, text);
    expect(view.screen == PASSPORT_SCREEN_TALK, "reply opens reader");
    expect(passport_reply_pages(&view) == 2, "reader fills nine lines before paginating");
    passport_reply_page_text(&view, page, sizeof(page));
    int chinese = 0, breaks = 0;
    for (size_t i = 0; i < strlen(page); i++) {
        if (page[i] == '\n') breaks++;
        if ((unsigned char)page[i] == 0xe6) chinese++;
    }
    expect(chinese == 99 && breaks == 8, "first page contains ninety-nine complete Han glyphs");
    expect(passport_key_binding(&view, &store, PASSPORT_BTN_UP).action == PASSPORT_ACT_BACK &&
           passport_key_binding(&view, &store, PASSPORT_BTN_DOWN).action == PASSPORT_ACT_NEXT_PAGE,
           "first of two pages: up is Back, down is Next page");
    expect(!strcmp(passport_action_label(PASSPORT_ACT_BACK, true), "返回"), "Back has its Chinese caption");
    expect(passport_on_key(&view, &store, PASSPORT_BTN_UP, PASSPORT_CLICK) == PASSPORT_ACT_BACK,
           "up leaves the first reply page");
    expect(view.screen == PASSPORT_SCREEN_HOME, "first reply page has an exit action");
    passport_set_reply(&view, text);
    passport_on_key(&view, &store, PASSPORT_BTN_DOWN, PASSPORT_CLICK);
    expect(view.reply_page == 1, "down moves to page two");
    passport_reply_page_text(&view, page, sizeof(page));
    expect(strlen(page) == 33, "last page contains the remaining eleven glyphs");
    expect(passport_key_binding(&view, &store, PASSPORT_BTN_UP).action == PASSPORT_ACT_PREV_PAGE &&
           passport_key_binding(&view, &store, PASSPORT_BTN_DOWN).action == PASSPORT_ACT_BACK,
           "last page: up is Previous page, down is Back");
    passport_on_key(&view, &store, PASSPORT_BTN_UP, PASSPORT_CLICK);
    expect(view.reply_page == 0, "up returns to the first page");
    passport_on_key(&view, &store, PASSPORT_BTN_DOWN, PASSPORT_CLICK);
    expect(passport_on_key(&view, &store, PASSPORT_BTN_DOWN, PASSPORT_CLICK) == PASSPORT_ACT_BACK &&
           view.screen == PASSPORT_SCREEN_HOME && !view.recording && !view.sending,
           "down on the last page returns home without recording");
    const char *single_replies[]={"","一页","One page"};
    for (int language=0;language<2;language++) {
        passport_lang_set(&store,language ? "en" : "zh");
        for (size_t reply=0;reply<sizeof(single_replies)/sizeof(single_replies[0]);reply++) {
            passport_set_reply(&view,single_replies[reply]);
            expect(passport_key_binding(&view,&store,PASSPORT_BTN_UP).action==PASSPORT_ACT_BACK &&
                   passport_key_binding(&view,&store,PASSPORT_BTN_UP).enabled &&
                   passport_key_binding(&view,&store,PASSPORT_BTN_DOWN).action==PASSPORT_ACT_NONE &&
                   !passport_key_binding(&view,&store,PASSPORT_BTN_DOWN).enabled &&
                   passport_key_binding(&view,&store,PASSPORT_BTN_OK).action==PASSPORT_ACT_CONTINUE,
                   "single-page replies expose one Back, one Continue, and no duplicate action");
            passport_view_t unchanged=view;
            expect(passport_on_key(&view,&store,PASSPORT_BTN_DOWN,PASSPORT_CLICK)==PASSPORT_ACT_NONE &&
                   !memcmp(&view,&unchanged,sizeof(view)),"hidden paging key leaves single-page replies unchanged");
        }
    }
    passport_set_reply(&view,"正在回复");view.sending=true;
    expect(passport_key_binding(&view,&store,PASSPORT_BTN_DOWN).action==PASSPORT_ACT_NONE &&
           !passport_key_binding(&view,&store,PASSPORT_BTN_DOWN).enabled &&
           passport_key_binding(&view,&store,PASSPORT_BTN_OK).action==PASSPORT_ACT_CANCEL_SEND,
           "a short streaming reply retains cancellation without a duplicate Back");
    snprintf(view.reply,sizeof(view.reply),"%s",text);
    expect(passport_key_binding(&view,&store,PASSPORT_BTN_DOWN).action==PASSPORT_ACT_NEXT_PAGE &&
           passport_key_binding(&view,&store,PASSPORT_BTN_DOWN).enabled,
           "paging becomes available when a streaming reply grows beyond one page");
    view.sending=false;
    passport_set_reply(&view, text);
    expect(passport_on_key(&view, &store, PASSPORT_BTN_OK, PASSPORT_CLICK) == PASSPORT_ACT_CONTINUE,
           "OK continues the same conversation");
    expect(view.screen == PASSPORT_SCREEN_CAPTURE && view.recording,
           "OK starts the next voice turn");
    passport_on_key(&view, &store, PASSPORT_BTN_UP, PASSPORT_CLICK);

    passport_set_reply(&view,text);
    view.sending=true;
    expect(passport_key_binding(&view,&store,PASSPORT_BTN_DOWN).enabled &&
           passport_key_binding(&view,&store,PASSPORT_BTN_OK).action==PASSPORT_ACT_CANCEL_SEND,
           "live reader permits paging and cancellation, not a concurrent recording");
    passport_on_key(&view,&store,PASSPORT_BTN_DOWN,PASSPORT_CLICK);
    expect(view.reply_page==1,"live reply can be paged before completion");
    passport_finish_send(&view,true,false,text);
    expect(view.reply_page==1 && view.screen==PASSPORT_SCREEN_TALK && !view.sending &&
           passport_key_binding(&view,&store,PASSPORT_BTN_OK).action==PASSPORT_ACT_CONTINUE,
           "final reply keeps reading position and enables the next turn");
    view.sending=true;
    expect(passport_on_key(&view,&store,PASSPORT_BTN_DOWN,PASSPORT_CLICK)==PASSPORT_ACT_BACK && view.cancel_pending &&
           view.screen==PASSPORT_SCREEN_CANCELLING,"last-page Back cancels a live run rather than leaving an orphan");
    passport_finish_cancel(&view,"cancelled");
    passport_set_reply(&view,text);view.sending=true;
    passport_finish_send(&view,false,true,"interrupted");
    expect(view.screen==PASSPORT_SCREEN_FAILURE && view.retry_send,"live failure offers recovery instead of a stuck reader");
    passport_finish_cancel(&view,"cancelled");

    passport_set_reply(&view, "1\n2\n3\n4\n5\n6\n7\n8\n9\n10");
    expect(passport_reply_pages(&view) == 2, "explicit newlines consume display rows");
    view.reply_page = 1;
    passport_reply_page_text(&view, page, sizeof(page));
    expect(!strcmp(page, "10"), "paragraph page boundary resumes without losing content");
    passport_set_reply(&view, "a\r\nb\n\nc");
    passport_reply_page_text(&view, page, sizeof(page));
    expect(!strcmp(page, "a\nb\n\nc"), "CRLF and blank paragraphs remain readable");
    passport_set_reply(&view, "hello world readable words wrap without splitting them");
    passport_reply_page_text(&view, page, sizeof(page));
    expect(strstr(page, "hello world readable\nwords") != NULL, "English wraps at word boundaries");
    passport_reply_set_glyph_width(wide_latin);
    memset(text, 'W', 110); text[110] = 0;
    passport_set_reply(&view, text);
    expect(passport_reply_pages(&view) == 2, "wide Latin glyphs use actual font advances");
    passport_reply_set_glyph_width(NULL);
    expect(passport_reply_pages(&view) == 1, "narrow glyph advances allow more content");
    passport_set_reply(&view, "一二三四五六七八九十甲，继续");
    passport_reply_page_text(&view, page, sizeof(page));
    expect(strstr(page, "十\n甲，继续") != NULL, "closing punctuation cannot start a line");
    passport_set_reply(&view, "一二三四五六七八九十（继续");
    passport_reply_page_text(&view, page, sizeof(page));
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
        passport_set_reply(&view,reply);passport_reply_page_text(&view,page,sizeof(page));
        snprintf(expected,sizeof(expected),"十\n%s继续",punctuation_pairs[index][0]);
        expect(strstr(page,expected) != NULL,"opening quotation marks and brackets stay with their text");
        snprintf(reply,sizeof(reply),"一二三四五六七八九十甲%s继续",punctuation_pairs[index][1]);
        passport_set_reply(&view,reply);passport_reply_page_text(&view,page,sizeof(page));
        snprintf(expected,sizeof(expected),"十\n甲%s继续",punctuation_pairs[index][1]);
        expect(strstr(page,expected) != NULL,"closing quotation marks and brackets do not start a line");
    }
    char oversized[481] = "";
    for (int i = 0; i < 160; i++) strcat(oversized, "春");
    passport_set_reply(&view, oversized);
    expect(strlen(view.reply) == 477, "stored reply truncation preserves complete UTF-8 glyphs");
    char small[5];
    passport_set_reply(&view, "中文内容");
    passport_reply_page_text(&view, small, sizeof(small));
    expect(!strcmp(small, "中"), "small output buffers preserve UTF-8 boundaries");
    passport_on_key(&view, &store, PASSPORT_BTN_OK, PASSPORT_HOLD);
    expect(view.screen == PASSPORT_SCREEN_TALK, "hold OK has no alternate action");
    expect(passport_on_key(&view, &store, PASSPORT_BTN_OK, PASSPORT_CLICK) == PASSPORT_ACT_CONTINUE,
           "OK starts another turn after a reply");
    expect(view.screen == PASSPORT_SCREEN_CAPTURE && view.recording,
           "reply reader transitions directly to recording");
}

static void test_capture_input_ownership(void) {
    passport_store_t store;
    passport_view_t view;
    passport_store_init(&store);
    passport_view_init(&view);
    passport_on_key(&view, &store, PASSPORT_BTN_OK, PASSPORT_CLICK);
    expect(passport_capture_input_valid(&view, &store, PASSPORT_BTN_OK, PASSPORT_ACT_SEND, 7, 7),
           "current capture accepts its send event");
    expect(passport_capture_input_valid(&view, &store, PASSPORT_BTN_UP, PASSPORT_ACT_CANCEL_RECORD, 7, 7),
           "current capture accepts its cancel event");
    expect(passport_capture_input_valid(&view, &store, PASSPORT_BTN_DOWN, PASSPORT_ACT_RERECORD, 7, 7),
           "current capture accepts its rerecord event");
    expect(!passport_capture_input_valid(&view, &store, PASSPORT_BTN_UP, PASSPORT_ACT_SEND, 7, 7),
           "capture action must match the pressed key");
    expect(!passport_capture_input_valid(&view, &store, PASSPORT_BTN_OK, PASSPORT_ACT_SEND, 6, 7),
           "old capture cannot submit a new recording");
    expect(!passport_capture_input_valid(&view, &store, PASSPORT_BTN_OK, PASSPORT_ACT_NONE, 7, 7),
           "unguarded actions do not pass capture validation");
    view.cancel_pending = true;
    expect(!passport_capture_input_valid(&view, &store, PASSPORT_BTN_OK, PASSPORT_ACT_SEND, 7, 7),
           "cancel takes precedence over automatic send");
    view.cancel_pending = false;
    passport_on_key(&view, &store, PASSPORT_BTN_OK, PASSPORT_CLICK);
    expect(view.sending && !view.cancel_pending, "first send starts uploading");
    for (int button = PASSPORT_BTN_UP; button <= PASSPORT_BTN_OK; button++) {
        passport_action_t expected = button == PASSPORT_BTN_UP ? PASSPORT_ACT_CANCEL_RECORD
            : button == PASSPORT_BTN_DOWN ? PASSPORT_ACT_RERECORD : PASSPORT_ACT_SEND;
        expect(!passport_capture_input_valid(&view, &store, (passport_btn_t)button, expected, 7, 7),
               "late recording keys cannot cancel or restart an upload");
    }
    passport_finish_cancel(&view, "cancelled");
    expect(!passport_capture_input_valid(&view, &store, PASSPORT_BTN_OK, PASSPORT_ACT_SEND, 7, 7),
           "late automatic send cannot restart a cancelled operation");
    passport_on_key(&view, &store, PASSPORT_BTN_OK, PASSPORT_CLICK);
    expect(!passport_capture_input_valid(&view, &store, PASSPORT_BTN_OK, PASSPORT_ACT_SEND, 7, 8),
           "new recording rejects old automatic send");
    expect(passport_capture_input_valid(&view, &store, PASSPORT_BTN_OK, PASSPORT_ACT_SEND, 8, 8),
           "new recording accepts its own send");
}

static void test_frame(void) {
    char buf[512];
    passport_turn_t turn;
    memset(&turn, 0, sizeof(turn));
    turn.has_audio = true;
    snprintf(turn.audio_format, sizeof(turn.audio_format), "audio/ogg");
    int n = passport_openclaw_chat_send(buf, sizeof(buf), &turn);
    expect(n > 0 && (size_t)n < sizeof(buf), "frame fits");
    expect(strstr(buf, "\"method\":\"chat.send\"") != NULL, "method");
    expect(strstr(buf, "agent:main:passport") != NULL, "session");
    expect(strstr(buf, "audio/ogg") != NULL, "audio attachment");
    expect(strstr(buf, "\"message\":\"\"") != NULL, "empty message with audio");

    memset(&turn, 0, sizeof(turn));
    snprintf(turn.transcript, sizeof(turn.transcript), "hello");
    passport_openclaw_chat_send(buf, sizeof(buf), &turn);
    expect(strstr(buf, "\"message\":\"hello\"") != NULL, "text fallback");
    expect(strstr(buf, "attachments") == NULL, "text has no attachment");
}

int main(void) {
    test_hotspot_detection();
    test_store_order();
    test_navigation();
    test_network_navigation();
    test_navigation_sequences();
    test_preference_navigation();
    test_live_navigation();
    test_persist_and_portal();
    test_usb();
    test_independent_configuration();
    test_agent_rename();
    test_agent_endpoint_move();
    test_gateway_route();
    test_encoded_code_limit();
    test_continue_conversation();
    test_chat_history();
    test_pages();
    test_capture_input_ownership();
    test_frame();
    if (g_failed != 0) {
        fprintf(stderr, "%d assertion(s) failed\n", g_failed);
        return 1;
    }
    printf("passport tests passed\n");
    return 0;
}
