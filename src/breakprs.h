// Apple ][+ and //e Emhanced emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#pragma once

typedef enum {
    MODE_PC     = 0,        // These are bit masks but pc == 0 for memset
    MODE_READ   = 1,
    MODE_WRITE  = 2,
    MODE_ACCESS = 3
} brkmode_t;

typedef enum {
    ACTION_UNUSED,
    ACTION_FAST,
    ACTION_SLOW,
    ACTION_RESTORE,
    ACTION_TRON,
    ACTION_TRON_APPEND,
    ACTION_TROFF
} brkaction_t;

typedef struct {
    uint32_t    start;     // required
    uint32_t    end;       // = start if no range
    brkmode_t   mode;      // default = MODE_PC
    brkaction_t action;    // default = ACTION_UNUSED
    int         count;     // optional; 0 if unset
    int         reset;     // optional; 0 if unset
} parsed_t;

int parse_line(const char *val, parsed_t *out);
int parse_breakpoint_line(const char *val, parsed_t *out);
