#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "passport.h"
#include "nvs.h"

static unsigned allocations, releases;
static bool fail_allocation, fail_open, fail_read, fail_write, fail_commit;
static unsigned commits;
static uint8_t persisted[5120];
static size_t persisted_size;
static uint8_t persisted_volume;
static bool has_volume;

static void *allocate(size_t bytes) {
    assert(bytes==5120);
    if (fail_allocation) return NULL;
    allocations++;
    return malloc(bytes);
}

static void release(void *buffer) {
    assert(buffer);
    releases++;
    free(buffer);
}

#define malloc allocate
#define free release
#include "../main/passport_nvs.c"
#undef malloc
#undef free

esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *out) {
    assert(!strcmp(name,"passport") && (mode==NVS_READONLY || mode==NVS_READWRITE));
    *out=1;
    return fail_open?ESP_FAIL:ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *name, void *out, size_t *size) {
    if (!strcmp(name,"volume")) {
        assert(handle==1 && *size==1);
        if (fail_read || !has_volume) return ESP_FAIL;
        *(uint8_t *)out=persisted_volume;
        *size=1;
        return ESP_OK;
    }
    assert(handle==1 && !strcmp(name,"store") && *size>=persisted_size);
    if (fail_read) return ESP_FAIL;
    memcpy(out,persisted,persisted_size);
    *size=persisted_size;
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *name, const void *data, size_t size) {
    if (!strcmp(name,"volume")) {
        assert(handle==1 && size==1);
        if (fail_write) return ESP_FAIL;
        if (!fail_commit) { persisted_volume=*(const uint8_t *)data;has_volume=true; }
        return ESP_OK;
    }
    assert(handle==1 && !strcmp(name,"store") && size<=sizeof(persisted));
    if (fail_write) return ESP_FAIL;
    if (!fail_commit) { memcpy(persisted,data,size);persisted_size=size; }
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle) { assert(handle==1);commits++;return fail_commit?ESP_FAIL:ESP_OK; }
void nvs_close(nvs_handle_t handle) { assert(handle==1); }

int main(void) {
    passport_store_t store, restored;
    passport_store_init(&store);
    assert(passport_store_add(&store,"fixture","fixture","fixture-password"));
    assert(passport_agent_upsert(&store,"fixture","openclaw","wss://fixture.example","",NULL,NULL,NULL));
    assert(passport_nvs_save(&store) && commits==1 && allocations==releases);
    for (unsigned attempt=0;attempt<32;attempt++) {
        passport_store_init(&restored);
        assert(passport_nvs_load(&restored));
        assert(restored.count==1 && restored.agent_count==1 && !strcmp(restored.nets[0].pass,"fixture-password"));
        assert(passport_nvs_save(&restored) && allocations==releases);
    }
    for (unsigned failure=0;failure<5;failure++) {
        passport_store_t before=restored;
        fail_allocation=failure==0;fail_open=failure==1;fail_read=failure==2;
        fail_write=failure==3;fail_commit=failure==4;
        if (failure<3) assert(!passport_nvs_load(&restored) && !memcmp(&restored,&before,sizeof(before)));
        if (failure!=2) assert(!passport_nvs_save(&restored));
        assert(allocations==releases);
        fail_allocation=fail_open=fail_read=fail_write=fail_commit=false;
        assert(passport_nvs_load(&restored) && !strcmp(restored.nets[0].pass,"fixture-password"));
    }
    assert(allocations==releases);
    unsigned allocations_before=allocations;
    assert(passport_nvs_volume_load()==55);
    assert(passport_nvs_volume_save(75) && passport_nvs_volume_load()==75);
    assert(passport_nvs_save(&store) && passport_nvs_volume_load()==75);
    assert(!passport_nvs_volume_save(101) && passport_nvs_volume_load()==75);
    persisted_volume=255;
    assert(passport_nvs_volume_load()==55);
    persisted_volume=75;
    fail_write=true;
    assert(!passport_nvs_volume_save(20));
    fail_write=false;fail_commit=true;
    assert(!passport_nvs_volume_save(20));
    fail_commit=false;
    assert(passport_nvs_volume_load()==75);
    fail_open=true;
    assert(passport_nvs_volume_load()==55 && !passport_nvs_volume_save(20));
    fail_open=false;fail_read=true;
    assert(passport_nvs_volume_load()==55);
    fail_read=false;
    assert(passport_nvs_volume_save(0) && passport_nvs_volume_load()==0);
    assert(passport_nvs_volume_save(100) && passport_nvs_volume_load()==100);
    assert(allocations==allocations_before+1 && allocations==releases);
    assert(passport_nvs_load(&restored) && restored.count==1 && restored.agent_count==1);
    puts("NVS workspace: repeated load/save, allocation/storage failures and complete release PASS");
}
