// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

typedef enum {
    MODE_PC     = 0,        // These are bit masks but pc == 0 for memset
    MODE_READ   = 1,
    MODE_WRITE  = 2,
    MODE_ACCESS = 3
} BRKMODE;

typedef enum {
    ACTION_UNUSED,
    ACTION_FAST,
    ACTION_RESTORE,
    ACTION_SLOW,
    ACTION_SWAP,
    ACTION_TROFF,
    ACTION_TRON_APPEND,
    ACTION_TRON,
    ACTION_TYPE,
} BRKACTION;

typedef struct {
    uint32_t    start;     // required
    uint32_t    end;       // = start if no range
    BRKMODE     mode;      // default = MODE_PC
    BRKACTION   action;    // default = ACTION_UNUSED
    int         count;     // optional; 0 if unset
    int         reset;     // optional; 0 if unset
    int         slot;
    int         device;
    char        *type_text;
} PARSEDBP;

int parse_line(const char *val, PARSEDBP *out);
int parse_breakpoint_line(const char *val, PARSEDBP *out);
