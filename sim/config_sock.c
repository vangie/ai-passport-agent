#include "badge.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define SOCK_PATH "/tmp/badge-config.sock"

static badge_store_t g_store;
static badge_scan_t g_scan[2];
static int g_client = -1;

static int reply(void *ctx, const char *line) {
    (void)ctx;
    char buf[BADGE_URL_MAX + 64];
    int n = snprintf(buf, sizeof(buf), "%s\n", line);
    if (n < 0 || g_client < 0) {
        return -1;
    }
    return write(g_client, buf, (size_t)n) == n ? 0 : -1;
}

/* 替身：密码写 wrong 时当作密码不对。 */
static const char *join(void *ctx, const char *ssid, const char *pass, bool keep) {
    (void)ctx;
    (void)keep;
    usleep(600000);
    if (ssid == NULL || ssid[0] == '\0') {
        return "notfound";
    }
    return strcmp(pass, "wrong") == 0 ? "pass" : NULL;
}

static void persist(void) {
    uint8_t blob[2048];
    int n = badge_store_save(&g_store, blob, sizeof(blob));
    FILE *fp = n > 0 ? fopen("/tmp/badge-store.bin", "wb") : NULL;
    if (fp != NULL) {
        fwrite(blob, 1, (size_t)n, fp);
        fclose(fp);
    }
}

static void serve(int client) {
    char line[BADGE_CONFIG_LINE_MAX];
    size_t got = 0;
    g_client = client;
    while (1) {
        char ch = 0;
        ssize_t n = read(client, &ch, 1);
        if (n <= 0) {
            break;
        }
        if (ch == '\n' || ch == '\r') {
            if (got == 0) {
                continue;
            }
            line[got] = '\0';
            badge_usb_t usb;
            memset(&usb, 0, sizeof(usb));
            usb.store = &g_store;
            usb.scan = g_scan;
            usb.scan_count = 2;
            usb.reply = reply;
            usb.join = join;
            uint32_t generation = g_store.generation;
            badge_usb_line(&usb, line);
            if (g_store.generation != generation) {
                persist();
            }
            got = 0;
            continue;
        }
        if (got + 1 < sizeof(line)) {
            line[got++] = ch;
        }
    }
    g_client = -1;
    close(client);
}

int main(void) {
    badge_store_init(&g_store);
    snprintf(g_scan[0].ssid, sizeof(g_scan[0].ssid), "home-wifi");
    g_scan[0].rssi = -48;
    snprintf(g_scan[1].ssid, sizeof(g_scan[1].ssid), "Cafe");
    g_scan[1].rssi = -61;

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", SOCK_PATH);
    unlink(SOCK_PATH);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(fd, 1) != 0) {
        fprintf(stderr, "socket: %s\n", strerror(errno));
        return 1;
    }
    printf("%s\n", SOCK_PATH);
    fflush(stdout);
    while (1) {
        int client = accept(fd, NULL, NULL);
        if (client < 0) {
            continue;
        }
        serve(client);
    }
}
