// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

#ifndef ASM_SCOPE_TYPEDEF
#define ASM_SCOPE_TYPEDEF
typedef struct SCOPE SCOPE;
#endif

#ifndef ASM_ASSEMBLER_TYPEDEF
#define ASM_ASSEMBLER_TYPEDEF
typedef struct ASSEMBLER ASSEMBLER;
#endif

typedef enum {
    SYMBOL_VARIABLE,
    SYMBOL_ADDRESS,
    SYMBOL_UNKNOWN,
    SYMBOL_LOCAL
} SYMBOL_TYPE;

typedef struct {
    SYMBOL_TYPE symbol_type;
    const char *symbol_name;
    uint32_t symbol_length;
    uint32_t symbol_hash;
    uint64_t symbol_value;
    uint32_t symbol_width;
} SYMBOL_LABEL;

typedef struct {
    char *user_name;
    uint32_t user_name_len;
    char *generated_name;
} RENAME_MAP;

typedef struct {
    const char *name;
    int name_len;
    char *value;
    int value_len;
} MACRO_ARG_VALUE;

typedef struct {
    const char *macro_name;
    uint32_t macro_name_length;
    AM65_DYNARRAY renames;
    AM65_DYNARRAY args;
} MACRO_EXPAND;

#define GEN_NAME_FMT "__macro_local_%04X"
#define GEN_NAME_LEN 18

int symbol_has_scope_path(const char *p, int len);

SYMBOL_LABEL *symbol_lookup_chain(SCOPE *s, uint32_t name_hash, const char *name, uint32_t len);
SYMBOL_LABEL *symbol_read(ASSEMBLER *as, const char *sym, uint32_t sym_len);

SYMBOL_LABEL *symbol_store_in_scope(ASSEMBLER *as, SCOPE *scope, const char *symbol_name, uint32_t symbol_name_length, SYMBOL_TYPE symbol_type, uint64_t value);
SYMBOL_LABEL *symbol_write(ASSEMBLER *as, const char *sym, uint32_t sym_len, SYMBOL_TYPE symbol_type, uint64_t value);

SYMBOL_LABEL *symbol_declare_local(ASSEMBLER *as, const char *name, uint32_t name_len);
int symbol_delete_local(SCOPE *scope, const char *symbol_name, uint32_t symbol_name_length, int force);
