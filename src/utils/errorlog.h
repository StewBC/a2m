// Apple ][+ and //e Emhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

typedef struct ERROR_ENTRY {
    size_t line_number;
    size_t message_length;
    size_t supressed;
    uint32_t file_name_hash;
    char *err_str;
} ERROR_ENTRY;

typedef struct ERRORLOG {
    DYNARRAY log_array;
    size_t longest_error_message_length;
} ERRORLOG;

void errlog(ERRORLOG *log, ERROR_ENTRY *e);
void errlog_init(ERRORLOG *log);
void errlog_clean(ERRORLOG *log);
void errlog_shutdown(ERRORLOG *log);
