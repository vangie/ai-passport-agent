#ifndef PASSPORT_LINE_H
#define PASSPORT_LINE_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
    bool discarding;
} passport_line_t;

typedef enum {
    PASSPORT_LINE_PENDING,
    PASSPORT_LINE_READY,
    PASSPORT_LINE_TOO_LONG,
    PASSPORT_LINE_NO_MEMORY,
} passport_line_result_t;

/* READY transfers a terminated line to the caller, who must free it.
 * Empty lines and partial input return PENDING. No buffer is retained at idle. */
passport_line_result_t passport_line_push(passport_line_t *buffer, unsigned char byte,
                                    size_t limit, char **completed);
void passport_line_reset(passport_line_t *buffer);

#endif
