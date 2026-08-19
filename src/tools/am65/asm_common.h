// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

#include <stddef.h>
#include <stdint.h>

#define ASM_OK  0
#define ASM_ERR 1

#define HASH_BUCKETS 64
#define HASH_MASK (HASH_BUCKETS - 1)

typedef enum assembler_cpu_profile {
    ASM_CPU_6502 = 0,
    ASM_CPU_65C02,
    ASM_CPU_ROCKWELL,
    ASM_CPU_WDC
} assembler_cpu_profile;

#ifndef ASM_ASSEMBLER_TYPEDEF
#define ASM_ASSEMBLER_TYPEDEF
typedef struct ASSEMBLER ASSEMBLER;
#endif

typedef struct OPCODEINFO {
    const char *mnemonic;
    uint8_t opcode_id;
    uint8_t width;
    uint8_t addressing_mode;
    uint64_t value;
} OPCODEINFO;

static inline uint32_t asm_fnv_1a_hash(const char *s, size_t len) {
    uint32_t hash = 2166136261u;
    for(size_t i = 0; i < len; i++) {
        hash ^= (uint8_t)s[i];
        hash *= 16777619u;
    }
    return hash;
}

static inline int asm_strnicmp(const char *a, const char *b, size_t len) {
    for(size_t i = 0; i < len; i++) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        if(ca >= 'A' && ca <= 'Z') {
            ca = (unsigned char)(ca + ('a' - 'A'));
        }
        if(cb >= 'A' && cb <= 'Z') {
            cb = (unsigned char)(cb + ('a' - 'A'));
        }
        if(ca != cb) {
            return (int)ca - (int)cb;
        }
    }
    return 0;
}
