#pragma once

/*
 * Host keyboard → Apple II key codes.
 *
 * Product input is not a C64 matrix. Keys are abstract host symbols that the
 * runtime maps to Apple ASCII+strobe ($C000). Shift may be a separate
 * HOST_KEY_SHIFT press, or the frontend may emit an explicit shifted symbol.
 */

#include <stdbool.h>
#include <stdint.h>

typedef enum host_key {
    HOST_KEY_A = 0,
    HOST_KEY_B,
    HOST_KEY_C,
    HOST_KEY_D,
    HOST_KEY_E,
    HOST_KEY_F,
    HOST_KEY_G,
    HOST_KEY_H,
    HOST_KEY_I,
    HOST_KEY_J,
    HOST_KEY_K,
    HOST_KEY_L,
    HOST_KEY_M,
    HOST_KEY_N,
    HOST_KEY_O,
    HOST_KEY_P,
    HOST_KEY_Q,
    HOST_KEY_R,
    HOST_KEY_S,
    HOST_KEY_T,
    HOST_KEY_U,
    HOST_KEY_V,
    HOST_KEY_W,
    HOST_KEY_X,
    HOST_KEY_Y,
    HOST_KEY_Z,
    HOST_KEY_0,
    HOST_KEY_1,
    HOST_KEY_2,
    HOST_KEY_3,
    HOST_KEY_4,
    HOST_KEY_5,
    HOST_KEY_6,
    HOST_KEY_7,
    HOST_KEY_8,
    HOST_KEY_9,
    HOST_KEY_SPACE,
    HOST_KEY_RETURN,
    HOST_KEY_DELETE,
    HOST_KEY_SHIFT,
    HOST_KEY_CTRL,
    HOST_KEY_ESCAPE,
    HOST_KEY_TAB,
    HOST_KEY_LEFT,
    HOST_KEY_RIGHT,
    HOST_KEY_UP,
    HOST_KEY_DOWN,
    HOST_KEY_COMMA,
    HOST_KEY_PERIOD,
    HOST_KEY_SLASH,
    HOST_KEY_SEMICOLON,
    HOST_KEY_EQUALS,
    HOST_KEY_MINUS,
    HOST_KEY_QUOTE,
    HOST_KEY_LEFT_BRACKET,
    HOST_KEY_RIGHT_BRACKET,
    HOST_KEY_BACKSLASH,
    HOST_KEY_BACKQUOTE,
    HOST_KEY_PLUS,
    HOST_KEY_ASTERISK,
    HOST_KEY_AT,
    HOST_KEY_COLON,
    /* //e solid-apple keys → BUTN0/BUTN1 ($C061/$C062). */
    HOST_KEY_OPEN_APPLE,
    HOST_KEY_CLOSED_APPLE,
    /* Apple keyboard DEL ($7F), distinct from modern Backspace/cursor-left ($08). */
    HOST_KEY_APPLE_DEL,
    HOST_KEY_COUNT
} host_key;

/* Map a host key press to an Apple keyboard latch value (bit7 = strobe).
 * Returns 0 if the key does not produce a character (pure modifiers).
 * Ctrl+A..Z → $01..$1A (e.g. Ctrl+C break = $03); shift applies to punctuation. */
uint8_t host_key_to_apple_strobe(host_key key, bool shift_held, bool ctrl_held);

typedef struct host_keyboard {
    bool shift_held;
    bool ctrl_held;
} host_keyboard;

void host_keyboard_reset(host_keyboard *keyboard);
void host_keyboard_set_key(host_keyboard *keyboard, host_key key, bool pressed);
