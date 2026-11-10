#ifndef PASSPORT_HOTSPOT_H
#define PASSPORT_HOTSPOT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static inline bool passport_hotspot_vendor_ie(const uint8_t *element, size_t size) {
    static const uint8_t signature[] = {0xdd, 0x0a, 0x00, 0x17, 0xf2, 0x06, 0x01, 0x01, 0x03};
    return element != NULL && size >= 12 && memcmp(element, signature, sizeof(signature)) == 0;
}

#endif
