// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

#ifndef ASM_ASSEMBLER_TYPEDEF
#define ASM_ASSEMBLER_TYPEDEF
typedef struct ASSEMBLER ASSEMBLER;
#endif

#include <stdint.h>

#include "file.h"

typedef struct {
    int was_true;
    int else_seen;
} IF_FRAME;

typedef enum {
    LOOP_FOR,
    LOOP_REPEAT
} LOOP_TYPE;

typedef struct {
    LOOP_TYPE type;
    ASM_FILE *body_file;
    const char *body_start;
    size_t body_line;
    union {
        struct {
            char *condition;
            char *adjust;
        };
        struct {
            int64_t max_iterations;
            char *var_name;
            int var_name_len;
        };
    };
    size_t iterations;
} LOOP;

typedef struct {
    char *name;
    int name_len;
} MACRO_PARAM;

typedef struct {
    char *name;
    int name_len;
    ASM_FILE *body_file;
    const char *body_start;
    size_t body_line;
    AM65_DYNARRAY parameters;
} MACRO;

int is_address(ASSEMBLER *as);
int is_label(ASSEMBLER *as);
int is_opcode(ASSEMBLER *as);
int is_parse_dot_command(ASSEMBLER *as);
int is_variable(ASSEMBLER *as);

void loop_stack_clear(ASSEMBLER *as);
void macro_definitions_clear(ASSEMBLER *as);
void macro_stack_clear(ASSEMBLER *as);
void macro_substitute_line(ASSEMBLER *as);
void parse_address(ASSEMBLER *as);
void parse_dot_command(ASSEMBLER *as);
void parse_if_skip(ASSEMBLER *as);
void parse_label(ASSEMBLER *as);
int parse_macro_if_is_macro(ASSEMBLER *as);
void parse_opcode(ASSEMBLER *as);
void parse_variable(ASSEMBLER *as);
