// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

// Emit writes through an output function inside a conext
typedef void (*output_byte)(void *user, uint16_t address, uint8_t byte_value);

typedef struct {
    void *user;
    output_byte output_byte;
} CB_ASSEMBLER_CTX;


typedef struct ASSEMBLER {
    CB_ASSEMBLER_CTX cb_assembler_ctx;                      // Emit uses this FNP to output the actual byte
    DYNARRAY anon_symbols;                                  // Array of anonymous symbols
    DYNARRAY input_stack;                                   // Array of token "reset" points (saved token_start, input, etc)
    DYNARRAY loop_stack;                                    // Array of for loops
    DYNARRAY macros;                                        // Array of all macros
    DYNARRAY macro_buffers;                                 // Array of all buffers that macros expand into
    DYNARRAY macro_expand_stack;                            // Tracks macro renames per invocation
    DYNARRAY segments;                                      // .segdef creates segment defenitions
    DYNARRAY scope_stack;                                   // Needed since scopes can be created outside parent
    INCLUDE_FILES include_files;                            // The arrays for files and stack for .include
    OPCODEINFO opcode_info;                                 // State of what is to be emitted in terms of 6502 opcodes
    TOKEN current_token;                                    // What is being parsed
    int error_log_level;                                    // logging level (0 - filter duplicates; 1 - all to limit)
    int expression_size;                                    // Forward defs can't change size (16 bit can't become 8 later)
    int macro_rename_id;                                    // Makes all .local's in macros unique
    int pass;                                               // 1 or 2 for 2 pass assembler
    int valid_opcodes;                                      // 0 = 65c02 (default), 1 = 6502
    int verbose;                                            // cmd-line; 0 supress duplicates, 1 show all (up to 100)
    size_t current_line;                                    // for error reporting, line being processed
    size_t next_line_count;                                 // count of lines past last token
    uint16_t current_address;                               // Address where next byte will be emitted
    uint16_t if_active;                                     // Count of if's (or else's) active
    uint16_t last_address;                                  // Last address where the assembler put a byte
    uint16_t start_address;                                 // First address where the assembler output a byte
    const char *current_file;                               // Points at a UTIL_FILE path_name
    const char *input;                                      // Points at the assembly language buffer (start through end)
    const char *line_start;                                 // Just past \n of line input is on
    const char *next_line_start;                            // So errors get reported on line of last token
    const char *strcode;                                    // Active .strcode expression
    const char *token_start;                                // Points at the start of a token (and input the end)
    SCOPE *root_scope;
    SCOPE *active_scope;
    SEGMENT *active_segment;
    DYNARRAY *symbol_table;                                 // Array of arrays of symbols
    ERRORLOG *errorlog;                                     // ptr to log that tracks errors
} ASSEMBLER;

static inline uint16_t current_output_address(ASSEMBLER *as) {
        return as->active_segment ? as->active_segment->segment_output_address : as->current_address;
}

int is_label(ASSEMBLER *as);
int is_opcode(ASSEMBLER *as);
int is_parse_dot_command(ASSEMBLER *as);
int is_variable(ASSEMBLER *as);

int assembler_init(ASSEMBLER *as, ERRORLOG *errorlog, void *user, output_byte ob);
int assembler_assemble(ASSEMBLER *as, const char *input_file, uint16_t address);
void assembler_shutdown(ASSEMBLER *as);
