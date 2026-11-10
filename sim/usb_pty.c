#include "passport.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern int grantpt(int);
extern int unlockpt(int);
extern char *ptsname(int);

static passport_store_t g_store;
static passport_scan_t g_scan[2];

static int reply(void *ctx, const char *line) {
    int fd = *(int *)ctx;
    char buf[PASSPORT_URL_MAX + 64];
    int n = snprintf(buf, sizeof(buf), "%s\n", line);
    if (n < 0) {
        return -1;
    }
    return write(fd, buf, (size_t)n) == n ? 0 : -1;
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
    int n = passport_store_save(&g_store, blob, sizeof(blob));
    if (n < 0) {
        return;
    }
    FILE *fp = fopen("/tmp/passport-store.bin", "wb");
    if (fp == NULL) {
        return;
    }
    fwrite(blob, 1, (size_t)n, fp);
    fclose(fp);
}

int main(void) {
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0 || grantpt(master) != 0 || unlockpt(master) != 0) {
        fprintf(stderr, "pty: %s\n", strerror(errno));
        return 1;
    }
    char *name = ptsname(master);
    printf("%s\n", name);
    fflush(stdout);

    passport_store_init(&g_store);
    snprintf(g_scan[0].ssid, sizeof(g_scan[0].ssid), "home-wifi");
    g_scan[0].rssi = -48;
    snprintf(g_scan[1].ssid, sizeof(g_scan[1].ssid), "Cafe");
    g_scan[1].rssi = -61;

    char line[PASSPORT_CONFIG_LINE_MAX];
    size_t got = 0;
    while (1) {
        char ch = 0;
        ssize_t n = read(master, &ch, 1);
        if (n <= 0) {
            break;
        }
        if (ch == '\n' || ch == '\r') {
            if (got == 0) {
                continue;
            }
            line[got] = '\0';
            passport_usb_t usb;
            memset(&usb, 0, sizeof(usb));
            usb.store = &g_store;
            usb.scan = g_scan;
            usb.scan_count = 2;
            usb.reply = reply;
            usb.reply_ctx = &master;
            usb.join = join;
            uint32_t generation = g_store.generation;
            passport_usb_line(&usb, line);
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
    return 0;
}
