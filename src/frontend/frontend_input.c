#include "frontend_input.h"

#include <string.h>

static bool has_shift_modifier(const SDL_KeyboardEvent *key)
{
    return key != NULL && (key->keysym.mod & KMOD_SHIFT) != 0;
}

bool frontend_input_has_option_modifier(const SDL_KeyboardEvent *key)
{
    return key != NULL && (key->keysym.mod & KMOD_ALT) != 0;
}

bool frontend_input_has_shift_modifier(const SDL_KeyboardEvent *key)
{
    return has_shift_modifier(key);
}

bool frontend_input_is_host_quit_shortcut(const SDL_KeyboardEvent *key)
{
    SDL_Keymod modifiers;

    if (key == NULL || key->keysym.sym != SDLK_q) {
        return false;
    }

    modifiers = key->keysym.mod;

#if defined(__APPLE__)
    return (modifiers & KMOD_GUI) != 0;
#else
    return (modifiers & KMOD_ALT) != 0;
#endif
}

void frontend_input_mapper_reset(frontend_input_mapper *mapper)
{
    if (mapper == NULL) {
        return;
    }
    memset(mapper, 0, sizeof(*mapper));
}

void frontend_input_mapper_set_original_del(frontend_input_mapper *mapper, bool enabled)
{
    if (mapper != NULL) {
        mapper->original_del = enabled;
    }
}

static void add_key(frontend_input_chord *chord, host_key key, bool pressed)
{
    frontend_input_action *action;

    if (chord == NULL || chord->count >= FRONTEND_INPUT_MAX_ACTIONS) {
        return;
    }

    action = &chord->actions[chord->count++];
    action->type = FRONTEND_INPUT_ACTION_KEY;
    action->key = key;
    action->pressed = pressed;
}

/* US QWERTY → host_key. Shift is tracked as HOST_KEY_SHIFT so the runtime
 * can produce Apple ASCII (e.g. shift+1 → '!'). Host F-keys are product
 * shell shortcuts and are not forwarded to the machine. */
