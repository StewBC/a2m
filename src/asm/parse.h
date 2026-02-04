// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

typedef struct {
    const char *input;
    const char *token_start;
    const char *current_file;
    const char *next_line_start;
    size_t next_line_count;
    size_t current_line;
    const char *line_start;
    TOKEN token;
} INPUT_STACK;

typedef enum {
    LOOP_FOR,
    LOOP_REPEAT
} LOOP_TYPE;

typedef struct {
    LOOP_TYPE loop_type;
    union {
        struct {                                            // FOR
            const char *loop_condition_start;               // Points at condition
            const char *loop_adjust_start;                  // Points at loop counter adjust expression
        };
        struct {                                            // REPEAT
            int max_iterations;                             // how many times to loop
            const char *variable_name;
            int variable_name_lengt;                        // 0 if optional var not given
        };

    };
    const char *loop_body_start;                            // Points at text to execute in loop
    const char *loop_start_file;                            // .repeat and .endrepeat must be in same file
    size_t body_line;                                       // Line number where body loop starts
    size_t iterations;                                      // Break out of runaway loops
} LOOP;

typedef struct {
    const char *macro_name;
    int macro_name_length;
    INPUT_STACK macro_body_input;                           // Start of macro body text
    const char *macro_body_end;
    DYNARRAY macro_parameters;
} MACRO;

typedef struct {                                            // Maps a .local user name to a generated name
    const char *user_name;
    uint32_t user_name_len;
    char *generated_name;
} RENAME_MAP;

typedef struct {                                            // Tracks all the RENAM_MAPs for a macro
    const char *macro_name;
    uint32_t macro_name_length;
    DYNARRAY renames;                                       // Array of local to generated names
} MACRO_EXPAND;

// Parser helpers that asre used external to parse
int input_stack_push(ASSEMBLER *as);
void input_stack_pop(ASSEMBLER *as);

MACRO_EXPAND *macro_stack_push(ASSEMBLER *as, const char *name, uint32_t name_length);
void macro_stack_pop(ASSEMBLER *as);

void parse_address(ASSEMBLER *as);
void parse_dot_command(ASSEMBLER *as);
void parse_label(ASSEMBLER *as);
int parse_macro_if_is_macro(ASSEMBLER *as);
void parse_opcode(ASSEMBLER *as);
void parse_variable(ASSEMBLER *as);
