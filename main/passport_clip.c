#include "passport_clip.h"
#include "esp_partition.h"
#include "esp_random.h"
#include <stdio.h>
#include <ctype.h>
#include <string.h>

static const esp_partition_t *s_partition;
/* Written by the audio task, read by the gateway worker while streaming. */
static volatile size_t s_size;
/* Bytes from the partition start that are already erased for this clip. */
static size_t s_erased;
static char s_id[33];

#define CLIP_SECTOR 4096
/* Erase ahead in 16 KB steps: about 5 ms each, well inside the I2S DMA
 * buffering, and 32 steps for a full 30 s clip instead of one 0.45 s stall
 * that froze the screen at the moment the user pressed record. */
#define CLIP_ERASE_STEP (4 * CLIP_SECTOR)

esp_err_t passport_clip_begin(void) {
    s_size = 0;
    s_erased = 0;
    for (int i = 0; i < 4; i++) snprintf(s_id + i * 8, 9, "%08lx", (unsigned long)esp_random());
    if (!s_partition) s_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x41, "audio");
    if (!s_partition || s_partition->size < PASSPORT_CLIP_MAX_BYTES) return ESP_ERR_NOT_FOUND;
    /* Only the first step up front; the rest is erased as the clip grows. */
    esp_err_t err = esp_partition_erase_range(s_partition, 0, CLIP_ERASE_STEP);
    if (err == ESP_OK) s_erased = CLIP_ERASE_STEP;
    return err;
}

esp_err_t passport_clip_append(const void *samples, size_t bytes) {
    if (!s_partition || bytes % 2 || bytes > PASSPORT_CLIP_MAX_BYTES - s_size) return ESP_ERR_INVALID_SIZE;
    while (s_size + bytes > s_erased) {
        size_t limit = (PASSPORT_CLIP_MAX_BYTES + CLIP_SECTOR - 1) & ~(size_t)(CLIP_SECTOR - 1);
        size_t step = limit - s_erased < CLIP_ERASE_STEP ? limit - s_erased : CLIP_ERASE_STEP;
        if (!step) return ESP_ERR_INVALID_SIZE;
        esp_err_t err = esp_partition_erase_range(s_partition, s_erased, step);
        if (err != ESP_OK) return err;
        s_erased += step;
    }
    esp_err_t err = esp_partition_write(s_partition, s_size, samples, bytes);
    if (err == ESP_OK) s_size += bytes;
    return err;
}

esp_err_t passport_clip_restore(size_t bytes, const char *id, uint32_t expected_crc) {
    if (!bytes || bytes>PASSPORT_CLIP_MAX_BYTES || bytes%2) return ESP_ERR_INVALID_SIZE;
    if (!id || strlen(id)!=32) return ESP_ERR_INVALID_ARG;
    for (size_t i=0;i<32;i++) if (!isxdigit((unsigned char)id[i])) return ESP_ERR_INVALID_ARG;
    const esp_partition_t *partition=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,0x41,"audio");
    if (!partition || partition->size<bytes) return ESP_ERR_NOT_FOUND;
    static uint8_t buffer[512];
    uint32_t crc=~0u;
    for (size_t offset=0;offset<bytes;) {
        size_t n=bytes-offset<sizeof(buffer) ? bytes-offset : sizeof(buffer);
        esp_err_t err=esp_partition_read(partition,offset,buffer,n);if (err!=ESP_OK) return err;
        for(size_t i=0;i<n;i++) {
            crc^=buffer[i];
            for(int bit=0;bit<8;bit++) crc=(crc>>1)^((crc&1) ? 0xedb88320u : 0);
        }
        offset+=n;
    }
    if (~crc!=expected_crc) return ESP_ERR_INVALID_CRC;
    s_partition=partition;s_size=bytes;memcpy(s_id,id,33);return ESP_OK;
}

size_t passport_clip_size(void) { return s_size; }
size_t passport_clip_wire_size(void) { return s_size / 2 + PASSPORT_CLIP_WAV_HEADER_BYTES; }
const char *passport_clip_id(void) { return s_id; }
bool passport_clip_full(void) { return s_size + 512 > PASSPORT_CLIP_MAX_BYTES; }
void passport_clip_discard(void) { s_size = 0; }

static void le16(uint8_t *p, uint16_t x) { p[0] = x; p[1] = x >> 8; }
static void le32(uint8_t *p, uint32_t x) { le16(p, x); le16(p + 2, x >> 16); }

static uint8_t mulaw(int16_t sample) {
    int32_t magnitude = sample;
    uint8_t sign = 0;
    if (magnitude < 0) { sign = 0x80; magnitude = -magnitude; }
    if (magnitude > 32635) magnitude = 32635;
    magnitude += 132;
    unsigned exponent = 7;
    for (unsigned mask = 0x4000; !(magnitude & mask) && exponent; mask >>= 1) exponent--;
    return (uint8_t)~(sign | (exponent << 4) | ((magnitude >> (exponent + 3)) & 15));
}

/* While streaming, the clip is still growing: the WAV header declares the
 * maximum length so it can be sent first. Whisper reads to the end of the
 * stream, so a header longer than the data is harmless. */
static bool s_stream_header;
void passport_clip_stream_header(bool streaming) { s_stream_header = streaming; }

esp_err_t passport_clip_read(size_t offset, void *out, size_t bytes) {
    size_t total = passport_clip_wire_size();
    if (!s_partition || offset > total || bytes > total - offset) return ESP_ERR_INVALID_SIZE;
    uint8_t *dest = out;
    uint8_t header[PASSPORT_CLIP_WAV_HEADER_BYTES] = {0};
    size_t declared = (s_stream_header ? PASSPORT_CLIP_MAX_BYTES : s_size) / 2;
    memcpy(header, "RIFF", 4); le32(header + 4, declared + sizeof(header) - 8);
    memcpy(header + 8, "WAVEfmt ", 8); le32(header + 16, 18);
    le16(header + 20, 7); le16(header + 22, 1);
    le32(header + 24, 16000); le32(header + 28, 16000);
    le16(header + 32, 1); le16(header + 34, 8);
    memcpy(header + 38, "fact", 4); le32(header + 42, 4); le32(header + 46, declared);
    memcpy(header + 50, "data", 4); le32(header + 54, declared);
    if (offset < sizeof(header)) {
        size_t take = sizeof(header) - offset < bytes ? sizeof(header) - offset : bytes;
        memcpy(dest, header + offset, take);
        dest += take; offset += take; bytes -= take;
    }
    int16_t samples[256];
    while (bytes) {
        size_t count = bytes < 256 ? bytes : 256;
        esp_err_t err = esp_partition_read(s_partition, (offset - sizeof(header)) * 2, samples, count * 2);
        if (err != ESP_OK) return err;
        for (size_t index = 0; index < count; index++) dest[index] = mulaw(samples[index]);
        dest += count; offset += count; bytes -= count;
    }
    return ESP_OK;
}
