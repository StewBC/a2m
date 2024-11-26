// Apple ][+ emulator and assembler
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#pragma once

typedef struct ERRORLOG {
    DYNARRAY log_array;
} ERRORLOG;

extern ERRORLOG errorlog;

void errlog_init();
void errlog(const char *format, ...);
void errlog_clean();
void errlog_shutdown();