static void map_keydown(
    const frontend_input_mapper *mapper,
    const SDL_KeyboardEvent *event,
    frontend_input_chord *chord)
{
    SDL_Keycode sym;
    host_key key;

    if (event == NULL || chord == NULL) {
        return;
    }

    sym = event->keysym.sym;

    if (sym >= SDLK_a && sym <= SDLK_z) {
        add_key(chord, (host_key)(HOST_KEY_A + (sym - SDLK_a)), true);
        return;
    }
    if (sym >= SDLK_0 && sym <= SDLK_9) {
        add_key(chord, (host_key)(HOST_KEY_0 + (sym - SDLK_0)), true);
        return;
    }

    switch (sym) {
    case SDLK_LSHIFT:
    case SDLK_RSHIFT:
        add_key(chord, HOST_KEY_SHIFT, true);
        break;
    case SDLK_LCTRL:
    case SDLK_RCTRL:
        add_key(chord, HOST_KEY_CTRL, true);
        break;
    case SDLK_LALT:
        /* //e Open-Apple → $C061 / BUTN0 (a2m: Left-ALT). */
        add_key(chord, HOST_KEY_OPEN_APPLE, true);
        break;
    case SDLK_RALT:
        /* //e Closed-Apple → $C062 / BUTN1 (a2m: Right-ALT). */
        add_key(chord, HOST_KEY_CLOSED_APPLE, true);
        break;
    case SDLK_SPACE:
        add_key(chord, HOST_KEY_SPACE, true);
        break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        add_key(chord, HOST_KEY_RETURN, true);
        break;
    case SDLK_BACKSPACE:
        add_key(
            chord,
            mapper->original_del ? HOST_KEY_APPLE_DEL : HOST_KEY_DELETE,
            true);
        break;
    case SDLK_DELETE:
        add_key(chord, HOST_KEY_APPLE_DEL, true);
        break;
    case SDLK_ESCAPE:
        add_key(chord, HOST_KEY_ESCAPE, true);
        break;
    case SDLK_TAB:
        add_key(chord, HOST_KEY_TAB, true);
        break;
    case SDLK_LEFT:
        add_key(chord, HOST_KEY_LEFT, true);
        break;
    case SDLK_RIGHT:
        add_key(chord, HOST_KEY_RIGHT, true);
        break;
    case SDLK_UP:
        add_key(chord, HOST_KEY_UP, true);
        break;
    case SDLK_DOWN:
        add_key(chord, HOST_KEY_DOWN, true);
        break;
    case SDLK_COMMA:
        add_key(chord, HOST_KEY_COMMA, true);
        break;
    case SDLK_PERIOD:
        add_key(chord, HOST_KEY_PERIOD, true);
        break;
    case SDLK_SLASH:
    case SDLK_KP_DIVIDE:
        add_key(chord, HOST_KEY_SLASH, true);
        break;
    case SDLK_SEMICOLON:
        add_key(chord, HOST_KEY_SEMICOLON, true);
        break;
    case SDLK_EQUALS:
        add_key(chord, HOST_KEY_EQUALS, true);
        break;
    case SDLK_MINUS:
    case SDLK_KP_MINUS:
        add_key(chord, HOST_KEY_MINUS, true);
        break;
    case SDLK_QUOTE:
        add_key(chord, HOST_KEY_QUOTE, true);
        break;
    case SDLK_LEFTBRACKET:
        add_key(chord, HOST_KEY_LEFT_BRACKET, true);
        break;
    case SDLK_RIGHTBRACKET:
        add_key(chord, HOST_KEY_RIGHT_BRACKET, true);
        break;
    case SDLK_BACKSLASH:
        add_key(chord, HOST_KEY_BACKSLASH, true);
        break;
    case SDLK_BACKQUOTE:
        add_key(chord, HOST_KEY_BACKQUOTE, true);
        break;
    case SDLK_PLUS:
    case SDLK_KP_PLUS:
        add_key(chord, HOST_KEY_PLUS, true);
        break;
    case SDLK_ASTERISK:
    case SDLK_KP_MULTIPLY:
        add_key(chord, HOST_KEY_ASTERISK, true);
        break;
    case SDLK_AT:
        add_key(chord, HOST_KEY_AT, true);
        break;
    case SDLK_COLON:
        add_key(chord, HOST_KEY_COLON, true);
        break;
    default:
        (void)key;
        break;
    }
}

static void copy_actions(
    frontend_input_action *actions,
    size_t capacity,
    const frontend_input_chord *chord)
{
    size_t i;
    size_t count;

    if (actions == NULL || capacity == 0 || chord == NULL) {
        return;
    }
    count = chord->count;
    if (count > capacity) {
        count = capacity;
    }
    for (i = 0; i < count; i++) {
        actions[i] = chord->actions[i];
    }
}

size_t frontend_input_map_keyboard_event(
    frontend_input_mapper *mapper,
    const SDL_KeyboardEvent *event,
    frontend_input_action *actions,
    size_t capacity)
{
    frontend_input_chord chord;
    SDL_Scancode scancode;

    if (mapper == NULL || event == NULL || actions == NULL || capacity == 0) {
        return 0;
    }

    scancode = event->keysym.scancode;
    if (scancode >= SDL_NUM_SCANCODES) {
        return 0;
    }

    memset(&chord, 0, sizeof(chord));

    if (event->type == SDL_KEYDOWN) {
        map_keydown(mapper, event, &chord);
        if (chord.count == 0) {
            return 0;
        }
        mapper->active[scancode] = chord;
        copy_actions(actions, capacity, &chord);
        return chord.count > capacity ? capacity : chord.count;
    }

    if (event->type == SDL_KEYUP) {
        chord = mapper->active[scancode];
        memset(&mapper->active[scancode], 0, sizeof(mapper->active[scancode]));
        if (chord.count == 0) {
            return 0;
        }
        {
            size_t i;
            for (i = 0; i < chord.count; i++) {
                chord.actions[i].pressed = false;
            }
        }
        copy_actions(actions, capacity, &chord);
        return chord.count > capacity ? capacity : chord.count;
    }

    return 0;
}
