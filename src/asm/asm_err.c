// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "asm_lib.h"

#define MAX_LOG_LINES 100

// Turn text logs into ERROR_ENTRIES to store in an ERRORLOG
void asm_err(ASSEMBLER *as, ASM_ERR_CLASS cls,const char *format, ...) {
    ERRORLOG *log = as->errorlog;
    int active_log_level = as->error_log_level;
    switch(cls) {
    case ASM_ERR_RESOLVE:
        if(as->pass == 1) {     // Detect and show in Pass 2
            return;             // Many things unresolved in pass 1
        }
        break;
    case ASM_ERR_DEFINE:
        if(as->pass == 2) {     // Detect and show in Pass 1
             return;            // Avoid 2x logging in Pass 2
        }
        break;
    case ASM_ERR_FATAL:
        /* never suppress */
        as->error_log_level = 1;
        break;
    }    
    ERROR_ENTRY e;
    char temp_string[ASM_ERR_MAX_STR_LEN];
    size_t entries = log->log_array.items;

    if(entries > MAX_LOG_LINES) {
        return;
    }

    uint32_t current_file_name_hash = util_fnv_1a_hash(as->current_file, strlen(as->current_file));
    if(entries < MAX_LOG_LINES) {
        va_list args;
        va_start(args, format);

        if(as->error_log_level < 1) {
            for(int i = 0; i < entries; i++) {
                ERROR_ENTRY *le = ARRAY_GET(&log->log_array, ERROR_ENTRY, i);
                if(le->line_number == as->current_line && le->file_name_hash == current_file_name_hash) {
                    le->supressed++;
                    return;
                }
            }
        }
        vsnprintf(temp_string, ASM_ERR_MAX_STR_LEN, format, args);
    } else {
        snprintf(temp_string, ASM_ERR_MAX_STR_LEN, "%zu Errors logged.  Logging stopped.", entries);
    }

    memset(&e, 0, sizeof(ERROR_ENTRY));
    // SQW -This can go wrong and needs to be looked at:
    // If, for example, a .strcode is in effect, the line/col numbers will
    // be wrong, making the error report wrong (inaccurate about where...)
    e.err_str = (char *)malloc(ASM_ERR_MAX_STR_LEN);
    e.line_number = as->current_line;
    e.file_name_hash = current_file_name_hash;
    size_t col = as->line_start < as->token_start ? as->token_start - as->line_start : 0;
    e.message_length = snprintf(e.err_str, ASM_ERR_MAX_STR_LEN, "File: %s L:%05zu C:%03zu: %s", as->current_file, as->current_line, col, temp_string);
    errlog(as->errorlog, &e);
    as->error_log_level = active_log_level;
}
