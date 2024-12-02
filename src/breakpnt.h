// Apple ][+ emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#pragma once

typedef struct BREAKPOINT {
    uint16_t address;
    uint16_t address_range_end;
    int counter_count;
    int counter_stop_value;
    int counter_reset;
    uint8_t access;                                         // 4 (write) | 2 (read) when !use_pc (address bp) [1 is port]
    uint8_t disabled:1;
    uint8_t use_pc:1;
    uint8_t break_on_read:1;
    uint8_t break_on_write:1;
    uint8_t use_range:1;
    uint8_t use_counter:1;
} BREAKPOINT;

typedef struct FLOWMANAGER {
    DYNARRAY breakpoints;
    uint16_t run_to_pc;
    int16_t jsr_counter;
    uint16_t run_to_pc_set:1;
    uint16_t run_to_rts_set:1;
} FLOWMANAGER;

void breakpoints_init(FLOWMANAGER *b);
BREAKPOINT *breakpoint_at(FLOWMANAGER * b, uint16_t pc, int running);
void breakpoint_callback(APPLE2 * m, uint16_t address);
void breakpoint_reapply_address_masks(APPLE2 * m);
