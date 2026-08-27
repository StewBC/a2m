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
/* Resolve relative to the current file only (no .search list). */
char *file_resolve_against_current(ASSEMBLER *as, const char *path);
/* Resolve for .include / .incbin: current dir first, then .search dirs.
   Returns NULL when no candidate can be opened (relative paths). */
char *file_resolve_path(ASSEMBLER *as, const char *path);
int file_path_is_directory(const char *path);
/* Heap string describing a failed open, including search candidates tried. */
char *file_format_open_miss(ASSEMBLER *as, const char *kind, const char *path);
void file_search_dirs_clear(ASSEMBLER *as);
/* Takes ownership of resolved_dir; frees it if duplicate or on failure. */
int file_search_dir_add(ASSEMBLER *as, char *resolved_dir);
/* Takes ownership of resolved_dir for the persistent seed list (-I / host). */
int file_seed_search_dir_add(ASSEMBLER *as, char *resolved_dir);
/* Replace search_dirs with a copy of seed_search_dirs. */
int file_search_dirs_reset_from_seed(ASSEMBLER *as);
void file_seed_search_dirs_clear(ASSEMBLER *as);
int file_stack_push(ASSEMBLER *as, ASM_FILE *f, const char *read_ptr, size_t line_num, int is_macro);
FILE_FRAME *file_stack_top(ASSEMBLER *as);
void file_stack_pop(ASSEMBLER *as);
int file_read_line(ASSEMBLER *as);
int file_stack_reset_for_pass2(ASSEMBLER *as);
void files_free(ASSEMBLER *as);
