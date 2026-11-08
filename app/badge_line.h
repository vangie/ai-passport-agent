#ifndef BADGE_LINE_H
#define BADGE_LINE_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
    bool discarding;
} badge_line_t;

typedef enum {
    BADGE_LINE_PENDING,
    BADGE_LINE_READY,
    BADGE_LINE_TOO_LONG,
    BADGE_LINE_NO_MEMORY,
} badge_line_result_t;

/* READY transfers a terminated line to the caller, who must free it.
 * Empty lines and partial input return PENDING. No buffer is retained at idle. */
badge_line_result_t badge_line_push(badge_line_t *buffer, unsigned char byte,
                                    size_t limit, char **completed);
void badge_line_reset(badge_line_t *buffer);

#endif
