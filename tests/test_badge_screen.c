/* Render the actual firmware view: glyph coverage, content bounds, steady
 * frames and changing capture levels all matter on a small wearable screen. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../managed_components/lvgl__lvgl/src/misc/lv_text_private.h"
#include "../main/badge_screen.c"

static unsigned char s_pixels[BADGE_LCD_W * BADGE_LCD_H * 3];
static unsigned s_flushes;
static const char *s_output;

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *data) {
    s_flushes++;
    for (int y=area->y1;y<=area->y2;y++) for (int x=area->x1;x<=area->x2;x++) {
        unsigned rgb=data[0]|((unsigned)data[1]<<8);data+=2;
        size_t at=(size_t)(y*BADGE_LCD_W+x)*3;
        s_pixels[at]=((rgb>>11)&31)*255/31;
        s_pixels[at+1]=((rgb>>5)&63)*255/63;
        s_pixels[at+2]=(rgb&31)*255/31;
    }
    lv_display_flush_ready(display);
}

static void check_labels(lv_obj_t *parent) {
    if (lv_obj_has_flag(parent,LV_OBJ_FLAG_HIDDEN)) return;
    if (lv_obj_check_type(parent,&lv_label_class)) {
        const char *value=lv_label_get_text(parent);
        const lv_font_t *font=lv_obj_get_style_text_font(parent,0);
        uint32_t offset=0;
        while (value[offset]) {
            uint32_t cp=lv_text_encoded_next(value,&offset);
            if (cp<' ') continue;
            lv_font_glyph_dsc_t glyph={0};
            assert(lv_font_get_glyph_dsc(font,&glyph,cp,0) && !glyph.is_placeholder);
        }
        if (lv_label_get_long_mode(parent)!=LV_LABEL_LONG_DOT) {
            lv_point_t measured;
            lv_text_get_size(&measured,value,font,lv_obj_get_style_text_letter_space(parent,0),
                lv_obj_get_style_text_line_space(parent,0),lv_obj_get_content_width(parent),LV_TEXT_FLAG_NONE);
            if (measured.x>lv_obj_get_content_width(parent) || measured.y>lv_obj_get_content_height(parent)) {
                fprintf(stderr,"Clipped label '%s': measured %d x %d, available %d x %d\n",value,
                        (int)measured.x,(int)measured.y,(int)lv_obj_get_content_width(parent),(int)lv_obj_get_content_height(parent));
                assert(false);
            }
        }
    }
    for (uint32_t i=0;i<lv_obj_get_child_count(parent);i++) check_labels(lv_obj_get_child(parent,i));
}

static void render(const char *name, badge_view_t *v, badge_store_t *store) {
    badge_screen_show(v,store);lv_obj_update_layout(lv_screen_active());lv_refr_now(NULL);
    check_labels(lv_screen_active());assert(lv_mem_test()==LV_RESULT_OK);
    for (int i=0;i<3;i++) {
        assert(lv_obj_get_y(s_keys[i])+lv_obj_get_height(s_keys[i])/2==s_key_centres[i]);
        assert(lv_obj_get_x(s_keys[i])+lv_obj_get_width(s_keys[i])<=238);
        assert(lv_obj_get_y(s_markers[i])==lv_obj_get_y(s_keys[i]) &&
               lv_obj_get_height(s_markers[i])==lv_obj_get_height(s_keys[i]));
        if (!lv_obj_has_flag(s_keys[i],LV_OBJ_FLAG_HIDDEN) &&
            lv_obj_get_style_bg_opa(s_keys[i],0)==LV_OPA_COVER) {
            int key_left=lv_obj_get_x(s_keys[i]);
            int key_right=key_left+lv_obj_get_width(s_keys[i])-1;
            int key_top=lv_obj_get_y(s_keys[i]);
            int key_bottom=key_top+lv_obj_get_height(s_keys[i])-1;
            const unsigned char *background=&s_pixels[(key_top*BADGE_LCD_W+key_left+15)*3];
            assert(memcmp(background,&s_pixels[(key_top*BADGE_LCD_W+key_left)*3],3)!=0);
            assert(memcmp(background,&s_pixels[(key_bottom*BADGE_LCD_W+key_left)*3],3)!=0);
            for (int edge_x=key_right-7;edge_x<=key_right;edge_x++) {
                assert(memcmp(background,&s_pixels[(key_top*BADGE_LCD_W+edge_x)*3],3)==0);
                assert(memcmp(background,&s_pixels[(key_bottom*BADGE_LCD_W+edge_x)*3],3)==0);
            }
        }
    }
    assert(lv_obj_get_x(s_wifi) == (v->bluetooth_ready ? 108 : 122));
    /* The title must end before the first status icon, even with BLE on. */
    assert(lv_obj_get_x(s_title)+lv_obj_get_width(s_title) <= lv_obj_get_x(s_wifi));
    /* Nothing in the status row may reach the key-caption column. */
    assert(lv_obj_get_x(s_battery_text)+lv_obj_get_width(s_battery_text) <= lv_obj_get_x(s_keys[0]));
    int battery_level = v->battery_soc < 0 ? 0 : v->battery_soc <= 0 ? 0 : (v->battery_soc + 19) / 20;
    if (battery_level > 5) battery_level = 5;
    for (int i=0;i<5;i++) assert(lv_obj_has_flag(s_battery_segments[i],LV_OBJ_FLAG_HIDDEN) == (i >= battery_level));
    unsigned before=s_flushes;
    for (int i=0;i<12;i++) { badge_screen_show(v,store);lv_obj_update_layout(lv_screen_active());lv_refr_now(NULL); }
    if (before!=s_flushes) fprintf(stderr,"Unexpected redraw on %s: %u -> %u\n",name,before,s_flushes);
    assert(before==s_flushes); /* Unchanged views must not invalidate the LCD. */
    if (s_output) {
        char path[512];snprintf(path,sizeof(path),"%s/%s.ppm",s_output,name);
        FILE *file=fopen(path,"wb");assert(file);
        fprintf(file,"P6\n240 320\n255\n");assert(fwrite(s_pixels,1,sizeof(s_pixels),file)==sizeof(s_pixels));fclose(file);
    }
}

