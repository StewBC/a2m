// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

#define a2_flags_def     uint32_t altcharset: 1;\
    uint32_t altzpset: 1;\
    uint32_t c3slotrom: 1;\
    uint32_t closed_apple: 1;\
    uint32_t col80set: 1;\
    uint32_t cxromset: 1;\
    uint32_t dhires: 1;\
    uint32_t franklin80installed: 1;\
    uint32_t franklin80active: 1;\
    uint32_t hires: 1;\
    uint32_t lc_bank2_enable: 1;\
    uint32_t lc_pre_write: 1;\
    uint32_t lc_read_ram_enable: 1;\
    uint32_t lc_write_enable: 1;\
    uint32_t mixed: 1;\
    uint32_t model: 1;\
    uint32_t open_apple: 1;\
    uint32_t page2set: 1;\
    uint32_t ramrdset: 1;\
    uint32_t ramwrtset: 1;\
    uint32_t store80set: 1;\
    uint32_t strobed: 1;\
    uint32_t text: 1;\
    uint32_t a2flags_pad: 9


typedef struct A2FLAGS {
    a2_flags_def;
} A2FLAGS;

typedef union {
    uint32_t u32;
    A2FLAGS  b;
} A2FLAGSPACK;
