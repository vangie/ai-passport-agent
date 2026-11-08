#include "badge.h"

#include "nvs.h"
#include "nvs_flash.h"
#include <stdlib.h>
#include <string.h>

static const char *k_ns = "badge";
static const char *k_key = "store";
enum { STORE_BYTES = 5120 };

uint8_t badge_nvs_volume_load(void) {
    nvs_handle_t handle;
    uint8_t volume = BADGE_VOLUME_DEFAULT;
    if (nvs_open(k_ns, NVS_READONLY, &handle) != ESP_OK) return volume;
    size_t bytes = sizeof(volume);
    esp_err_t err = nvs_get_blob(handle, "volume", &volume, &bytes);
    nvs_close(handle);
    return err == ESP_OK && bytes == sizeof(volume) && volume <= 100 ? volume : BADGE_VOLUME_DEFAULT;
}

bool badge_nvs_volume_save(uint8_t volume) {
    if (volume > 100) return false;
    nvs_handle_t handle;
    if (nvs_open(k_ns, NVS_READWRITE, &handle) != ESP_OK) return false;
    esp_err_t err = nvs_set_blob(handle, "volume", &volume, sizeof(volume));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK;
}

bool badge_nvs_load(badge_store_t *store) {
    uint8_t *blob = malloc(STORE_BYTES);
    if (!blob) return false;
    nvs_handle_t handle;
    if (nvs_open(k_ns, NVS_READONLY, &handle) != ESP_OK) {
        free(blob);
        return false;
    }
    size_t len = STORE_BYTES;
    esp_err_t err = nvs_get_blob(handle, k_key, blob, &len);
    nvs_close(handle);
    bool ok = err == ESP_OK && badge_store_load(store, blob, len);
    memset(blob, 0, STORE_BYTES);
    free(blob);
    return ok;
}

bool badge_nvs_save(const badge_store_t *store) {
    uint8_t *blob = malloc(STORE_BYTES);
    if (!blob) return false;
    int n = badge_store_save(store, blob, STORE_BYTES);
    if (n < 0) {
        free(blob);
        return false;
    }
    nvs_handle_t handle;
    if (nvs_open(k_ns, NVS_READWRITE, &handle) != ESP_OK) {
        memset(blob, 0, STORE_BYTES);
        free(blob);
        return false;
    }
    esp_err_t err = nvs_set_blob(handle, k_key, blob, (size_t)n);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    memset(blob, 0, STORE_BYTES);
    free(blob);
    return err == ESP_OK;
}

/* Undo an in-memory change after a failed save by reloading what the flash
 * still holds, instead of keeping a second copy of the whole store in RAM. */
void badge_nvs_revert(badge_store_t *store) {
    /* badge_store_load only replaces the store when decoding succeeds. */
    (void)badge_nvs_load(store);
}
