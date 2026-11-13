#include "passport.h"

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define PASSPORT_BLOB_MAGIC 0x31505350u /* PSP1 */

static void copy_cap(char *dst, size_t cap, const char *src) {
    if (src == NULL) {
        src = "";
    }
    snprintf(dst, cap, "%s", src);
}

void passport_store_init(passport_store_t *store) {
    memset(store, 0, sizeof(*store));
    store->connected = -1;
    store->active_agent = -1;
    store->generation = 1;
    snprintf(store->lang, sizeof(store->lang), "zh");
}

bool passport_lang_zh(const passport_store_t *store) {
    return store == NULL || strcmp(store->lang, "en") != 0;
}

bool passport_lang_set(passport_store_t *store, const char *lang) {
    if (store == NULL || lang == NULL) {
        return false;
    }
    if (strcmp(lang, "zh") != 0 && strcmp(lang, "en") != 0) {
        return false;
    }
    snprintf(store->lang, sizeof(store->lang), "%s", lang);
    return true;
}

bool passport_store_add(passport_store_t *store, const char *name, const char *ssid,
                     const char *pass) {
    if (store == NULL || ssid == NULL || ssid[0] == '\0') {
        return false;
    }
    for (size_t i = 0; i < store->count; i++) {
        if (strcmp(store->nets[i].ssid, ssid) == 0) {
            copy_cap(store->nets[i].pass, sizeof(store->nets[i].pass), pass);
            if (name != NULL && name[0] != '\0') {
                copy_cap(store->nets[i].name, sizeof(store->nets[i].name), name);
            }
            store->nets[i].enabled = true;
            return true;
        }
    }
    if (store->count >= PASSPORT_NET_MAX) {
        return false;
    }
    passport_net_t *net = &store->nets[store->count];
    memset(net, 0, sizeof(*net));
    copy_cap(net->name, sizeof(net->name),
             (name != NULL && name[0] != '\0') ? name : ssid);
    copy_cap(net->ssid, sizeof(net->ssid), ssid);
    copy_cap(net->pass, sizeof(net->pass), pass);
    net->priority = (int)store->count;
    net->enabled = true;
    store->count++;
    return true;
}

bool passport_store_forget(passport_store_t *store, size_t index) {
    if (store == NULL || index >= store->count) {
        return false;
    }
    if ((int)index < store->connected) {
        store->connected--;
    } else if ((int)index == store->connected) {
        store->connected = -1;
    }
    for (size_t i = index; i + 1 < store->count; i++) {
        store->nets[i] = store->nets[i + 1];
    }
    store->count--;
    memset(&store->nets[store->count], 0, sizeof(store->nets[0]));
    return true;
}

bool passport_store_promote(passport_store_t *store, size_t index) {
    if (store == NULL || index >= store->count || index == 0) {
        return index < store->count;
    }
    passport_net_t moved = store->nets[index];
    for (size_t i = index; i > 0; i--) {
        store->nets[i] = store->nets[i - 1];
    }
    store->nets[0] = moved;
    for (size_t i = 0; i < store->count; i++) {
        store->nets[i].priority = (int)i;
    }
    if (store->connected == (int)index) {
        store->connected = 0;
    } else if (store->connected >= 0 && store->connected < (int)index) {
        store->connected++;
    }
    return true;
}

static bool url_ok(const char *url) {
    return strncmp(url, "wss://", 6) == 0 || strncmp(url, "ws://", 5) == 0;
}

bool passport_agent_set_proxy(passport_store_t *store, size_t index, const passport_proxy_t *proxy,
                           const char *secret) {
    if (!store || !proxy || index >= store->agent_count) return false;
    passport_proxy_t next = *proxy;
    if (secret) snprintf(next.secret, sizeof(next.secret), "%s", secret);
    else memcpy(next.secret, store->agents[index].proxy.secret, sizeof(next.secret));
    if (!next.enabled && !next.host[0]) memset(&next, 0, sizeof(next));
    if (!passport_proxy_valid(&next)) return false;
    store->agents[index].proxy = next;
    return true;
}

