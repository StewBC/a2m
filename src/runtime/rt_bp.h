// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

typedef struct RUNTIME RUNTIME;

typedef struct BREAKPOINT {
    uint16_t address;               // BP/watch address
    uint16_t address_range_end;     // if use_range, end of range value
    VIEW_FLAGS selected_bank;       // Memory in view for user, not 65x02
    int counter_count;              // Active count value
    int counter_stop_value;         // Where to fire
    int counter_reset;              // On fitst fire, set stop_value to this
    int slot;                       // For swap, slot
    int device;                     // For swap drive
    BRKACTION action;               // Break, swap, type, tron, etc.
    uint16_t access_mask;           // RAM_WATCH mask based on break_on_*
    uint16_t disabled: 1;           // Makes this basically a bookmark
    uint16_t break_on_exec: 1;      // Break on execution of opcode at address
    uint16_t break_on_read: 1;      // Break on read of address
    uint16_t break_on_write: 1;     // Break on write of address
    uint16_t use_range: 1;          // True if BP covers a range
    uint16_t use_counter: 1;        // True if counters matter
    uint16_t model: 1;              // True = //e, false = ][+
    uint16_t pad: 9;                // Extra available bits
    char *type_text;                // Action = type, text to "type" 
} BREAKPOINT;

BREAKPOINT *rt_find_breakpoint(RUNTIME *rt, uint16_t address);
void rt_exec_matching_breakpoint(RUNTIME *rt, uint16_t address);
void rt_bp_callback(void *user, uint16_t address, uint8_t mask);
void rt_apply_bp_mask(RUNTIME *rt, BREAKPOINT *bp, int clear);
void rt_apply_bp_masks(RUNTIME *rt);
