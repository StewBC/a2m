#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "disasm_6502.h"

typedef struct symbol_table symbol_table;

typedef enum symbol_source_kind {
    SYMBOL_SOURCE_FILE = 0,
    SYMBOL_SOURCE_ASSEMBLER,
    SYMBOL_SOURCE_USER,
    SYMBOL_SOURCE_BUILTIN
} symbol_source_kind;

typedef enum symbol_result {
    SYMBOL_OK = 0,
    SYMBOL_NOT_FOUND,
    SYMBOL_ALREADY_EXISTS,
    SYMBOL_REPLACED,
    SYMBOL_CONFLICT,
    SYMBOL_INVALID,
    SYMBOL_OUT_OF_MEMORY
} symbol_result;

typedef struct symbol_info {
    const char *name;
    const char *scope_path;
    const char *display_name;
    size_t scope_path_length;
    size_t display_name_length;
    uint16_t address;
    symbol_source_kind source_kind;
    const char *source_name;
} symbol_info;

symbol_table *symbol_table_create(void);
void symbol_table_destroy(symbol_table *table);
void symbol_table_clear(symbol_table *table);

symbol_result symbol_table_add(
    symbol_table *table,
    uint16_t address,
    const char *name,
    symbol_source_kind source_kind,
    const char *source_name,
    bool overwrite);

symbol_result symbol_table_remove_source(
    symbol_table *table,
    symbol_source_kind source_kind,
    const char *source_name);

symbol_result symbol_table_remove_kind(
    symbol_table *table,
    symbol_source_kind source_kind);

symbol_result symbol_table_find_by_address(
    const symbol_table *table,
    uint16_t address,
    symbol_info *out_symbol);

symbol_result symbol_table_find_by_name(
    const symbol_table *table,
    const char *name,
    symbol_info *out_symbol);

symbol_result symbol_table_find_nearest_before(
    const symbol_table *table,
    uint16_t address,
    uint16_t max_offset,
    symbol_info *out_symbol,
    uint16_t *out_offset);

size_t symbol_table_count(const symbol_table *table);
symbol_result symbol_table_get(
    const symbol_table *table,
    size_t index,
    symbol_info *out_symbol);

symbol_result symbol_table_load_file(
    symbol_table *table,
    const char *path,
    const char *source_name,
    size_t *out_loaded);

void symbol_table_make_resolver(symbol_table *table, symbol_resolver *resolver);
