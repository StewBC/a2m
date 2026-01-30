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

SYMBOL_LABEL *symbol_lookup(ASSEMBLER *as, uint32_t name_hash, const char *symbol_name, uint32_t symbol_name_length);
SYMBOL_LABEL *symbol_lookup_parent_chain(ASSEMBLER *as, uint32_t name_hash, const char *symbol_name, uint32_t symbol_name_length);
SYMBOL_LABEL *symbol_lookup_local(ASSEMBLER *as, uint32_t name_hash, const char *symbol_name, uint32_t symbol_name_length);

int symbol_delete_local(ASSEMBLER *as, uint32_t name_hash, const char *symbol_name, uint32_t symbol_name_length);

SYMBOL_LABEL *symbol_store_in_scope(ASSEMBLER *as, SCOPE *scope, const char *symbol_name, uint32_t symbol_name_length, SYMBOL_TYPE symbol_type, uint64_t value);
SYMBOL_LABEL *symbol_store_qualified(ASSEMBLER *as, const char *symbol_name, uint32_t symbol_name_length, SYMBOL_TYPE symbol_type, uint64_t value);
