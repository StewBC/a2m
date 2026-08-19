#pragma once

/* Apple TYPE / script language for breakpoint Type actions (not clipboard).
 *
 * Plain text → $C000 keys (newlines → Return). `\r` / `\n` are Return;
 * `\t` is a space. Other escapes use \[…]:
 *   \[OA] \[OA+] \[OA-]   Open-Apple pulse / hold / release
 *   \[CA] \[CA+] \[CA-]   Closed-Apple
 *   \[B0] \[B0+] \[B0-]   BUTN0; same for B1
 *   \[RESET] \[COLDRESET] Warm / cold machine reset
 *   \[W:N] \[WAIT:N]      Wait N units (sequencer-defined)
 *   \[J1X=n] \[J1Y=n]     Stick 1 axes 0..255 (128 = center); J2 same
 *   \[J1XL] \[J1XR] \[J1YU] \[J1YD] \[J1XC] \[J1YC]  extremes / center one axis
 *   \[J1C] \[J2C]         Both axes of stick → 128
 *
 * Names are case-insensitive. Pulse forms expand to assert + wait:1 + deassert.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    APPLE_TYPE_EVENTS_MAX = 128
};

typedef enum apple_type_event_kind {
    APPLE_TYPE_EV_CHAR = 0,
    APPLE_TYPE_EV_OA_SET,      /* value 0=release 1=press */
    APPLE_TYPE_EV_CA_SET,
    APPLE_TYPE_EV_BUTTON_SET,  /* axis_or_btn = 0/1, value = 0/1 */
    APPLE_TYPE_EV_AXIS_SET,    /* stick 0/1, axis 0=X 1=Y, value 0..255 */
    APPLE_TYPE_EV_WAIT,        /* value = unit count */
    APPLE_TYPE_EV_RESET_WARM,
    APPLE_TYPE_EV_RESET_COLD
} apple_type_event_kind;

typedef struct apple_type_event {
    uint8_t kind;       /* apple_type_event_kind */
    uint8_t stick;      /* 0=J1 1=J2 for axis */
    uint8_t axis_or_btn;/* 0=X/B0 1=Y/B1 */
    uint8_t value;      /* char, 0/1, axis level, or wait count (see wait_hi) */
    uint16_t wait_count;/* for WAIT: full count (value alone is 8-bit) */
} apple_type_event;

typedef struct apple_type_parse_error {
    int offset;         /* -1 if none */
    const char *message;/* static */
} apple_type_parse_error;

/* Parse script into out[0..*out_count). Returns false on error (*error set). */
bool apple_type_script_parse(
    const char *text,
    apple_type_event *out,
    size_t out_max,
    size_t *out_count,
    apple_type_parse_error *error);
