#pragma once

#include "passport.h"

typedef struct {
    bool pressed;
    passport_btn_t button;
} passport_buttons_t;

static inline bool passport_buttons_event(passport_buttons_t *buttons, passport_input_t *input, bool suppress) {
    if (suppress) {
        buttons->pressed = false;
        return false;
    }
    if (input->press == PASSPORT_PRESS) {
        buttons->pressed = true;
        buttons->button = input->btn;
        return false;
    }
    if (input->press != PASSPORT_RELEASE) return false;
    bool click = buttons->pressed && buttons->button == input->btn;
    buttons->pressed = false;
    if (click) input->press = PASSPORT_CLICK;
    return click;
}
