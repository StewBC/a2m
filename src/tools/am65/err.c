// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "asm_lib.h"

#define MAX_LOG_LINES 100

static uint32_t fnv_1a_hash(const char *s) {
    uint32_t hash = 2166136261u;
    while(s && *s) {
        hash ^= (uint8_t)*s++;
        hash *= 16777619u;
    }
    return hash;
}

void asm_err(ASSEMBLER *as, ASM_ERR_CLASS cls, const char *format, ...) {
    if(!as || !as->errorlog) {
        return;
    }

    int active_log_level = as->error_log_level;
    switch(cls) {
    case ASM_ERR_RESOLVE:
        if(as->pass == 1) {
            return;
        }
        break;
    case ASM_ERR_DEFINE:
        if(as->pass == 2) {
            return;
        }
        break;
    case ASM_ERR_FATAL:
        as->error_log_level = 1;
        break;
    }

    ERRORLOG *log = as->errorlog;
    size_t entries = log->log_array.items;
    if(entries > MAX_LOG_LINES) {
        return;
    }

    const char *file_name = as->current_file_name ? as->current_file_name : "<unknown>";
    if(as->root_dir && as->current_file_name) {
        size_t root_len = strlen(as->root_dir);
        if(strncmp(as->current_file_name, as->root_dir, root_len) == 0) {
            file_name = as->current_file_name + root_len;
        }
    }
    uint32_t file_name_hash = fnv_1a_hash(file_name);
    if(as->error_log_level < 1) {
        for(size_t i = 0; i < entries; i++) {
            ERROR_ENTRY *le = AM65_ARRAY_GET(&log->log_array, ERROR_ENTRY, i);
            if(le->line_number == as->current_line && le->file_name_hash == file_name_hash) {
                le->suppressed++;
                return;
            }
        }
    }

    char temp_string[ASM_ERR_MAX_STR_LEN];
    if(entries < MAX_LOG_LINES) {
        va_list args;
        va_start(args, format);
        vsnprintf(temp_string, ASM_ERR_MAX_STR_LEN, format, args);
        va_end(args);
    } else {
        snprintf(temp_string, ASM_ERR_MAX_STR_LEN, "%zu errors logged. Logging stopped.", entries);
    }

    ERROR_ENTRY e;
    memset(&e, 0, sizeof(e));
    e.err_str = malloc(ASM_ERR_MAX_STR_LEN);
    if(!e.err_str) {
        as->error_log_level = active_log_level;
        return;
    }

    size_t col = 0;
    if(as->cur && as->cur >= as->line) {
        col = (size_t)(as->cur - as->line);
    }

    e.line_number = as->current_line;
    e.file_name_hash = file_name_hash;
    e.message_length = (size_t)snprintf(e.err_str, ASM_ERR_MAX_STR_LEN,
                                        "File: %s L:%05zu C:%03zu: %s",
                                        file_name, as->current_line, col, temp_string);
    errlog(log, &e);
    as->error_log_level = active_log_level;
}
