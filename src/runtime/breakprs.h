// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

typedef enum {
    ACTION_BREAK,
    ACTION_FAST,
    ACTION_RESTORE,
    ACTION_SLOW,
    ACTION_SWAP,
    ACTION_TROFF,
    ACTION_TRON,
    ACTION_TYPE,
} BRKACTION;

typedef struct {
    uint32_t    start;     // required
    uint32_t    end;       // = start if no range
    uint32_t    mode;      // default = BREAK_MODE_PC
    BRKACTION   action;    // default = ACTION_BREAK
    int         count;     // optional; 0 if unset
    int         reset;     // optional; 0 if unset
    int         slot;
    int         device;
    char        *type_text;
} PARSEDBP;

int parse_line(const char *val, PARSEDBP *out);
int parse_breakpoint_line(const char *val, PARSEDBP *out);
char *parse_decode_c_string(const char *in, size_t *out_len);
int parse_encode_c_string(const char *in, char *out, int out_len);
