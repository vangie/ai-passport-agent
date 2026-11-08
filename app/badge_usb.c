#include "badge.h"
#include "badge_pair.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int percent_encode(const char *src, char *out, size_t cap) {
    static const char hex[] = "0123456789ABCDEF";
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)src; *p != '\0'; p++) {
        int plain = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                    (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' || *p == '.';
        size_t take = plain ? 1 : 3;
        if (n + take + 1 > cap) {
            return -1;
        }
        if (plain) {
            out[n++] = (char)*p;
        } else {
            out[n++] = '%';
            out[n++] = hex[*p >> 4];
            out[n++] = hex[*p & 0x0f];
        }
    }
    out[n] = '\0';
    return (int)n;
}

static int say(badge_usb_t *usb, const char *line) {
    if (usb->reply == NULL) {
        return 0;
    }
    return usb->reply(usb->reply_ctx, line);
}

static const char *field(const char *line, int index, char *out, size_t cap) {
    const char *p = line;
    for (int i = 0; i < index; i++) {
        p = strchr(p, ' ');
        if (p == NULL) {
            out[0] = '\0';
            return out;
        }
        p++;
    }
    size_t n = 0;
    while (p[n] != '\0' && p[n] != ' ' && n + 1 < cap) {
        out[n] = p[n];
        n++;
    }
    out[n] = '\0';
    return out;
}

/* Decode directly into the final field buffer. The encoded setup code can
 * expand threefold on the wire; it must not reserve that expansion on the
 * permanently resident command task's stack. */
static bool decoded_field(const char *line, int index, char *out, size_t cap) {
    const char *p = line;
    for (int i=0;i<index;i++) {
        p = strchr(p,' ');
        if (!p) { out[0]=0; return true; }
        p++;
    }
    size_t n=0;
    while (*p && *p!=' ') {
        unsigned char c=(unsigned char)*p++;
        if (c=='%' && p[0] && p[1]) {
            unsigned value;
            if (sscanf(p,"%2x",&value)==1) { c=(unsigned char)value; p+=2; }
        }
        if (!c || n+1>=cap) { out[0]=0; return false; }
        out[n++]=(char)c;
    }
    out[n]=0; return true;
}

static void percent_decode(char *s) {
    char *r = s;
    char *w = s;
    while (*r != '\0') {
        if (*r == '%' && r[1] != '\0' && r[2] != '\0') {
            unsigned int v = 0;
            if (sscanf(r + 1, "%2x", &v) == 1) {
                *w++ = (char)v;
                r += 3;
                continue;
            }
        }
        *w++ = *r++;
    }
    *w = '\0';
}

/* 密码留空：已经记住的网络沿用原密码，新网络当作开放网络。 */
static void known_pass(const badge_store_t *store, const char *ssid, char *pass, size_t cap) {
    /* "-" means "keep the saved one", as for other secrets. Storing it
     * literally left the badge unable to rejoin after a reboot. */
    if (!strcmp(pass, "-")) pass[0] = '\0';
    if (pass[0] != '\0') {
        return;
    }
    for (size_t i = 0; i < store->count; i++) {
        if (strcmp(store->nets[i].ssid, ssid) == 0) {
            snprintf(pass, cap, "%s", store->nets[i].pass);
            return;
        }
    }
}

