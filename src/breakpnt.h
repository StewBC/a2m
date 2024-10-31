// Apple ][+ emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#pragma once

typedef enum CONDITION {
    CONDITION_NONE      = 0,
    CONDITION_PC        = (1<<0),
    CONDITION_COUNT     = (1<<1),
    CONDITION_REG_VALUE = (1<<2),
    CONDITION_MEM_VALUE = (1<<3),
    CONDITION_ONE_TIME  = (1<<4),
} CONDITION;

// enum {
//     VALUE_A,
//     VALUE_X,
//     VALUE_Y,
//     VALUE_ADDRESS,
// };

typedef struct BREAKPOINT {
    uint16_t    pc;
    uint16_t    counter;
    CONDITION   condition;
    uint8_t     address;
    uint8_t     value[4]; // a, x, y, address
    uint8_t     disabled:1;
} BREAKPOINT;

typedef struct FLOWMANAGER {
    DYNARRAY    breakpoints;
    uint16_t    run_to_pc;
    int16_t     jsr_counter;
    uint16_t    run_to_pc_set:1;
    uint16_t    run_to_rts_set:1;
} FLOWMANAGER;

void breakpoints_init();
BREAKPOINT *get_breakpoint_at(FLOWMANAGER *b, uint16_t pc);
