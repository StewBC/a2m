// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

typedef struct UTXT {
    // Window title MHz display helpers
    uint64_t prev_cycles;
    uint64_t prev_ticks;
    double mhz_moving_average;
    double fps_moving_average;

    // Shadow of machine states
    // int shadow_flags;
    A2FLAGSPACK shadow_flags;
    int help_page;

    // Game controller values
    int8_t num_controllers;
    uint8_t button_a[2];
    uint8_t button_b[2];
    uint8_t button_x[2];
    uint8_t axis_left_x[2];
    uint8_t axis_left_y[2];
    uint64_t ptrig_cycle;

    // Flags that contain machine states
    uint32_t debug_view: 1;
    uint32_t dlg_assembler_config: 1;
    uint32_t dlg_assembler_errors: 1;
    uint32_t dlg_breakpoint: 1;
    uint32_t dlg_filebrowser: 1;
    uint32_t dlg_memory_find: 1;
    uint32_t dlg_memory_go: 1;
    uint32_t dlg_symbol_lookup_dbg: 1;
    uint32_t dlg_symbol_lookup_mem: 1;
    uint32_t shadow_run: 1;
    uint32_t show_help: 1;
    uint32_t show_leds: 1;
    uint32_t dlg_modal_active: 1;
    uint32_t display_override: 1;
    uint32_t clear_a2_view: 1;
} UTXT;

extern const UI_OPS utxt_ops;

int utxt_init(UTXT *v, INI_STORE *ini_store);
void utxt_shutdown(UTXT *v);
