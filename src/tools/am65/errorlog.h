// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "dynarray.h"

typedef struct {
    size_t line_number;
    size_t message_length;
    size_t suppressed;
    uint32_t file_name_hash;
    char *err_str;
} ERROR_ENTRY;

typedef struct {
    AM65_DYNARRAY log_array;
    size_t longest_error_message_length;
} ERRORLOG;

void errlog(ERRORLOG *log, ERROR_ENTRY *e);
void errlog_init(ERRORLOG *log);
void errlog_clean(ERRORLOG *log);
void errlog_shutdown(ERRORLOG *log);
