// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "asm_lib.h"

void errlog(ERRORLOG *log, ERROR_ENTRY *e) {
    AM65_ARRAY_ADD(&log->log_array, *e);
    if(e->err_str && e->message_length > log->longest_error_message_length) {
        log->longest_error_message_length = e->message_length;
    }
}

void errlog_init(ERRORLOG *log) {
    memset(log, 0, sizeof(ERRORLOG));
    AM65_ARRAY_INIT(&log->log_array, ERROR_ENTRY);
}

void errlog_clean(ERRORLOG *log) {
    for(size_t i = 0; i < log->log_array.items; i++) {
        ERROR_ENTRY *e = AM65_ARRAY_GET(&log->log_array, ERROR_ENTRY, i);
        free(e->err_str);
    }
    log->log_array.items = 0;
    log->longest_error_message_length = 0;
}

void errlog_shutdown(ERRORLOG *log) {
    errlog_clean(log);
    am65_array_free(&log->log_array);
}
