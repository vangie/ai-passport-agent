#include "passport.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORTAL_PORT 8787

static passport_store_t g_store;
static char g_page[8192];

static const char *json_get(const char *json, const char *key, char *out, size_t cap) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *hit = strstr(json, pattern);
    if (hit == NULL) {
        out[0] = '\0';
        return out;
    }
    hit = strchr(hit + strlen(pattern), '"');
    if (hit == NULL) {
        out[0] = '\0';
        return out;
    }
    hit++;
    size_t n = 0;
    while (*hit != '\0' && *hit != '"' && n + 1 < cap) {
        out[n++] = *hit++;
    }
    out[n] = '\0';
    return out;
}

static int send_all(int fd, const char *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, data + sent, len - sent, 0);
        if (n < 0) {
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

static void respond(int fd, int status, const char *type, const char *body) {
    char head[256];
    int n = snprintf(head, sizeof(head),
                     "HTTP/1.1 %d\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
                     "Connection: close\r\n\r\n",
                     status, type, strlen(body));
    send_all(fd, head, (size_t)n);
    send_all(fd, body, strlen(body));
}

static void handle(int fd) {
    char req[4096];
    size_t got = 0;
    while (got + 1 < sizeof(req)) {
        ssize_t n = recv(fd, req + got, sizeof(req) - 1 - got, 0);
        if (n <= 0) {
            break;
        }
        got += (size_t)n;
        req[got] = '\0';
        if (strstr(req, "\r\n\r\n") != NULL) {
            break;
        }
    }
    if (strncmp(req, "GET / ", 6) == 0 || strncmp(req, "GET /index.html", 15) == 0) {
        respond(fd, 200, "text/html; charset=utf-8", g_page);
        return;
    }
    if (strncmp(req, "POST /save", 10) == 0) {
        const char *body = strstr(req, "\r\n\r\n");
        passport_portal_form_t form;
        memset(&form, 0, sizeof(form));
        if (body != NULL) {
            body += 4;
            json_get(body, "ssid", form.ssid, sizeof(form.ssid));
            json_get(body, "pass", form.pass, sizeof(form.pass));
            json_get(body, "gateway_url", form.gateway_url, sizeof(form.gateway_url));
            json_get(body, "bootstrap", form.bootstrap, sizeof(form.bootstrap));
        }
        const char *result = passport_portal_apply(&g_store, &form);
        if (strcmp(result, "ok") == 0) {
            uint8_t blob[2048];
            int n = passport_store_save(&g_store, blob, sizeof(blob));
            FILE *fp = fopen("/tmp/passport-store.bin", "wb");
            if (fp != NULL && n > 0) {
                fwrite(blob, 1, (size_t)n, fp);
                fclose(fp);
            }
            respond(fd, 200, "application/json",
                    "{\"message\":\"已保存。回到 AI Passport 即可看到这个网络。\"}");
        } else if (strcmp(result, "url") == 0) {
            respond(fd, 400, "application/json",
                    "{\"error\":\"地址要以 ws:// 或 wss:// 开头\"}");
        } else {
            respond(fd, 400, "application/json",
                    "{\"error\":\"网络没有保存\"}");
        }
        return;
    }
    respond(fd, 404, "text/plain", "not found");
}

int main(void) {
    char qr[96];
    FILE *page = fopen("portal/index.html", "rb");
    if (page == NULL) {
        page = fopen("../portal/index.html", "rb");
    }
    if (page == NULL) {
        fprintf(stderr, "portal/index.html missing\n");
        return 1;
    }
    size_t n = fread(g_page, 1, sizeof(g_page) - 1, page);
    g_page[n] = '\0';
    fclose(page);

    FILE *blob = fopen("/tmp/passport-store.bin", "rb");
    passport_store_init(&g_store);
    if (blob != NULL) {
        uint8_t raw[2048];
        size_t len = fread(raw, 1, sizeof(raw), blob);
        fclose(blob);
        passport_store_load(&g_store, raw, len);
    }
    passport_setup_wifi_qr(qr, sizeof(qr));
    printf("setup qr: %s\n", qr);
    printf("portal http://127.0.0.1:%d\n", PORTAL_PORT);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORTAL_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        fprintf(stderr, "bind: %s\n", strerror(errno));
        return 1;
    }
    listen(fd, 4);
    for (;;) {
        int client = accept(fd, NULL, NULL);
        if (client < 0) {
            continue;
        }
        handle(client);
        close(client);
    }
}
