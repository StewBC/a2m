#pragma once

#include <SDL.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Host-keyboard driven joystick.
 *
 * Frontend-only input source. Accumulates direction/fire bits from the active
 * layout. Product converts that mask to Apple paddle axes (maxed cardinals /
 * center) + buttons via frontend_joystick_apple_state().
 *
 * While the stick is assigned, Option/Left-Alt is consumed as a fire key (not
 * as Open-Apple). With stick off, LALT/RALT still set A2S_OPEN/CLOSED_APPLE.
 * swap_buttons exchanges FIRE↔FIRE2 (Space primary, Option secondary).
 */
enum {
    FRONTEND_JOYSTICK_UP     = 0x01,
    FRONTEND_JOYSTICK_DOWN   = 0x02,
    FRONTEND_JOYSTICK_LEFT   = 0x04,
    FRONTEND_JOYSTICK_RIGHT  = 0x08,
    FRONTEND_JOYSTICK_FIRE   = 0x10, /* logical fire 0 (Option/KP0; → BUTN0 unless swap) */
    FRONTEND_JOYSTICK_FIRE2  = 0x20  /* logical fire 1 (Space; → BUTN1 unless swap) */
};

/* Apple paddle center / extremes for keyboard stick.
 * MAX is 254, not 255: the PTRIG timer saturates at 255 and bit7 only clears
 * when timer > axis (a2m / Penetrator hang on 255). */
enum {
    FRONTEND_JOYSTICK_APPLE_AXIS_MIN = 0,
    FRONTEND_JOYSTICK_APPLE_AXIS_MID = 128,
    FRONTEND_JOYSTICK_APPLE_AXIS_MAX = 254
};

enum {
    /* Largest layout (numpad: 8 dirs + KP0 + Space + Option). */
    FRONTEND_JOYSTICK_MAX_BINDINGS = 11
};

typedef enum frontend_joystick_layout {
    FRONTEND_JOYSTICK_LAYOUT_NUMPAD = 0, /* KP dirs; KP0/Option=fire0; Space=fire1 */
    FRONTEND_JOYSTICK_LAYOUT_WASD        /* WASD; Option=fire0; Space=fire1 */
} frontend_joystick_layout;

typedef struct frontend_joystick_input {
    frontend_joystick_layout layout;
    unsigned port;    /* 0 = unassigned/disabled; 1 or 2 = gameport stick */
    bool     swap_buttons; /* when true, Space→BUTN0 and Option→BUTN1 */
    uint8_t  inputs;  /* current accumulated FRONTEND_JOYSTICK_* mask */
    bool     key_down[FRONTEND_JOYSTICK_MAX_BINDINGS]; /* per active binding */
} frontend_joystick_input;

/* Release all keys (clears inputs and per-key state); layout and port kept. */
void frontend_joystick_reset(frontend_joystick_input *joystick);

/* Parse a layout name; unknown/NULL falls back to numpad. */
frontend_joystick_layout frontend_joystick_layout_from_string(const char *name);
const char *frontend_joystick_layout_to_string(frontend_joystick_layout layout);

/* Change the active layout; releases any held keys. */
void frontend_joystick_set_layout(frontend_joystick_input *joystick,
                                  frontend_joystick_layout layout);

/* Assign to a host port (0 disables). Disabling releases all held keys. */
void frontend_joystick_set_port(frontend_joystick_input *joystick, unsigned port);

/* Swap logical fire keys: default Option/KP0=BUTN0, Space=BUTN1; swapped is
 * the reverse (WASD-friendly). No-op if joystick is NULL. */
void frontend_joystick_set_swap_buttons(frontend_joystick_input *joystick,
                                        bool swap_buttons);

/* True when the joystick is assigned to a port and this key belongs to the
 * active layout, i.e. the event loop should consume the key instead of routing
 * it to the machine keyboard. */
bool frontend_joystick_consumes(const frontend_joystick_input *joystick,
                                SDL_Keycode sym);

/* Apply a key up/down event. Returns true if the accumulated joystick mask
 * changed (so the caller should re-publish the port state). Keys that are not
 * part of the active layout, or events while unassigned, return false. */
bool frontend_joystick_handle_key(frontend_joystick_input *joystick,
                                  const SDL_KeyboardEvent *event);

/* Map current mask to Apple gameport axes + buttons for one stick.
 * X/Y: held direction → 0 or 254; release → center 128.
 * FIRE → BUTN0, FIRE2 → BUTN1 (swapped when swap_buttons is set).
 * Outputs are always written when joystick is non-NULL; if unassigned/idle
 * axes are centered and buttons are 0. */
void frontend_joystick_apple_state(const frontend_joystick_input *joystick,
                                   uint8_t *out_x,
                                   uint8_t *out_y,
                                   uint8_t *out_buttons);
