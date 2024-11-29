// Apple ][+ emulator and assembler
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#pragma once

// Also see ../gperf/asm6502.gperf
enum {
    GPERF_DOT_ALIGN,
    GPERF_DOT_BYTE,
    GPERF_DOT_DROW,
    GPERF_DOT_DROWD,
    GPERF_DOT_DROWQ,
    GPERF_DOT_DWORD,
    GPERF_DOT_ENDFOR,
    GPERF_DOT_ENDMACRO,
    GPERF_DOT_FOR,
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
    GPERF_OPCODE_DEC,
    GPERF_OPCODE_DEX,
    GPERF_OPCODE_DEY,
    GPERF_OPCODE_EOR,
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
    GPERF_OPCODE_PLA,
    GPERF_OPCODE_PLP,
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
    GPERF_OPCODE_TAX,
    GPERF_OPCODE_TAY,
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
    ADDRESS_MODE_ZEROPAGE,
    ADDRESS_MODE_ZEROPAGE_X,
    ADDRESS_MODE_ZEROPAGE_Y,
};

typedef struct OpcodeInfo {
    const char *mnemonic;                                   // lda, etc.
    uint8_t opcode_id;                                      // GPERF_OPCODE_*
    uint8_t width;                                          // 0 (implied), 1 (relative), 8, 16
    uint8_t addressing_mode;
    uint64_t value;
} OpcodeInfo;

enum {
    BYTE_ORDER_LO,                                          // for exmple .word vs .drow
    BYTE_ORDER_HI,
};

typedef struct INPUT_STACK {
    const char *input;
    const char *token_start;
    const char *current_file;
    const char *next_line_start;
    size_t next_line_count;
    size_t current_line;
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
    const char *loop_body_start;                            // Points at text to excute in loop
    const char *loop_start_file;                            // .for and .endfor must be in same file
    size_t body_line;                                       // Line nuber where body loop starts
    size_t iterations;                                      // Break out of runaway loops
} FOR_LOOP;

typedef struct MACRO_VARIABLE {
    const char *variable_name;
    int variable_name_length;
} MACRO_VARIABLE;

typedef struct MACRO {
    const char *macro_name;
    int macro_name_length;
    INPUT_STACK macro_body_input;
    DYNARRAY macro_parameters;
} MACRO;

typedef struct {
    APPLE2 *m;                                              // To be able to write to memory
    const char *strcode;                                    // Active .strcode expression
    const char *current_file;                               // Points at a UTIL_FILE path_name
    const char *input;                                      // Points at the assembly language buffer (start through end)
    const char *line_start;                                 // Just past \n of line input is on
    const char *next_line_start;                                 // So errors get reported on line of last token
    const char *token_start;                                // Points at the start of a token (and input the end)
    DYNARRAY anon_symbols;                                  // Array of anonymous symbols
    DYNARRAY loop_stack;                                    // Array of for loops
    DYNARRAY macros;                                        // Array of all macros
    DYNARRAY input_stack;                                   // Array of token "reset" points (saved token_start, input, etc)
    DYNARRAY symbol_table;                                  // Array of arrays of symbols
    INCLUDE_FILES include_files;                            // The arrays for files and stack for .include
    int expression_size;                                    // Forward defs can't change size (16 bit can't become 8 later)
    int pass;                                               // 1 or 2 for 2 pass assembler
    OpcodeInfo opcode_info;                                 // State of what is to be emitted in terms of 6502 opcodes
    size_t current_line;                                    // for error reporting, line being processed
    size_t next_line_count;                                 // count of lines past last token
    TOKEN current_token;                                    // What is being parsed
    uint16_t current_address;                               // Address where next byte will be emitted
    uint16_t start_address;                                 // First address where the assembler output a byte
    uint16_t last_address;                                  // Last address where the assembler put a byte
#ifdef IS_ASSEMBLER
    int verbose;
#endif                                                      // IS_ASSEMBLER
} ASSEMBLER;

extern ASSEMBLER *as;

uint32_t fnv_1a_hash(const char *key, size_t len);
void include_files_cleanup();
UTIL_FILE *include_files_find_file(const char *file_name);
void include_files_init();
int include_files_pop();
int include_files_push(const char *file_name);
int input_stack_empty();
int input_stack_push();
void input_stack_pop();
void flush_macros();
void emit(uint8_t byte_value);
void write_opcode();
void write_bytes(uint64_t value, int width, int order);
void write_values(int width, int order);
int anonymous_symbol_lookup(uint16_t * address, int direction);
int symbol_sort(const void *lhs, const void *rhs);
SYMBOL_LABEL *symbol_store(const char *symbol_name, uint32_t symbol_name_length, SYMBOL_TYPE symbol_type, uint64_t value);
SYMBOL_LABEL *symbol_lookup(uint32_t name_hash, const char *symbol_name, uint32_t symbol_name_length);
char character_in_characters(const char character, const char *characters);
void expect(char op);
const char *find_delimiter(const char *delimitors);
int match(int number, ...);
void decode_abs_rel_zp_opcode();
int is_indexed_indirect(char *reg);
int is_address();
int is_dot_command();
int is_label();
int is_macro_parse_macro();
int is_newline(char c);
int is_opcode();
int is_variable();
int is_valid_instruction_only();
void parse_dot_endfor();
void parse_dot_for();
void process_dot_include();
void process_dot_org();
void process_dot_strcode();
void process_dot_string();
void parse_address();
uint16_t parse_anonymous_address();
void parse_dot_command();
void parse_label();
void parse_opcode();
void parse_variable();
int assembler_init(APPLE2 * m);
int assembler_assemble(const char *input_file, uint16_t address);
void assembler_shutdown();
