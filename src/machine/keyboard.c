#include "keyboard.h"

#include <string.h>

void host_keyboard_reset(host_keyboard *keyboard)
{
    if (keyboard != NULL) {
        memset(keyboard, 0, sizeof(*keyboard));
    }
}

void host_keyboard_set_key(host_keyboard *keyboard, host_key key, bool pressed)
{
    if (keyboard == NULL) {
        return;
    }
    if (key == HOST_KEY_SHIFT) {
        keyboard->shift_held = pressed;
    } else if (key == HOST_KEY_CTRL) {
        keyboard->ctrl_held = pressed;
    }
}

uint8_t host_key_to_apple_strobe(host_key key, bool shift_held, bool ctrl_held)
{
    uint8_t ch = 0;

    if (key >= HOST_KEY_A && key <= HOST_KEY_Z) {
        if (ctrl_held) {
            /* Apple CTRL+letter: Ctrl+A=$01 … Ctrl+Z=$1A (Ctrl+C break = $03). */
            ch = (uint8_t)(1u + (unsigned)(key - HOST_KEY_A));
            return (uint8_t)(ch | 0x80u);
        }
        ch = (uint8_t)('A' + (key - HOST_KEY_A));
        return (uint8_t)(ch | 0x80u);
    }
    if (key >= HOST_KEY_0 && key <= HOST_KEY_9) {
        static const char unshifted[] = "0123456789";
        static const char shifted[] = ")!@#$%^&*(";
        ch = (uint8_t)(shift_held ? shifted[key - HOST_KEY_0] : unshifted[key - HOST_KEY_0]);
        return (uint8_t)(ch | 0x80u);
    }

    switch (key) {
    case HOST_KEY_SPACE:
        ch = ' ';
        break;
    case HOST_KEY_RETURN:
        ch = 0x0D;
        break;
    case HOST_KEY_DELETE:
    case HOST_KEY_LEFT:
        ch = 0x08;
        break;
    case HOST_KEY_APPLE_DEL:
        ch = 0x7F;
        break;
    case HOST_KEY_ESCAPE:
        ch = 0x1B;
        break;
    case HOST_KEY_TAB:
        ch = 0x09;
        break;
    case HOST_KEY_RIGHT:
        ch = 0x15;
        break;
    case HOST_KEY_UP:
        ch = 0x0B;
        break;
    case HOST_KEY_DOWN:
        ch = 0x0A;
        break;
    case HOST_KEY_COMMA:
        ch = (uint8_t)(shift_held ? '<' : ',');
        break;
    case HOST_KEY_PERIOD:
        ch = (uint8_t)(shift_held ? '>' : '.');
        break;
    case HOST_KEY_SLASH:
        ch = (uint8_t)(shift_held ? '?' : '/');
        break;
    case HOST_KEY_SEMICOLON:
        ch = (uint8_t)(shift_held ? ':' : ';');
        break;
    case HOST_KEY_EQUALS:
        ch = (uint8_t)(shift_held ? '+' : '=');
        break;
    case HOST_KEY_MINUS:
        ch = (uint8_t)(shift_held ? '_' : '-');
        break;
    case HOST_KEY_QUOTE:
        ch = (uint8_t)(shift_held ? '"' : '\'');
        break;
    case HOST_KEY_LEFT_BRACKET:
        ch = (uint8_t)(shift_held ? '{' : '[');
        break;
    case HOST_KEY_RIGHT_BRACKET:
        ch = (uint8_t)(shift_held ? '}' : ']');
        break;
    case HOST_KEY_BACKSLASH:
        ch = (uint8_t)(shift_held ? '|' : '\\');
        break;
    case HOST_KEY_BACKQUOTE:
        ch = (uint8_t)(shift_held ? '~' : '`');
        break;
    case HOST_KEY_PLUS:
        ch = '+';
        break;
    case HOST_KEY_ASTERISK:
        ch = '*';
        break;
    case HOST_KEY_AT:
        ch = '@';
        break;
    case HOST_KEY_COLON:
        ch = ':';
        break;
    default:
        return 0;
    }
    return (uint8_t)(ch | 0x80u);
}
