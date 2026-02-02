// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

typedef struct SCOPE SCOPE;

typedef enum {
    SYMBOL_UNKNOWN,
    SYMBOL_VARIABLE,
    SYMBOL_ADDRESS
} SYMBOL_TYPE;

// Named symbols lookup struct
typedef struct {
    SYMBOL_TYPE symbol_type;
    const char *symbol_name;                                // Non-null-terminated name
    uint32_t symbol_length;                                 // Name length
    uint64_t symbol_hash;
    uint64_t symbol_value;
    uint32_t symbol_width;                                  // 0 - depends on value, 16 not 1 byte
} SYMBOL_LABEL;

#define GEN_NAME_FMT "__macro_local_%04X"                   // printf fmt for a generated .local name
#define GEN_NAME_LEN 18                                     // max length of a GEN_NAME_FMT (excl `\0`)


int symbol_has_scope_path(const char *p, int len);

SYMBOL_LABEL *symbol_lookup_scope(SCOPE *scope, uint32_t name_hash, const char *name, uint32_t len);
SYMBOL_LABEL *symbol_read(ASSEMBLER *as, const char *sym, uint32_t sym_len);

SYMBOL_LABEL *symbol_store_in_scope(ASSEMBLER *as, SCOPE *scope, const char *symbol_name, uint32_t symbol_name_length, SYMBOL_TYPE symbol_type, uint64_t value);
SYMBOL_LABEL *symbol_write(ASSEMBLER *as, const char *sym, uint32_t sym_len, SYMBOL_TYPE symbol_type, uint64_t value);

SYMBOL_LABEL *symbol_declare_local(ASSEMBLER *as, const char *name, uint32_t name_len);
int symbol_delete_local(SCOPE *scope, const char *symbol_name, uint32_t symbol_name_length);
