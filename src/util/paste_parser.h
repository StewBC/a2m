#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PASTE_EVENTS_MAX 128

typedef enum {
    PASTE_EV_KEY_PRESS,    /* press-and-release; key field valid */
    PASTE_EV_KEY_ASSERT,   /* hold down (\[KEY+]); base key only — SHIFT not auto-asserted */
    PASTE_EV_KEY_DEASSERT, /* release (\[KEY-]); base key only */
    PASTE_EV_KEY_ONESHOT,  /* bare modifier token; held until next non-modifier event completes */
    PASTE_EV_PETSCII,      /* raw value from \x \d \o; petscii field valid */
    PASTE_EV_MATRIX,       /* direct matrix address \mR,C; row, col fields valid */
    PASTE_EV_JOYSTICK,     /* joystick event \jPD[,B]; port/dir/button fields valid */
    PASTE_EV_NMI,          /* RESTORE key — pulses NMI line, not a matrix key */
    PASTE_EV_WAIT          /* \[W:N] / \[WAIT:N]; wait.count normal keypress durations */
} paste_event_type_t;

/* key.key holds the host_key ordinal (cast to host_key when used by the sequencer).
   key.needs_shift is true for keys reached via SHIFT on the physical matrix:
   F2/F4/F6/F8 (= SHIFT+F1/F3/F5/F7), CUU/CUL (= SHIFT+cursor-down/right), PI
   (= SHIFT+UP_ARROW).  For KEY_ASSERT and KEY_DEASSERT events the base key is
   actuated without auto-asserting SHIFT; use \[SH+] explicitly for those cases. */
typedef struct {
    paste_event_type_t type;
    union {
        struct { uint8_t key; uint8_t needs_shift; } key;
        struct { uint8_t petscii; }                  petscii;
        struct { uint8_t row;  uint8_t col; }        matrix;
        struct { uint8_t port; uint8_t dir;
                 uint8_t button; uint8_t has_button; } joy;
        struct { uint32_t count; }                   wait;
    };
} paste_event_t;

typedef struct {
    int         offset;   /* byte offset of offending char; -1 if no error */
    const char *message;  /* static string; do not free */
} paste_parse_error_t;

/* Parses input into at most out_max events.  Returns event count on success
   (0 for empty input).  Returns 0 and populates *err on any parse error.
   The caller must initialise err before calling; on success err->offset is -1. */
bool paste_parse(const char          *input,
                 paste_event_t       *out,
                 size_t               out_max,
                 size_t              *count,
                 paste_parse_error_t *err);
