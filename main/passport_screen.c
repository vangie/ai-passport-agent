#include "passport.h"
#include "passport_network.h"

#include <stdio.h>
#include <string.h>
#include "lvgl.h"

LV_FONT_DECLARE(passport_font_16);
LV_FONT_DECLARE(passport_font_20);
LV_FONT_DECLARE(lv_font_montserrat_14);
LV_FONT_DECLARE(lv_font_montserrat_20);
LV_IMAGE_DECLARE(passport_lobster_22);

#define INK         0x0D1420
#define SURFACE     0x172230
#define EDGE        0x29394B
#define WHITE       0xF4F0E8
#define MUTED       0xA4B0BE
#define FAINT       0x52647B
#define MINT        0x87DECC
#define CORAL       0xFF9B85
#define AMBER       0xF2C27D

typedef enum { IDLE, CAPTURE, WORK, FAILURE, CANCELLING, DETAILS, READER, CALL, TRANSCRIPT } layout_t;
static lv_obj_t *s_status, *s_headline, *s_body, *s_hint, *s_card, *s_network_name;
static lv_obj_t *s_body_view;
static lv_obj_t *s_chat_rows[PASSPORT_REPLY_ROWS];
static passport_chat_row_t s_chat_metadata[PASSPORT_REPLY_ROWS];
static lv_obj_t *s_orbit, *s_symbol, *s_waveform[PASSPORT_WAVEFORM_BARS];
static lv_obj_t *s_keys[3], *s_key_words[3], *s_markers[3];
static lv_image_dsc_t s_key_word_dsc[3];
static uint8_t s_key_word_px[3][56 * 18];
static char s_key_word_text[3][12];
static lv_obj_t *s_title, *s_wifi, *s_bluetooth, *s_battery_segments[5], *s_battery_text;
static lv_obj_t *s_page_count, *s_progress_track, *s_progress_fill;
static char s_page_text[PASSPORT_PAGE_TEXT_MAX];
static char s_network_name_text[PASSPORT_SSID_MAX];
static int s_layout = -1, s_key_language = -1, s_tone = -1;
static int s_last_soc = -2, s_last_rotation = -1;
static int s_last_wave[PASSPORT_WAVEFORM_BARS];
static passport_key_binding_t s_last_binding[3];
static bool s_bindings_valid, s_icons_valid, s_last_wifi, s_last_bt;
/* Mapped from frontal shell dimensions, not evenly spaced screen thirds. */
static const int s_key_centres[] = { 47, 163, 280 };

static const char *tr(const passport_store_t *s, const char *zh, const char *en) {
    return passport_lang_zh(s) ? zh : en;
}

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h,
                      uint32_t color, int radius) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(o, x, y); lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

static void visible(lv_obj_t *o, bool show) {
    bool hidden = lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN);
    if (hidden && show) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else if (!hidden && !show) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void text(lv_obj_t *o, const char *value) {
    if (strcmp(lv_label_get_text(o), value ? value : "")) lv_label_set_text(o, value ? value : "");
}