static void copy_secret(char *dest, size_t cap, const char *value, const char *kept) {
    if (value == NULL) {
        snprintf(dest, cap, "%s", kept != NULL ? kept : "");
        return;
    }
    snprintf(dest, cap, "%s", value);
}

bool passport_agent_upsert(passport_store_t *store, const char *name, const char *kind,
                        const char *url, const char *bootstrap, const char *device_token,
                        const char *access_id, const char *access_secret) {
    if (store == NULL || name == NULL || name[0] == '\0' || kind == NULL ||
        (strcmp(kind, "openclaw") && strcmp(kind, "hermes")) || url == NULL || !url_ok(url)) {
        return false;
    }
    for (size_t i = 0; i < store->agent_count; i++) {
        passport_agent_t *agent = &store->agents[i];
        if (strcmp(agent->name, name) == 0) {
            snprintf(agent->kind, sizeof(agent->kind), "%s", kind);
            snprintf(agent->url, sizeof(agent->url), "%s", url);
            copy_secret(agent->bootstrap, sizeof(agent->bootstrap), bootstrap, agent->bootstrap);
            copy_secret(agent->device_token, sizeof(agent->device_token), device_token,
                        agent->device_token);
            copy_secret(agent->access_id, sizeof(agent->access_id), access_id, agent->access_id);
            copy_secret(agent->access_secret, sizeof(agent->access_secret), access_secret,
                        agent->access_secret);
            return true;
        }
    }
    if (store->agent_count >= PASSPORT_AGENT_MAX) {
        return false;
    }
    passport_agent_t *agent = &store->agents[store->agent_count];
    memset(agent, 0, sizeof(*agent));
    snprintf(agent->name, sizeof(agent->name), "%s", name);
    snprintf(agent->kind, sizeof(agent->kind), "%s", kind);
    snprintf(agent->url, sizeof(agent->url), "%s", url);
    copy_secret(agent->bootstrap, sizeof(agent->bootstrap), bootstrap, "");
    copy_secret(agent->device_token, sizeof(agent->device_token), device_token, "");
    copy_secret(agent->access_id, sizeof(agent->access_id), access_id, "");
    copy_secret(agent->access_secret, sizeof(agent->access_secret), access_secret, "");
    if (store->active_agent < 0) {
        store->active_agent = (int)store->agent_count;
    }
    store->agent_count++;
    return true;
}

const char *passport_agent_platform(const passport_agent_t *agent) {
    return !agent ? "Agent" : !strcmp(agent->kind, "hermes") ? "Hermes" : !strcmp(agent->kind, "openclaw") ? "OpenClaw" : "Agent";
}

void passport_store_bump(passport_store_t *store) {
    if (store != NULL && store->generation < UINT32_MAX) {
        store->generation++;
    }
}

bool passport_agent_drop(passport_store_t *store, size_t index) {
    if (store == NULL || index >= store->agent_count) {
        return false;
    }
    if ((int)index < store->active_agent) {
        store->active_agent--;
    } else if ((int)index == store->active_agent) {
        store->active_agent = store->agent_count > 1 ? 0 : -1;
    }
    for (size_t i = index; i + 1 < store->agent_count; i++) {
        store->agents[i] = store->agents[i + 1];
    }
    store->agent_count--;
    memset(&store->agents[store->agent_count], 0, sizeof(store->agents[0]));
    if (store->active_agent >= (int)store->agent_count) {
        store->active_agent = store->agent_count == 0 ? -1 : 0;
    }
    return true;
}

bool passport_agent_select(passport_store_t *store, size_t index) {
    if (store == NULL || index >= store->agent_count) {
        return false;
    }
    store->active_agent = (int)index;
    return true;
}

