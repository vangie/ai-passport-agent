#include "passport_network.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char *failure;
static bool saved = true;
static unsigned joins, saves;
static passport_store_t persisted;

static const char *join(void *context, const char *ssid, const char *password, bool keep) {
    assert(context == &persisted && keep);
    assert(!strcmp(ssid, "office") && !strcmp(password, "office-password"));
    joins++;
    return failure;
}

static bool save(void *context, const passport_store_t *store) {
    assert(context == &persisted);
    saves++;
    if (saved) persisted = *store;
    return saved;
}

int main(void) {
    passport_store_t store;
    passport_store_init(&store);
    assert(passport_store_add(&store, "Phone", "phone", "phone-password"));
    assert(passport_store_add(&store, "Office", "office", "office-password"));
    store.connected = 0;
    uint32_t generation = store.generation;
    const passport_network_io_t io = { .context = &persisted, .join = join, .save = save };
    assert(passport_network_connect(&store, 1, generation + 1, &io) == PASSPORT_NETWORK_STALE);
    assert(passport_network_connect(&store, -1, generation, &io) == PASSPORT_NETWORK_STALE);
    assert(passport_network_connect(&store, (int)store.count, generation, &io) == PASSPORT_NETWORK_STALE);
    store.nets[1].enabled = false;
    assert(passport_network_connect(&store, 1, generation, &io) == PASSPORT_NETWORK_STALE);
    store.nets[1].enabled = true;
    assert(!joins && !saves);
    const char *reasons[] = {"pass", "notfound", "security", "timeout", "fail"};
    const passport_network_state_t states[] = {PASSPORT_NETWORK_PASSWORD, PASSPORT_NETWORK_NOT_FOUND,
        PASSPORT_NETWORK_SECURITY, PASSPORT_NETWORK_TIMEOUT, PASSPORT_NETWORK_FAILED};
    for (size_t index = 0; index < sizeof(states) / sizeof(states[0]); index++) {
        failure = reasons[index];
        passport_store_t before = store;
        assert(passport_network_connect(&store, 1, generation, &io) == states[index]);
        assert(!memcmp(&store, &before, sizeof(store)) && !saves);
        assert(passport_network_feedback(states[index], true, false)[0]);
        assert(passport_network_feedback(states[index], false, false)[0]);
    }
    failure = NULL;
    assert(passport_network_connect(&store, 1, generation, &io) == PASSPORT_NETWORK_CONNECTED);
    assert(saves == 1 && store.connected == 1 && store.generation != generation);
    assert(persisted.connected == 1 && persisted.generation == store.generation);
    assert(!strcmp(store.nets[0].ssid, "phone") && !strcmp(store.nets[1].ssid, "office"));
    assert(!strcmp(store.nets[0].pass, "phone-password"));
    generation = store.generation;
    saved = false;
    store.connected = 0;
    assert(passport_network_connect(&store, 1, generation, &io) == PASSPORT_NETWORK_STORAGE);
    assert(store.connected == 1 && store.generation == generation && saves == 2);
    puts("Saved Wi-Fi switching: target validation, password reuse, failure preservation and persistence PASS");
}