static void network_name(const char *value) {
    /* LV_LABEL_LONG_DOT shortens its own text buffer. Compare the source,
     * otherwise a long SSID is reset and redraws on every app iteration. */
    if (strcmp(s_network_name_text,value)) {
        snprintf(s_network_name_text,sizeof(s_network_name_text),"%s",value);
        lv_label_set_text(s_network_name,s_network_name_text);
    }
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, int x, int y,
                        int w, int h, uint32_t color) {
    lv_obj_t *o = lv_label_create(parent);
    lv_label_set_text(o, ""); lv_label_set_long_mode(o, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(o, font, 0);
    lv_obj_set_style_text_color(o, lv_color_hex(color), 0);
    lv_obj_set_pos(o, x, y); lv_obj_set_size(o, w, h);
    return o;
}

static void draw_key_edge(lv_event_t *event) {
    lv_obj_t *key = lv_event_get_target_obj(event);
    lv_draw_rect_dsc_t fill;
    lv_draw_rect_dsc_init(&fill);
    fill.bg_color = lv_obj_get_style_bg_color(key, LV_PART_MAIN);
    fill.bg_opa = lv_obj_get_style_bg_opa(key, LV_PART_MAIN);
    if (fill.bg_opa == LV_OPA_TRANSP) return;
    lv_area_t area;
    lv_obj_get_coords(key, &area);
    area.x1 = area.x2 - lv_obj_get_style_radius(key, LV_PART_MAIN) + 1;
    lv_draw_rect(lv_event_get_layer(event), &fill, &area);
}

static lv_obj_t *line(lv_obj_t *parent, int x, int y, const lv_point_precise_t *points,
                       size_t count, uint32_t color, int width) {
    lv_obj_t *o = lv_line_create(parent);
    lv_line_set_points(o, points, (uint32_t)count);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_line_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_line_width(o, width, 0);
    lv_obj_set_style_line_rounded(o, true, 0);
    return o;
}

static void add_status_icons(lv_obj_t *screen) {
    /* Rounded vector strokes replace the old horizontal Wi-Fi bars. */
    static const lv_point_precise_t wifi_outer[] = {{1,5},{4,3},{8,2},{12,3},{15,5}};
    static const lv_point_precise_t wifi_inner[] = {{4,8},{6,7},{8,7},{10,7},{12,8}};
    static const lv_point_precise_t bluetooth[] = {{3,3},{11,11},{7,15},{7,1},{11,5},{3,13}};
    s_wifi = box(screen, 108, 9, 18, 18, INK, 0);
    line(s_wifi, 0, 0, wifi_outer, 5, MINT, 2);
    line(s_wifi, 0, 0, wifi_inner, 5, MINT, 2);
    box(s_wifi, 7, 11, 3, 3, MINT, LV_RADIUS_CIRCLE);
    s_bluetooth = box(screen, 125, 9, 14, 18, INK, 0);
    line(s_bluetooth, 0, 0, bluetooth, 6, 0x9BBFFF, 1);

    /* The status row stops at x=204: the full-height column from x=206 to
     * the edge belongs to the key captions, so the top key never covers
     * the battery reading. */
    lv_obj_t *battery = box(screen, 141, 11, 19, 11, INK, 3);
    lv_obj_set_style_border_width(battery, 1, 0);
    lv_obj_set_style_border_color(battery, lv_color_hex(MUTED), 0);
    box(screen, 160, 14, 2, 5, MUTED, 1);
    static const int segment_x[] = {3, 5, 8, 10, 13};
    for (size_t i = 0; i < 5; i++)
        s_battery_segments[i] = box(battery, segment_x[i], 3, 2, 5, MINT, 1);
    s_battery_text = label(screen, &lv_font_montserrat_14, 166, 9, 38, 18, WHITE);
}

static void set_tone(uint32_t color) {
    if (s_tone == (int)color) return;
    s_tone = (int)color;
    lv_obj_set_style_arc_color(s_orbit, lv_color_hex(color), LV_PART_INDICATOR);
    lv_obj_set_style_text_color(s_symbol, lv_color_hex(color), 0);
    for (size_t i = 0; i < PASSPORT_WAVEFORM_BARS; i++)
        lv_obj_set_style_bg_color(s_waveform[i], lv_color_hex(color), 0);
}

static void layout(layout_t mode, bool zh) {
    if (s_layout == (int)mode && s_key_language == (int)zh) return;
    s_layout = mode;
    if (s_key_language != (int)zh) {
        s_key_language = zh; s_bindings_valid = false;
        for (size_t i = 0; i < 3; i++) {
            lv_obj_set_style_text_font(s_keys[i], &passport_font_16, 0);
            lv_obj_set_style_transform_rotation(s_keys[i], 0, 0);
            lv_obj_set_style_text_line_space(s_keys[i], 0, 0);
            lv_obj_set_pos(s_keys[i], 206, s_key_centres[i] - 28);
            lv_obj_set_size(s_keys[i], 30, 56);
            lv_obj_set_style_pad_top(s_keys[i], 6, 0);
            visible(s_keys[i], zh);
            visible(s_key_words[i], !zh);
        }
    }
    bool reader = mode == READER || mode == TRANSCRIPT, details = mode == DETAILS;
    bool quiet = mode == IDLE;
    lv_obj_set_width(s_status, quiet ? 184 : 124);
    visible(s_card, reader || details);
    visible(s_orbit, !reader && !details && mode != CALL);
    visible(s_symbol, mode == FAILURE || mode == CANCELLING);
    visible(s_headline, !reader);
    visible(s_page_count, mode == READER || details);
    visible(s_progress_track, mode == READER);
    visible(s_hint, details);
    lv_label_set_long_mode(s_body, mode == TRANSCRIPT ? LV_LABEL_LONG_DOT : LV_LABEL_LONG_WRAP);
    lv_obj_scroll_to_y(s_body_view, 0, LV_ANIM_OFF);
    lv_obj_set_pos(s_body_view, 0, 0);
    lv_obj_set_size(s_body_view, PASSPORT_LCD_W, PASSPORT_LCD_H);
    if (reader) {
        lv_obj_set_pos(s_card, 8, 72); lv_obj_set_size(s_card, 194, 222);
        lv_obj_set_pos(s_body, 13, 77); lv_obj_set_size(s_body, PASSPORT_REPLY_BODY_WIDTH, 212);
        lv_obj_set_style_text_align(s_body, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_text_color(s_body, lv_color_hex(WHITE), 0);
        lv_obj_set_style_text_line_space(s_body, PASSPORT_REPLY_LINE_SPACE, 0);
    } else if (details) {
        lv_obj_set_pos(s_card, 12, 79); lv_obj_set_size(s_card, 190, 214);
        lv_obj_set_pos(s_headline, 24, 94); lv_obj_set_size(s_headline, 166, 27);
        lv_obj_set_style_text_align(s_headline, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_pos(s_body, 24, 134); lv_obj_set_size(s_body, 166, 116);
        lv_obj_set_style_text_align(s_body, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_text_color(s_body, lv_color_hex(WHITE), 0);
        lv_obj_set_style_text_line_space(s_body, 3, 0);
    } else if (mode == CALL) {
        lv_obj_set_pos(s_headline, 14, 87); lv_obj_set_size(s_headline, 188, 28);
        lv_obj_set_style_text_align(s_headline, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(s_body_view, 20, 176); lv_obj_set_size(s_body_view, 178, 118);
        lv_obj_set_pos(s_body, 0, 0); lv_obj_set_size(s_body, 178, LV_SIZE_CONTENT);
        lv_obj_set_style_text_align(s_body, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_text_color(s_body, lv_color_hex(WHITE), 0);
        lv_obj_set_style_text_line_space(s_body, 3, 0);
    } else {
        lv_obj_set_pos(s_headline, 14, quiet ? 191 : 196); lv_obj_set_size(s_headline, 188, 28);
        lv_obj_set_style_text_align(s_headline, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(s_body, 20, quiet ? 231 : 236); lv_obj_set_size(s_body, 178, 62);
        lv_obj_set_style_text_align(s_body, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(s_body, lv_color_hex(MUTED), 0);
        lv_obj_set_style_text_line_space(s_body, 3, 0);
    }
    bool waveform = mode == CAPTURE || mode == CALL;
    for (size_t i = 0; i < PASSPORT_WAVEFORM_BARS; i++) {
        visible(s_waveform[i], mode == IDLE || mode == CAPTURE || mode == CALL || mode == WORK);
        lv_obj_set_x(s_waveform[i], waveform ? 32 + (int)i * 13 : 80 + (int)i * 5);
        lv_obj_set_width(s_waveform[i], waveform ? 5 : 3);
        s_last_wave[i] = -1;
    }
    lv_obj_set_style_arc_width(s_orbit, mode == WORK ? 3 : 1, LV_PART_INDICATOR);
    lv_arc_set_angles(s_orbit, 0, mode == WORK ? 80 : 359);
    lv_obj_set_style_arc_opa(s_orbit, mode == CAPTURE ? LV_OPA_TRANSP : LV_OPA_COVER, LV_PART_INDICATOR);
    text(s_symbol, mode == FAILURE ? "!" : "...");
    s_last_rotation = -1;
}

static void update_waveform(const passport_view_t *v, layout_t mode) {
    static const int resting[] = {4,8,14,23,29,35,35,29,23,14,8,4};
    if (mode == WORK) {
        /* 30 degrees every 70 ms: a smooth turn with a third of the redraws
         * of 10-degree steps, which competed with the upload for the CPU. */
        int rotation = (int)((lv_tick_get() / 70) % 12) * 30;
        if (rotation != s_last_rotation) { lv_arc_set_rotation(s_orbit, rotation); s_last_rotation = rotation; }
    } else if (s_last_rotation != 270) { lv_arc_set_rotation(s_orbit, 270); s_last_rotation = 270; }
    if (mode != IDLE && mode != CAPTURE && mode != CALL && mode != WORK) return;
    for (size_t i = 0; i < PASSPORT_WAVEFORM_BARS; i++) {
        int level = v->waveform[i] > 100 ? 100 : v->waveform[i];
        int height = mode == CALL ? 4 + level * 36 / 100 : mode == CAPTURE ? 6 + level * 72 / 100 : mode == WORK ? resting[i] / 2 + 3 : resting[i];
        if (s_last_wave[i] == height) continue;
        lv_obj_set_height(s_waveform[i], height);
        lv_obj_set_y(s_waveform[i], (mode == CALL ? 145 : 138) - height / 2);
        s_last_wave[i] = height;
    }
}

static int reply_glyph_width(uint32_t cp, uint32_t next) {
    lv_font_glyph_dsc_t glyph = {0};
    return lv_font_get_glyph_dsc(&passport_font_16, &glyph, cp, next) ? glyph.adv_w : 16;
}

static bool primary_action(passport_action_t action) {
    return action == PASSPORT_ACT_RECORD || action == PASSPORT_ACT_CONTINUE || action == PASSPORT_ACT_SEND || action == PASSPORT_ACT_RETRY ||
           action == PASSPORT_ACT_SELECT_AGENT || action == PASSPORT_ACT_READ_REPLY ||
           action == PASSPORT_ACT_ENABLE_CONFIG;
}

static void choice_text(char *out, size_t cap, const char *name, bool selected) {
    snprintf(out, cap, "%s%s", selected ? "> " : "  ", name);
    for (char *cursor = out; *cursor; cursor++)
        if (*cursor == '\n' || *cursor == '\r' || *cursor == '\t') *cursor = ' ';
    lv_point_t measured;
    lv_text_get_size(&measured, out, &passport_font_16, 0, 0, 4096, LV_TEXT_FLAG_NONE);
    if (measured.x <= PASSPORT_CHAT_BODY_WIDTH) return;
    size_t used = strlen(out);
    do {
        do { used--; } while (used > 2 && ((unsigned char)out[used] & 0xc0) == 0x80);
        snprintf(out + used, cap - used, "…");
        lv_text_get_size(&measured, out, &passport_font_16, 0, 0, 4096, LV_TEXT_FLAG_NONE);
    } while (used > 2 && measured.x > PASSPORT_CHAT_BODY_WIDTH);
}

static void show_choices(const passport_view_t *view, const passport_store_t *store) {
    bool networks = view->screen == PASSPORT_SCREEN_NETS;
    size_t count = networks ? store->count + 1 : store->agent_count;
    size_t selected = view->cursor >= 0 && (size_t)view->cursor < count ? (size_t)view->cursor : 0;
    size_t first = selected > 2 ? selected - 2 : 0;
    if (count > 5 && first > count - 5) first = count - 5;
    if (count <= 5) first = 0;
    for (size_t row = 0; row < PASSPORT_REPLY_ROWS; row++) {
        lv_obj_t *item = s_chat_rows[row];
        size_t index = first + row;
        bool show = row < 5 && index < count;
        visible(item, show);
        if (!show) continue;
        bool focused = index == selected;
        bool active = networks ? view->wifi_online && (int)index == store->connected : (int)index == store->active_agent;
        const char *name = networks ? (index < store->count ? store->nets[index].ssid
            : tr(store, "添加 Wi-Fi", "Add Wi-Fi")) : store->agents[index].name;
        char caption[72];
        choice_text(caption, sizeof(caption), name, focused);
        text(item, caption);
        int top = 132 + (int)row * 24;
        if (lv_obj_get_x(item) != 20) lv_obj_set_x(item, 20);
        if (lv_obj_get_y(item) != top) lv_obj_set_y(item, top);
        if (lv_obj_get_style_text_align(item, 0) != LV_TEXT_ALIGN_LEFT)
            lv_obj_set_style_text_align(item, LV_TEXT_ALIGN_LEFT, 0);
        lv_color_t color = lv_color_hex(focused ? CORAL : active ? MINT : WHITE);
        if (!lv_color_eq(lv_obj_get_style_text_color(item, 0), color))
            lv_obj_set_style_text_color(item, color, 0);
        lv_opa_t opacity = focused ? LV_OPA_COVER : LV_OPA_TRANSP;
        if (lv_obj_get_style_bg_opa(item, 0) != opacity)
            lv_obj_set_style_bg_opa(item, opacity, 0);
    }
}

static int raw_alpha(const uint8_t *bitmap, int bpp, int x, int y, int box_w) {
    int bit = (y * box_w + x) * bpp;
    int value = (bitmap[bit / 8] >> (8 - bpp - (bit & 7))) & ((1 << bpp) - 1);
    return (value * 255) / ((1 << bpp) - 1);
}

/* Draw the whole English caption horizontally, then store it rotated a
 * quarter turn counter-clockwise. LVGL object rotation needs an off-screen
 * layer and stalls this display. */
static void set_rotated_caption(int index, const char *caption, uint32_t color) {
    const int width = 56, height = 18;
    uint8_t *pixels = s_key_word_px[index];
    memset(pixels, 0, (size_t)width * height);
    const lv_font_t *font = &lv_font_montserrat_14;
    int pen = 0;
    int baseline = font->line_height - font->base_line;
    for (const char *p = caption; *p && pen < width; p++) {
        lv_font_glyph_dsc_t glyph;
        if (!lv_font_get_glyph_dsc(font, &glyph, (uint32_t)(uint8_t)*p, 0)) {
            pen += 4;
            continue;
        }
        const uint8_t *bitmap = lv_font_get_glyph_static_bitmap(&glyph);
        if (!bitmap && glyph.box_w && glyph.resolved_font && glyph.resolved_font->get_glyph_bitmap) {
            glyph.req_raw_bitmap = 1;
            bitmap = glyph.resolved_font->get_glyph_bitmap(&glyph, NULL);
        }
        if (bitmap && glyph.box_w && glyph.box_h) {
            for (int y = 0; y < glyph.box_h; y++) {
                int dest_y = baseline - glyph.box_h - glyph.ofs_y + y;
                if (dest_y < 0 || dest_y >= height) continue;
                for (int x = 0; x < glyph.box_w && pen + glyph.ofs_x + x < width; x++) {
                    int alpha = raw_alpha(bitmap, glyph.format, x, y, glyph.box_w);
                    int dest = dest_y * width + pen + glyph.ofs_x + x;
                    if (dest >= 0 && alpha > pixels[dest]) pixels[dest] = (uint8_t)alpha;
                }
            }
        }
        pen += glyph.adv_w > 0 ? glyph.adv_w : 4;
    }
    if (pen > 0 && pen < width) {
        int shift = (width - pen) / 2;
        if (shift > 0) {
            for (int y = height - 1; y >= 0; y--) {
                memmove(pixels + y * width + shift, pixels + y * width, (size_t)pen);
                memset(pixels + y * width, 0, (size_t)shift);
            }
        }
    }
    uint8_t rotated[56 * 18];
    memset(rotated, 0, sizeof(rotated));
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            rotated[(width - 1 - x) * height + y] = pixels[y * width + x];
        }
    }
    memcpy(pixels, rotated, sizeof(rotated));
    lv_image_dsc_t *dsc = &s_key_word_dsc[index];
    memset(dsc, 0, sizeof(*dsc));
    dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc->header.cf = LV_COLOR_FORMAT_A8;
    dsc->header.w = height;
    dsc->header.h = width;
    dsc->header.stride = height;
    dsc->data_size = width * height;
    dsc->data = pixels;
    lv_image_set_src(s_key_words[index], dsc);
    lv_obj_set_style_image_recolor(s_key_words[index], lv_color_hex(color), 0);
    lv_obj_set_style_image_recolor_opa(s_key_words[index], LV_OPA_COVER, 0);
    snprintf(s_key_word_text[index], sizeof(s_key_word_text[index]), "%s", caption);
}

static void show_keys(const passport_view_t *v, const passport_store_t *s) {
    bool zh = passport_lang_zh(s);
    for (int i = 0; i < 3; i++) {
        passport_key_binding_t binding = passport_key_binding(v, s, (passport_btn_t)i);
        if (s_bindings_valid && binding.action == s_last_binding[i].action &&
            binding.enabled == s_last_binding[i].enabled) continue;
        const char *caption = passport_action_label(binding.action, zh);
        if (!zh && (binding.action == PASSPORT_ACT_DISCARD || binding.action == PASSPORT_ACT_CANCEL_SEND)) caption = "Stop";
        char vertical[12];
        if (zh && strlen(caption) == 6) {
            memcpy(vertical, caption, 3); vertical[3] = '\n';
            memcpy(vertical + 4, caption + 3, 3); vertical[7] = 0; caption = vertical;
        }
        text(s_keys[i], zh ? caption : "");
        bool primary = binding.enabled && primary_action(binding.action);
        uint32_t color = primary ? CORAL : binding.enabled ? MUTED : FAINT;
        lv_obj_set_style_text_color(s_keys[i], lv_color_hex(color), 0);
        lv_obj_set_style_bg_color(s_keys[i], lv_color_hex(primary ? 0x30252B : SURFACE), 0);
        lv_obj_set_style_bg_opa(s_keys[i], binding.enabled ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(s_markers[i], lv_color_hex(color), 0);
        visible(s_markers[i], binding.action != PASSPORT_ACT_NONE);
        visible(s_keys[i], zh && binding.action != PASSPORT_ACT_NONE);
        visible(s_key_words[i], !zh && binding.action != PASSPORT_ACT_NONE);
        if (!zh && binding.action != PASSPORT_ACT_NONE) set_rotated_caption(i, caption, color);
        s_last_binding[i] = binding;
    }
    s_bindings_valid = true;
}

static void update_icons(const passport_view_t *v) {
    /* Keep the active radio indicators grouped against the battery. When BLE
     * is off, Wi-Fi takes its slot instead of leaving a visual hole. */
    lv_obj_set_x(s_wifi, v->bluetooth_ready ? 108 : 122);
    if (!s_icons_valid || s_last_wifi != v->wifi_online) { visible(s_wifi, v->wifi_online); s_last_wifi = v->wifi_online; }
    if (!s_icons_valid || s_last_bt != v->bluetooth_ready) { visible(s_bluetooth, v->bluetooth_ready); s_last_bt = v->bluetooth_ready; }
    if (!s_icons_valid || s_last_soc != v->battery_soc) {
        int soc = v->battery_soc;
        int level = soc < 0 ? 0 : soc <= 0 ? 0 : (soc + 19) / 20;
        if (level > 5) level = 5;
        uint32_t color = soc < 0 ? FAINT : soc <= 20 ? AMBER : MINT;
        for (size_t i = 0; i < 5; i++) {
            lv_obj_set_style_bg_color(s_battery_segments[i], lv_color_hex(color), 0);
            visible(s_battery_segments[i], (int)i < level);
        }
        char value[8];
        if (soc < 0) snprintf(value, sizeof(value), "--");
        else snprintf(value, sizeof(value), "%d%%", soc > 100 ? 100 : soc);
        text(s_battery_text, value); s_last_soc = soc;
    }
    s_icons_valid = true;
}

void passport_screen_open(void) {
    s_layout = s_key_language = s_tone = -1;
    s_bindings_valid = s_icons_valid = false;
    s_network_name_text[0]=0;
    lv_obj_t *screen = box(NULL, 0, 0, PASSPORT_LCD_W, PASSPORT_LCD_H, INK, 0);
    lv_obj_t *lobster = lv_image_create(screen);
    lv_image_set_src(lobster, &passport_lobster_22); lv_obj_set_pos(lobster, 6, 5);
    s_title = label(screen, &lv_font_montserrat_14, 31, 9, 76, 18, WHITE);
    lv_obj_t *title = s_title;
    text(title, "OpenClaw"); add_status_icons(screen);
    box(screen, 16, 69, 180, 1, EDGE, 0);
    s_status = label(screen, &passport_font_16, 16, 42, 124, 21, MUTED);
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_DOT);
    s_page_count = label(screen, &lv_font_montserrat_14, 143, 44, 54, 18, FAINT);
    lv_obj_set_style_text_align(s_page_count, LV_TEXT_ALIGN_RIGHT, 0);
    s_card = box(screen, 12, 79, 190, 214, SURFACE, 12);
    s_orbit = lv_arc_create(screen); lv_obj_set_pos(s_orbit, 68, 98); lv_obj_set_size(s_orbit, 80, 80);
    lv_obj_remove_flag(s_orbit, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_arc_opa(s_orbit, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_orbit, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_orbit, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_orbit, 0, LV_PART_MAIN);
    lv_obj_remove_style(s_orbit, NULL, LV_PART_KNOB);
    s_symbol = label(screen, &lv_font_montserrat_20, 86, 122, 44, 29, CORAL);
    lv_obj_set_style_text_align(s_symbol, LV_TEXT_ALIGN_CENTER, 0);
    for (size_t i = 0; i < PASSPORT_WAVEFORM_BARS; i++)
        s_waveform[i] = box(screen, 80 + (int)i * 5, 136, 3, 4, MINT, 3);
    s_headline = label(screen, &passport_font_20, 14, 191, 188, 28, WHITE);
    s_body_view = box(screen, 0, 0, PASSPORT_LCD_W, PASSPORT_LCD_H, INK, 0);
    lv_obj_set_style_bg_opa(s_body_view, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(s_body_view, LV_SCROLLBAR_MODE_OFF);
    s_body = label(s_body_view, &passport_font_16, 20, 231, 178, 62, MUTED);
    for (size_t row = 0; row < PASSPORT_REPLY_ROWS; row++) {
        s_chat_rows[row] = label(s_body_view, &passport_font_16, 13,
            77 + (int)row * (20 + PASSPORT_REPLY_LINE_SPACE), PASSPORT_CHAT_BODY_WIDTH, 20, WHITE);
        lv_obj_set_style_bg_color(s_chat_rows[row], lv_color_hex(EDGE), 0);
        visible(s_chat_rows[row], false);
    }
    s_hint = label(screen, &passport_font_16, 24, 253, 166, 40, MINT);
    s_network_name = label(screen, &passport_font_16, 24, 125, 166, 23, WHITE);
    lv_label_set_long_mode(s_network_name, LV_LABEL_LONG_DOT);
    visible(s_network_name, false);
    s_progress_track = box(screen, 13, 302, PASSPORT_REPLY_BODY_WIDTH, 2, EDGE, 1);
    s_progress_fill = box(s_progress_track, 0, 0, 1, 2, MINT, 1);
    for (size_t i = 0; i < 3; i++) {
        s_keys[i] = label(screen, &passport_font_16, 206, s_key_centres[i] - 28, 30, 56, MUTED);
        lv_obj_set_style_text_align(s_keys[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_radius(s_keys[i], 8, 0);
        lv_obj_add_event_cb(s_keys[i], draw_key_edge, LV_EVENT_DRAW_MAIN_BEGIN, NULL);
        s_key_words[i] = lv_image_create(screen);
        lv_obj_set_pos(s_key_words[i], 217, s_key_centres[i] - 28);
        lv_obj_set_size(s_key_words[i], 18, 56);
        lv_obj_remove_flag(s_key_words[i], LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        visible(s_key_words[i], false);
        /* The accent rail is the full height of the caption pill, so the
         * software control reads as one object beside the physical key. */
        s_markers[i] = box(screen, 237, s_key_centres[i] - 28, 2, 56, MUTED, 1);
    }
    passport_reply_set_glyph_width(reply_glyph_width);
    lv_screen_load(screen);
}

static layout_t mode_for(const passport_view_t *v) {
    if (v->screen == PASSPORT_SCREEN_TALK) return READER;
    if (v->screen == PASSPORT_SCREEN_CAPTURE) return CAPTURE;
    if (v->screen == PASSPORT_SCREEN_LIVE) return CALL;
    if (v->cancel_pending || v->screen == PASSPORT_SCREEN_CANCELLING) return CANCELLING;
    if (v->screen == PASSPORT_SCREEN_PROGRESS || (v->screen == PASSPORT_SCREEN_HOME && v->sending))
        return v->heard[0] ? TRANSCRIPT : WORK;
    if (v->screen == PASSPORT_SCREEN_FAILURE) return FAILURE;
    if (v->screen == PASSPORT_SCREEN_HOME) return IDLE;
    return DETAILS;
}

void passport_screen_show(const passport_view_t *v, const passport_store_t *s) {
    if (!s_status || !v || !s) return;
    layout_t mode = mode_for(v);
    bool layout_changed = s_layout != (int)mode || s_key_language != (int)passport_lang_zh(s);
    layout(mode, passport_lang_zh(s)); update_icons(v);
    bool chat = mode == READER && v->chat_visible && v->chat_count;
    bool choices = v->screen == PASSPORT_SCREEN_NETS ||
        (v->screen == PASSPORT_SCREEN_SETTINGS && s->agent_count > 0);
    visible(s_body, !chat && !choices);
    if (!chat && !choices) for (size_t row = 0; row < PASSPORT_REPLY_ROWS; row++) visible(s_chat_rows[row], false);
    bool network = v->screen == PASSPORT_SCREEN_NET_MENU;
    visible(s_network_name, network);
    /* Keep even a maximum-length SSID clear of the feedback area. */
    if (mode == DETAILS) {
        const lv_font_t *font = v->screen == PASSPORT_SCREEN_VOLUME ? &passport_font_16 : &passport_font_20;
        if (lv_obj_get_style_text_font(s_headline, 0) != font) lv_obj_set_style_text_font(s_headline, font, 0);
        lv_obj_set_y(s_body, network ? 156 : 134);
        lv_obj_set_height(s_body, network ? 94 : 116);
    }
    uint32_t tone = mode == FAILURE ? AMBER : mode == CAPTURE || mode == CALL ? CORAL : mode == CANCELLING ? MUTED : MINT;
    set_tone(tone); update_waveform(v, mode);
    char value[224];
    switch (mode) {
        case TRANSCRIPT: {
            text(s_status, tr(s, "等待回复", "WAITING"));
            const char *caption = tr(s, "你说：", "You said:");
            size_t caption_size = strlen(caption);
            if (layout_changed || strncmp(s_page_text, caption, caption_size) ||
                s_page_text[caption_size] != '\n' || strcmp(s_page_text + caption_size + 1, v->heard)) {
                snprintf(s_page_text, sizeof(s_page_text), "%s\n%s", caption, v->heard);
                lv_label_set_text(s_body, s_page_text);
            }
            break;
        }
        case READER: {
            if (chat) {
                passport_chat_page(v, s_page_text, sizeof(s_page_text), s_chat_metadata);
                for (size_t row = 0; row < PASSPORT_REPLY_ROWS; row++) {
                    const passport_chat_row_t *metadata = &s_chat_metadata[row];
                    lv_obj_t *message_row = s_chat_rows[row];
                    visible(message_row, metadata->length > 0);
                    if (!metadata->length) continue;
                    char *start = s_page_text + metadata->offset;
                    char saved = start[metadata->length];
                    start[metadata->length] = 0;
                    text(message_row, start);
                    start[metadata->length] = saved;
                    int left = metadata->assistant ? 13 : 25;
                    int top = 77 + (int)row * (20 + PASSPORT_REPLY_LINE_SPACE);
                    lv_text_align_t alignment = metadata->assistant ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_RIGHT;
                    lv_color_t color = lv_color_hex(metadata->assistant ? (metadata->caption ? MUTED : WHITE) : MINT);
                    if (lv_obj_get_x(message_row) != left) lv_obj_set_x(message_row, left);
                    if (lv_obj_get_y(message_row) != top) lv_obj_set_y(message_row, top);
                    if (lv_obj_get_style_bg_opa(message_row, 0) != LV_OPA_TRANSP)
                        lv_obj_set_style_bg_opa(message_row, LV_OPA_TRANSP, 0);
                    if (lv_obj_get_style_text_align(message_row, 0) != alignment)
                        lv_obj_set_style_text_align(message_row, alignment, 0);
                    if (!lv_color_eq(lv_obj_get_style_text_color(message_row, 0), color))
                        lv_obj_set_style_text_color(message_row, color, 0);
                }
            } else passport_reply_page_text(v, s_page_text, sizeof(s_page_text));
            int pages = passport_reply_pages(v);
            snprintf(value, sizeof(value), "%d / %d", v->reply_page + 1, pages);
            bool waiting = v->chat_pending && v->chat_count && !v->chat[v->chat_count - 1].answer[0];
            text(s_status, v->sending ? (waiting ? tr(s,"等待回复","WAITING") : tr(s,"正在回复","REPLYING"))
                : v->chat_visible ? tr(s,"对话","CHAT") : tr(s, "回复", "REPLY"));
            text(s_page_count, value);
            if (!chat) text(s_body, s_page_text);
            lv_obj_set_width(s_progress_fill, PASSPORT_REPLY_BODY_WIDTH * (v->reply_page + 1) / pages);
            break;
        }
        case CALL:
        case CAPTURE: {
            const lv_font_t *font = v->screen == PASSPORT_SCREEN_LIVE && (v->live_failed || v->live_muted) ? &passport_font_16 : &passport_font_20;
            if (lv_obj_get_style_text_font(s_headline, 0) != font) lv_obj_set_style_text_font(s_headline, font, 0);
            text(s_status, v->screen == PASSPORT_SCREEN_LIVE ? (v->live_muted ? tr(s, "通话 · 静音", "CALL / MUTED") : tr(s, "通话", "CALL")) : tr(s, "本次对话", "VOICE"));
            text(s_headline, v->screen == PASSPORT_SCREEN_LIVE ? (v->live_failed ? tr(s,"连接中断","Disconnected") : !v->live_ready ? tr(s,"正在连接","Connecting") : v->live_waiting ? tr(s,"等待回复","Thinking") : v->live_speaking ? tr(s,"正在回复","Speaking") : v->live_muted ? tr(s,"已静音","Muted") : tr(s,"正在听","Listening"))
                : v->recording ? tr(s, "正在听", "Listening") : tr(s, "准备录音", "Getting ready"));
            text(s_body, v->screen == PASSPORT_SCREEN_LIVE ? v->reply
                : v->notice[0] ? v->notice : tr(s, "停顿 2 秒自动发送\n也可点右侧发送", "Pause 2s to send.\nOr choose Send."));
            if (mode == CALL) {
                lv_obj_update_layout(s_body_view);
                int bottom = lv_obj_get_height(s_body) - lv_obj_get_height(s_body_view);
                lv_obj_scroll_to_y(s_body_view, bottom > 0 ? bottom : 0, LV_ANIM_OFF);
            }
            break;
        }
        case WORK: {
            bool waiting = strstr(v->reply, "回复") || strstr(v->reply, "处理中") || strstr(v->reply, "处理，可") || strstr(v->reply, "取回");
            text(s_status, tr(s, "本次对话", "VOICE"));
            text(s_headline, waiting ? tr(s, "等待回复", "Waiting for reply") : tr(s, "正在发送", "Sending"));
            text(s_body, v->reply); break;
        }
        case FAILURE:
            text(s_status, tr(s, "本次对话", "VOICE"));
            text(s_headline, strstr(v->reply, "已恢复") ? tr(s, "录音可重试", "Ready to retry") : tr(s, "未能发送", "Couldn't send"));
            text(s_body, !strcmp(v->reply, "发送失败，请重试") ? tr(s, "录音已保留，可重试或取消", "Your recording is kept. Retry or cancel.") : v->reply);
            break;
        case CANCELLING:
            text(s_status, tr(s, "本次对话", "VOICE"));
            text(s_headline, tr(s, "正在结束", "Finishing"));
            text(s_body, tr(s, "结束后自动返回首页", "Returning home when finished."));
            break;
        case IDLE: {
            const passport_agent_t *agent = passport_agent_active(s);
            if (agent) {
                snprintf(value, sizeof(value), tr(s, "助手：%s", "AI: %s"), agent->name);
                text(s_status, value);
            } else text(s_status, tr(s, "对话", "VOICE"));
            text(s_headline, tr(s, "想聊点什么？", "Let's talk"));
            bool local_error = strstr(v->reply,"失败") || strstr(v->reply,"不可用") || strstr(v->reply,"忙") || strstr(v->reply,"重试");
            text(s_body, v->network_state == PASSPORT_NETWORK_CONNECTING ?
                passport_network_feedback(v->network_state, passport_lang_zh(s), v->wifi_online)
                : local_error ? v->reply : v->retry_send ? tr(s, "录音已保留，可继续发送", "Your recording is ready to send.")
                : v->reply_unread ? tr(s, "有一条新回复，点右侧查看", "A new reply is ready to read.")
                : tr(s, "通话听回复，提问看文字", "Call to listen. Ask to read."));
            break;
        }
        case DETAILS:
            if (v->screen == PASSPORT_SCREEN_PREFERENCES) {
                text(s_status, tr(s, "设备设置", "SETTINGS")); text(s_headline, tr(s, "设置", "Settings"));
                const char *bluetooth = v->bluetooth_busy ? tr(s, "处理中", "Busy")
                    : v->bluetooth_ready ? tr(s, "已开启", "On") : tr(s, "已关闭", "Off");
                snprintf(value, sizeof(value), tr(s, "%s 网络与代理\n%s OpenClaw 助手\n%s 蓝牙  %s\n%s 音量  %u%%", "%s Wi-Fi & routing\n%s OpenClaw agents\n%s Bluetooth  %s\n%s Volume  %u%%"),
                    v->preference_cursor == 0 ? ">" : " ", v->preference_cursor == 1 ? ">" : " ",
                    v->preference_cursor == 2 ? ">" : " ", bluetooth, v->preference_cursor == 3 ? ">" : " ", v->speaker_volume);
                text(s_body, value);
                text(s_page_count, "");
            } else if (v->screen == PASSPORT_SCREEN_VOLUME) {
                text(s_status, tr(s, "设备设置", "SETTINGS"));
                text(s_headline, tr(s, "扬声器音量", "Speaker volume"));
                snprintf(value, sizeof(value), "%u%%\n\n%s", v->speaker_volume,
                    v->speaker_volume ? tr(s, "不影响麦克风收音", "Microphone\nunchanged") : tr(s, "回复声音已关闭", "Replies are silent"));
                text(s_body, value);
                text(s_page_count, "");
            } else if (choices) {
                bool networks = v->screen == PASSPORT_SCREEN_NETS;
                size_t count = networks ? s->count + 1 : s->agent_count;
                size_t selected = v->cursor >= 0 && (size_t)v->cursor < count ? (size_t)v->cursor : 0;
                text(s_status, networks ? tr(s, "连接设置", "CONNECTION") : tr(s, "对话设置", "ASSISTANTS"));
                text(s_headline, networks ? tr(s, "网络", "Wi-Fi") : tr(s, "助手", "Assistants"));
                show_choices(v, s);
                snprintf(value, sizeof(value), "%u / %u", (unsigned)selected + 1, (unsigned)count);
                text(s_page_count, value);
                if (networks && v->network_state != PASSPORT_NETWORK_IDLE && (int)selected == v->network_target)
                    text(s_hint, passport_network_feedback(v->network_state, passport_lang_zh(s), v->wifi_online && (int)selected == s->connected));
                else if (v->notice[0]) text(s_hint, v->notice);
                else if (networks) text(s_hint, selected == s->count
                    ? tr(s, "用手机或 Mac 添加", "Add from phone or Mac")
                    : v->wifi_online && (int)selected == s->connected ? tr(s, "已连接", "Connected") : tr(s, "未连接", "Not connected"));
                else {
                    snprintf(value, sizeof(value), "%s\n%s", (int)selected == s->active_agent
                        ? tr(s, "当前助手", "In use") : tr(s, "可切换到此助手", "Available to use"),
                        s->agents[selected].proxy.enabled ? tr(s, "经 Shadowsocks", "Via Shadowsocks") : tr(s, "直连", "Direct"));
                    text(s_hint, value);
                }
                break;
            } else if (network) {
                text(s_status, tr(s, "连接设置", "CONNECTION")); text(s_headline, tr(s, "网络", "Wi-Fi"));
                if (v->cursor >= 0 && (size_t)v->cursor < s->count) {
                    const passport_net_t *net = &s->nets[v->cursor];
                    network_name(net->ssid);
                    const char *connection = v->cursor == s->connected && v->wifi_online ? tr(s, "已连接", "Connected") : tr(s, "未连接", "Not connected");
                    if (v->cursor == v->network_target && v->network_state != PASSPORT_NETWORK_IDLE)
                        connection = passport_network_feedback(v->network_state, passport_lang_zh(s), v->wifi_online && v->cursor == s->connected);
                    snprintf(value, sizeof(value), "%s", connection);
                    text(s_body, value);
                } else {
                    network_name("");
                    text(s_body, tr(s, "尚无网络\n用手机或 Mac 添加 Wi-Fi", "No saved network.\nAdd Wi-Fi from your phone or Mac."));
                }
                text(s_page_count, "");
            } else if (v->screen == PASSPORT_SCREEN_SETTINGS) {
                text(s_status, tr(s, "对话设置", "ASSISTANTS")); text(s_headline, tr(s, "助手", "Assistant"));
                text(s_page_count, "");
                text(s_body, tr(s, "尚无助手\n\n用手机或 Mac 配置 OpenClaw", "No assistant yet.\n\nSet up OpenClaw from your phone or Mac."));
            } else {
                text(s_page_count, "");
                text(s_status, tr(s, "连接设置", "CONNECTION")); text(s_headline, tr(s, "蓝牙配置", "Bluetooth"));
                text(s_body, v->bluetooth_ready ? tr(s, "蓝牙已开启\n\n打开手机或 Mac 应用\n连接 AI Passport", "Bluetooth is on.\n\nOpen the app and connect AI Passport.")
                    : tr(s, "蓝牙已关闭\n\n点右侧开启进行配置", "Bluetooth is off.\n\nChoose On to configure."));
            }
            text(s_hint, network && v->cursor == v->network_target && v->network_state != PASSPORT_NETWORK_IDLE
                ? v->network_state == PASSPORT_NETWORK_CONNECTING ? tr(s, "可以返回，连接继续", "Back keeps connecting")
                : v->network_state == PASSPORT_NETWORK_CONNECTED ? ""
                : v->network_state == PASSPORT_NETWORK_STORAGE ? tr(s, "再次连接可重试保存", "Join again to save")
                : tr(s, "可重试或配置网络", "Retry or edit in app") : v->notice);
            break;
    }
    show_keys(v, s);
}
