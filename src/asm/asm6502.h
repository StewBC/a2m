// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

// Also see ../gperf/asm6502.gperf
enum {
    GPERF_DOT_6502,
    GPERF_DOT_65c02,
    GPERF_DOT_ALIGN,
    GPERF_DOT_BYTE,
    GPERF_DOT_DROW,
    GPERF_DOT_DROWD,
    GPERF_DOT_DROWQ,
    GPERF_DOT_DWORD,
    GPERF_DOT_ELSE,
    GPERF_DOT_ENDFOR,
    GPERF_DOT_ENDIF,
    GPERF_DOT_ENDMACRO,
    GPERF_DOT_FOR,
    GPERF_DOT_IF,
    GPERF_DOT_INCBIN,
    GPERF_DOT_INCLUDE,
    GPERF_DOT_MACRO,
    GPERF_DOT_ORG,
    GPERF_DOT_QWORD,
    GPERF_DOT_STRCODE,
    GPERF_DOT_STRING,
    GPERF_DOT_WORD,
};

enum {
    GPERF_OPCODE_ADC,
    GPERF_OPCODE_AND,
    GPERF_OPCODE_ASL,
    GPERF_OPCODE_BCC,
    GPERF_OPCODE_BCS,
    GPERF_OPCODE_BEQ,
    GPERF_OPCODE_BIT,
    GPERF_OPCODE_BMI,
    GPERF_OPCODE_BNE,
    GPERF_OPCODE_BPL,
    GPERF_OPCODE_BRA,
    GPERF_OPCODE_BRK,
    GPERF_OPCODE_BVC,
    GPERF_OPCODE_BVS,
    GPERF_OPCODE_CLC,
    GPERF_OPCODE_CLD,
    GPERF_OPCODE_CLI,
    GPERF_OPCODE_CLV,
    GPERF_OPCODE_CMP,
    GPERF_OPCODE_CPX,
    GPERF_OPCODE_CPY,
    GPERF_OPCODE_DEA,
    GPERF_OPCODE_DEC,
    GPERF_OPCODE_DEX,
    GPERF_OPCODE_DEY,
    GPERF_OPCODE_EOR,
    GPERF_OPCODE_INA,
    GPERF_OPCODE_INC,
    GPERF_OPCODE_INX,
    GPERF_OPCODE_INY,
    GPERF_OPCODE_JMP,
    GPERF_OPCODE_JSR,
    GPERF_OPCODE_LDA,
    GPERF_OPCODE_LDX,
    GPERF_OPCODE_LDY,
    GPERF_OPCODE_LSR,
    GPERF_OPCODE_NOP,
    GPERF_OPCODE_ORA,
    GPERF_OPCODE_PHA,
    GPERF_OPCODE_PHP,
    GPERF_OPCODE_PHX,
    GPERF_OPCODE_PHY,
    GPERF_OPCODE_PLA,
    GPERF_OPCODE_PLP,
    GPERF_OPCODE_PLX,
    GPERF_OPCODE_PLY,
    GPERF_OPCODE_ROL,
    GPERF_OPCODE_ROR,
    GPERF_OPCODE_RTI,
    GPERF_OPCODE_RTS,
    GPERF_OPCODE_SBC,
    GPERF_OPCODE_SEC,
    GPERF_OPCODE_SED,
    GPERF_OPCODE_SEI,
    GPERF_OPCODE_STA,
    GPERF_OPCODE_STX,
    GPERF_OPCODE_STY,
    GPERF_OPCODE_STZ,
    GPERF_OPCODE_TAX,
    GPERF_OPCODE_TAY,
    GPERF_OPCODE_TRB,
    GPERF_OPCODE_TSB,
    GPERF_OPCODE_TSX,
    GPERF_OPCODE_TXA,
    GPERF_OPCODE_TXS,
    GPERF_OPCODE_TYA,
};

// Define token types
typedef enum {
    TOKEN_NUM,
    TOKEN_OP,
    TOKEN_VAR,
    TOKEN_END,
} TOKENTYPE;

// Token structure
typedef struct {
    TOKENTYPE type;                                         // For the expression parser state machine
    int64_t value;
    char op;                                                // Im expressions the operator but also useful for token ID
    const char *name;                                       // Variable name for lookup
    uint32_t name_length;
    uint32_t name_hash;
} TOKEN;

typedef enum {
    SYMBOL_UNKNOWN,
    SYMBOL_VARIABLE,
    SYMBOL_ADDRESS
} SYMBOL_TYPE;

// Named symbols lookup struct
typedef struct {
    SYMBOL_TYPE symbol_type;
    const char *symbol_name;                                // Non-null-terminated name
    uint32_t symbol_length;                                 // Name length
    uint64_t symbol_hash;
    uint64_t symbol_value;
    uint32_t symbol_width;                                  // 0 - depends on value, 16 not 1 byte
} SYMBOL_LABEL;

