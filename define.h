// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

#ifndef ASM_ASSEMBLER_TYPEDEF
#define ASM_ASSEMBLER_TYPEDEF
typedef struct ASSEMBLER ASSEMBLER;
#endif

typedef struct {
    char *from;
    int from_len;
    char *to;
    int to_len;
} DEFINE;

void define_add(ASSEMBLER *as, const char *from, int from_len, const char *to, int to_len);
void define_substitute(ASSEMBLER *as);
void defines_free(ASSEMBLER *as);
