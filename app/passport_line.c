#include "passport_line.h"

#include <stdlib.h>
#include <string.h>

void passport_line_reset(passport_line_t *buffer) {
    free(buffer->data);
    memset(buffer, 0, sizeof(*buffer));
}

passport_line_result_t passport_line_push(passport_line_t *buffer, unsigned char byte,
                                    size_t limit, char **completed) {
    *completed = NULL;
    if (byte == '\r' || byte == '\n') {
        if (buffer->discarding || buffer->length == 0) {
            passport_line_reset(buffer);
            return PASSPORT_LINE_PENDING;
        }
        buffer->data[buffer->length] = '\0';
        *completed = buffer->data;
        memset(buffer, 0, sizeof(*buffer));
        return PASSPORT_LINE_READY;
    }
    if (buffer->discarding) { return PASSPORT_LINE_PENDING; }
    if (byte == 0 || limit < 2 || buffer->length + 1 >= limit) {
        passport_line_reset(buffer);
        buffer->discarding = true;
        return PASSPORT_LINE_TOO_LONG;
    }
    if (buffer->length + 1 >= buffer->capacity) {
        size_t capacity = buffer->capacity == 0 ? 64 : buffer->capacity * 2;
        if (capacity > limit) { capacity = limit; }
        char *data = realloc(buffer->data, capacity);
        if (data == NULL) {
            passport_line_reset(buffer);
            buffer->discarding = true;
            return PASSPORT_LINE_NO_MEMORY;
        }
        buffer->data = data;
        buffer->capacity = capacity;
    }
    buffer->data[buffer->length++] = (char)byte;
    return PASSPORT_LINE_PENDING;
}
