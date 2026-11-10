#include "passport_network.h"

#include <string.h>

passport_network_state_t passport_network_connect(passport_store_t *store, int index,
    uint32_t generation, const passport_network_io_t *io) {
    if (!store || store->generation != generation || index < 0 || (size_t)index >= store->count ||
        !store->nets[index].enabled) return PASSPORT_NETWORK_STALE;
    if (!io || !io->join || !io->save) return PASSPORT_NETWORK_FAILED;
    const passport_net_t *network = &store->nets[index];
    const char *reason = io->join(io->context, network->ssid, network->pass, true);
    if (reason) {
        if (!strcmp(reason, "pass")) return PASSPORT_NETWORK_PASSWORD;
        if (!strcmp(reason, "notfound")) return PASSPORT_NETWORK_NOT_FOUND;
        if (!strcmp(reason, "security")) return PASSPORT_NETWORK_SECURITY;
        if (!strcmp(reason, "timeout")) return PASSPORT_NETWORK_TIMEOUT;
        return PASSPORT_NETWORK_FAILED;
    }
    store->connected = index;
    passport_store_bump(store);
    if (!io->save(io->context, store)) {
        store->generation = generation;
        return PASSPORT_NETWORK_STORAGE;
    }
    return PASSPORT_NETWORK_CONNECTED;
}

const char *passport_network_feedback(passport_network_state_t state, bool chinese, bool online) {
    switch (state) {
        case PASSPORT_NETWORK_CONNECTING: return chinese ? "正在切换网络" : "Switching Wi-Fi";
        case PASSPORT_NETWORK_CONNECTED: return online ? (chinese ? "已连接" : "Connected") : (chinese ? "未连接" : "Not connected");
        case PASSPORT_NETWORK_PASSWORD: return chinese ? "密码错误" : "Incorrect password";
        case PASSPORT_NETWORK_NOT_FOUND: return chinese ? "未找到网络" : "Network not found";
        case PASSPORT_NETWORK_SECURITY: return chinese ? "网络加密不兼容" : "Unsupported security";
        case PASSPORT_NETWORK_TIMEOUT: return chinese ? "连接超时" : "Connection timed out";
        case PASSPORT_NETWORK_FAILED: return chinese ? "连接失败" : "Connection failed";
        case PASSPORT_NETWORK_STALE: return chinese ? "网络配置已变化" : "Settings changed";
        case PASSPORT_NETWORK_STORAGE: return online ? (chinese ? "已连接，保存失败" : "Connected; not saved") : (chinese ? "未连接，保存失败" : "Offline; not saved");
        case PASSPORT_NETWORK_BUSY: return chinese ? "设备忙，请重试" : "Device busy; retry";
        default: return online ? (chinese ? "已连接" : "Connected") : (chinese ? "未连接" : "Not connected");
    }
}
