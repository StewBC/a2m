// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

#include <stddef.h>

#ifndef ASM_ASSEMBLER_TYPEDEF
#define ASM_ASSEMBLER_TYPEDEF
typedef struct ASSEMBLER ASSEMBLER;
#endif

typedef struct {
    char *display_name;
    char *buf;
    size_t size;
} ASM_FILE;

typedef struct {
    ASM_FILE *file;
    const char *read_ptr;
    size_t line_num;
    int is_macro;
} FILE_FRAME;

int file_load(ASSEMBLER *as, const char *path);
char *file_resolve_path(ASSEMBLER *as, const char *path);
int file_stack_push(ASSEMBLER *as, ASM_FILE *f, const char *read_ptr, size_t line_num, int is_macro);
FILE_FRAME *file_stack_top(ASSEMBLER *as);
void file_stack_pop(ASSEMBLER *as);
int file_read_line(ASSEMBLER *as);
int file_stack_reset_for_pass2(ASSEMBLER *as);
void files_free(ASSEMBLER *as);