bool passport_agent_rename(passport_store_t *store, size_t index, const char *name) {
    if (store == NULL || index >= store->agent_count || name == NULL || name[0] == '\0' ||
        strlen(name) >= PASSPORT_NAME_MAX) return false;
    for (size_t i = 0; i < store->agent_count; i++) {
        if (i != index && strcmp(store->agents[i].name, name) == 0) return false;
    }
    snprintf(store->agents[index].name, sizeof(store->agents[index].name), "%s", name);
    return true;
}

static bool secure_endpoint_ok(const char *url) {
    if (!url || strlen(url) >= PASSPORT_URL_MAX || strncmp(url,"wss://",6)) return false;
    for (const unsigned char *p=(const unsigned char *)url;*p;p++)
        if (*p<=32 || *p>=127 || *p=='@' || *p=='#' || *p=='?' || *p=='\\') return false;
    const char *p=url+6, *host=p;
    while ((*p>='a' && *p<='z') || (*p>='A' && *p<='Z') ||
           (*p>='0' && *p<='9') || *p=='.' || *p=='-') p++;
    if (p==host || (size_t)(p-host)>=96 || *host=='.' || *host=='-' || p[-1]=='.' || p[-1]=='-') return false;
    if (*p==':') {
        const char *digits=++p;unsigned port=0;
        while (*p>='0' && *p<='9') {
            port=port*10+(unsigned)(*p++-'0');
            if (port>65535) return false;
        }
        if (p==digits || !port) return false;
    }
    return !*p || *p=='/';
}

bool passport_agent_move_endpoint(passport_store_t *store, size_t index,
                               const char *expected_url, const char *url) {
    if (!store || index>=store->agent_count || !expected_url || !secure_endpoint_ok(url)) return false;
    passport_agent_t *agent=&store->agents[index];
    if (strcmp(agent->url,expected_url) || strncmp(agent->url,"wss://",6) ||
        !agent->device_token[0] || agent->bootstrap[0]) return false;
    snprintf(agent->url,sizeof(agent->url),"%s",url);
    return true;
}

const passport_agent_t *passport_agent_active(const passport_store_t *store) {
    if (store == NULL || store->active_agent < 0 ||
        (size_t)store->active_agent >= store->agent_count) {
        return NULL;
    }
    return &store->agents[store->active_agent];
}

int passport_setup_wifi_qr(char *out, size_t cap) {
    return snprintf(out, cap, "WIFI:T:WPA;S:%s;P:%s;;", PASSPORT_AP_SSID, PASSPORT_AP_PASS);
}

const char *passport_portal_apply(passport_store_t *store, const passport_portal_form_t *form) {
    if (store == NULL || form == NULL || form->ssid[0] == '\0') {
        return "ssid";
    }
    if (form->gateway_url[0] != '\0') {
        const passport_agent_t *current = passport_agent_active(store);
        const char *name = form->agent_name;
        const char *kind = form->agent_kind;
        if (name[0] == '\0') {
            name = current != NULL ? current->name : "OpenClaw";
        }
        if (kind[0] == '\0') {
            kind = current != NULL ? current->kind : "openclaw";
        }
        if (!passport_agent_upsert(store, name, kind, form->gateway_url,
                                form->bootstrap[0] != '\0' ? form->bootstrap : NULL, NULL,
                                form->access_id[0] != '\0' ? form->access_id : NULL,
                                form->access_secret[0] != '\0' ? form->access_secret : NULL)) {
            return "url";
        }
        /* 配置页写下的这一套就是接下来要用的。 */
        for (size_t i = 0; i < store->agent_count; i++) {
            if (strcmp(store->agents[i].name, name) == 0) {
                store->active_agent = (int)i;
                break;
            }
        }
    }
    if (!passport_portal_save(store, form->ssid, form->pass)) {
        return "full";
    }
    return "ok";
}

