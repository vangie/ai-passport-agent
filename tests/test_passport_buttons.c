#include <assert.h>
#include <stdio.h>
#include "passport_buttons.h"

int main(void) {
    passport_buttons_t buttons = {0};
    for (int key = PASSPORT_BTN_UP; key <= PASSPORT_BTN_OK; key++) {
        passport_input_t input = { .btn = (passport_btn_t)key, .press = PASSPORT_RELEASE };
        assert(!passport_buttons_event(&buttons, &input, false));
        input.press = PASSPORT_PRESS;
        assert(!passport_buttons_event(&buttons, &input, false) && buttons.pressed);
        input.press = PASSPORT_RELEASE;
        assert(passport_buttons_event(&buttons, &input, false));
        assert(input.press == PASSPORT_CLICK && !buttons.pressed);
        input.press = PASSPORT_RELEASE;
        assert(!passport_buttons_event(&buttons, &input, false));
    }
    passport_input_t input = { .btn = PASSPORT_BTN_UP, .press = PASSPORT_PRESS };
    assert(!passport_buttons_event(&buttons, &input, true) && !buttons.pressed);
    input.press = PASSPORT_RELEASE;
    assert(!passport_buttons_event(&buttons, &input, false));
    input.press = PASSPORT_PRESS;
    assert(!passport_buttons_event(&buttons, &input, false));
    input.press = PASSPORT_RELEASE;
    assert(!passport_buttons_event(&buttons, &input, true) && !buttons.pressed);
    input.press = PASSPORT_PRESS;
    assert(!passport_buttons_event(&buttons, &input, false));
    input.btn = PASSPORT_BTN_OK;
    input.press = PASSPORT_RELEASE;
    assert(!passport_buttons_event(&buttons, &input, false) && !buttons.pressed);
    for (unsigned cycle = 0; cycle < 100; cycle++) {
        input.press = PASSPORT_PRESS;
        assert(!passport_buttons_event(&buttons, &input, false));
        input.press = PASSPORT_HOLD;
        assert(!passport_buttons_event(&buttons, &input, false) && buttons.pressed);
        input.press = PASSPORT_RELEASE;
        assert(passport_buttons_event(&buttons, &input, false));
    }
    puts("Passport queued physical keys: release-only click, held keys, duplicates and wake suppression PASS");
}
