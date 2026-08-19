#pragma once

#include "symbol_table.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct runtime_assembler_options {
    bool auto_adjust_segments;
    bool enable_65c02;
} runtime_assembler_options;

bool runtime_assemble_file_legacy(
    void *machine,
    symbol_table *symbols,
    const char *path,
    uint16_t address,
    const char *source_name,
    char *error,
    size_t error_size);

bool runtime_assemble_file(
    void *machine,
    symbol_table *symbols,
    const char *path,
    uint16_t address,
    const char *source_name,
    char *error,
    size_t error_size);

/* out_start_address: lowest/first emitted origin (where code landed).
   out_end_address:   one past the highest emitted byte (BASIC VARTAB target).
   out_byte_count:    total bytes emitted. Any out-param may be NULL. */
bool runtime_assemble_file_ex(
    void *machine,
    symbol_table *symbols,
    const char *path,
    uint16_t address,
    const char *source_name,
    uint16_t *out_start_address,
    uint16_t *out_end_address,
    uint32_t *out_byte_count,
    char *error,
    size_t error_size);

bool runtime_assemble_file_ex_options(
    void *machine,
    symbol_table *symbols,
    const char *path,
    uint16_t address,
    const char *source_name,
    const runtime_assembler_options *options,
    uint16_t *out_start_address,
    uint16_t *out_end_address,
    uint32_t *out_byte_count,
    char *notice,
    size_t notice_size,
    char *error,
    size_t error_size);