bool passport_portal_save(passport_store_t *store, const char *ssid, const char *pass) {
    if (!passport_store_add(store, ssid, ssid, pass)) {
        return false;
    }
    for (size_t i = 0; i < store->count; i++) {
        if (strcmp(store->nets[i].ssid, ssid) == 0) {
            store->connected = (int)i;
            return true;
        }
    }
    return false;
}

static void put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
    p[2] = (uint8_t)((v >> 16) & 0xff);
    p[3] = (uint8_t)((v >> 24) & 0xff);
}

static uint32_t get_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

int passport_store_save(const passport_store_t *store, uint8_t *out, size_t cap) {
    if (store == NULL || out == NULL || cap < 16) {
        return -1;
    }
    size_t need = 16;
    for (size_t i = 0; i < store->count; i++) {
        need += 2 + strlen(store->nets[i].name) + 1 + strlen(store->nets[i].ssid) +
                1 + strlen(store->nets[i].pass);
    }
    for (size_t i = 0; i < store->agent_count; i++) {
        need += strlen(store->agents[i].name) + 1 + strlen(store->agents[i].kind) + 1 +
                strlen(store->agents[i].url) + 1 + strlen(store->agents[i].bootstrap) + 1 +
                strlen(store->agents[i].device_token) + 1 + strlen(store->agents[i].access_id) + 1 +
                strlen(store->agents[i].access_secret) + 1;
    }
    /* Optional trailing extension keeps PSP1 Wi-Fi/agent records compatible. */
    need += 5;
    for (size_t i=0;i<store->agent_count;i++) {
        const passport_proxy_t *proxy=&store->agents[i].proxy;
        if (!passport_proxy_valid(proxy)) return -1;
        need += 6 + strlen(proxy->host) + strlen(proxy->secret);
    }
    if (need > cap || need > 65535) {
        return -1;
    }
    put_u32(out, PASSPORT_BLOB_MAGIC);
    put_u32(out + 4, store->generation);
    out[8] = (uint8_t)store->count;
    out[9] = store->connected < 0 ? 0xff : (uint8_t)store->connected;
    out[10] = (uint8_t)store->agent_count;
    out[11] = store->active_agent < 0 ? 0xff : (uint8_t)store->active_agent;
    size_t lang_len = strlen(store->lang);
    memcpy(out + 12, store->lang, lang_len + 1);
    size_t at = 12 + lang_len + 1;
    for (size_t i = 0; i < store->count; i++) {
        const passport_net_t *net = &store->nets[i];
        out[at++] = net->enabled ? 1 : 0;
        out[at++] = (uint8_t)net->priority;
        size_t nlen = strlen(net->name);
        size_t slen = strlen(net->ssid);
        size_t plen = strlen(net->pass);
        memcpy(out + at, net->name, nlen + 1);
        at += nlen + 1;
        memcpy(out + at, net->ssid, slen + 1);
        at += slen + 1;
        memcpy(out + at, net->pass, plen + 1);
        at += plen + 1;
    }
    for (size_t i = 0; i < store->agent_count; i++) {
        const passport_agent_t *agent = &store->agents[i];
        size_t alen = strlen(agent->name);
        size_t klen = strlen(agent->kind);
        size_t ulen = strlen(agent->url);
        size_t blen = strlen(agent->bootstrap);
        size_t dlen = strlen(agent->device_token);
        size_t ilen = strlen(agent->access_id);
        size_t slen = strlen(agent->access_secret);
        memcpy(out + at, agent->name, alen + 1);
        at += alen + 1;
        memcpy(out + at, agent->kind, klen + 1);
        at += klen + 1;
        memcpy(out + at, agent->url, ulen + 1);
        at += ulen + 1;
        memcpy(out + at, agent->bootstrap, blen + 1);
        at += blen + 1;
        memcpy(out + at, agent->device_token, dlen + 1);
        at += dlen + 1;
        memcpy(out + at, agent->access_id, ilen + 1);
        at += ilen + 1;
        memcpy(out + at, agent->access_secret, slen + 1);
        at += slen + 1;
    }
    memcpy(out+at,"GSS2",4);at+=4;
    out[at++]=(uint8_t)store->agent_count;
    for (size_t i=0;i<store->agent_count;i++) {
        const passport_proxy_t *p=&store->agents[i].proxy;
        out[at++]=p->enabled?1:0;
        out[at++]=p->method;
        out[at++]=(uint8_t)p->port;out[at++]=(uint8_t)(p->port>>8);
        size_t n=strlen(p->host)+1;memcpy(out+at,p->host,n);at+=n;
        n=strlen(p->secret)+1;memcpy(out+at,p->secret,n);at+=n;
    }
    return (int)at;
}

