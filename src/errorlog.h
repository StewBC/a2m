#pragma once

typedef struct ERRORLOG {
    DYNARRAY log_array;
} ERRORLOG;

extern ERRORLOG errorlog;

void errlog_init();
void errlog(const char *format, ...);
void errlog_clean();
void errlog_shutdown();
