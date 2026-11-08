#include "badge.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FONT_PATH "/System/Library/Fonts/Supplemental/Songti.ttc"

#define SCALE 2

static badge_store_t g_store;
static badge_view_t g_view;
static char g_form_ssid[BADGE_SSID_MAX] = "Cafe";
typedef struct {
    SDL_Texture *tex;
    int w;
    int h;
    char text[160];
    SDL_Color color;
} label_t;

#define LABEL_CACHE 48
static label_t g_labels[LABEL_CACHE];
static int g_label_count;
static SDL_Renderer *g_renderer;

static SDL_Texture *bake_label(const char *text, SDL_Color color, int *w, int *h) {
    char path[] = "/tmp/badge-label.ppm";
    char script[] = "/tmp/badge-label.py";
    FILE *fp = fopen(script, "w");
    if (fp == NULL) {
        return NULL;
    }
    fprintf(fp,
            "import sys\n"
            "from PIL import Image, ImageDraw, ImageFont\n"
            "text=open('/tmp/badge-label.txt',encoding='utf-8').read()\n"
            "font=ImageFont.truetype('%s', 18)\n"
            "box=font.getbbox(text)\n"
            "w=max(1, box[2]-box[0]+2); h=max(1, box[3]-box[1]+2)\n"
            "im=Image.new('RGB',(w,h),(%d,%d,%d))\n"
            "ImageDraw.Draw(im).text((-box[0], -box[1]), text, font=font, fill=(0,0,0))\n"
            "im.save('/tmp/badge-label.ppm')\n",
            FONT_PATH, color.r, color.g, color.b);
    fclose(fp);
    fp = fopen("/tmp/badge-label.txt", "w");
    if (fp == NULL) {
        return NULL;
    }
    fputs(text, fp);
    fclose(fp);
    int rc = system("python3 /tmp/badge-label.py");
    if (rc != 0) {
        return NULL;
    }
    fp = fopen(path, "rb");
    if (fp == NULL) {
        return NULL;
    }
    char magic[3];
    int width = 0;
    int height = 0;
    int maxv = 0;
    if (fscanf(fp, "%2s %d %d %d", magic, &width, &height, &maxv) != 4) {
        fclose(fp);
        return NULL;
    }
    fgetc(fp);
    size_t bytes = (size_t)width * (size_t)height * 3;
    uint8_t *px = malloc(bytes);
    if (px == NULL || fread(px, 1, bytes, fp) != bytes) {
        free(px);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom(
        px, width, height, 24, width * 3, SDL_PIXELFORMAT_RGB24);
    SDL_Texture *tex = NULL;
    if (surface != NULL) {
        tex = SDL_CreateTextureFromSurface(g_renderer, surface);
        SDL_FreeSurface(surface);
    }
    free(px);
    *w = width;
    *h = height;
    return tex;
}

static void draw_text(SDL_Renderer *r, int x, int y, const char *text, SDL_Color color) {
    if (text == NULL || text[0] == '\0') {
        return;
    }
    g_renderer = r;
    label_t *slot = NULL;
    for (int i = 0; i < g_label_count; i++) {
        if (strcmp(g_labels[i].text, text) == 0 &&
            g_labels[i].color.r == color.r && g_labels[i].color.g == color.g &&
            g_labels[i].color.b == color.b) {
            slot = &g_labels[i];
            break;
        }
    }
    if (slot == NULL) {
        if (g_label_count >= LABEL_CACHE) {
            return;
        }
        slot = &g_labels[g_label_count];
        snprintf(slot->text, sizeof(slot->text), "%s", text);
        slot->color = color;
        slot->tex = bake_label(text, color, &slot->w, &slot->h);
        if (slot->tex == NULL) {
            return;
        }
        g_label_count++;
    }
    SDL_Rect dst = {x, y, slot->w, slot->h};
    SDL_RenderCopy(r, slot->tex, NULL, &dst);
}

static void fill(SDL_Renderer *r, int x, int y, int w, int h, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
    SDL_Rect rect = {x, y, w, h};
    SDL_RenderFillRect(r, &rect);
}

static void row(SDL_Renderer *r, int y, const char *text, bool selected) {
    SDL_Color paper = selected ? (SDL_Color){24, 32, 28, 255}
                               : (SDL_Color){236, 228, 210, 255};
    SDL_Color ink = selected ? (SDL_Color){236, 228, 210, 255}
                             : (SDL_Color){24, 32, 28, 255};
    fill(r, 12, y, 216, 36, paper);
    draw_text(r, 22, y + 8, text, ink);
}

static void draw_wifi(SDL_Renderer *r, int x, int y, bool online) {
    SDL_Color c = online ? (SDL_Color){112, 230, 245, 255}
                         : (SDL_Color){100, 105, 102, 255};
    fill(r, x + 1, y, 16, 2, c);
    fill(r, x + 4, y + 4, 10, 2, c);
    fill(r, x + 7, y + 8, 4, 2, c);
    fill(r, x + 8, y + 12, 2, 2, c);
}

static void draw_bluetooth(SDL_Renderer *r, int x, int y, bool ready) {
    SDL_Color c = ready ? (SDL_Color){128, 191, 255, 255}
                        : (SDL_Color){100, 105, 102, 255};
    fill(r, x + 8, y + 1, 2, 16, c);
    fill(r, x + 10, y + 2, 4, 2, c);
    fill(r, x + 12, y + 4, 2, 3, c);
    fill(r, x + 10, y + 7, 3, 2, c);
    fill(r, x + 12, y + 9, 2, 3, c);
    fill(r, x + 10, y + 12, 4, 2, c);
    fill(r, x + 5, y + 3, 3, 2, c);
    fill(r, x + 5, y + 12, 3, 2, c);
}

static void draw_battery(SDL_Renderer *r, int x, int y, int soc) {
    SDL_Color white = {236, 228, 210, 255};
    SDL_Color green = {104, 227, 154, 255};
    fill(r, x, y + 2, 24, 14, white);
    fill(r, x + 2, y + 4, 20, 10, (SDL_Color){20, 24, 22, 255});
    fill(r, x + 24, y + 6, 3, 6, white);
    int width = soc < 0 ? 4 : (20 * (soc > 100 ? 100 : soc)) / 100;
    fill(r, x + 2, y + 4, width, 10, green);
}

static void draw_lobster(SDL_Renderer *r, int x, int y) {
    SDL_Color coral = {240, 90, 71, 255};
    SDL_Color dark = {182, 49, 61, 255};
    SDL_Color orange = {255, 179, 62, 255};
    fill(r, x + 24, y + 2, 3, 8, orange);
    fill(r, x + 32, y + 2, 3, 8, orange);
    fill(r, x + 20, y + 11, 18, 14, coral);
    fill(r, x + 24, y + 8, 14, 12, orange);
    fill(r, x + 8, y + 14, 10, 5, coral);
    fill(r, x + 1, y + 8, 9, 5, dark);
    fill(r, x + 39, y + 14, 10, 5, coral);
    fill(r, x + 47, y + 8, 9, 5, dark);
    fill(r, x + 22, y + 25, 18, 5, dark);
    fill(r, x + 16, y + 30, 28, 4, coral);
}

static const char *link_name(badge_link_t link) {
    switch (link) {
        case BADGE_LINK_JOINING: return "连接中";
        case BADGE_LINK_ONLINE: return "已联网";
        case BADGE_LINK_GATEWAY: return "Agent 在线";
        case BADGE_LINK_FAILED: return "失败";
        case BADGE_LINK_OFF:
        default: return "未联网";
    }
}

static void render(SDL_Renderer *r) {
    SDL_Color bg = {20, 24, 22, 255};
    SDL_Color cream = {236, 228, 210, 255};
    SDL_Color dim = {150, 146, 132, 255};
    SDL_SetRenderDrawColor(r, bg.r, bg.g, bg.b, 255);
    SDL_RenderClear(r);
    draw_text(r, 16, 12, "OpenClaw", cream);
    draw_wifi(r, 170, 12, g_view.wifi_online);
    draw_bluetooth(r, 190, 12, g_view.bluetooth_ready);
    draw_battery(r, 211, 10, g_view.battery_soc);

    if (g_view.screen == BADGE_SCREEN_HOME || g_view.screen == BADGE_SCREEN_CAPTURE || g_view.screen == BADGE_SCREEN_PROGRESS || g_view.screen == BADGE_SCREEN_FAILURE) {
        char line[96];
        const char *ssid = "无网络";
        if (g_store.connected >= 0) {
            ssid = g_store.nets[g_store.connected].name;
        }
        snprintf(line, sizeof(line), "%s  %s", link_name(g_view.link), ssid);
        draw_text(r, 16, 48, line, dim);
        fill(r, 16, 70, 168, 178, (SDL_Color){11, 46, 85, 255});
        draw_lobster(r, 26, 80);
        draw_text(r, 26, 126, g_view.recording ? "正在听" : g_view.reply, cream);
        if (g_view.recording) {
            for (int i = 0; i < BADGE_WAVEFORM_BARS; i++) {
                int h = 4 + g_view.waveform[i] / 5;
                fill(r, 28 + i * 7, 220 - h / 2, 4, h,
                     (SDL_Color){112, 230, 245, 255});
            }
        }
    } else if (g_view.screen == BADGE_SCREEN_NETS) {
        draw_text(r, 16, 44, "已保存的网络", dim);
        for (size_t i = 0; i < g_store.count; i++) {
            char line[80];
            snprintf(line, sizeof(line), "%s%s", g_store.nets[i].name,
                     (int)i == g_store.connected ? "  在用" : "");
            row(r, 72 + (int)i * 42, line, g_view.cursor == (int)i);
        }
        row(r, 72 + (int)g_store.count * 42, "添加网络",
            g_view.cursor == (int)g_store.count);
    } else if (g_view.screen == BADGE_SCREEN_NET_MENU) {
        const badge_net_t *net = &g_store.nets[g_view.cursor];
        draw_text(r, 16, 48, net->name, cream);
        draw_text(r, 16, 74, net->ssid, dim);
        draw_text(r, 16, 120, "可设为优先网络", dim);
    } else if (g_view.screen == BADGE_SCREEN_ADD) {
        char line[96];
        draw_text(r, 16, 48, "扫码打开配置页", cream);
        fill(r, 70, 96, 100, 100, cream);
        fill(r, 86, 112, 68, 68, bg);
        fill(r, 102, 128, 36, 36, cream);
        snprintf(line, sizeof(line), "SSID %s", g_form_ssid);
        draw_text(r, 16, 214, line, dim);
        draw_text(r, 16, 246, "打开配置应用进行编辑", dim);
    } else if (g_view.screen == BADGE_SCREEN_SETTINGS) {
        char line[120];
        const badge_agent_t *agent = badge_agent_active(&g_store);
        draw_text(r, 16, 48, agent != NULL ? agent->name : "尚未配置", cream);
        snprintf(line, sizeof(line), "%s", agent != NULL ? agent->url : "用手机添加 agent");
        draw_text(r, 16, 84, line, dim);
        row(r, 140, "网关已记住", g_view.cursor == 0);
        row(r, 184, "返回", g_view.cursor == 1);
    } else {
        draw_text(r, 16, 80, g_view.reply, cream);
        
    }
    fill(r,208,29,32,291,bg);
    const int centres[] = {47,163,280};
    for(int i=0;i<3;i++) {
        badge_key_binding_t binding=badge_key_binding(&g_view,&g_store,(badge_btn_t)i);
        const char *text=badge_action_label(binding.action,true);
        SDL_Color color=binding.enabled ? (i==2 ? (SDL_Color){255,179,62,255} : (SDL_Color){112,230,245,255}) : dim;
        if(strlen(text)==6) {
            char glyph[4]={0};memcpy(glyph,text,3);draw_text(r,215,centres[i]-20,glyph,color);
            memcpy(glyph,text+3,3);draw_text(r,215,centres[i],glyph,color);
        }
    }
    SDL_RenderPresent(r);
}

static void handle_key(badge_btn_t btn, badge_press_t press) {
    int selected=g_view.cursor;
    badge_action_t action=badge_on_key(&g_view,&g_store,btn,press);
    if (g_view.cancel_pending) {
        bool restart=badge_finish_cancel(&g_view,"本次操作已取消");
        if (restart) badge_on_key(&g_view,&g_store,BADGE_BTN_OK,BADGE_CLICK);
        return;
    }
    if(action==BADGE_ACT_PRIORITIZE) {
        badge_store_promote(&g_store,(size_t)selected);g_view.cursor=0;g_view.screen=BADGE_SCREEN_NETS;
    } else if(action==BADGE_ACT_SELECT_AGENT) badge_agent_select(&g_store,(size_t)selected);
    else if(action==BADGE_ACT_ADD_NETWORK || action==BADGE_ACT_CONFIGURE || action==BADGE_ACT_ENABLE_CONFIG) g_view.bluetooth_ready=true;
    else if(action==BADGE_ACT_CLOSE_CONFIG) g_view.bluetooth_ready=false;
    else if(action==BADGE_ACT_SEND || action==BADGE_ACT_RETRY) badge_finish_send(&g_view,true,false,"模拟回复：语音已发送");
}

int main(void) {
    badge_store_init(&g_store);
    badge_view_init(&g_view);
    badge_store_add(&g_store, "家里", "home-wifi", "secret");
    badge_store_add(&g_store, "手机热点", "iPhone", "hotspot");
    g_store.connected = 0;
    badge_agent_upsert(&g_store, "家里", "openclaw", "wss://gateway.example", NULL, "device", NULL,
                       NULL);
    badge_agent_upsert(&g_store, "公司", "openclaw", "wss://work.example", "boot", NULL, NULL, NULL);
    g_view.wifi_online = true;
    g_view.bluetooth_ready = true;
    g_view.battery_soc = 82;
    badge_set_status(&g_view, BADGE_LINK_GATEWAY, "Agent 在线");

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window *window = SDL_CreateWindow(
        "ai-passport-openclaw", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        BADGE_LCD_W * SCALE, BADGE_LCD_H * SCALE, SDL_WINDOW_SHOWN);
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    SDL_RenderSetLogicalSize(renderer, BADGE_LCD_W, BADGE_LCD_H);

    int running = 1;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = 0;
            } else if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
                SDL_Keycode key = event.key.keysym.sym;
                if (key == SDLK_u) {
                    printf("usb setup lives in ./sim/usb-pty\n");
                    fflush(stdout);
                }
                if (key == SDLK_ESCAPE || key == SDLK_q) {
                    running = 0;
                } else if (key == SDLK_UP) {
                    handle_key(BADGE_BTN_UP, BADGE_CLICK);
                } else if (key == SDLK_DOWN) {
                    handle_key(BADGE_BTN_DOWN, BADGE_CLICK);
                } else if (key == SDLK_RETURN) {
                    handle_key(BADGE_BTN_OK, BADGE_CLICK);
                } else if (key == SDLK_SPACE) {
                    handle_key(BADGE_BTN_OK, BADGE_CLICK);
                }
            }
        }
        render(renderer);
        SDL_Delay(16);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
