// Apple ][+ and //e Emhanced emulator and assembler
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#include "header.h"

ERRORLOG errorlog;

void errlog(const char *format, ...) {
    if(as->pass == 2) {
        char arg_message[256], final_message[256];
        va_list args;
        va_start(args, format);

        if(errorlog.log_level < 1) {
            int start = errorlog.error_number < ERRORLOG_LAST_ERRORS ? errorlog.error_number : ERRORLOG_LAST_ERRORS - 1;
            for(; start >= 0; --start) {
                if(errorlog.error_line_number[start] == as->current_line && 0 == strcmp(errorlog.error_file_name[start], as->current_file)) {
                    errorlog.errors_supressed++;
                    return;
                }
            }
            int entry = errorlog.error_number % ERRORLOG_LAST_ERRORS;
            errorlog.error_line_number[entry] = as->current_line;
            errorlog.error_file_name[entry] = as->current_file;
            errorlog.error_number++;
        }

        vsnprintf(arg_message, 256, format, args);
        int message_length =
            snprintf(final_message, 256, "File: %s L:%05zu C:%03zu: %s", as->current_file, as->current_line,
                     as->token_start - as->line_start,
                     arg_message);
        char *err_str = strdup(final_message);
        if(err_str) {
            ARRAY_ADD(&errorlog.log_array, err_str);
        }
        if(message_length > errorlog.longest_error_message_length) {
            errorlog.longest_error_message_length = message_length;
        }
    }
}

void errlog_init() {
    ARRAY_INIT(&errorlog.log_array, char *);
    errlog_clean();
}

void errlog_clean() {
    size_t i;
    for(i = 0; i < errorlog.log_array.items; i++) {
        char *log_line = *ARRAY_GET(&errorlog.log_array, char *, i);
        free(log_line);
    }
    errorlog.log_array.items = 0;
    errorlog.error_number = 0;
    errorlog.errors_supressed = 0;
    errorlog.longest_error_message_length = 0;
}

void errlog_shutdown() {
    errlog_clean();
    array_free(&errorlog.log_array);
}
