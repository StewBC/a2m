// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

typedef struct {
    DYNARRAY included_files;                                // Array of all files loaded (UTIL_FILE)
    DYNARRAY stack;                                         // .include causes a push of INCLUDE_FILE_DATA
} INCLUDE_FILES;

void include_files_cleanup(ASSEMBLER *as);
UTIL_FILE *include_files_find_file(ASSEMBLER *as, const char *file_name);
void include_files_init(ASSEMBLER *as);
int include_files_pop(ASSEMBLER *as);
int include_files_push(ASSEMBLER *as, const char *file_name);
