// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

typedef enum {
    UI_CLASS_GUI,
    UI_CLASS_TEXT
} UI_CLASS;

enum {
    SYMBOL_VIEW_ALL,
    SYMBOL_VIEW_MARGIN_FUNC,
    SYMBOL_VIEW_MARGIN,
    SYMBOL_VIEW_NONE,
};

typedef struct UI UI;
typedef struct UI_OPS UI_OPS;

struct UI_OPS {
    void (*cpu_update)(UI *ui, APPLE2 *m);
    void (*disk_read_led)(UI *ui);
    void (*disk_write_led)(UI *ui);
    int (*process_events)(UI *ui, APPLE2 *m);
    void (*ptrig)(UI *ui, uint64_t cycle);
    uint8_t (*read_axis)(UI *ui, int controller_id, int axis_id, uint64_t cycle);
    uint8_t (*read_button)(UI *ui, int controller_id, int button_id);
    void (*render)(UI *ui, APPLE2 *m, int cursor_sync);
    void (*set_runtime)(UI *ui, RUNTIME *rt);
    void (*set_shadow_flags)(UI *ui, uint32_t flags);
    void (*shutdown)(UI *ui);
    void (*speaker_on_cycles)(UI *ui, uint32_t cycles_executed);
    void (*speaker_toggle)(UI *ui);
};

struct UI {
    const UI_OPS *ops;
    UI_CLASS class;
    void *user;   // UI_NUKLEAR*, UI_CLI*, etc.
    int reconfig;
};

typedef enum KEY_MOD {
    KEYMOD_OPEN_APPLE,
    KEYMOD_CLOSED_APPLE,
} KEY_MOD;

typedef struct RUNTIME {
    APPLE2 *m;

    // Turbo (1MHz+ settings)
    double turbo_active;                                    // turbo[turbo_index] unless bp->speed override
    uint32_t turbo_index;                                   // active entry in turbo array
    uint32_t turbo_count;                                   // entries in turbo array
    double *turbo;                                          // Array of multipliers (* 1MHz) to run at

    // Flow
    uint16_t pc_to_run_to;
    int16_t jsr_counter;

    // Counters
    size_t prev_stop_cycles;
    size_t stop_cycles;

    // Flags
    uint16_t run: 1;                                    // 1 - not running, 0 - run
    uint16_t run_step: 1;
    uint16_t run_to_pc: 1;                              // 1 - stepping / conditionally running
    uint16_t run_step_out: 1;                             // 1 - step out
    uint16_t pad: 13;

    // debugger and symbold
    DYNARRAY breakpoints;
    DYNARRAY symbols_search;

    // Trace
    TRACE_LOG trace_log;

    // Clipboard
    int clipboard_index;
    char *clipboard_text;

    // FLOWMANAGER flowmanager;
    DYNARRAY *symbols;
} RUNTIME;

#define adjust(a,b,c)           do { a += c; b -= c; } while (0)

void rt_bind(RUNTIME *rt, APPLE2 *m, UI *ui);
int rt_init(RUNTIME *rt, INI_STORE *ini_store);
int rt_run(RUNTIME *rt, APPLE2 *m, UI *ui);
void rt_shutdown(RUNTIME *rt);

// UI accesses rt through these calls
void rt_machine_reset(RUNTIME *rt);
void rt_brk_callback(RUNTIME *rt, uint16_t address, uint8_t mask);
int rt_disassemble_line(RUNTIME *rt, uint16_t *address, int selected, char symbol_view, char *str_buf, int str_buf_len);

// Debugger control
void rt_machine_pause(RUNTIME *rt);
void rt_machine_run(RUNTIME *rt);
void rt_machine_run_to_pc(RUNTIME *rt, uint16_t pc);
void rt_machine_set_pc(RUNTIME *rt, uint16_t pc);
void rt_machine_set_sp(RUNTIME *rt, uint16_t sp);
void rt_machine_set_A(RUNTIME *rt, uint8_t A);
void rt_machine_set_X(RUNTIME *rt, uint8_t X);
void rt_machine_set_Y(RUNTIME *rt, uint8_t Y);
void rt_machine_set_flags(RUNTIME *rt, uint8_t flags);
void rt_machine_step(RUNTIME *rt);
void rt_machine_step_out(RUNTIME *rt);
void rt_machine_step_over(RUNTIME *rt);
void rt_machine_stop(RUNTIME *rt);
void rt_paste_clipboard(RUNTIME *rt, char *clipboard_text);
void rt_machine_toggle_franklin80_active(RUNTIME *rt);
int rt_feed_clipboard_key(RUNTIME *rt);