static bool decode_store(passport_store_t *next, const uint8_t *blob, size_t len) {
    if (next == NULL || blob == NULL || len < 16 || get_u32(blob) != PASSPORT_BLOB_MAGIC) {
        return false;
    }
    size_t count = blob[8];
    if (count > PASSPORT_NET_MAX) {
        return false;
    }
    passport_store_init(next);
    next->count = count;
    next->generation = get_u32(blob + 4);
    next->connected = blob[9] == 0xff ? -1 : (int)blob[9];
    if (next->connected >= (int)count) {
        return false;
    }
    next->agent_count = blob[10];
    next->active_agent = blob[11] == 0xff ? -1 : (int)blob[11];
    if (next->agent_count > PASSPORT_AGENT_MAX ||
        (next->active_agent >= 0 && (size_t)next->active_agent >= next->agent_count) ||
        (next->agent_count == 0 && next->active_agent >= 0)) {
        return false;
    }
    const char *lang = (const char *)(blob + 12);
    size_t lang_len = strnlen(lang, len - 12);
    if (lang_len == 0 || lang_len >= sizeof(next->lang) || 12 + lang_len >= len) {
        return false;
    }
    memcpy(next->lang, lang, lang_len + 1);
    size_t at = 12 + lang_len + 1;
    for (size_t i = 0; i < count; i++) {
        if (at + 2 > len) {
            return false;
        }
        passport_net_t *net = &next->nets[i];
        net->enabled = blob[at++] == 1;
        net->priority = blob[at++];
        const char *name = (const char *)(blob + at);
        size_t nlen = strnlen(name, len - at);
        if (nlen >= PASSPORT_NAME_MAX || at + nlen >= len) {
            return false;
        }
        memcpy(net->name, name, nlen + 1);
        at += nlen + 1;
        const char *ssid = (const char *)(blob + at);
        size_t slen = strnlen(ssid, len - at);
        if (slen == 0 || slen >= PASSPORT_SSID_MAX || at + slen >= len) {
            return false;
        }
        memcpy(net->ssid, ssid, slen + 1);
        at += slen + 1;
        const char *pass = (const char *)(blob + at);
        size_t plen = strnlen(pass, len - at);
        if (plen >= PASSPORT_PASS_MAX || at + plen >= len) {
            return false;
        }
        memcpy(net->pass, pass, plen + 1);
        at += plen + 1;
    }
    for (size_t i = 0; i < next->agent_count; i++) {
        passport_agent_t *agent = &next->agents[i];
        if (at >= len) {
            return false;
        }
        const char *name = (const char *)(blob + at);
        size_t nlen = strnlen(name, len - at);
        if (nlen == 0 || nlen >= PASSPORT_NAME_MAX || at + nlen >= len) {
            return false;
        }
        memcpy(agent->name, name, nlen + 1);
        at += nlen + 1;
        const char *kind = (const char *)(blob + at);
        size_t klen = strnlen(kind, len - at);
        if (klen == 0 || klen >= PASSPORT_KIND_MAX || at + klen >= len) {
            return false;
        }
        memcpy(agent->kind, kind, klen + 1);
        at += klen + 1;
        const char *url = (const char *)(blob + at);
        size_t ulen = strnlen(url, len - at);
        if (ulen >= PASSPORT_URL_MAX || at + ulen >= len) {
            return false;
        }
        memcpy(agent->url, url, ulen + 1);
        at += ulen + 1;
        const char *bootstrap = (const char *)(blob + at);
        size_t blen = strnlen(bootstrap, len - at);
        if (blen >= PASSPORT_TOKEN_MAX || at + blen >= len) {
            return false;
        }
        memcpy(agent->bootstrap, bootstrap, blen + 1);
        at += blen + 1;
        const char *device_token = (const char *)(blob + at);
        size_t dlen = strnlen(device_token, len - at);
        if (dlen >= PASSPORT_TOKEN_MAX || at + dlen >= len) {
            return false;
        }
        memcpy(agent->device_token, device_token, dlen + 1);
        at += dlen + 1;
        const char *access_id = (const char *)(blob + at);
        size_t ilen = strnlen(access_id, len - at);
        if (ilen >= PASSPORT_SECRET_MAX || at + ilen >= len) {
            return false;
        }
        memcpy(agent->access_id, access_id, ilen + 1);
        at += ilen + 1;
        const char *access_secret = (const char *)(blob + at);
        size_t slen = strnlen(access_secret, len - at);
        if (slen >= PASSPORT_SECRET_MAX || at + slen >= len) {
            return false;
        }
        memcpy(agent->access_secret, access_secret, slen + 1);
        at += slen + 1;
    }
    /* PXY1/PXY2 held per-Wi-Fi proxies; routes now belong to each gateway,
     * so an old Wi-Fi proxy is dropped and every gateway loads as direct. */
    if (at<len && (!memcmp(blob+at,"PXY2",4) || !memcmp(blob+at,"PXY1",4))) return true;
    if (at<len) {
        /* GSS1 predates the method byte; those routes are aes-256-cfb. */
        int version = len-at<5 ? 0 : !memcmp(blob+at,"GSS2",4) ? 2 : !memcmp(blob+at,"GSS1",4) ? 1 : 0;
        if (!version || blob[at+4]!=next->agent_count) return false;
        at+=5;
        for (size_t i=0;i<next->agent_count;i++) {
            passport_proxy_t *p=&next->agents[i].proxy;
            if (len-at<(version==2?5u:4u) || blob[at]>1) return false;
            p->enabled=blob[at++]!=0;
            if (version==2) p->method=blob[at++];
            p->port=blob[at]|((uint16_t)blob[at+1]<<8);at+=2;
            size_t n=strnlen((const char *)blob+at,len-at);
            if (n>=sizeof(p->host) || n>=len-at) return false;
            memcpy(p->host,blob+at,n+1);at+=n+1;
            n=strnlen((const char *)blob+at,len-at);
            if (n>=sizeof(p->secret) || n>=len-at) return false;
            memcpy(p->secret,blob+at,n+1);at+=n+1;
            if (!passport_proxy_valid(p)) return false;
        }
        if (at!=len) return false;
    }
    return true;
}

bool passport_store_load(passport_store_t *store, const uint8_t *blob, size_t len) {
    if (!store) return false;
    passport_store_t *next = malloc(sizeof(*next));
    if (!next) return false;
    bool ok = decode_store(next, blob, len);
    if (ok) *store = *next;
    memset(next, 0, sizeof(*next));
    free(next);
    return ok;
}

int passport_store_pick(const passport_store_t *store, const bool *visible,
                     const int *rssi, size_t scan_count) {
    int best = -1;
    int best_rank = 100000;
    if (store == NULL || visible == NULL || scan_count < store->count) {
        return -1;
    }
    for (size_t i = 0; i < store->count; i++) {
        if (!store->nets[i].enabled || !visible[i]) {
            continue;
        }
        int strength = rssi != NULL ? rssi[i] : 0;
        int rank = store->nets[i].priority * 1000 - strength;
        if (best < 0 || rank < best_rank) {
            best = (int)i;
            best_rank = rank;
        }
    }
    return best;
}
