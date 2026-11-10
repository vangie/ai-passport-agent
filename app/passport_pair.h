#ifndef PASSPORT_PAIR_H
#define PASSPORT_PAIR_H

#include "passport.h"

/* openclaw qr --setup-code-only 打出来的那一串。 */
#define PASSPORT_PAIR_MAX 1400

typedef struct {
    char url[PASSPORT_URL_MAX];
    char host[96];
    char bootstrap[PASSPORT_TOKEN_MAX];
} passport_pair_t;

/* 成功返回 NULL。失败返回 code、url 或 token，out 里什么都不留。 */
const char *passport_pair_parse(const char *text, passport_pair_t *out);

#endif
