// Apple ][+ and //e Emhanced emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#pragma once

typedef struct EMUFLAGS {
    uint32_t debug_view: 1;
    uint32_t disk_activity_read: 1;
    uint32_t disk_activity_write: 1;
    uint32_t franklin80installed: 1;
    uint32_t model: 1;
    uint32_t monitor_type: 2;
    uint32_t original_del: 1;
    uint32_t step: 1;
    uint32_t stopped: 1;
    uint32_t trace: 1;
    uint32_t emuflags_pad: 21;
} EMUFLAGS;

typedef struct A2FLAGS {
    uint32_t altcharset: 1;
    uint32_t altzpset: 1;
    uint32_t c3slotrom: 1;
    uint32_t col80set: 1;
    uint32_t cxromset: 1;
    uint32_t dhires: 1;
    uint32_t franklin80active: 1;
    uint32_t hires: 1;
    uint32_t mixed: 1;
    uint32_t page2set: 1;
    uint32_t ramrdset: 1;
    uint32_t ramwrtset: 1;
    uint32_t store80set: 1;
    uint32_t strobed: 1;
    uint32_t text: 1;
    uint32_t a2flags_pad: 17;
} A2FLAGS;

typedef union {
    uint32_t u32;
    A2FLAGS  b;
} A2FLAGSPACK;
