// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

// int trace_log(UTIL_FILE *f, APPLE2 *m);
void trace_off(UTIL_FILE *f);
int trace_on(UTIL_FILE *f, const char *filename, const char *file_mode);