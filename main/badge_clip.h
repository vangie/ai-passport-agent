#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BADGE_CLIP_MAX_BYTES (16000 * 2 * 30)
#define BADGE_CLIP_WAV_HEADER_BYTES 58
esp_err_t badge_clip_begin(void);
esp_err_t badge_clip_append(const void *samples, size_t bytes);
esp_err_t badge_clip_read(size_t offset, void *out, size_t bytes);
/* Declare the maximum length in the WAV header while the clip still grows. */
void badge_clip_stream_header(bool streaming);
size_t badge_clip_size(void);
size_t badge_clip_wire_size(void);
/* Restore metadata only; the existing PCM must match the supplied checksum. */
esp_err_t badge_clip_restore(size_t bytes, const char *id, uint32_t crc32);
bool badge_clip_full(void);
const char *badge_clip_id(void);
/* Discards the logical pending clip. Physical erase happens before next capture. */
void badge_clip_discard(void);