void badge_usb_line(badge_usb_t *usb, const char *line) {
    char cmd[16];
    if (usb == NULL || usb->store == NULL || line == NULL) {
        return;
    }
    while (*line == ' ') {
        line++;
    }
    field(line, 0, cmd, sizeof(cmd));
    if (strcmp(cmd, "HELLO") == 0) {
        char out[192];
        snprintf(out, sizeof(out), "HELLO ai-passport-openclaw %lu %zu %zu %s %s endpoint-move gateway-route",
                 (unsigned long)usb->store->generation, usb->store->count,
                 usb->store->agent_count, usb->store->lang,
                 usb->build != NULL && usb->build[0] != '\0' ? usb->build : "-");
        say(usb, out);
        return;
    }
    if (strcmp(cmd, "NETS") == 0) {
        char head[48];
        snprintf(head, sizeof(head), "NETS %lu %zu %d",
                 (unsigned long)usb->store->generation, usb->store->count, usb->store->connected);
        say(usb, head);
        for (size_t i = 0; i < usb->store->count; i++) {
            char name[BADGE_NAME_MAX * 3];
            char ssid[BADGE_SSID_MAX * 3];
            char row[BADGE_NAME_MAX * 3 + BADGE_SSID_MAX * 3 + 24];
            percent_encode(usb->store->nets[i].name, name, sizeof(name));
            percent_encode(usb->store->nets[i].ssid, ssid, sizeof(ssid));
            snprintf(row, sizeof(row), "SAVED %zu %s %s", i, name, ssid);
            say(usb, row);
        }
        say(usb, "NETS END");
        return;
    }
    if (strcmp(cmd, "AGENTS") == 0) {
        char head[48];
        snprintf(head, sizeof(head), "AGENTS %lu %zu %d",
                 (unsigned long)usb->store->generation, usb->store->agent_count,
                 usb->store->active_agent);
        say(usb, head);
        for (size_t i = 0; i < usb->store->agent_count; i++) {
            const badge_agent_t *agent = &usb->store->agents[i];
            char name[BADGE_NAME_MAX * 3];
            char url[BADGE_URL_MAX * 3];
            char row[BADGE_NAME_MAX * 3 + BADGE_URL_MAX * 3 + 48];
            const char *state = agent->device_token[0] != '\0' ? "ready"
                                : agent->bootstrap[0] != '\0'   ? "pending"
                                                                 : "none";
            percent_encode(agent->name, name, sizeof(name));
            percent_encode(agent->url, url, sizeof(url));
            snprintf(row, sizeof(row), "AGENT %zu %s %s %s", i, name, url, state);
            say(usb, row);
            /* The Shadowsocks password is write-only, like gateway tokens. */
            const badge_proxy_t *proxy = &agent->proxy;
            snprintf(row, sizeof(row), "ROUTE %zu %s %s %u %u %s", i, proxy->enabled ? "ss" : "direct",
                     proxy->host[0] ? proxy->host : "-", (unsigned)proxy->port, proxy->secret[0] ? 1u : 0u,
                     badge_proxy_method_name(proxy->method));
            say(usb, row);
        }
        say(usb, "AGENTS END");
        return;
    }
    if (strcmp(cmd, "LANG") == 0) {
        char generation_text[16];
        char lang[8];
        field(line, 1, generation_text, sizeof(generation_text));
        field(line, 2, lang, sizeof(lang));
        if (strtoul(generation_text, NULL, 10) != usb->store->generation) {
            say(usb, "ERR stale");
            return;
        }
        if (!badge_lang_set(usb->store, lang)) {
            say(usb, "ERR lang");
            return;
        }
        badge_store_bump(usb->store);
        say(usb, "OK lang");
        return;
    }
    if (strcmp(cmd, "AGENT") == 0 || strcmp(cmd, "RENAME") == 0 || strcmp(cmd, "ENDPOINT") == 0 ||
        strcmp(cmd, "ROUTE") == 0 ||
        strcmp(cmd, "USE") == 0 || strcmp(cmd, "DROP") == 0 || strcmp(cmd, "FORGET") == 0) {
        char generation_text[16];
        field(line, 1, generation_text, sizeof(generation_text));
        if (strtoul(generation_text, NULL, 10) != usb->store->generation) {
            say(usb, "ERR stale");
            return;
        }
    }
    if (strcmp(cmd, "AGENT") == 0) {
        char name[BADGE_NAME_MAX * 3];
        char code[BADGE_PAIR_MAX];
        badge_pair_t pair;
        const char *why;
        field(line, 2, name, sizeof(name));
        if (!decoded_field(line,3,code,sizeof(code))) { say(usb,"ERR code"); return; }
        percent_decode(name);

        why = badge_pair_parse(code, &pair);
        if (why != NULL) {
            char err[32];
            snprintf(err, sizeof(err), "ERR %s", why);
            say(usb, err);
            return;
        }
        if (name[0] == '\0') {
            memcpy(name, pair.host, sizeof(name) - 1);
            name[sizeof(name) - 1] = '\0';
        }
        if (!badge_agent_upsert(usb->store, name, "openclaw", pair.url, pair.bootstrap, "", "",
                                "")) {
            say(usb, "ERR agent");
            return;
        }
        /* 独立写入 OpenClaw 也要成为当前助手；合并 SAVE 已有同样语义。 */
        for (size_t i = 0; i < usb->store->agent_count; i++) {
            if (strcmp(usb->store->agents[i].name, name) == 0) {
                usb->store->active_agent = (int)i;
                break;
            }
        }
        badge_store_bump(usb->store);
        say(usb, "OK agent");
        return;
    }
    if (strcmp(cmd, "USE") == 0) {
        char index_text[8];
        field(line, 2, index_text, sizeof(index_text));
        if (!badge_agent_select(usb->store, (size_t)atoi(index_text))) {
            say(usb, "ERR agent");
            return;
        }
        badge_store_bump(usb->store);
        say(usb, "OK use");
        return;
    }
    if (strcmp(cmd, "ENDPOINT") == 0) {
        char index_text[8],expected[BADGE_URL_MAX],url[BADGE_URL_MAX];
        if (!decoded_field(line,2,index_text,sizeof(index_text))) { say(usb,"ERR url");return; }
        char *end;
        unsigned long index=strtoul(index_text,&end,10);
        if (index_text[0]<'0' || index_text[0]>'9' || *end || index>=usb->store->agent_count ||
            !decoded_field(line,3,expected,sizeof(expected)) ||
            !decoded_field(line,4,url,sizeof(url)) ||
            !badge_agent_move_endpoint(usb->store,(size_t)index,expected,url)) {
            say(usb,"ERR url");return;
        }
        badge_store_bump(usb->store);
        say(usb,"OK endpoint");return;
    }
    if (strcmp(cmd, "ROUTE") == 0) {
        /* ROUTE <gen> <index> direct | ss <host> <port> <password|-> [method]
         * "-" keeps the saved password; a missing method keeps the saved one;
         * direct keeps the endpoint for later. */
        char index_text[8], mode[8], port_text[8], secret[BADGE_PROXY_SECRET_MAX], method[32];
        badge_proxy_t proxy = {0};
        field(line, 2, index_text, sizeof(index_text));
        field(line, 3, mode, sizeof(mode));
        char *end;
        unsigned long index = strtoul(index_text, &end, 10);
        bool ok = index_text[0] >= '0' && index_text[0] <= '9' && !*end && index < usb->store->agent_count;
        if (ok && !strcmp(mode, "direct")) {
            proxy = usb->store->agents[index].proxy;
            proxy.enabled = false;
            ok = badge_agent_set_proxy(usb->store, (size_t)index, &proxy, NULL);
        } else if (ok && !strcmp(mode, "ss")) {
            field(line, 5, port_text, sizeof(port_text));
            unsigned long port = strtoul(port_text, &end, 10);
            ok = port_text[0] && !*end && port > 0 && port <= 65535 &&
                 decoded_field(line, 4, proxy.host, sizeof(proxy.host)) &&
                 decoded_field(line, 6, secret, sizeof(secret));
            proxy.port = (uint16_t)port;
            proxy.enabled = true;
            field(line, 7, method, sizeof(method));
            int id = method[0] ? badge_proxy_method_id(method) : usb->store->agents[index].proxy.method;
            ok = ok && id >= 0;
            proxy.method = (uint8_t)(id < 0 ? 0 : id);
            ok = ok && badge_agent_set_proxy(usb->store, (size_t)index, &proxy,
                                             strcmp(secret, "-") ? secret : NULL);
        } else ok = false;
        if (!ok) { say(usb, "ERR proxy"); return; }
        badge_store_bump(usb->store);
        say(usb, "OK route");
        return;
    }
    if (strcmp(cmd, "RENAME") == 0) {
        char index_text[8], name[BADGE_NAME_MAX];
        if (!decoded_field(line, 2, index_text, sizeof(index_text))) {
            say(usb, "ERR name");
            return;
        }
        char *end;
        unsigned long index = strtoul(index_text, &end, 10);
        if (index_text[0] < '0' || index_text[0] > '9' || *end ||
            index >= usb->store->agent_count ||
            !decoded_field(line, 3, name, sizeof(name)) ||
            !badge_agent_rename(usb->store, (size_t)index, name)) {
            say(usb, "ERR name");
            return;
        }
        badge_store_bump(usb->store);
        say(usb, "OK rename");
        return;
    }
    if (strcmp(cmd, "DROP") == 0) {
        char index_text[8];
        field(line, 2, index_text, sizeof(index_text));
        if (!badge_agent_drop(usb->store, (size_t)atoi(index_text))) {
            say(usb, "ERR agent");
            return;
        }
        badge_store_bump(usb->store);
        say(usb, "OK drop");
        return;
    }
    if (strcmp(cmd, "FORGET") == 0) {
        char index_text[8];
        field(line, 2, index_text, sizeof(index_text));
        if (!badge_store_forget(usb->store, (size_t)atoi(index_text))) {
            say(usb, "ERR net");
            return;
        }
        badge_store_bump(usb->store);
        say(usb, "OK forget");
        return;
    }
    if (strcmp(cmd, "TRY") == 0) {
        /* 只试连，不保存，不需要版本号。 */
        char ssid[BADGE_SSID_MAX * 3];
        char pass[BADGE_PASS_MAX * 3];
        field(line, 1, ssid, sizeof(ssid));
        field(line, 2, pass, sizeof(pass));
        percent_decode(ssid);
        percent_decode(pass);
        if (ssid[0] == '\0' || usb->join == NULL) {
            say(usb, "TRY fail fail");
            return;
        }
        known_pass(usb->store, ssid, pass, sizeof(pass));
        const char *why = usb->join(usb->join_ctx, ssid, pass, false);
        char out[32];
        snprintf(out, sizeof(out), why == NULL ? "TRY ok" : "TRY fail %s", why);
        say(usb, out);
        return;
    }
    if (strcmp(cmd, "SCAN") == 0) {
        if (usb->scan == NULL) {
            say(usb, "SCAN 0");
            return;
        }
        char head[32];
        snprintf(head, sizeof(head), "SCAN %zu", usb->scan_count);
        say(usb, head);
        for (size_t i = 0; i < usb->scan_count && i < BADGE_SCAN_MAX; i++) {
            char row[BADGE_SSID_MAX + 24];
            snprintf(row, sizeof(row), "NET %d %s", usb->scan[i].rssi, usb->scan[i].ssid);
            say(usb, row);
        }
        say(usb, "SCAN END");
        return;
    }
    if (strcmp(cmd, "SAVE") == 0) {
        char generation_text[16];
        field(line, 1, generation_text, sizeof(generation_text));
        if (strtoul(generation_text, NULL, 10) != usb->store->generation) {
            say(usb, "ERR stale");
            return;
        }
        badge_portal_form_t form;
        char ssid[BADGE_SSID_MAX * 3];
        char pass[BADGE_PASS_MAX * 3];
        char code[BADGE_PAIR_MAX];
        char name[BADGE_NAME_MAX * 3];
        badge_pair_t pair;
        memset(&form, 0, sizeof(form));
        field(line, 2, ssid, sizeof(ssid));
        field(line, 3, pass, sizeof(pass));
        if (!decoded_field(line,4,code,sizeof(code))) { say(usb,"ERR code"); return; }
        field(line, 5, name, sizeof(name));
        percent_decode(ssid);
        percent_decode(pass);

        percent_decode(name);
        memcpy(form.ssid, ssid, sizeof(form.ssid) - 1);
        memcpy(form.pass, pass, sizeof(form.pass) - 1);
        memcpy(form.agent_name, name, sizeof(form.agent_name) - 1);
        if (code[0] != '\0') {
            const char *why = badge_pair_parse(code, &pair);
            if (why != NULL) {
                char err[32];
                snprintf(err, sizeof(err), "ERR %s", why);
                say(usb, err);
                return;
            }
            memcpy(form.gateway_url, pair.url, sizeof(form.gateway_url) - 1);
            memcpy(form.bootstrap, pair.bootstrap, sizeof(form.bootstrap) - 1);
            if (form.agent_name[0] == '\0') {
                memcpy(form.agent_name, pair.host, sizeof(form.agent_name) - 1);
                form.agent_name[sizeof(form.agent_name) - 1] = '\0';
            }
        }
        known_pass(usb->store, form.ssid, form.pass, sizeof(form.pass));
        const char *why = usb->join != NULL ? usb->join(usb->join_ctx, form.ssid, form.pass, true) : NULL;
        if (why != NULL) {
            char err[32];
            snprintf(err, sizeof(err), "ERR join %s", why);
            say(usb, err);
            return;
        }
        const char *result = badge_portal_apply(usb->store, &form);
        if (strcmp(result, "ok") == 0) {
            badge_store_bump(usb->store);
            say(usb, "OK saved");
        } else {
            char err[32];
            snprintf(err, sizeof(err), "ERR %s", result);
            say(usb, err);
        }
        return;
    }
    say(usb, "ERR command");
}