int main(int argc, char **argv) {
    s_output=argc>1?argv[1]:NULL;
    lv_init();lv_display_t *display=lv_display_create(BADGE_LCD_W,BADGE_LCD_H);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    static uint16_t buffer[BADGE_LCD_W*10];
    lv_display_set_buffers(display,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,flush);
    badge_store_t store;badge_store_init(&store);
    badge_store_add(&store,"Home","Home-WiFi","fixture");store.connected=0;
    badge_agent_upsert(&store,"助手·Cloudflare","openclaw","wss://gateway.example","",NULL,NULL,NULL);
    badge_view_t v;badge_view_init(&v);v.wifi_online=true;v.battery_soc=98;
    badge_screen_open();
    v.screen=BADGE_SCREEN_PREFERENCES;render("settings-menu",&v,&store);
    v.preference_cursor=2;render("settings-volume-selected",&v,&store);
    v.screen=BADGE_SCREEN_VOLUME;render("speaker-volume",&v,&store);
    v.speaker_volume=0;render("speaker-silent",&v,&store);
    v.speaker_volume=75;
    strcpy(store.lang,"en");render("english-speaker-volume",&v,&store);strcpy(store.lang,"zh");
    v.screen=BADGE_SCREEN_LIVE;v.sending=true;v.live_ready=true;snprintf(v.reply,sizeof(v.reply),"可以直接说话，停顿后自动回复");
    render("live-listening",&v,&store);
    assert(lv_obj_get_scroll_y(s_body_view)==0);
    snprintf(v.reply,sizeof(v.reply),"第一句回复\n第二句回复\n第三句回复\n第四句回复\n第五句回复\n第六句回复\n第七句回复\n第八句回复\n最后一句回复");
    render("live-long-reply",&v,&store);
    assert(lv_obj_get_scroll_y(s_body_view)>0 && lv_obj_get_scroll_bottom(s_body_view)==0);
    assert(lv_obj_get_y(s_body)+lv_obj_get_height(s_body)-lv_obj_get_scroll_y(s_body_view)==lv_obj_get_height(s_body_view));
    v.live_muted=true;render("live-muted-long-reply",&v,&store);
    assert(strstr(lv_label_get_text(s_body),"最后一句回复"));
    v.live_muted=false;snprintf(v.reply,sizeof(v.reply),"新一轮回复");render("live-new-turn",&v,&store);
    assert(lv_obj_get_scroll_y(s_body_view)==0);
    strcpy(store.lang,"en");
    snprintf(v.reply,sizeof(v.reply),"First line\nSecond line\nThird line\nFourth line\nFifth line\nSixth line\nSeventh line\nLatest reply");
    render("english-live-long-reply",&v,&store);
    assert(lv_obj_get_scroll_y(s_body_view)>0 && lv_obj_get_scroll_bottom(s_body_view)==0);
    strcpy(store.lang,"zh");
    v.live_waiting=true;snprintf(v.reply,sizeof(v.reply),"助手正在处理，请稍候");render("live-waiting",&v,&store);v.live_waiting=false;
    v.live_speaking=true;render("live-speaking",&v,&store);
    v.live_muted=true;render("live-muted",&v,&store);
    v.live_speaking=false;render("live-microphone-muted",&v,&store);
    assert(!strcmp(lv_label_get_text(s_headline),"已静音"));
    badge_finish_send(&v,false,false,"实时连接中断，请重新连接");
    render("live-disconnected",&v,&store);
    assert(!strcmp(lv_label_get_text(s_headline),"连接中断"));
    assert(!strcmp(lv_label_get_text(s_body),"实时连接中断，请重新连接"));
    v.sending=true;v.live_failed=false;v.live_speaking=true;v.live_ready=true;
    strcpy(store.lang,"en");render("english-live-speaking",&v,&store);
    v.live_waiting=true;render("english-live-waiting",&v,&store);v.live_waiting=false;
    v.live_speaking=false;v.live_muted=false;render("english-live-listening",&v,&store);
    v.live_muted=true;render("english-live-microphone-muted",&v,&store);
    assert(!strcmp(lv_label_get_text(s_headline),"Muted"));
    badge_finish_send(&v,false,false,"Connection interrupted. Retry to reconnect.");
    render("english-live-disconnected",&v,&store);
    assert(!strcmp(lv_label_get_text(s_headline),"Disconnected"));
    assert(strstr(lv_label_get_text(s_body),"interrupted"));
    v.sending=true;v.live_failed=false;v.live_muted=false;
    v.live_ready=false;render("english-live-connecting",&v,&store);
    v.screen=BADGE_SCREEN_PREFERENCES;render("english-settings-menu",&v,&store);
    strcpy(store.lang,"zh");v.recording=false;
    v.screen=BADGE_SCREEN_HOME;v.sending=false;v.live_speaking=false;
    const char *supported_caption =
        "\"'，。！？；：、﹐﹑﹒﹔﹕﹖﹗"
        "“”‘’「」『』〝〞〟"
        "（）［］｛｝｟｠〔〕【】〖〗〘〙〚〛《》〈〉﹙﹚﹛﹜﹝﹞"
        "—―–‐‑‒…‥⋯·・•‧"
        "／＼｜～〜〰﹏＿﹋﹌※＊＃＠＆％‰‱′″‵‶‴‷§¶†‡‖‗"
        "能帮忙查天气？好的，可以帮你查天气。天气：多云，14～19℃，体感 18°C。";
    const lv_font_t *reply_font = lv_obj_get_style_text_font(s_body, LV_PART_MAIN);
    uint32_t offset = 0;
    while (supported_caption[offset]) {
        uint32_t codepoint = lv_text_encoded_next(supported_caption, &offset);
        lv_font_glyph_dsc_t glyph = {0};
        assert(lv_font_get_glyph_dsc(reply_font, &glyph, codepoint, 0) && !glyph.is_placeholder);
        if (codepoint != ' ') assert(glyph.box_w > 0 && glyph.box_h > 0);
    }
    lv_font_glyph_dsc_t missing_glyph = {0};
    assert(!lv_font_get_glyph_dsc(reply_font, &missing_glyph, 0x1f680, 0) || missing_glyph.is_placeholder);
    render("home",&v,&store);
    assert(!strcmp(lv_label_get_text(s_status),"助手：助手·Cloudflare"));
    lv_point_t name_size;
    lv_text_get_size(&name_size,lv_label_get_text(s_status),&badge_font_16,0,0,1000,LV_TEXT_FLAG_NONE);
    assert(name_size.x<=lv_obj_get_content_width(s_status));
    badge_on_key(&v,&store,BADGE_BTN_OK,BADGE_CLICK);
    for (size_t i=0;i<BADGE_WAVEFORM_BARS;i++)v.waveform[i]=(uint8_t)((i%4)*25+10);
    render("recording",&v,&store);
    unsigned before=s_flushes;memset(v.waveform,75,sizeof(v.waveform));
    render("recording-louder",&v,&store);assert(s_flushes>before);
    badge_on_key(&v,&store,BADGE_BTN_OK,BADGE_CLICK);strcpy(v.reply,"正在连接 OpenClaw");
    render("sending",&v,&store);strcpy(v.reply,"已发送，助手处理中");render("waiting",&v,&store);
    badge_finish_send(&v,false,true,"连接中断，请重试");render("failure",&v,&store);
    badge_offer_retry(&v,"录音已恢复，可重试");render("restored",&v,&store);
    v.screen=BADGE_SCREEN_CANCELLING;v.cancel_pending=true;render("cancelling",&v,&store);
    badge_finish_cancel(&v,"本次操作已取消");render("home-after-cancel",&v,&store);
    badge_on_key(&v,&store,BADGE_BTN_UP,BADGE_CLICK);render("networks",&v,&store);
    v.preference_cursor=0;
    badge_on_key(&v,&store,BADGE_BTN_DOWN,BADGE_CLICK);
    badge_on_key(&v,&store,BADGE_BTN_DOWN,BADGE_CLICK);
    render("network-detail",&v,&store);
    assert(!strstr(lv_label_get_text(s_body),"代理") && !strcmp(lv_label_get_text(s_keys[1]),"配\n置"));
    badge_on_key(&v,&store,BADGE_BTN_OK,BADGE_CLICK);
    v.screen=BADGE_SCREEN_SETTINGS;render("assistants",&v,&store);
    badge_agent_upsert(&store,"助手·SakuraFrp","openclaw","wss://domestic.example","",NULL,NULL,NULL);
    v.cursor=1; render("assistant-domestic",&v,&store);
    assert(!strcmp(lv_label_get_text(s_body),"助手·SakuraFrp\n直连\n\n可切换到此助手"));
    store.agents[0].proxy=(badge_proxy_t){.enabled=true,.host="proxy.example",.secret="pw",.port=443};
    v.cursor=0; render("assistant-shadowsocks",&v,&store);
    assert(strstr(lv_label_get_text(s_body),"经 Shadowsocks")!=NULL);
    v.cursor=1;
    store.active_agent=1; v.screen=BADGE_SCREEN_HOME; render("home-domestic",&v,&store);
    assert(!strcmp(lv_label_get_text(s_status),"助手：助手·SakuraFrp"));
    lv_text_get_size(&name_size,lv_label_get_text(s_status),&badge_font_16,0,0,1000,LV_TEXT_FLAG_NONE);
    assert(name_size.x<=lv_obj_get_content_width(s_status));
    v.screen=BADGE_SCREEN_ADD;v.bluetooth_ready=true;render("bluetooth",&v,&store);
    v.screen=BADGE_SCREEN_NETS;memset(store.nets[0].ssid,'W',32);store.nets[0].ssid[32]=0;
    render("long-network-name",&v,&store);
    badge_set_reply(&v,"助手说：“你好！”\n‘单引号’「」『』\n，。！？；：、\n（）［］｛｝〔〕\n【】〖〗《》〈〉\n—— …… · • ※\n～ ／ ＼ ｜ ＿\n现在 23℃，体感 23°C。");
    render("reply-punctuation",&v,&store);
    assert(strstr(lv_label_get_text(s_body),"“") && strstr(lv_label_get_text(s_body),"”"));
    strcpy(store.lang,"en");render("english-reply-punctuation",&v,&store);strcpy(store.lang,"zh");
    badge_set_reply(&v,supported_caption);
    size_t punctuation_pages = badge_reply_pages(&v);
    assert(punctuation_pages > 0);
    for (size_t page = 0; page < punctuation_pages; page++) {
        v.reply_page = page;
        char name[64];snprintf(name,sizeof(name),"punctuation-inventory-%zu",page+1);
        render(name,&v,&store);
        strcpy(store.lang,"en");
        snprintf(name,sizeof(name),"english-punctuation-inventory-%zu",page+1);
        render(name,&v,&store);strcpy(store.lang,"zh");
    }
    badge_set_reply(&v,"好的，可以帮你查天气。天气：多云，14～19℃，体感 18°C。");
    render("reply-weather",&v,&store);
    badge_set_reply(&v,"现在 23℃，体感 23°C。\n\n这是一段较长的回复，用来验证中文、标点和多页阅读。回复文字应充分利用屏幕，按钮提示独立排列在右侧，与三个实体按键一一对应。\n\n翻到下一页，按 OK 可以继续说话，并保留当前会话的上下文。");
    render("reply",&v,&store);v.reply_page=badge_reply_pages(&v)-1;render("reply-last",&v,&store);
    v.sending=true;render("replying",&v,&store);
    assert(!strcmp(lv_label_get_text(s_status),"正在回复"));
    assert(s_last_binding[BADGE_BTN_OK].action==BADGE_ACT_CANCEL_SEND && s_last_binding[BADGE_BTN_OK].enabled);
    strcpy(store.lang,"en");render("english-replying",&v,&store);
    assert(!strcmp(lv_label_get_text(s_status),"REPLYING"));
    strcpy(store.lang,"en");badge_view_init(&v);v.wifi_online=true;v.battery_soc=100;
    render("english-home",&v,&store);
    v.screen=BADGE_SCREEN_NETS;render("english-network",&v,&store);
    badge_on_key(&v,&store,BADGE_BTN_DOWN,BADGE_CLICK);render("english-network-detail",&v,&store);
    v.screen=BADGE_SCREEN_SETTINGS;render("english-assistants",&v,&store);
    v.screen=BADGE_SCREEN_CAPTURE;render("english-prepare",&v,&store);
    v.recording=true;render("english-recording",&v,&store);v.recording=false;
    v.screen=BADGE_SCREEN_PROGRESS;strcpy(v.reply,"Connecting to OpenClaw");render("english-sending",&v,&store);
    strcpy(v.reply,"助手处理中");render("english-waiting",&v,&store);
    v.screen=BADGE_SCREEN_ADD;render("english-setup",&v,&store);
    badge_offer_retry(&v,"Connection interrupted. Please retry.");render("english-failure",&v,&store);
    badge_store_init(&store);badge_view_init(&v);render("offline",&v,&store);
    v.screen=BADGE_SCREEN_NETS;render("no-networks",&v,&store);
    v.screen=BADGE_SCREEN_SETTINGS;render("no-assistants",&v,&store);
    v.battery_soc=5;v.screen=BADGE_SCREEN_ADD;render("bluetooth-off",&v,&store);
    lv_mem_monitor_t memory;lv_mem_monitor(&memory);
    printf("Screen PASS: zh/en, complete glyphs, text bounds, aligned keys, steady redraw, live waveform; host pool used=%zu\n",memory.total_size-memory.free_size);
    return 0;
}
