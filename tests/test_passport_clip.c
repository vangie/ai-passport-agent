#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "esp_partition.h"
#include "../main/passport_clip.c"
static unsigned random_count;
uint32_t esp_random(void) { return ++random_count; }
static esp_partition_t partition = { .size = 0x4a0000 };
static uint8_t flash[PASSPORT_CLIP_MAX_BYTES + 4096];
static bool fail_write;
const esp_partition_t *esp_partition_find_first(int type, int subtype, const char *label) {
    assert(type == ESP_PARTITION_TYPE_DATA && subtype == 0x41 && !strcmp(label, "audio"));
    return &partition;
}
/* Tracks which bytes are erased, so a write to unerased flash (a NOR flash
 * write can only clear bits) fails the test. */
static uint8_t erased[sizeof(flash)];
static size_t largest_erase;
esp_err_t esp_partition_erase_range(const esp_partition_t *p, size_t offset, size_t size) {
    assert(p == &partition && offset % 4096 == 0 && size % 4096 == 0 && offset + size <= sizeof(flash));
    memset(flash + offset, 0xff, size); memset(erased + offset, 1, size);
    if (size > largest_erase) largest_erase = size;
    return ESP_OK;
}
esp_err_t esp_partition_write(const esp_partition_t *p, size_t offset, const void *data, size_t size) {
    assert(p == &partition && offset + size <= PASSPORT_CLIP_MAX_BYTES);
    for (size_t i = 0; i < size; i++) assert(erased[offset + i]);
    if (fail_write) return ESP_FAIL;
    memcpy(flash + offset, data, size); memset(erased + offset, 0, size); return ESP_OK;
}
esp_err_t esp_partition_read(const esp_partition_t *p, size_t offset, void *out, size_t size) {
    assert(p == &partition && offset + size <= PASSPORT_CLIP_MAX_BYTES);
    memcpy(out, flash + offset, size); return ESP_OK;
}
int main(int argc, char **argv) {
    uint8_t samples[512], out[314]; memset(samples, 0x5a, sizeof(samples));
    assert(passport_clip_begin() == ESP_OK && passport_clip_size() == 0);
    /* Starting a clip must not erase the whole 30 s area at once: that
     * stalled the UI for ~0.45 s exactly when recording began. */
    assert(largest_erase <= 16384);
    char first[33]; snprintf(first, sizeof(first), "%s", passport_clip_id());
    fail_write = true; assert(passport_clip_append(samples, 512) == ESP_FAIL && passport_clip_size() == 0);
    fail_write = false; assert(passport_clip_append(samples, 512) == ESP_OK);
    assert(passport_clip_append(samples, 1) == ESP_ERR_INVALID_SIZE && passport_clip_size() == 512);
    assert(passport_clip_read(0, out, sizeof(out)) == ESP_OK);
    assert(!memcmp(out, "RIFF", 4) && !memcmp(out + 8, "WAVEfmt ", 8));
    assert(out[20] == 7 && out[24] == 0x80 && out[25] == 0x3e && out[34] == 8);
    assert(!memcmp(out + 38, "fact", 4) && !memcmp(out + 50, "data", 4));
    assert(passport_clip_wire_size() == sizeof(out));
    for (size_t index = PASSPORT_CLIP_WAV_HEADER_BYTES; index < sizeof(out); index++) assert(out[index] == mulaw(0x5a5a));
    uint8_t slice[314];
    for (size_t offset = 0; offset < sizeof(out); offset++) {
        assert(passport_clip_read(offset, slice, sizeof(out) - offset) == ESP_OK);
        assert(!memcmp(slice, out + offset, sizeof(out) - offset));
    }
    assert(out[54] == 0 && out[55] == 1 && out[46] == 0 && out[47] == 1);
    passport_clip_stream_header(true);
    assert(passport_clip_read(0, slice, PASSPORT_CLIP_WAV_HEADER_BYTES) == ESP_OK);
    assert(slice[54] == 0 && slice[55] == 0x53 && slice[56] == 7);
    passport_clip_stream_header(false);
    assert(mulaw(0) == 0xff && mulaw(32767) == 0x80 && mulaw(-32768) == 0);
    passport_clip_discard();
    assert(passport_clip_restore(512,first,0xc6d765f6u)==ESP_OK && passport_clip_size()==512);
    assert(!strcmp(passport_clip_id(),first));
    assert(passport_clip_restore(512,first,0)==ESP_ERR_INVALID_CRC && passport_clip_size()==512);
    assert(passport_clip_restore(512,"wrong-id",0)==ESP_ERR_INVALID_ARG);
    assert(passport_clip_restore(PASSPORT_CLIP_MAX_BYTES+2,first,0)==ESP_ERR_INVALID_SIZE);
    assert(!memcmp(flash,samples,512));
    assert(passport_clip_read(56, out, 8) == ESP_OK && out[2] == mulaw(0x5a5a));
    assert(passport_clip_read(314, out, 1) == ESP_ERR_INVALID_SIZE);
    assert(passport_clip_read((size_t)-1, out, 1) == ESP_ERR_INVALID_SIZE);
    assert(passport_clip_append(samples, PASSPORT_CLIP_MAX_BYTES) == ESP_ERR_INVALID_SIZE);
    while (!passport_clip_full()) assert(passport_clip_append(samples, 512) == ESP_OK);
    assert(passport_clip_size() <= PASSPORT_CLIP_MAX_BYTES && largest_erase <= 16384);
    /* A new clip after a full one erases again before every write. */
    assert(passport_clip_begin() == ESP_OK);
    for (int i = 0; i < 100; i++) assert(passport_clip_append(samples, 512) == ESP_OK);
    passport_clip_discard(); assert(!passport_clip_size());
    assert(passport_clip_begin() == ESP_OK && strcmp(first, passport_clip_id()));
    uint8_t streamed[PASSPORT_CLIP_WAV_HEADER_BYTES + 40 * 256];
    size_t streamed_bytes = 0;
    passport_clip_stream_header(true);
    for (unsigned block = 0; block < 40; block++) {
        assert(passport_clip_append(samples, sizeof(samples)) == ESP_OK);
        while (passport_clip_wire_size() - streamed_bytes >= 747) {
            assert(passport_clip_read(streamed_bytes, streamed + streamed_bytes, 747) == ESP_OK);
            streamed_bytes += 747;
        }
    }
    size_t remaining = passport_clip_wire_size() - streamed_bytes;
    assert(passport_clip_read(streamed_bytes, streamed + streamed_bytes, remaining) == ESP_OK);
    streamed_bytes += remaining;
    assert(streamed_bytes == sizeof(streamed));
    for (size_t index = PASSPORT_CLIP_WAV_HEADER_BYTES; index < streamed_bytes; index++) assert(streamed[index] == mulaw(0x5a5a));
    passport_clip_stream_header(false);
    assert(passport_clip_read(PASSPORT_CLIP_WAV_HEADER_BYTES, slice, sizeof(slice)) == ESP_OK);
    assert(!memcmp(slice, streamed + PASSPORT_CLIP_WAV_HEADER_BYTES, sizeof(slice)));
    assert(passport_clip_begin() == ESP_OK);
    if (argc == 2) {
        FILE *fixture = fopen(argv[1], "wb"); assert(fixture);
        for (int value = -32768; value < 32768; value += 256) {
            int16_t ramp[256];
            for (int index = 0; index < 256; index++) ramp[index] = (int16_t)(value + index);
            assert(passport_clip_append(ramp, sizeof(ramp)) == ESP_OK);
        }
        for (size_t offset = 0; offset < passport_clip_wire_size();) {
            size_t count = passport_clip_wire_size() - offset;
            if (count > sizeof(out)) count = sizeof(out);
            assert(passport_clip_read(offset, out, count) == ESP_OK);
            assert(fwrite(out, 1, count, fixture) == count);
            offset += count;
        }
        assert(!fclose(fixture));
    }
    puts("Clip storage: bounded Flash writes, incremental erase, WAV reads, failure preservation and retry identity PASS");
}
