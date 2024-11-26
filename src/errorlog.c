// Apple ][+ emulator and assembler
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#include "header.h"

ERRORLOG errorlog;

void errlog_init() {
    ARRAY_INIT(&errorlog.log_array, char *);
}

void errlog(const char *format, ...) {
    if(as->pass == 2) {
        char arg_message[256], final_message[256];
        va_list args;
        va_start(args, format);
        vsnprintf(arg_message, 256, format, args);
        snprintf(final_message, 256, "File: %s L:%05zu C:%03zu: %s", as->current_file, as->current_line, as->token_start - as->line_start,
                 arg_message);
        char *err_str = strdup(final_message);
        if(err_str) {
            ARRAY_ADD(&errorlog.log_array, err_str);
        }
    }
}

void errlog_clean() {
    size_t i;
    for(i = 0; i < errorlog.log_array.items; i++) {
        char *log_line = *ARRAY_GET(&errorlog.log_array, char *, i);
        free(log_line);
    }
    errorlog.log_array.items = 0;
}

void errlog_shutdown() {
    errlog_clean();
    array_free(&errorlog.log_array);
}
