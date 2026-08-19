#include "frontend_joystick_input.h"

#include <string.h>

typedef struct joystick_binding {
    SDL_Keycode sym;
    uint8_t     mask;
} joystick_binding;

/* Numpad digits are not mapped to any host key (see frontend_input.c), so this
 * cluster can be consumed unconditionally while assigned without stealing a
 * keystroke. */
static const joystick_binding s_numpad_bindings[] = {
    {SDLK_KP_8, FRONTEND_JOYSTICK_UP},
    {SDLK_KP_2, FRONTEND_JOYSTICK_DOWN},
    {SDLK_KP_4, FRONTEND_JOYSTICK_LEFT},
    {SDLK_KP_6, FRONTEND_JOYSTICK_RIGHT},
    {SDLK_KP_7, FRONTEND_JOYSTICK_UP | FRONTEND_JOYSTICK_LEFT},
    {SDLK_KP_9, FRONTEND_JOYSTICK_UP | FRONTEND_JOYSTICK_RIGHT},
    {SDLK_KP_1, FRONTEND_JOYSTICK_DOWN | FRONTEND_JOYSTICK_LEFT},
    {SDLK_KP_3, FRONTEND_JOYSTICK_DOWN | FRONTEND_JOYSTICK_RIGHT},
    {SDLK_KP_0, FRONTEND_JOYSTICK_FIRE},   /* logical fire 0 */
    {SDLK_LALT, FRONTEND_JOYSTICK_FIRE},   /* Option: fire 0 (//e OA role) */
    {SDLK_SPACE, FRONTEND_JOYSTICK_FIRE2}, /* logical fire 1 */
};

/* W/A/S/D are letter keys, so while assigned they are stolen from the Apple
 * keyboard (the accepted trade-off for the WASD layout).
 * Option = logical fire 0, Space = fire 1; swap_buttons exchanges them for
 * ergonomic WASD (Space primary). */
static const joystick_binding s_wasd_bindings[] = {
    {SDLK_w, FRONTEND_JOYSTICK_UP},
    {SDLK_s, FRONTEND_JOYSTICK_DOWN},
    {SDLK_a, FRONTEND_JOYSTICK_LEFT},
    {SDLK_d, FRONTEND_JOYSTICK_RIGHT},
    {SDLK_LALT, FRONTEND_JOYSTICK_FIRE},
    {SDLK_SPACE, FRONTEND_JOYSTICK_FIRE2},
};

static const joystick_binding *active_bindings(frontend_joystick_layout layout,
                                               size_t *count) {
    if (layout == FRONTEND_JOYSTICK_LAYOUT_WASD) {
        *count = sizeof(s_wasd_bindings) / sizeof(s_wasd_bindings[0]);
        return s_wasd_bindings;
    }
    *count = sizeof(s_numpad_bindings) / sizeof(s_numpad_bindings[0]);
    return s_numpad_bindings;
}

/* Index of sym in the active layout, or -1. */
static int binding_index(frontend_joystick_layout layout, SDL_Keycode sym) {
    const joystick_binding *bindings;
    size_t count;
    size_t i;

    bindings = active_bindings(layout, &count);
    for (i = 0; i < count; ++i) {
        if (bindings[i].sym == sym) {
            return (int)i;
        }
    }
    return -1;
}

/* Recompute the accumulated mask from currently held keys. Recomputing from
 * scratch (rather than OR/clear per event) avoids a diagonal key release
 * clearing a direction bit that a cardinal key still holds. */
static void recompute_inputs(frontend_joystick_input *joystick) {
    const joystick_binding *bindings;
    size_t count;
    size_t i;
    uint8_t mask = 0;

    bindings = active_bindings(joystick->layout, &count);
    for (i = 0; i < count; ++i) {
        if (joystick->key_down[i]) {
            mask |= bindings[i].mask;
        }
    }
    joystick->inputs = mask;
}

void frontend_joystick_reset(frontend_joystick_input *joystick) {
    if (joystick == NULL) {
        return;
    }
    joystick->inputs = 0;
    memset(joystick->key_down, 0, sizeof(joystick->key_down));
}

frontend_joystick_layout frontend_joystick_layout_from_string(const char *name) {
    if (name != NULL && SDL_strcasecmp(name, "wasd") == 0) {
        return FRONTEND_JOYSTICK_LAYOUT_WASD;
    }
    return FRONTEND_JOYSTICK_LAYOUT_NUMPAD;
}

