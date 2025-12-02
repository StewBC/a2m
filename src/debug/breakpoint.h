// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

typedef struct BREAKPOINT {
    uint16_t address;
    uint16_t address_range_end;
    int counter_count;
    int counter_stop_value;
    int counter_reset;
    int slot;
    int device;
    BRKACTION action;
    uint16_t access;                // 4 (write) | 2 (read) when !use_pc (address bp) [1 is port]
    uint16_t disabled: 1;
    uint16_t use_pc: 1;
    uint16_t break_on_read: 1;
    uint16_t break_on_write: 1;
    uint16_t use_range: 1;
    uint16_t use_counter: 1;
    uint16_t pad: 10;
    char *type_text;
} BREAKPOINT;
