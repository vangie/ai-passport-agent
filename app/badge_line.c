#include "badge_line.h"

#include <stdlib.h>
#include <string.h>

void badge_line_reset(badge_line_t *buffer) {
    free(buffer->data);
    memset(buffer, 0, sizeof(*buffer));
}

badge_line_result_t badge_line_push(badge_line_t *buffer, unsigned char byte,
                                    size_t limit, char **completed) {
    *completed = NULL;
    if (byte == '\r' || byte == '\n') {
        if (buffer->discarding || buffer->length == 0) {
            badge_line_reset(buffer);
            return BADGE_LINE_PENDING;
        }
        buffer->data[buffer->length] = '\0';
        *completed = buffer->data;
        memset(buffer, 0, sizeof(*buffer));
        return BADGE_LINE_READY;
    }
    if (buffer->discarding) { return BADGE_LINE_PENDING; }
    if (byte == 0 || limit < 2 || buffer->length + 1 >= limit) {
        badge_line_reset(buffer);
        buffer->discarding = true;
        return BADGE_LINE_TOO_LONG;
    }
    if (buffer->length + 1 >= buffer->capacity) {
        size_t capacity = buffer->capacity == 0 ? 64 : buffer->capacity * 2;
        if (capacity > limit) { capacity = limit; }
        char *data = realloc(buffer->data, capacity);
        if (data == NULL) {
            badge_line_reset(buffer);
            buffer->discarding = true;
            return BADGE_LINE_NO_MEMORY;
        }
        buffer->data = data;
        buffer->capacity = capacity;
    }
    buffer->data[buffer->length++] = (char)byte;
    return BADGE_LINE_PENDING;
}
