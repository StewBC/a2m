// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "asm_lib.h"

typedef struct {
    const char *file_name;                                  // Name of the file in which .include encountered
    const char *input;                                      // Input token position when .include encountered
    size_t line_number;                                     // Line number when include .include encountered
} INCLUDE_FILE_DATA;

//----------------------------------------------------------------------------
// Include file stack management
void include_files_cleanup(ASSEMBLER *as) {
    size_t i;
    // discard all the files
    for(i = 0; i < as->include_files.included_files.items; i++) {
        UTIL_FILE *included_file = ARRAY_GET(&as->include_files.included_files, UTIL_FILE, i);
        util_file_discard(included_file);
    }
    // Discard the arrays
    array_free(&as->include_files.included_files);
    array_free(&as->include_files.stack);
}

UTIL_FILE *include_files_find_file(ASSEMBLER *as, const char *file_name) {
    size_t i;
    // Search for a matching file by name
    for(i = 0; i < as->include_files.included_files.items; i++) {
        UTIL_FILE *f = ARRAY_GET(&as->include_files.included_files, UTIL_FILE, i);
        if(0 == stricmp(f->file_path, file_name)) {
            return f;
        }
    }
    return NULL;
}

void include_files_init(ASSEMBLER *as) {
    ARRAY_INIT(&as->include_files.included_files, UTIL_FILE);
    ARRAY_INIT(&as->include_files.stack, INCLUDE_FILE_DATA);
}

int include_files_pop(ASSEMBLER *as) {
    // when the last item is popped, there is nothing to "pop to" so error
    if(as->include_files.stack.items <= 1) {
        as->include_files.stack.items = 0;
        return A2_ERR;
    }
    // Pop to the previous item on the stack
    as->include_files.stack.items--;
    INCLUDE_FILE_DATA *pd = ARRAY_GET(&as->include_files.stack, INCLUDE_FILE_DATA, as->include_files.stack.items);
    as->current_file = pd->file_name;
    as->token_start = as->input = pd->input;
    as->current_line = pd->line_number;
    as->next_line_count = 0;
    return A2_OK;
}

int include_files_push(ASSEMBLER *as, const char *file_name) {
    int recursive_include = 0;
    UTIL_FILE new_file;

    // See if the file had previously been loaded
    UTIL_FILE *f = include_files_find_file(as, file_name);
    if(!f) {
        // If not, it's a new file, load it
        memset(&new_file, 0, sizeof(UTIL_FILE));
        new_file.load_padding = 1;

        if(A2_OK == util_file_load(&new_file, file_name, "r")) {
            // Success, add to list of loaded files and assign it to f
            if(A2_OK != ARRAY_ADD(&as->include_files.included_files, new_file)) {
                asm_err(as, ASM_ERR_FATAL, "Out of memory");
            }
            f = &new_file;
        }
    } else {
        size_t i;
        // See if previously loaded file is in the stack - then this is a recursive include
        for(i = 0; i < as->include_files.stack.items; i++) {
            INCLUDE_FILE_DATA *pd = ARRAY_GET(&as->include_files.stack, INCLUDE_FILE_DATA, i);
            if(0 == stricmp(pd->file_name, file_name)) {
                recursive_include = 1;
                break;
            }
        }
    }

    if(!f) {
        asm_err(as, ASM_ERR_FATAL, "Error loading file %s", file_name);
        return A2_ERR;
    } else if(recursive_include) {
        asm_err(as, ASM_ERR_DEFINE, "Recursive included of file %s ignored", file_name);
        return A2_ERR;
    }
    // Push the file onto the stack, documenting the current parse data
    INCLUDE_FILE_DATA pd;
    pd.file_name = as->current_file;
    pd.input = as->input;
    pd.line_number = as->current_line;
    as->current_file = f->file_path;

    if(A2_OK != ARRAY_ADD(&as->include_files.stack, pd)) {
        asm_err(as, ASM_ERR_FATAL, "Out of memory");
    }
    // Prepare to parse the buffer that has now become active
    as->current_file = f->file_path;
    as->line_start = as->token_start = as->input = f->file_data;
    as->next_line_count = 0;
    as->current_line = 1;
    return A2_OK;
}
