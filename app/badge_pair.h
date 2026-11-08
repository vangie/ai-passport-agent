#ifndef BADGE_PAIR_H
#define BADGE_PAIR_H

#include "badge.h"

/* openclaw qr --setup-code-only 打出来的那一串。 */
#define BADGE_PAIR_MAX 1400

typedef struct {
    char url[BADGE_URL_MAX];
    char host[96];
    char bootstrap[BADGE_TOKEN_MAX];
} badge_pair_t;

/* 成功返回 NULL。失败返回 code、url 或 token，out 里什么都不留。 */
const char *badge_pair_parse(const char *text, badge_pair_t *out);

#endif
