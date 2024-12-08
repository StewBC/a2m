// Apple ][+ emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#pragma once

#define OPCODE_JSR  0x20
#define OPCODE_RTS  0x60

#define CODE_LINES_COUNT    31                              // Should be dynamic based on window size

typedef struct SYMBOL {
    uint16_t pc;
    char *symbol_name;
    const char *symbol_source;
} SYMBOL;

typedef struct CODE_LINE {
    uint16_t pc;                                            // the program counter at this line
    uint16_t is_breakpoint:1;                               // Glag indicating there's a breakpoint on this line
    int length;                                             // How many bytes is this opcode and parameters
    char *text;                                             // the text of the code at this PC disassebled
} CODE_LINE;

typedef struct DEBUGGER {
    DYNARRAY *code_lines;
    int num_lines;                                          // Number of lines the display uses
    uint16_t cursor_pc;                                     // The pc the debugger wants to show (could be user controlled)
    size_t prev_stop_cycles;
    size_t stop_cycles;
    uint16_t symbol_view:2;
    DYNARRAY *symbols;
    DYNARRAY symbols_search;
    FLOWMANAGER flowmanager;
    ASSEMBLER_CONFIG assembler_config;
} DEBUGGER;

int viewdbg_add_symbol(DEBUGGER * d, const char *symbol_source, const char *symbol_name, size_t symbol_name_length, uint16_t address, int overwrite);
int viewdbg_add_symbols(DEBUGGER * d, const char *symbol_source, char *data, size_t data_length, int overwrite);
void viewdbg_build_code_lines(APPLE2 * m, uint16_t pc, int lines_needed);
int viewdbg_disassemble_line(APPLE2 * m, uint16_t pc, CODE_LINE * line);
char *viewdbg_find_symbols(DEBUGGER * d, uint32_t address);
int viewdbg_init(DEBUGGER * d, int num_lines);
int viewdbg_init_symbols(DEBUGGER * d);
uint16_t viewdbg_next_pc(APPLE2 * m, uint16_t pc);
uint16_t viewdbg_prev_pc(APPLE2 * m, uint16_t pc);
int viewdbg_process_event(APPLE2 * m, SDL_Event * e);
void viewdbg_remove_symbols(DEBUGGER * d, const char *symbol_source);
void viewdbg_show(APPLE2 * m);
void viewdbg_shutdown(DEBUGGER * d);
void viewdbg_set_run_to_pc(APPLE2 * m, uint16_t pc);
void viewdbg_step_over_pc(APPLE2 * m, uint16_t pc);
int viewdbg_symbol_sort(const void *lhs, const void *rhs);
int viewdbg_symbol_search_update(DEBUGGER * d);
void viewdbg_update(APPLE2 * m);
