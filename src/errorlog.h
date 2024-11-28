// Apple ][+ emulator and assembler
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#pragma once

#define ERRORLOG_LAST_ERRORS    20

typedef struct ERRORLOG {
    DYNARRAY log_array;
    int log_level;
    size_t error_number;
    size_t errors_supressed;
    size_t longest_error_message_length;
    int error_line_number[ERRORLOG_LAST_ERRORS];
    const char *error_file_name[ERRORLOG_LAST_ERRORS];
} ERRORLOG;

extern ERRORLOG errorlog;

void errlog(const char *format, ...);
void errlog_init();
void errlog_clean();
void errlog_shutdown();
