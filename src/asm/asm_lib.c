// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "asm_lib.h"

#define MAX_LOG_LINES 100

// Turn text logs into ERROR_ENTRIES to store in an ERRORLOG
void asm_err(ASSEMBLER *as, const char *format, ...) {
    ERRORLOG *log = as->errorlog;
    if(as->pass == 2) {
        ERROR_ENTRY e;
        char temp_string[ASM_ERR_MAX_STR_LEN];
        size_t entries = log->log_array.items;
        
        if(entries > MAX_LOG_LINES) {
            return;
        } 

        uint32_t current_file_name_hash = utils_fnv_1a_hash(as->current_file, strlen(as->current_file));
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
        e.err_str = (char*)malloc(ASM_ERR_MAX_STR_LEN);
        e.line_number = as->current_line;
        e.file_name_hash = current_file_name_hash;
        e.message_length = snprintf(e.err_str, ASM_ERR_MAX_STR_LEN, "File: %s L:%05zu C:%03zu: %s", as->current_file, as->current_line, as->token_start - as->line_start, temp_string);
        errlog(as->errorlog, &e);
    }
}