enum {
    ADDRESS_MODE_ACCUMULATOR,
    ADDRESS_MODE_ABSOLUTE,
    ADDRESS_MODE_ABSOLUTE_X,
    ADDRESS_MODE_ABSOLUTE_Y,
    ADDRESS_MODE_IMMEDIATE,
    ADDRESS_MODE_INDIRECT_X,
    ADDRESS_MODE_INDIRECT_Y,
    ADDRESS_MODE_INDIRECT,
    ADDRESS_MODE_ZEROPAGE,
    ADDRESS_MODE_ZEROPAGE_X,
    ADDRESS_MODE_ZEROPAGE_Y,
};

typedef struct OPCODEINFO {
    const char *mnemonic;                                   // lda, etc.
    uint8_t opcode_id;                                      // GPERF_OPCODE_*
    uint8_t width;                                          // 0 (implied), 1 (relative), 8, 16
    uint8_t addressing_mode;
    uint64_t value;
} OPCODEINFO;

enum {
    BYTE_ORDER_LO,                                          // for example .word vs .drow
    BYTE_ORDER_HI,
};

typedef struct INPUT_STACK {
    const char *input;
    const char *token_start;
    const char *current_file;
    const char *next_line_start;
    size_t next_line_count;
    size_t current_line;
    const char *line_start;
} INPUT_STACK;

typedef struct PARSE_DATA {
    const char *file_name;                                  // Name of the file in which .include encountered
    const char *input;                                      // Input token position when .include encountered
    size_t line_number;                                     // Line number when include .include encountered
} PARSE_DATA;

typedef struct INCLUDE_FILES {
    DYNARRAY included_files;                                // Array of all files loaded (UTIL_FILE)
    DYNARRAY stack;                                         // .include causes a push of PARSE_DATA
} INCLUDE_FILES;

typedef struct FOR_LOOP {                                   // For init, condition, adjust
    const char *loop_condition_start;                       // Points at condition
    const char *loop_adjust_start;                          // Points at loop counter adjust expression
    const char *loop_body_start;                            // Points at text to execute in loop
    const char *loop_start_file;                            // .for and .endfor must be in same file
    size_t body_line;                                       // Line number where body loop starts
    size_t iterations;                                      // Break out of runaway loops
} FOR_LOOP;

typedef struct MACRO_VARIABLE {                             // This is for the formal macro parameters
    const char *variable_name;
    int variable_name_length;
} MACRO_VARIABLE;

typedef struct MACRO_ARG {                                  // This is for the macro call arguments
    const char *text;
    int text_length;
} MACRO_ARG;

typedef struct MACRO {
    const char *macro_name;
    int macro_name_length;
    INPUT_STACK macro_body_input;                           // Start of macro body text
    const char *macro_body_end;
    DYNARRAY macro_parameters;
} MACRO;

typedef struct ASSEMBLER ASSEMBLER;

typedef void (*output_byte)(void *user, VIEW_FLAGS vf, uint16_t address, uint8_t byte_value);

typedef struct CB_ASSEMBLER_CTX {
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
    INCLUDE_FILES include_files;                            // The arrays for files and stack for .include
    OPCODEINFO opcode_info;                                 // State of what is to be emitted in terms of 6502 opcodes
    VIEW_FLAGS vf;                                          // default is MEM_MAPPED_6502
    TOKEN current_token;                                    // What is being parsed
    int error_log_level;                                    // logging level (0 - filter duplicates; 1 - all to limit)
    int expression_size;                                    // Forward defs can't change size (16 bit can't become 8 later)
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
    DYNARRAY *symbol_table;                                 // Array of arrays of symbols
    ERRORLOG *errorlog;                                     // ptr to log that tracks errors
} ASSEMBLER;

// extern ASSEMBLER *as;

SYMBOL_LABEL *symbol_lookup(ASSEMBLER *as, uint32_t name_hash, const char *symbol_name, uint32_t symbol_name_length);
int symbol_sort(const void *lhs, const void *rhs);
SYMBOL_LABEL *symbol_store(ASSEMBLER *as, const char *symbol_name, uint32_t symbol_name_length, SYMBOL_TYPE symbol_type, uint64_t value);
int assembler_init(ASSEMBLER *as, ERRORLOG *errorlog, void *user, output_byte ob);
int assembler_assemble(ASSEMBLER *as, const char *input_file, uint16_t address);
void assembler_shutdown(ASSEMBLER *as);
uint16_t parse_anonymous_address(ASSEMBLER *as);
void expect(ASSEMBLER *as, char op);
