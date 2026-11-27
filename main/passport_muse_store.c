#include "passport_muse.h"
#include "esp_mac.h"
#include "nvs.h"
#include <stdio.h>
#include <string.h>

static const char *storage = "passport_muse";

static bool read_value(const char *key, void *value, size_t size) {
    nvs_handle_t handle;
    if (nvs_open(storage, NVS_READONLY, &handle) != ESP_OK) return false;
    size_t length = size;
    esp_err_t result = nvs_get_blob(handle, key, value, &length);
    nvs_close(handle);
    return result == ESP_OK && length == size;
}

static bool write_value(const char *key, const void *value, size_t size) {
    nvs_handle_t handle;
    if (nvs_open(storage, NVS_READWRITE, &handle) != ESP_OK) return false;
    esp_err_t result = value ? nvs_set_blob(handle, key, value, size) : nvs_erase_key(handle, key);
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
    return result == ESP_OK || (!value && result == ESP_ERR_NVS_NOT_FOUND);
}

bool passport_muse_token_save(const char *token) {
    if (strlen(token) != 48 || strncmp(token, "mgst_", 5) ||
        strspn(token + 5, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_") != 43 ||
        !strchr("AEIMQUYcgkosw048", token[47])) return false;
    return write_value("sdk", token, 49);
}

bool passport_muse_token_load(char token[49]) {
    memset(token, 0, 49);
    return read_value("sdk", token, 49) && token[0] && token[48] == 0;
}

bool passport_muse_credentials_load(passport_muse_credentials_t *credentials) {
    memset(credentials, 0, sizeof(*credentials));
    return read_value("credentials", credentials, sizeof(*credentials)) &&
           credentials->access[0] && credentials->refresh[0] &&
           !credentials->access[sizeof(credentials->access) - 1] &&
           !credentials->refresh[sizeof(credentials->refresh) - 1];
}

bool passport_muse_credentials_save(const passport_muse_credentials_t *credentials) {
    return write_value("credentials", credentials, sizeof(*credentials));
}

bool passport_muse_credentials_clear(void) {
    return write_value("credentials", NULL, 0);
}

bool passport_muse_paired(void) {
    nvs_handle_t handle;
    if (nvs_open(storage, NVS_READONLY, &handle) != ESP_OK) return false;
    size_t length = 0;
    esp_err_t result = nvs_get_blob(handle, "credentials", NULL, &length);
    nvs_close(handle);
    return result == ESP_OK && length == sizeof(passport_muse_credentials_t);
}

void passport_muse_node_id(char node[16]) {
    uint8_t address[6] = {0};
    (void)esp_read_mac(address, ESP_MAC_BT);
    snprintf(node, 16, "homelink-%02x%02x%02x", address[3], address[4], address[5]);
}
