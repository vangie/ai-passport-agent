/* Render the actual firmware view: glyph coverage, content bounds, steady
 * frames and changing capture levels all matter on a small wearable screen. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../managed_components/lvgl__lvgl/src/misc/lv_text_private.h"
#include "../main/passport_screen.c"

static unsigned char s_pixels[PASSPORT_LCD_W * PASSPORT_LCD_H * 3];
static unsigned s_flushes;
static const char *s_output;

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *data) {
    s_flushes++;
    for (int y=area->y1;y<=area->y2;y++) for (int x=area->x1;x<=area->x2;x++) {
        unsigned rgb=data[0]|((unsigned)data[1]<<8);data+=2;
        size_t at=(size_t)(y*PASSPORT_LCD_W+x)*3;
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

static bool chat_contains(const char *text_value) {
    for (size_t row=0;row<PASSPORT_REPLY_ROWS;row++)
        if (!lv_obj_has_flag(s_chat_rows[row],LV_OBJ_FLAG_HIDDEN) &&
            strstr(lv_label_get_text(s_chat_rows[row]),text_value)) return true;
    return false;
}

static void check_choice_rows(size_t count, size_t first, size_t selected) {
    for (size_t row=0;row<PASSPORT_REPLY_ROWS;row++) {
        bool show=row<5 && first+row<count;
        lv_obj_t *item=s_chat_rows[row];
        assert(lv_obj_has_flag(item,LV_OBJ_FLAG_HIDDEN)==!show);
        if (!show) continue;
        bool focused=first+row==selected;
        assert(lv_obj_get_x(item)==20 && lv_obj_get_y(item)==132+(int)row*24);
        assert(lv_obj_get_style_text_align(item,0)==LV_TEXT_ALIGN_LEFT);
        assert(lv_obj_get_style_bg_opa(item,0)==(focused?LV_OPA_COVER:LV_OPA_TRANSP));
        assert(!strncmp(lv_label_get_text(item),focused?"> ":"  ",2));
    }
}

static void render(const char *name, passport_view_t *v, passport_store_t *store) {
    passport_screen_show(v,store);lv_obj_update_layout(lv_screen_active());lv_refr_now(NULL);
    check_labels(lv_screen_active());assert(lv_mem_test()==LV_RESULT_OK);
    for (size_t row=0;row<PASSPORT_REPLY_ROWS;row++) {
        lv_obj_t *message_row=s_chat_rows[row];
        if (lv_obj_has_flag(message_row,LV_OBJ_FLAG_HIDDEN) || v->screen!=PASSPORT_SCREEN_TALK) continue;
        bool assistant=s_chat_metadata[row].assistant;
        assert(lv_obj_get_x(message_row)==(assistant?13:25));
        assert(lv_obj_get_style_text_align(message_row,0)==(assistant?LV_TEXT_ALIGN_LEFT:LV_TEXT_ALIGN_RIGHT));
        assert(lv_obj_get_x(message_row)+lv_obj_get_width(message_row)<=197);
        assert(lv_color_eq(lv_obj_get_style_text_color(message_row,0),lv_color_hex(assistant?
            (s_chat_metadata[row].caption?MUTED:WHITE):MINT)));
    }
    for (int i=0;i<3;i++) {
        assert(lv_obj_get_y(s_keys[i])+lv_obj_get_height(s_keys[i])/2==s_key_centres[i]);
        assert(lv_obj_get_height(s_keys[i])==40);
        assert(lv_obj_get_height(s_keys[i])==lv_obj_get_height(s_keys[0]));
        assert(lv_obj_get_width(s_keys[i])==24 && lv_obj_get_x(s_keys[i])==212);
        assert(lv_obj_get_x(s_keys[i])+lv_obj_get_width(s_keys[i])<=238);
        assert(lv_obj_get_y(s_key_words[i])+lv_obj_get_height(s_key_words[i])/2==s_key_centres[i]);
        assert(lv_obj_get_height(s_key_words[i])==lv_obj_get_height(s_keys[i]));
        assert(lv_obj_get_x(s_key_words[i])+lv_obj_get_width(s_key_words[i])/2==
               lv_obj_get_x(s_keys[i])+lv_obj_get_width(s_keys[i])/2);
        assert(lv_obj_get_y(s_markers[i])==lv_obj_get_y(s_keys[i]) &&
               lv_obj_get_height(s_markers[i])==lv_obj_get_height(s_keys[i]));
        if (!lv_obj_has_flag(s_keys[i],LV_OBJ_FLAG_HIDDEN) &&
            lv_obj_get_style_bg_opa(s_keys[i],0)==LV_OPA_COVER) {
            int key_left=lv_obj_get_x(s_keys[i]);
            int key_right=key_left+lv_obj_get_width(s_keys[i])-1;
            int key_top=lv_obj_get_y(s_keys[i]);
            int key_bottom=key_top+lv_obj_get_height(s_keys[i])-1;
            const unsigned char *background=&s_pixels[(key_top*PASSPORT_LCD_W+key_left+KEY_WIDTH/2)*3];
            assert(memcmp(background,&s_pixels[(key_top*PASSPORT_LCD_W+key_left)*3],3)!=0);
            assert(memcmp(background,&s_pixels[(key_bottom*PASSPORT_LCD_W+key_left)*3],3)!=0);
            for (int edge_x=key_right-7;edge_x<=key_right;edge_x++) {
                assert(memcmp(background,&s_pixels[(key_top*PASSPORT_LCD_W+edge_x)*3],3)==0);
                assert(memcmp(background,&s_pixels[(key_bottom*PASSPORT_LCD_W+edge_x)*3],3)==0);
            }
        }
        if (!lv_obj_has_flag(s_keys[i],LV_OBJ_FLAG_HIDDEN) || !lv_obj_has_flag(s_key_words[i],LV_OBJ_FLAG_HIDDEN)) {
            int key_left=lv_obj_get_x(s_keys[i]), key_top=lv_obj_get_y(s_keys[i]);
            int ink_left=KEY_WIDTH, ink_right=-1, ink_top=KEY_HEIGHT, ink_bottom=-1;
            for (int row=0;row<KEY_HEIGHT;row++) for (int column=0;column<KEY_WIDTH;column++) {
                const unsigned char *pixel=&s_pixels[((key_top+row)*PASSPORT_LCD_W+key_left+column)*3];
                if (pixel[0]<=100 && pixel[1]<=100 && pixel[2]<=100) continue;
                if (column<ink_left) ink_left=column;
                if (column>ink_right) ink_right=column;
                if (row<ink_top) ink_top=row;
                if (row>ink_bottom) ink_bottom=row;
            }
            int padding_right = (KEY_WIDTH - 1) - ink_right;
            int padding_bottom = (KEY_HEIGHT - 1) - ink_bottom;
            if (ink_right<ink_left || ink_left<4 || padding_right<4 || ink_top<4 || padding_bottom<4 ||
                abs(ink_left-padding_right)>2 || abs(ink_top-padding_bottom)>2)
                fprintf(stderr,"Uneven key padding on %s key %d: left=%d right=%d top=%d bottom=%d\n",name,i,ink_left,padding_right,ink_top,padding_bottom);
            assert(ink_right>=ink_left && ink_left>=4 && padding_right>=4 && ink_top>=4 && padding_bottom>=4);
            assert(abs(ink_left-padding_right)<=2 && abs(ink_top-padding_bottom)<=2);
            if (v->screen==PASSPORT_SCREEN_HOME && passport_lang_zh(store))
                assert(ink_left==5 && padding_right==5 && ink_top==5 && padding_bottom==5);
        }
    }
    assert(lv_obj_get_x(s_wifi) == (v->bluetooth_ready ? 134 : 148));
    bool bluetooth_connected = v->bluetooth_ready && v->bluetooth_connected;
    assert(lv_obj_has_flag(s_bluetooth, LV_OBJ_FLAG_HIDDEN) == !v->bluetooth_ready);
    assert(lv_obj_get_child_count(s_bluetooth) == 1);
    assert(lv_color_eq(lv_obj_get_style_line_color(s_bluetooth_symbol, 0),
        lv_color_hex(bluetooth_connected ? 0x9BBFFF : FAINT)));
    assert(lv_obj_get_style_line_width(s_bluetooth_symbol, 0) == (bluetooth_connected ? 2 : 1));
    assert(lv_obj_get_x(s_bluetooth) + lv_obj_get_width(s_bluetooth) + 3 == lv_obj_get_x(s_battery));
    assert(lv_obj_get_x(s_wifi) + lv_obj_get_width(s_wifi) == (v->bluetooth_ready ? 152 : 166));
    /* The title must end before the first status icon, even with BLE on. */
    assert(lv_obj_get_x(s_title)+lv_obj_get_width(s_title) <= lv_obj_get_x(s_wifi));
    assert(lv_obj_get_x(s_battery)==168 && lv_obj_get_x(s_battery_text)==194);
    assert(lv_obj_get_x(s_battery_text)+lv_obj_get_width(s_battery_text)==PASSPORT_LCD_W-8);
    lv_point_t battery_text_size;
    lv_text_get_size(&battery_text_size,lv_label_get_text(s_battery_text),&lv_font_montserrat_14,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
    assert(lv_obj_get_width(s_battery_text)>=battery_text_size.x);
    assert(lv_obj_get_x(s_battery_tip)==lv_obj_get_x(s_battery)+lv_obj_get_width(s_battery));
    assert(lv_obj_get_x(s_battery_text)-lv_obj_get_x(s_battery_tip)-lv_obj_get_width(s_battery_tip)==4);
    assert(lv_obj_get_y(s_battery_tip)+lv_obj_get_height(s_battery_tip)/2==lv_obj_get_y(s_battery)+lv_obj_get_height(s_battery)/2);
    assert(lv_obj_get_y(s_title)+lv_obj_get_height(s_title)+2<=lv_obj_get_y(s_keys[0]));
    assert(lv_obj_get_y(s_wifi)+lv_obj_get_height(s_wifi)+2<=lv_obj_get_y(s_keys[0]));
    assert(lv_obj_get_y(s_bluetooth)+lv_obj_get_height(s_bluetooth)+2<=lv_obj_get_y(s_keys[0]));
    assert(lv_obj_get_y(s_battery_text)+lv_obj_get_height(s_battery_text)+2<=lv_obj_get_y(s_keys[0]));
    int battery_soc=v->battery_soc>100 ? 100 : v->battery_soc;
    int battery_fill_width=battery_soc<=0 ? 0 : (battery_soc*18+99)/100;
    assert(lv_obj_has_flag(s_battery_fill,LV_OBJ_FLAG_HIDDEN)==(battery_fill_width==0));
    assert(lv_obj_get_width(s_battery_fill)==(battery_fill_width ? battery_fill_width : 1));
    assert(lv_obj_get_x(s_battery_fill)==0 && lv_obj_get_y(s_battery_fill)==0);
    assert(lv_obj_get_width(s_battery_fill)<=lv_obj_get_content_width(s_battery));
    assert(lv_obj_get_height(s_battery_fill)==lv_obj_get_content_height(s_battery));
    assert(lv_obj_get_style_shadow_width(s_battery,0)==0);
    assert(lv_obj_get_style_shadow_width(s_battery_fill,0)==0);
    assert(lv_obj_get_style_shadow_width(s_battery_tip,0)==0);
    assert(lv_color_eq(lv_obj_get_style_bg_color(s_battery_fill,0),lv_color_hex(battery_soc<0 ? FAINT : battery_soc<=20 ? AMBER : MINT)));
    lv_area_t battery_content;
    lv_obj_get_content_coords(s_battery,&battery_content);
    int battery_center_y=(battery_content.y1+battery_content.y2)/2;
    for (int column=0;column<lv_obj_get_content_width(s_battery);column++) {
        uint32_t expected_color=column<battery_fill_width ? (battery_soc<=20 ? AMBER : MINT) : INK;
        uint16_t expected_rgb=lv_color_to_u16(lv_color_hex(expected_color));
        unsigned char expected_pixel[]={((expected_rgb>>11)&31)*255/31,((expected_rgb>>5)&63)*255/63,(expected_rgb&31)*255/31};
        const unsigned char *actual_pixel=&s_pixels[(battery_center_y*PASSPORT_LCD_W+battery_content.x1+column)*3];
        assert(!memcmp(actual_pixel,expected_pixel,sizeof(expected_pixel)));
    }
    char battery_value[8];
    if (battery_soc<0) snprintf(battery_value,sizeof(battery_value),"--");
    else snprintf(battery_value,sizeof(battery_value),"%d%%",battery_soc);
    assert(!strcmp(lv_label_get_text(s_battery_text),battery_value));
    unsigned before=s_flushes;
    for (int i=0;i<12;i++) { passport_screen_show(v,store);lv_obj_update_layout(lv_screen_active());lv_refr_now(NULL); }
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
    lv_init();lv_display_t *display=lv_display_create(PASSPORT_LCD_W,PASSPORT_LCD_H);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    static uint16_t buffer[PASSPORT_LCD_W*10];
    lv_display_set_buffers(display,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,flush);
    passport_store_t store;passport_store_init(&store);
    passport_store_add(&store,"Home","Home-WiFi","fixture");store.connected=0;
    passport_agent_upsert(&store,"助手·Cloudflare","openclaw","wss://gateway.example","",NULL,NULL,NULL);
    passport_view_t v;passport_view_init(&v);v.wifi_online=true;v.battery_soc=98;
    passport_screen_open();
    for (passport_action_t action=PASSPORT_ACT_NETWORKS;action<=PASSPORT_ACT_LIVE_INTERRUPT;action++) {
        const char *caption=passport_action_label(action,false);
        if (!caption[0]) continue;
        assert(strlen(caption)<=4 || !strcmp(caption,"Wi-Fi"));
        lv_point_t measured;
        lv_text_get_size(&measured,caption,&lv_font_montserrat_14,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
        assert(measured.x<=42);
        set_rotated_caption(0,caption,MUTED);
        assert(s_key_word_dsc[0].header.w==18 && s_key_word_dsc[0].header.h==40);
        assert(s_key_word_dsc[0].data_size==40*18);
        bool has_ink=false;
        for (size_t pixel=0;pixel<sizeof(s_key_word_px[0]);pixel++) has_ink|=s_key_word_px[0][pixel]!=0;
        assert(has_ink && !strcmp(s_key_word_text[0],caption));
    }
    assert(!strcmp(passport_action_label(PASSPORT_ACT_PREFERENCES,false),"Set"));
    assert(!strcmp(passport_action_label(PASSPORT_ACT_AGENTS,false),"Agt"));
    assert(!strcmp(passport_action_label(PASSPORT_ACT_LIVE_UNMUTE,false),"Mic"));
    assert(!strcmp(passport_action_label(PASSPORT_ACT_LIVE_INTERRUPT,false),"Stop"));
    assert(!strcmp(passport_action_label(PASSPORT_ACT_VOLUME_UP,false),"Vol+"));
    assert(!strcmp(passport_action_label(PASSPORT_ACT_VOLUME_DOWN,false),"Vol-"));
    const char *settings_previews[][4]={
        {"settings-menu","settings-agents-selected","settings-bluetooth-selected","settings-volume-selected"},
        {"english-settings-menu","english-settings-agents-selected","english-settings-bluetooth-selected","english-settings-volume-selected"}
    };
    v.screen=PASSPORT_SCREEN_PREFERENCES;
    for (int language=0;language<2;language++) {
        strcpy(store.lang,language ? "en" : "zh");
        for (int selection=0;selection<4;selection++) {
            v.preference_cursor=selection;
            render(settings_previews[language][selection],&v,&store);
            if (language) {
                assert(!strcmp(s_key_word_text[0],"Next"));
                assert(!strcmp(s_key_word_text[1],selection==2 ? "On" : "Open"));
                assert(!strcmp(s_key_word_text[2],"Back"));
            } else {
                assert(!strcmp(lv_label_get_text(s_keys[0]),"切\n换"));
                assert(!strcmp(lv_label_get_text(s_keys[1]),selection==2 ? "开\n启" : "打\n开"));
                assert(!strcmp(lv_label_get_text(s_keys[2]),"返\n回"));
            }
        }
        v.preference_cursor=2;v.bluetooth_ready=true;
        render(language ? "english-settings-bluetooth-on" : "settings-bluetooth-on",&v,&store);
        assert(strstr(lv_label_get_text(s_body),language ? "Bluetooth  On" : "蓝牙  已开启"));
        assert(!strcmp(passport_action_label(s_last_binding[1].action,!language),language ? "Off" : "关闭"));
        v.bluetooth_connected=true;
        render(language ? "english-settings-bluetooth-connected" : "settings-bluetooth-connected",&v,&store);
        assert(strstr(lv_label_get_text(s_body),language ? "Bluetooth  Connected" : "蓝牙  已连接"));
        assert(s_last_binding[1].action==PASSPORT_ACT_CLOSE_CONFIG && s_last_binding[1].enabled);
        v.bluetooth_busy=true;
        render(language ? "english-settings-bluetooth-busy" : "settings-bluetooth-busy",&v,&store);
        assert(strstr(lv_label_get_text(s_body),language ? "Bluetooth  Busy" : "蓝牙  处理中"));
        assert(!s_last_binding[1].enabled && s_last_binding[0].enabled && s_last_binding[2].enabled);
        v.bluetooth_ready=false;v.bluetooth_busy=false;v.bluetooth_connected=false;
        v.screen=PASSPORT_SCREEN_HOME;
        render(language ? "english-bluetooth-disabled" : "bluetooth-disabled",&v,&store);
        v.bluetooth_ready=true;
        render(language ? "english-bluetooth-waiting" : "bluetooth-waiting",&v,&store);
        v.bluetooth_connected=true;
        render(language ? "english-bluetooth-linked" : "bluetooth-linked",&v,&store);
        v.wifi_online=false;
        render(language ? "english-bluetooth-linked-no-wifi" : "bluetooth-linked-no-wifi",&v,&store);
        v.wifi_online=true;v.bluetooth_connected=false;
        render(language ? "english-bluetooth-disconnected" : "bluetooth-disconnected",&v,&store);
        v.bluetooth_ready=false;
        v.screen=PASSPORT_SCREEN_PREFERENCES;
    }
    strcpy(store.lang,"zh");
    const int battery_previews[]={100,0,-1,1,20,21,101,-2,9,10,99};
    const char *battery_preview_names[]={"full-width-status-battery-full","full-width-status-battery-empty","full-width-status-battery-unknown","battery-one-percent","battery-low","battery-normal","battery-over-full","battery-unavailable","battery-single-digit","battery-double-digit","battery-nearly-full"};
    for (size_t index=0;index<sizeof(battery_previews)/sizeof(battery_previews[0]);index++) {
        v.battery_soc=battery_previews[index];
        render(battery_preview_names[index],&v,&store);
    }
    v.bluetooth_ready=true;v.bluetooth_connected=true;v.battery_soc=100;
    render("bluetooth-linked-full-battery",&v,&store);
    v.battery_soc=1;
    render("bluetooth-linked-one-percent",&v,&store);
    v.bluetooth_ready=false;v.bluetooth_connected=false;v.wifi_online=false;
    render("status-radios-disabled",&v,&store);
    v.wifi_online=true;
    v.battery_soc=98;
    v.screen=PASSPORT_SCREEN_VOLUME;render("speaker-volume",&v,&store);
    v.speaker_volume=0;render("speaker-silent",&v,&store);
    v.speaker_volume=75;
    strcpy(store.lang,"en");render("english-speaker-volume",&v,&store);strcpy(store.lang,"zh");
    v.screen=PASSPORT_SCREEN_LIVE;v.sending=true;v.live_ready=false;
    render("live-connecting",&v,&store);
    assert(!strcmp(lv_label_get_text(s_headline),"正在连接"));
    assert(s_last_binding[PASSPORT_BTN_UP].enabled && s_last_binding[PASSPORT_BTN_OK].enabled);
    v.live_ready=true;snprintf(v.reply,sizeof(v.reply),"可以直接说话，停顿后自动回复");
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
    assert(s_last_binding[PASSPORT_BTN_DOWN].action==PASSPORT_ACT_LIVE_INTERRUPT);
    v.live_muted=true;render("live-muted",&v,&store);
    v.live_speaking=false;render("live-microphone-muted",&v,&store);
    assert(!strcmp(lv_label_get_text(s_headline),"已静音"));
    passport_finish_send(&v,false,false,"实时连接中断，请重新连接");
    render("live-disconnected",&v,&store);
    assert(!strcmp(lv_label_get_text(s_headline),"连接中断"));
    assert(!strcmp(lv_label_get_text(s_body),"实时连接中断，请重新连接"));
    v.sending=true;v.live_failed=false;v.live_speaking=true;v.live_ready=true;
    strcpy(store.lang,"en");render("english-live-speaking",&v,&store);
    v.live_waiting=true;render("english-live-waiting",&v,&store);v.live_waiting=false;
    v.live_speaking=false;v.live_muted=false;render("english-live-listening",&v,&store);
    v.live_muted=true;render("english-live-microphone-muted",&v,&store);
    assert(!strcmp(lv_label_get_text(s_headline),"Muted"));
    passport_finish_send(&v,false,false,"Connection interrupted. Retry to reconnect.");
    render("english-live-disconnected",&v,&store);
    assert(!strcmp(lv_label_get_text(s_headline),"Disconnected"));
    assert(strstr(lv_label_get_text(s_body),"interrupted"));
    v.sending=true;v.live_failed=false;v.live_muted=false;
    v.live_ready=false;render("english-live-connecting",&v,&store);
    assert(!strcmp(lv_label_get_text(s_headline),"Connecting"));
    passport_finish_send(&v,true,false,"通话已结束");
    render("english-live-ended",&v,&store);
    assert(v.screen==PASSPORT_SCREEN_HOME && !v.live_ready && !v.live_muted && !v.sending);
    strcpy(store.lang,"zh");
    v.screen=PASSPORT_SCREEN_LIVE;v.sending=true;v.live_ready=true;
    passport_finish_send(&v,true,false,"通话已结束");
    render("live-ended",&v,&store);
    assert(v.screen==PASSPORT_SCREEN_HOME && !v.reply_unread);
    strcpy(store.lang,"en");
    v.screen=PASSPORT_SCREEN_PREFERENCES;render("english-settings-menu",&v,&store);
    strcpy(store.lang,"zh");v.recording=false;
    v.screen=PASSPORT_SCREEN_HOME;v.sending=false;v.live_speaking=false;
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
    lv_text_get_size(&name_size,lv_label_get_text(s_status),&passport_font_16,0,0,1000,LV_TEXT_FLAG_NONE);
    assert(name_size.x<=lv_obj_get_content_width(s_status));
    passport_on_key(&v,&store,PASSPORT_BTN_OK,PASSPORT_CLICK);
    for (size_t i=0;i<PASSPORT_WAVEFORM_BARS;i++)v.waveform[i]=(uint8_t)((i%4)*25+10);
    render("recording",&v,&store);
    unsigned before=s_flushes;memset(v.waveform,75,sizeof(v.waveform));
    render("recording-louder",&v,&store);assert(s_flushes>before);
    passport_on_key(&v,&store,PASSPORT_BTN_OK,PASSPORT_CLICK);strcpy(v.reply,"正在连接 OpenClaw");
    render("sending",&v,&store);strcpy(v.reply,"已发送，助手处理中");render("waiting",&v,&store);
    passport_finish_send(&v,false,true,"连接中断，请重试");render("failure",&v,&store);
    passport_offer_retry(&v,"录音已恢复，可重试");render("restored",&v,&store);
    v.screen=PASSPORT_SCREEN_CANCELLING;v.cancel_pending=true;render("cancelling",&v,&store);
    passport_finish_cancel(&v,"本次操作已取消");render("home-after-cancel",&v,&store);
    passport_on_key(&v,&store,PASSPORT_BTN_UP,PASSPORT_CLICK);render("networks",&v,&store);
    v.preference_cursor=0;
    passport_on_key(&v,&store,PASSPORT_BTN_DOWN,PASSPORT_CLICK);
    passport_on_key(&v,&store,PASSPORT_BTN_DOWN,PASSPORT_CLICK);
    render("network-detail",&v,&store);
    assert(!strstr(lv_label_get_text(s_body),"代理") && !strcmp(lv_label_get_text(s_keys[1]),"配\n置"));
    passport_on_key(&v,&store,PASSPORT_BTN_OK,PASSPORT_CLICK);
    v.screen=PASSPORT_SCREEN_SETTINGS;render("assistants",&v,&store);
    passport_agent_upsert(&store,"助手·SakuraFrp","openclaw","wss://domestic.example","",NULL,NULL,NULL);
    v.cursor=1; render("assistant-domestic",&v,&store);
    assert(!strcmp(lv_label_get_text(s_chat_rows[0]),"  助手·Cloudflare"));
    assert(!strcmp(lv_label_get_text(s_chat_rows[1]),"> 助手·SakuraFrp"));
    assert(!strcmp(lv_label_get_text(s_hint),"可切换到此助手\n直连"));
    store.agents[0].proxy=(passport_proxy_t){.enabled=true,.host="proxy.example",.secret="pw",.port=443};
    v.cursor=0; render("assistant-shadowsocks",&v,&store);
    assert(strstr(lv_label_get_text(s_hint),"经 Shadowsocks")!=NULL);
    v.cursor=1;
    store.active_agent=1; v.screen=PASSPORT_SCREEN_HOME; render("home-domestic",&v,&store);
    assert(!strcmp(lv_label_get_text(s_status),"助手：助手·SakuraFrp"));
    lv_text_get_size(&name_size,lv_label_get_text(s_status),&passport_font_16,0,0,1000,LV_TEXT_FLAG_NONE);
    assert(name_size.x<=lv_obj_get_content_width(s_status));
    v.screen=PASSPORT_SCREEN_ADD;v.bluetooth_ready=true;render("bluetooth",&v,&store);
    v.bluetooth_connected=true;render("bluetooth-config-connected",&v,&store);
    assert(strstr(lv_label_get_text(s_body),"蓝牙已连接"));
    strcpy(store.lang,"en");render("english-bluetooth-config-connected",&v,&store);
    assert(strstr(lv_label_get_text(s_body),"Bluetooth connected."));
    strcpy(store.lang,"zh");v.bluetooth_connected=false;
    v.screen=PASSPORT_SCREEN_NETS;memset(store.nets[0].ssid,'W',32);store.nets[0].ssid[32]=0;
    render("long-network-name",&v,&store);
    passport_set_reply(&v,"助手说：“你好！”\n‘单引号’「」『』\n，。！？；：、\n（）［］｛｝〔〕\n【】〖〗《》〈〉\n—— …… · • ※\n～ ／ ＼ ｜ ＿\n现在 23℃，体感 23°C。");
    render("reply-punctuation",&v,&store);
    assert(strstr(lv_label_get_text(s_body),"“") && strstr(lv_label_get_text(s_body),"”"));
    strcpy(store.lang,"en");render("english-reply-punctuation",&v,&store);strcpy(store.lang,"zh");
    passport_set_reply(&v,supported_caption);
    size_t punctuation_pages = passport_reply_pages(&v);
    assert(punctuation_pages > 0);
    for (size_t page = 0; page < punctuation_pages; page++) {
        v.reply_page = page;
        char name[64];snprintf(name,sizeof(name),"punctuation-inventory-%zu",page+1);
        render(name,&v,&store);
        strcpy(store.lang,"en");
        snprintf(name,sizeof(name),"english-punctuation-inventory-%zu",page+1);
        render(name,&v,&store);strcpy(store.lang,"zh");
    }
    passport_set_reply(&v,"好的，可以帮你查天气。天气：多云，14～19℃，体感 18°C。");
    render("reply-weather",&v,&store);
    for (int language=0;language<2;language++) {
        strcpy(store.lang,language ? "en" : "zh");
        for (int streaming=0;streaming<2;streaming++) {
            v.sending=streaming!=0;
            render(language ? (streaming ? "english-single-replying" : "english-single-reply")
                            : (streaming ? "single-replying" : "single-reply"),&v,&store);
            assert(s_last_binding[PASSPORT_BTN_UP].action==PASSPORT_ACT_BACK);
            assert(s_last_binding[PASSPORT_BTN_DOWN].action==PASSPORT_ACT_NONE && !s_last_binding[PASSPORT_BTN_DOWN].enabled);
            assert(lv_obj_has_flag(s_keys[PASSPORT_BTN_DOWN],LV_OBJ_FLAG_HIDDEN));
            assert(lv_obj_has_flag(s_key_words[PASSPORT_BTN_DOWN],LV_OBJ_FLAG_HIDDEN));
            assert(lv_obj_has_flag(s_markers[PASSPORT_BTN_DOWN],LV_OBJ_FLAG_HIDDEN));
            assert(s_last_binding[PASSPORT_BTN_OK].action==(streaming ? PASSPORT_ACT_CANCEL_SEND : PASSPORT_ACT_CONTINUE));
        }
    }
    v.sending=false;strcpy(store.lang,"zh");
    passport_set_reply(&v,"现在 23℃，体感 23°C。\n\n这是一段较长的回复，用来验证中文、标点和多页阅读。回复文字应充分利用屏幕，按钮提示独立排列在右侧，与三个实体按键一一对应。\n\n翻到下一页，按 OK 可以继续说话，并保留当前会话的上下文。");
    render("reply",&v,&store);
    assert(s_last_binding[PASSPORT_BTN_DOWN].action==PASSPORT_ACT_NEXT_PAGE &&
           !lv_obj_has_flag(s_keys[PASSPORT_BTN_DOWN],LV_OBJ_FLAG_HIDDEN));
    v.reply_page=passport_reply_pages(&v)-1;render("reply-last",&v,&store);
    assert(s_last_binding[PASSPORT_BTN_UP].action==PASSPORT_ACT_PREV_PAGE &&
           s_last_binding[PASSPORT_BTN_DOWN].action==PASSPORT_ACT_BACK);
    v.sending=true;render("replying",&v,&store);
    assert(!strcmp(lv_label_get_text(s_status),"正在回复"));
    assert(s_last_binding[PASSPORT_BTN_OK].action==PASSPORT_ACT_CANCEL_SEND && s_last_binding[PASSPORT_BTN_OK].enabled);
    strcpy(store.lang,"en");render("english-replying",&v,&store);
    assert(!strcmp(lv_label_get_text(s_status),"REPLYING"));
    strcpy(store.lang,"en");passport_view_init(&v);v.wifi_online=true;v.battery_soc=100;
    render("english-home",&v,&store);
    v.screen=PASSPORT_SCREEN_NETS;render("english-network",&v,&store);
    passport_on_key(&v,&store,PASSPORT_BTN_DOWN,PASSPORT_CLICK);render("english-network-detail",&v,&store);
    v.screen=PASSPORT_SCREEN_SETTINGS;render("english-assistants",&v,&store);
    v.screen=PASSPORT_SCREEN_CAPTURE;render("english-prepare",&v,&store);
    v.recording=true;render("english-recording",&v,&store);v.recording=false;
    v.screen=PASSPORT_SCREEN_PROGRESS;strcpy(v.reply,"Connecting to OpenClaw");render("english-sending",&v,&store);
    strcpy(v.reply,"助手处理中");render("english-waiting",&v,&store);
    strcpy(v.heard,"请介绍水浒传里的林冲。");
    render("english-ask-transcript",&v,&store);
    assert(!strcmp(lv_label_get_text(s_status),"WAITING"));
    assert(strstr(lv_label_get_text(s_body),"You said:\n请介绍"));
    strcpy(store.lang,"zh");render("ask-transcript",&v,&store);
    assert(!strcmp(lv_label_get_text(s_status),"等待回复"));
    assert(strstr(lv_label_get_text(s_body),"你说：\n请介绍"));
    assert(s_last_binding[PASSPORT_BTN_UP].action==PASSPORT_ACT_CANCEL_SEND);
    assert(s_last_binding[PASSPORT_BTN_DOWN].action==PASSPORT_ACT_RESTART_SEND);
    assert(s_last_binding[PASSPORT_BTN_OK].action==PASSPORT_ACT_BACK);
    for (size_t offset=0;offset+3<sizeof(v.heard);offset+=3) memcpy(v.heard+offset,"春",4);
    render("ask-long-transcript",&v,&store);
    assert(lv_label_get_long_mode(s_body)==LV_LABEL_LONG_DOT);
    passport_set_reply(&v,"林冲是水浒传中的人物。");render("ask-transcript-to-reply",&v,&store);
    assert(!strcmp(lv_label_get_text(s_status),"回复"));
    assert(!strstr(lv_label_get_text(s_body),"你说："));
    assert(lv_label_get_long_mode(s_body)==LV_LABEL_LONG_WRAP);
    passport_view_init(&v);v.wifi_online=true;v.battery_soc=97;
    passport_on_key(&v,&store,PASSPORT_BTN_OK,PASSPORT_CLICK);
    strcpy(v.heard,"林冲是谁？");
    passport_on_key(&v,&store,PASSPORT_BTN_OK,PASSPORT_CLICK);
    passport_chat_update(&v,NULL);v.screen=PASSPORT_SCREEN_TALK;
    render("chat-waiting",&v,&store);
    assert(chat_contains("你：") && chat_contains("林冲是谁？"));
    assert(!strcmp(lv_label_get_text(s_status),"等待回复"));
    passport_chat_update(&v,"林冲是水浒传中的人物。");
    render("chat-streaming",&v,&store);
    assert(chat_contains("你：") && chat_contains(v.chat_name));
    passport_finish_send(&v,true,false,"林冲是水浒传中的人物。");
    render("chat-first-turn",&v,&store);
    passport_on_key(&v,&store,PASSPORT_BTN_OK,PASSPORT_CLICK);
    strcpy(v.heard,"他的绰号呢？");
    passport_on_key(&v,&store,PASSPORT_BTN_OK,PASSPORT_CLICK);
    passport_finish_send(&v,true,false,"豹子头。");
    for (int page=0;page<passport_reply_pages(&v);page++) {
        char name[48];snprintf(name,sizeof(name),"chat-two-turns-%d",page);
        v.reply_page=page;render(name,&v,&store);
    }
    strcpy(store.lang,"en");passport_chat_sync(&v,&store);v.reply_page=0;
    render("english-chat",&v,&store);
    assert(chat_contains("You:") && chat_contains(v.chat_name));
    snprintf(v.chat_name,sizeof(v.chat_name),"WWWWWWWWWWWWWWWWWWWWWWW");
    render("chat-long-name",&v,&store);
    assert(chat_contains("…:"));
    v.chat_name[0]=0;render("chat-no-name",&v,&store);assert(chat_contains("Assistant:"));
    strcpy(store.lang,"zh");passport_chat_sync(&v,&store);
    passport_on_key(&v,&store,PASSPORT_BTN_OK,PASSPORT_CLICK);
    for (size_t offset=0;offset+3<sizeof(v.heard);offset+=3) memcpy(v.heard+offset,"春",4);
    passport_on_key(&v,&store,PASSPORT_BTN_OK,PASSPORT_CLICK);
    passport_finish_send(&v,true,false,"一段很长的提问也可以逐页阅读，回答不会盖住问题。");
    for (int page=0;page<passport_reply_pages(&v);page++) {
        char name[48];snprintf(name,sizeof(name),"chat-long-%d",page);
        v.reply_page=page;render(name,&v,&store);
    }
    passport_view_init(&v);
    v.heard[0]=0;
    v.screen=PASSPORT_SCREEN_ADD;render("english-setup",&v,&store);
    passport_offer_retry(&v,"Connection interrupted. Please retry.");render("english-failure",&v,&store);
    passport_store_init(&store);passport_view_init(&v);render("offline",&v,&store);
    v.screen=PASSPORT_SCREEN_NETS;render("no-networks",&v,&store);
    check_choice_rows(1,0,0);
    assert(!strcmp(lv_label_get_text(s_chat_rows[0]),"> 添加 Wi-Fi"));
    v.screen=PASSPORT_SCREEN_SETTINGS;render("no-assistants",&v,&store);
    for (size_t row=0;row<PASSPORT_REPLY_ROWS;row++) assert(lv_obj_has_flag(s_chat_rows[row],LV_OBJ_FLAG_HIDDEN));
    assert(!strcmp(lv_label_get_text(s_page_count),""));
    for (size_t index=0;index<PASSPORT_NET_MAX;index++) {
        char ssid[33];snprintf(ssid,sizeof(ssid),"Network-%u",(unsigned)index+1);
        assert(passport_store_add(&store,ssid,ssid,"fixture"));
    }
    store.connected=3;
    snprintf(store.nets[1].ssid,sizeof(store.nets[1].ssid),"WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW");
    snprintf(store.nets[2].ssid,sizeof(store.nets[2].ssid),"中文网络中文网络中文网络中文网");
    for (size_t index=0;index<PASSPORT_AGENT_MAX;index++) {
        char name[24];snprintf(name,sizeof(name),"Agent-%u",(unsigned)index+1);
        assert(passport_agent_upsert(&store,name,"openclaw","wss://gateway.example","",NULL,NULL,NULL));
    }
    snprintf(store.agents[1].name,sizeof(store.agents[1].name),"WWWWWWWWWWWWWWWWWWWWWWW");
    snprintf(store.agents[2].name,sizeof(store.agents[2].name),"春庭助手春庭助手春庭助");
    store.active_agent=2;
    for (int language=0;language<2;language++) {
        strcpy(store.lang,language?"en":"zh");
        v.cursor=1;
        v.network_target=1;
        for (int state=PASSPORT_NETWORK_CONNECTING;state<=PASSPORT_NETWORK_BUSY;state++) {
            v.network_state=(passport_network_state_t)state;
            for (int online=0;online<2;online++) {
                v.wifi_online=online;
                store.connected=online?1:0;
                v.screen=PASSPORT_SCREEN_NET_MENU;
                char shot[64];snprintf(shot,sizeof(shot),"%s-wifi-%d-%d",store.lang,state,online);
                render(shot,&v,&store);
                if (state==PASSPORT_NETWORK_CONNECTING) assert(!passport_key_binding(&v,&store,PASSPORT_BTN_UP).enabled);
                v.screen=PASSPORT_SCREEN_NETS;
                snprintf(shot,sizeof(shot),"%s-wifi-list-%d-%d",store.lang,state,online);
                render(shot,&v,&store);
            }
        }
        v.network_state=PASSPORT_NETWORK_IDLE;
        v.network_target=-1;
        v.wifi_online=true;
        store.connected=3;
        v.screen=PASSPORT_SCREEN_NETS;
        for (size_t selected=0;selected<=store.count;selected++) {
            v.cursor=(int)selected;
            char shot[64];snprintf(shot,sizeof(shot),"%s-network-list-%u",store.lang,(unsigned)selected);
            render(shot,&v,&store);
            size_t first=selected>2?selected-2:0;
            if (first>store.count+1-5) first=store.count+1-5;
            check_choice_rows(store.count+1,first,selected);
            char counter[16];snprintf(counter,sizeof(counter),"%u / 9",(unsigned)selected+1);
            assert(!strcmp(lv_label_get_text(s_page_count),counter));
            if (selected==1 || selected==2)
                assert(strstr(lv_label_get_text(s_chat_rows[selected-first]),"…"));
            if (selected==store.count) assert(strstr(lv_label_get_text(s_chat_rows[4]),"Wi-Fi"));
        }
        v.screen=PASSPORT_SCREEN_SETTINGS;
        for (size_t selected=0;selected<store.agent_count;selected++) {
            v.cursor=(int)selected;
            char shot[64];snprintf(shot,sizeof(shot),"%s-assistant-list-%u",store.lang,(unsigned)selected);
            render(shot,&v,&store);
            check_choice_rows(store.agent_count,0,selected);
            if (selected==store.active_agent)
                assert(strstr(lv_label_get_text(s_hint),language?"In use":"当前助手"));
        }
        assert(store.connected==3 && store.active_agent==2);
    }
    v.battery_soc=5;v.screen=PASSPORT_SCREEN_ADD;render("bluetooth-off",&v,&store);
    lv_mem_monitor_t memory;lv_mem_monitor(&memory);
    printf("Screen PASS: zh/en, complete glyphs, text bounds, aligned keys, steady redraw, live waveform; host pool used=%zu\n",memory.total_size-memory.free_size);
    return 0;
}