const char *frontend_joystick_layout_to_string(frontend_joystick_layout layout) {
    return layout == FRONTEND_JOYSTICK_LAYOUT_WASD ? "wasd" : "numpad";
}

void frontend_joystick_set_layout(frontend_joystick_input *joystick,
                                  frontend_joystick_layout layout) {
    if (joystick == NULL) {
        return;
    }
    joystick->layout = layout;
    frontend_joystick_reset(joystick);
}

void frontend_joystick_set_port(frontend_joystick_input *joystick, unsigned port) {
    if (joystick == NULL) {
        return;
    }
    joystick->port = (port == 1u || port == 2u) ? port : 0u;
    if (joystick->port == 0u) {
        frontend_joystick_reset(joystick);
    }
}

void frontend_joystick_set_swap_buttons(frontend_joystick_input *joystick,
                                        bool swap_buttons) {
    if (joystick == NULL) {
        return;
    }
    joystick->swap_buttons = swap_buttons;
}

bool frontend_joystick_consumes(const frontend_joystick_input *joystick,
                                SDL_Keycode sym) {
    if (joystick == NULL || joystick->port == 0u) {
        return false;
    }
    return binding_index(joystick->layout, sym) >= 0;
}

bool frontend_joystick_handle_key(frontend_joystick_input *joystick,
                                  const SDL_KeyboardEvent *event) {
    int index;
    uint8_t previous;

    if (joystick == NULL || event == NULL || joystick->port == 0u) {
        return false;
    }
    if (event->type != SDL_KEYDOWN && event->type != SDL_KEYUP) {
        return false;
    }

    index = binding_index(joystick->layout, event->keysym.sym);
    if (index < 0) {
        return false;
    }

    joystick->key_down[index] = (event->type == SDL_KEYDOWN);
    previous = joystick->inputs;
    recompute_inputs(joystick);
    return joystick->inputs != previous;
}

void frontend_joystick_apple_state(const frontend_joystick_input *joystick,
                                   uint8_t *out_x,
                                   uint8_t *out_y,
                                   uint8_t *out_buttons) {
    uint8_t mask = 0;
    uint8_t x = FRONTEND_JOYSTICK_APPLE_AXIS_MID;
    uint8_t y = FRONTEND_JOYSTICK_APPLE_AXIS_MID;
    uint8_t buttons = 0;
    bool swap = false;

    if (joystick != NULL && joystick->port != 0u) {
        mask = joystick->inputs;
        swap = joystick->swap_buttons;
    }
    if ((mask & FRONTEND_JOYSTICK_LEFT) != 0u &&
        (mask & FRONTEND_JOYSTICK_RIGHT) == 0u) {
        x = FRONTEND_JOYSTICK_APPLE_AXIS_MIN;
    } else if ((mask & FRONTEND_JOYSTICK_RIGHT) != 0u &&
               (mask & FRONTEND_JOYSTICK_LEFT) == 0u) {
        x = FRONTEND_JOYSTICK_APPLE_AXIS_MAX;
    }
    if ((mask & FRONTEND_JOYSTICK_UP) != 0u &&
        (mask & FRONTEND_JOYSTICK_DOWN) == 0u) {
        y = FRONTEND_JOYSTICK_APPLE_AXIS_MIN;
    } else if ((mask & FRONTEND_JOYSTICK_DOWN) != 0u &&
               (mask & FRONTEND_JOYSTICK_UP) == 0u) {
        y = FRONTEND_JOYSTICK_APPLE_AXIS_MAX;
    }
    /* Default: FIRE→BUTN0, FIRE2→BUTN1. Swap: Space primary, Option secondary. */
    if ((mask & FRONTEND_JOYSTICK_FIRE) != 0u) {
        buttons |= swap ? 0x02u : 0x01u;
    }
    if ((mask & FRONTEND_JOYSTICK_FIRE2) != 0u) {
        buttons |= swap ? 0x01u : 0x02u;
    }
    if (out_x != NULL) {
        *out_x = x;
    }
    if (out_y != NULL) {
        *out_y = y;
    }
    if (out_buttons != NULL) {
        *out_buttons = buttons;
    }
}
