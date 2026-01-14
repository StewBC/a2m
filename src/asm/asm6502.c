// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "asm_lib.h"

// Local function
void include_files_cleanup(ASSEMBLER *as);
UTIL_FILE *include_files_find_file(ASSEMBLER *as, const char *file_name);
void include_files_init(ASSEMBLER *as);
int include_files_pop(ASSEMBLER *as);
int include_files_push(ASSEMBLER *as, const char *file_name);
int input_stack_empty(ASSEMBLER *as);
int input_stack_push(ASSEMBLER *as);
void input_stack_pop(ASSEMBLER *as);
void flush_macros(ASSEMBLER *as);
void emit(ASSEMBLER *as, uint8_t byte_value);
void write_opcode(ASSEMBLER *as);
void write_bytes(ASSEMBLER *as, uint64_t value, int width, int order);
void write_values(ASSEMBLER *as, int width, int order);
int anonymous_symbol_lookup(ASSEMBLER *as, uint16_t *address, int direction);
void symbol_clear(ASSEMBLER *as, const char *symbol_name, uint32_t symbol_name_length);
const char *find_delimiter(ASSEMBLER *as, const char *delimitors);
int match(ASSEMBLER *as, int number, ...);
void append_bytes(char **buf, size_t *len, size_t *cap, const char *src, size_t n);
void decode_abs_rel_zp_opcode(ASSEMBLER *as);
int is_indirect(ASSEMBLER *as, char *reg);
int is_address(ASSEMBLER *as);
int is_dot_command(ASSEMBLER *as);
int is_label(ASSEMBLER *as);
int is_macro_parse_macro(ASSEMBLER *as);
int is_opcode(ASSEMBLER *as);
int is_variable(ASSEMBLER *as);
int is_valid_instruction_only(ASSEMBLER *as);
void parse_dot_endfor(ASSEMBLER *as);
void parse_dot_for(ASSEMBLER *as);
void process_dot_incbin(ASSEMBLER *as);
void process_dot_include(ASSEMBLER *as);
void process_dot_org(ASSEMBLER *as);
void process_dot_strcode(ASSEMBLER *as);
void process_dot_string(ASSEMBLER *as);
void parse_address(ASSEMBLER *as);
void parse_dot_command(ASSEMBLER *as);
void parse_label(ASSEMBLER *as);
void parse_opcode(ASSEMBLER *as);
void parse_variable(ASSEMBLER *as);

//     A  |  A  |  A  |  A  |  I  |  I  |  I  |  I  |  Z  |  Z  |  Z
//     C  |  B  |  B  |  B  |  M  |  N  |  N  |  N  |  E  |  E  |  E
//     C  |  S  |  S  |  S  |  M  |  D  |  D  |  D  |  R  |  R  |  R
//     U  |  O  |  O  |  O  |  E  |  I  |  I  |  I  |  O  |  O  |  O
//     M  |  L  |  L  |  L  |  D  |  R  |  R  |  R  |  P  |  P  |  P
//     U  |  U  |  U  |  U  |  I  |  E  |  E  |  E  |  A  |  A  |  A
//     L  |  T  |  T  |  T  |  A  |  C  |  C  |  C  |  G  |  G  |  G
//     A  |  E  |  E  |  E  |  T  |  T  |  T  |  T  |  E  |  E  |  E
//     T  |     |  ,  |  ,  |  E  |  ,  |  ,  |     |  /  |  ,  |  ,
//     O  |     |  X  |  Y  |     |  X  |  Y  |     |  R  |  X  |  Y
//     R  |     |     |     |     |     |     |     |  E  |     |  
//     /  |     |     |     |     |     |     |     |  L  |     |  
//     I  |     |     |     |     |     |     |     |  A  |     |  
//     M  |     |     |     |     |     |     |     |  T  |     |  
//     P  |     |     |     |     |     |     |     |  I  |     |  
//     L  |     |     |     |     |     |     |     |  V  |     |  
//     I  |     |     |     |     |     |     |     |  E  |     |  
//     E  |     |     |     |     |     |     |     |     |     |  
//     D  |     |     |     |     |     |     |     |     |     |  
const uint8_t asm_opcode[GPERF_OPCODE_TYA+1][ADDRESS_MODE_ZEROPAGE_Y+1] = {
    { -1  , 0x6d, 0x7d, 0x79, 0x69, 0x61, 0x71, 0x72, 0x65, 0x75, -1   }, /*  0: ADC */
    { -1  , 0x2d, 0x3d, 0x39, 0x29, 0x21, 0x31, 0x32, 0x25, 0x35, -1   }, /*  1: AND */
    { 0x0a, 0x0e, 0x1e, -1  , -1  , -1  , -1  , -1  , 0x06, 0x16, -1   }, /*  2: ASL */
    { -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , 0x90, -1  , -1   }, /*  3: BCC */
    { -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , 0xb0, -1  , -1   }, /*  4: BCS */
    { -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , 0xf0, -1  , -1   }, /*  5: BEQ */
    { -1  , 0x2c, 0x3c, -1  , 0x89, -1  , -1  , -1  , 0x24, 0x34, -1   }, /*  6: BIT */
    { -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , 0x30, -1  , -1   }, /*  7: BMI */
    { -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , 0xd0, -1  , -1   }, /*  8: BNE */
    { -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , 0x10, -1  , -1   }, /*  9: BPL */
    { -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , 0x80, -1  , -1   }, /* 10: BRA */
    { 0x00, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   }, /* 11: BRK */
    { -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , 0x50, -1  , -1   }, /* 12: BVC */
    { -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , 0x70, -1  , -1   }, /* 13: BVS */
    { 0x18, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   }, /* 14: CLC */
    { 0xd8, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   }, /* 15: CLD */
    { 0x58, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   }, /* 16: CLI */
    { 0xb8, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   }, /* 17: CLV */
    { -1  , 0xcd, 0xdd, 0xd9, 0xc9, 0xc1, 0xd1, 0xd2, 0xc5, 0xd5, -1   }, /* 18: CMP */
    { -1  , 0xec, -1  , -1  , 0xe0, -1  , -1  , -1  , 0xe4, -1  , -1   }, /* 19: CPX */
    { -1  , 0xcc, -1  , -1  , 0xc0, -1  , -1  , -1  , 0xc4, -1  , -1   }, /* 20: CPY */
    { 0x3a, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   }, /* 21: DEA */
    { -1  , 0xce, 0xde, -1  , -1  , -1  , -1  , -1  , 0xc6, 0xd6, -1   }, /* 22: DEC */
    { 0xca, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   }, /* 23: DEX */
    { 0x88, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   }, /* 24: DEY */
    { -1  , 0x4d, 0x5d, 0x59, 0x49, 0x41, 0x51, 0x52, 0x45, 0x55, -1   }, /* 25: EOR */
    { 0x1a, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   }, /* 26: INA */
    { -1  , 0xee, 0xfe, -1  , -1  , -1  , -1  , -1  , 0xe6, 0xf6, -1   }, /* 27: INC */
    { 0xe8, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   }, /* 28: INX */
    { 0xc8, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   }, /* 29: INY */
    { -1  , 0x4c, -1  , -1  , -1  , 0x7c, -1  , 0x6c, -1  , -1  , -1   }, /* 30: JMP */
    { -1  , 0x20, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   }, /* 31: JSR */
    { -1  , 0xad, 0xbd, 0xb9, 0xa9, 0xa1, 0xb1, 0xb2, 0xa5, 0xb5, -1   }, /* 32: LDA */
    { -1  , 0xae, -1  , 0xbe, 0xa2, -1  , -1  , -1  , 0xa6, -1  , 0xb6 }, /* 33: LDX */
    { -1  , 0xac, 0xbc, -1  , 0xa0, -1  , -1  , -1  , 0xa4, 0xb4, -1   }, /* 34: LDY */
    { 0x4a, 0x4e, 0x5e, -1  , -1  , -1  , -1  , -1  , 0x46, 0x56, -1   }, /* 35: LSR */
    { 0xea, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   }, /* 36: NOP */
    { -1  , 0x0d, 0x1d, 0x19, 0x09, 0x01, 0x11, 0x12, 0x05, 0x15, -1   }, /* 37: ORA */
    { 0x48, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   }, /* 38: PHA */
    { 0x08, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   }, /* 39: PHP */
    { 0xda, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   }, /* 40: PHX */
    { 0x5a, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   }, /* 41: PHY */
    { 0x68, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   }, /* 42: PLA */
    { 0x28, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   }, /* 43: PLP */
    { 0xfa, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   }, /* 44: PLX */
    { 0x7a, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   }, /* 45: PLY */
    { 0x2a, 0x2e, 0x3e, -1  , -1  , -1  , -1  , -1  , 0x26, 0x36, -1   }, /* 46: ROL */
    { 0x6a, 0x6e, 0x7e, -1  , -1  , -1  , -1  , -1  , 0x66, 0x76, -1   }, /* 47: ROR */
    { 0x40, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   }, /* 48: RTI */
    { 0x60, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   }, /* 49: RTS */
    { -1  , 0xed, 0xfd, 0xf9, 0xe9, 0xe1, 0xf1, 0xf2, 0xe5, 0xf5, -1   }, /* 50: SBC */
    { 0x38, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   }, /* 51: SEC */
    { 0xf8, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   }, /* 52: SED */
    { 0x78, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   }, /* 53: SEI */
    { -1  , 0x8d, 0x9d, 0x99, -1  , 0x81, 0x91, 0x92, 0x85, 0x95, -1   }, /* 54: STA */
    { -1  , 0x8e, -1  , -1  , -1  , -1  , -1  , -1  , 0x86, -1  , 0x96 }, /* 55: STX */
    { -1  , 0x8c, -1  , -1  , -1  , -1  , -1  , -1  , 0x84, 0x94, -1   }, /* 56: STY */
    { -1  , 0x9c, 0x9e, -1  , -1  , -1  , -1  , -1  , 0x64, 0x74, -1   }, /* 57: STZ */
    { 0xaa, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   }, /* 58: TAX */
    { 0xa8, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   }, /* 59: TAY */
    { -1  , 0x1c, -1  , -1  , -1  , -1  , -1  , -1  , 0x14, -1  , -1   }, /* 60: TRB */
    { -1  , 0x0c, -1  , -1  , -1  , -1  , -1  , -1  , 0x04, -1  , -1   }, /* 61: TSB */
    { 0xba, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   }, /* 62: TSX */
    { 0x8a, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   }, /* 63: TXA */
    { 0x9a, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   }, /* 64: TXS */
    { 0x98, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   }, /* 65: TYA */
};

// In this table the values are:
// -1 - Not a valid opcode
//  0 - A valid 65c02 opcode only
//  1 - A valid 6502 opcode
const uint8_t asm_opcode_type[GPERF_OPCODE_TYA+1][ADDRESS_MODE_ZEROPAGE_Y+1] = {
    {-1   , 1   , 1   , 1   , 1   , 1   , 1   ,  0  , 1   , 1   ,-1}    , /*  0: ADC */
    {-1   , 1   , 1   , 1   , 1   , 1   , 1   ,  0  , 1   , 1   ,-1}    , /*  1: AND */
    { 1   , 1   , 1   ,-1   ,-1   ,-1   ,-1   , -1  , 1   , 1   ,-1}    , /*  2: ASL */
    {-1   ,-1   ,-1   ,-1   ,-1   ,-1   ,-1   , -1  , 1   ,-1   ,-1}    , /*  3: BCC */
    {-1   ,-1   ,-1   ,-1   ,-1   ,-1   ,-1   , -1  , 1   ,-1   ,-1}    , /*  4: BCS */
    {-1   ,-1   ,-1   ,-1   ,-1   ,-1   ,-1   , -1  , 1   ,-1   ,-1}    , /*  5: BEQ */
    {-1   , 1   , 0   ,-1   , 0   ,-1   ,-1   , -1  , 1   , 0   ,-1}    , /*  6: BIT */
    {-1   ,-1   ,-1   ,-1   ,-1   ,-1   ,-1   , -1  , 1   ,-1   ,-1}    , /*  7: BMI */
    {-1   ,-1   ,-1   ,-1   ,-1   ,-1   ,-1   , -1  , 1   ,-1   ,-1}    , /*  8: BNE */
    {-1   ,-1   ,-1   ,-1   ,-1   ,-1   ,-1   , -1  , 1   ,-1   ,-1}    , /*  9: BPL */
    {-1   ,-1   ,-1   ,-1   ,-1   ,-1   ,-1   , -1  , 0   ,-1   ,-1}    , /* 10: BRA */
    { 1   ,-1   ,-1   ,-1   ,-1   ,-1   ,-1   , -1  ,-1   ,-1   ,-1}    , /* 10: BRK */
    {-1   ,-1   ,-1   ,-1   ,-1   ,-1   ,-1   , -1  , 1   ,-1   ,-1}    , /* 11: BVC */
    {-1   ,-1   ,-1   ,-1   ,-1   ,-1   ,-1   , -1  , 1   ,-1   ,-1}    , /* 12: BVS */
    { 1   ,-1   ,-1   ,-1   ,-1   ,-1   ,-1   , -1  ,-1   ,-1   ,-1}    , /* 13: CLC */
    { 1   ,-1   ,-1   ,-1   ,-1   ,-1   ,-1   , -1  ,-1   ,-1   ,-1}    , /* 14: CLD */
    { 1   ,-1   ,-1   ,-1   ,-1   ,-1   ,-1   , -1  ,-1   ,-1   ,-1}    , /* 15: CLI */
    { 1   ,-1   ,-1   ,-1   ,-1   ,-1   ,-1   , -1  ,-1   ,-1   ,-1}    , /* 16: CLV */
    {-1   , 1   , 1   , 1   , 1   , 1   , 1   ,  0  , 1   , 1   ,-1}    , /* 17: CMP */
    {-1   , 1   ,-1   ,-1   , 1   ,-1   ,-1   , -1  , 1   ,-1   ,-1}    , /* 18: CPX */
    {-1   , 1   ,-1   ,-1   , 1   ,-1   ,-1   , -1  , 1   ,-1   ,-1}    , /* 19: CPY */
    { 0   ,-1   ,-1   ,-1   ,-1   ,-1   ,-1   , -1  ,-1   ,-1   ,-1}    , /* 20: DEA */
    {-1   , 1   , 1   ,-1   ,-1   ,-1   ,-1   , -1  , 1   , 1   ,-1}    , /* 20: DEC */
    { 1   ,-1   ,-1   ,-1   ,-1   ,-1   ,-1   , -1  ,-1   ,-1   ,-1}    , /* 21: DEX */
    { 1   ,-1   ,-1   ,-1   ,-1   ,-1   ,-1   , -1  ,-1   ,-1   ,-1}    , /* 22: DEY */
    {-1   , 1   , 1   , 1   , 1   , 1   , 1   ,  0  , 1   , 1   ,-1}    , /* 23: EOR */
    { 0   ,-1   ,-1   ,-1   ,-1   ,-1   ,-1   , -1  ,-1   ,-1   ,-1}    , /* 24: INA */
    {-1   , 1   , 1   ,-1   ,-1   ,-1   ,-1   , -1  , 1   , 1   ,-1}    , /* 24: INC */
    { 1   ,-1   ,-1   ,-1   ,-1   ,-1   ,-1   , -1  ,-1   ,-1   ,-1}    , /* 25: INX */
    { 1   ,-1   ,-1   ,-1   ,-1   ,-1   ,-1   , -1  ,-1   ,-1   ,-1}    , /* 26: INY */
    {-1   , 1   ,-1   ,-1   ,-1   , 0   , 1   ,  1  ,-1   ,-1   ,-1}    , /* 27: JMP */
    {-1   , 1   ,-1   ,-1   ,-1   ,-1   ,-1   , -1  ,-1   ,-1   ,-1}    , /* 28: JSR */
    {-1   , 1   , 1   , 1   , 1   , 1   , 1   ,  0  , 1   , 1   ,-1}    , /* 29: LDA */
    {-1   , 1   ,-1   , 1   , 1   ,-1   ,-1   , -1  , 1   ,-1   , 1}    , /* 30: LDX */
    {-1   , 1   , 1   ,-1   , 1   ,-1   ,-1   , -1  , 1   , 1   ,-1}    , /* 31: LDY */
    { 1   , 1   , 1   ,-1   ,-1   ,-1   ,-1   , -1  , 1   , 1   ,-1}    , /* 32: LSR */
    { 1   ,-1   ,-1   ,-1   ,-1   ,-1   ,-1   , -1  ,-1   ,-1   ,-1}    , /* 33: NOP */
    {-1   , 1   , 1   , 1   , 1   , 1   , 1   ,  0  , 1   , 1   ,-1}    , /* 34: ORA */
    { 1   ,-1   ,-1   ,-1   ,-1   ,-1   ,-1   , -1  ,-1   ,-1   ,-1}    , /* 35: PHA */
    { 1   ,-1   ,-1   ,-1   ,-1   ,-1   ,-1   , -1  ,-1   ,-1   ,-1}    , /* 36: PHP */
    { 0   ,-1   ,-1   ,-1   ,-1   ,-1   ,-1   , -1  ,-1   ,-1   ,-1}    , /* 36: PHX */
    { 0   ,-1   ,-1   ,-1   ,-1   ,-1   ,-1   , -1  ,-1   ,-1   ,-1}    , /* 36: PHY */
    { 1   ,-1   ,-1   ,-1   ,-1   ,-1   ,-1   , -1  ,-1   ,-1   ,-1}    , /* 37: PLA */
    { 1   ,-1   ,-1   ,-1   ,-1   ,-1   ,-1   , -1  ,-1   ,-1   ,-1}    , /* 38: PLP */
    { 0   ,-1   ,-1   ,-1   ,-1   ,-1   ,-1   , -1  ,-1   ,-1   ,-1}    , /* 38: PLX */
    { 0   ,-1   ,-1   ,-1   ,-1   ,-1   ,-1   , -1  ,-1   ,-1   ,-1}    , /* 38: PLY */
    { 1   , 1   , 1   ,-1   ,-1   ,-1   ,-1   , -1  , 1   , 1   ,-1}    , /* 39: ROL */
    { 1   , 1   , 1   ,-1   ,-1   ,-1   ,-1   , -1  , 1   , 1   ,-1}    , /* 40: ROR */
    { 1   ,-1   ,-1   ,-1   ,-1   ,-1   ,-1   , -1  ,-1   ,-1   ,-1}    , /* 41: RTI */
    { 1   ,-1   ,-1   ,-1   ,-1   ,-1   ,-1   , -1  ,-1   ,-1   ,-1}    , /* 42: RTS */
    {-1   , 1   , 1   , 1   , 1   , 1   , 1   ,  0  , 1   , 1   ,-1}    , /* 43: SBC */
    { 1   ,-1   ,-1   ,-1   ,-1   ,-1   ,-1   , -1  ,-1   ,-1   ,-1}    , /* 44: SEC */
    { 1   ,-1   ,-1   ,-1   ,-1   ,-1   ,-1   , -1  ,-1   ,-1   ,-1}    , /* 45: SED */
    { 1   ,-1   ,-1   ,-1   ,-1   ,-1   ,-1   , -1  ,-1   ,-1   ,-1}    , /* 46: SEI */
    {-1   , 1   , 1   , 1   ,-1   , 1   , 1   ,  0  , 1   , 1   ,-1}    , /* 47: STA */
    {-1   , 1   ,-1   ,-1   ,-1   ,-1   ,-1   , -1  , 1   ,-1   , 1}    , /* 48: STX */
    {-1   , 1   ,-1   ,-1   ,-1   ,-1   ,-1   , -1  , 1   , 1   ,-1}    , /* 49: STY */
    {-1   , 0   , 0   ,-1   ,-1   ,-1   ,-1   , -1  , 0   , 0   ,-1}    , /* 49: STZ */
    { 1   ,-1   ,-1   ,-1   ,-1   ,-1   ,-1   , -1  ,-1   ,-1   ,-1}    , /* 50: TAX */
    { 1   ,-1   ,-1   ,-1   ,-1   ,-1   ,-1   , -1  ,-1   ,-1   ,-1}    , /* 51: TAY */
    {-1   , 0   ,-1   ,-1   ,-1   ,-1   ,-1   , -1  , 0   ,-1   ,-1}    , /* 52: TRB */
    {-1   , 0   ,-1   ,-1   ,-1   ,-1   ,-1   , -1  , 0   ,-1   ,-1}    , /* 52: TSB */
    { 1   ,-1   ,-1   ,-1   ,-1   ,-1   ,-1   , -1  ,-1   ,-1   ,-1}    , /* 52: TSX */
    { 1   ,-1   ,-1   ,-1   ,-1   ,-1   ,-1   , -1  ,-1   ,-1   ,-1}    , /* 53: TXA */
    { 1   ,-1   ,-1   ,-1   ,-1   ,-1   ,-1   , -1  ,-1   ,-1   ,-1}    , /* 54: TXS */
    { 1   ,-1   ,-1   ,-1   ,-1   ,-1   ,-1   , -1  ,-1   ,-1   ,-1}    , /* 55: TYA */
};

const char *address_mode_txt[] = {
    "ACCUMULATOR/IMPLIED",
    "ABSOLUTE",
    "ABSOLUTE,X",
    "ABSOLUTE,Y",
    "IMMEDIATE",
    "(INDIRECT,X)",
    "(INDIRECT),Y",
    "ZEROPAGE/RELATIVE",
    "ZEROPAGE,X",
    "ZEROPAGE,Y",
};

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
    ARRAY_INIT(&as->include_files.stack, PARSE_DATA);
}

int include_files_pop(ASSEMBLER *as) {
    // when the last item is popped, there is nothing to "pop to" so error
    if(as->include_files.stack.items <= 1) {
        as->include_files.stack.items = 0;
        return A2_ERR;
    }
    // Pop to the previous item on the stack
    as->include_files.stack.items--;
    PARSE_DATA *pd = ARRAY_GET(&as->include_files.stack, PARSE_DATA, as->include_files.stack.items);
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
                asm_err(as, "Out of memory");
            }
            f = &new_file;
        }
    } else {
        size_t i;
        // See if previously loaded file is in the stack - then this is a recursive include
        for(i = 0; i < as->include_files.stack.items; i++) {
            PARSE_DATA *pd = ARRAY_GET(&as->include_files.stack, PARSE_DATA, i);
            if(0 == stricmp(pd->file_name, file_name)) {
                recursive_include = 1;
                break;
            }
        }
    }

    if(!f) {
        asm_err(as, "Error loading file %s", file_name);
        return A2_ERR;
    } else if(recursive_include) {
        asm_err(as, "Recursive included of file %s ignored", file_name);
        return A2_ERR;
    }
    // Push the file onto the stack, documenting the current parse data
    PARSE_DATA pd;
    pd.file_name = as->current_file;
    pd.input = as->input;
    pd.line_number = as->current_line;
    as->current_file = f->file_path;

    if(A2_OK != ARRAY_ADD(&as->include_files.stack, pd)) {
        asm_err(as, "Out of memory");
    }
    // Prepare to parse the buffer that has now become active
    as->current_file = f->file_path;
    as->line_start = as->token_start = as->input = f->file_data;
    as->next_line_count = 0;
    as->current_line = 1;
    return A2_OK;
}

//----------------------------------------------------------------------------
// input stack & macros
int input_stack_empty(ASSEMBLER *as) {
    return as->input_stack.items == 0;
}

int input_stack_push(ASSEMBLER *as) {
    INPUT_STACK is;
    is.input = as->input;
    is.token_start = as->token_start;
    is.current_file = as->current_file;
    is.current_line = as->current_line;
    is.next_line_count = as->next_line_count;
    is.next_line_start = as->next_line_start;
    is.line_start = as->line_start;
    return ARRAY_ADD(&as->input_stack, is);
}

void input_stack_pop(ASSEMBLER *as) {
    if(as->input_stack.items) {
        as->input_stack.items--;
        INPUT_STACK *is = ARRAY_GET(&as->input_stack, INPUT_STACK, as->input_stack.items);
        as->input = is->input;
        as->token_start = is->token_start;
        as->current_file = is->current_file;
        as->current_line = is->current_line;
        as->next_line_count = is->next_line_count;
        as->next_line_start = is->next_line_start;
        as->line_start = is->line_start;
    }
}

void flush_macros(ASSEMBLER *as) {
    size_t i;
    for(i = 0; i < as->macros.items; i++) {
        MACRO *m = ARRAY_GET(&as->macros, MACRO, i);
        array_free(&m->macro_parameters);
    }
    as->macros.items = 0;
}

//----------------------------------------------------------------------------
// Output
void emit(ASSEMBLER *as, uint8_t byte_value) {
    if(as->pass == 2) {
        as->cb_assembler_ctx.output_byte(as->cb_assembler_ctx.user, as->selected, as->current_address, byte_value);
        as->last_address = ++as->current_address;
    } else {
        as->current_address++;
    }
}

void write_opcode(ASSEMBLER *as) {
    int8_t opcode = asm_opcode[as->opcode_info.opcode_id][as->opcode_info.addressing_mode];
    if(opcode == -1) {
        asm_err(as, "Invalid opcode %.3s with mode %s", as->opcode_info.mnemonic, address_mode_txt[as->opcode_info.addressing_mode]);
    }
    if(as->valid_opcodes && !asm_opcode_type[as->opcode_info.opcode_id][as->opcode_info.addressing_mode]) {
        asm_err(as, "Opcode %.3s with mode %s only valid in 65c02", as->opcode_info.mnemonic, address_mode_txt[as->opcode_info.addressing_mode]);
    }

    // First the opcode
    emit(as, opcode);
    // Then the operand (0, ie implied, will do nothing more)
    switch(as->opcode_info.width) {
        case 1: {                                               // Relative - 1 byte
                int32_t delta = as->opcode_info.value - 1 - as->current_address;
                if(delta > 128 || delta < -128) {
                    asm_err(as, "Relative branch out of range $%X", delta);
                }
                emit(as, delta);
            }
            break;
        case 8:                                                 // 1 byte
            if(as->opcode_info.value >= 256) {
                asm_err(as, "8-bit value expected but value = $%X", as->opcode_info.value);
            }
            emit(as, as->opcode_info.value);
            break;
        case 16:                                                // 2 bytes
            if(as->opcode_info.value >= 65536) {
                asm_err(as, "16-bit value expected but value = $%X", as->opcode_info.value);
            }
            emit(as, as->opcode_info.value);
            emit(as, as->opcode_info.value >> 8);
            break;
    }
}

void write_bytes(ASSEMBLER *as, uint64_t value, int width, int order) {
    if(order == BYTE_ORDER_HI) {
        if(width == 8) {
            emit(as, value);
            if(value >= 256) {
                asm_err(as, "Warning: value (%zd) > 255 output as byte value", value);
            }
        } else if(width == 16) {
            emit(as, value >> 8);
            emit(as, value);
            if(value >= 65536) {
                asm_err(as, "Warning: value (%zd) > 65535 output as drow value", value);
            }
        } else if(width == 32) {
            emit(as, value >> 24);
            emit(as, value >> 16);
            emit(as, value >> 8);
            emit(as, value);
            if(value >= 0X100000000) {
                asm_err(as, "Warning: value ($%zx) > $FFFFFFFF output as drowd value", value);
            }
        } else if(width == 64) {
            emit(as, value >> 56);
            emit(as, value >> 48);
            emit(as, value >> 40);
            emit(as, value >> 32);
            emit(as, value >> 24);
            emit(as, value >> 16);
            emit(as, value >> 8);
            emit(as, value);
            if(value >= 0X8000000000000000) {
                asm_err(as, "Warning: value ($%zx) > $7FFFFFFFFFFFFFFF output as drowq value", value);
            }
        }
    } else {
        if(width == 8) {
            emit(as, value);
            if(value >= 256) {
                asm_err(as, "Warning: value (%zd) > 255 output as byte value", value);
            }
        } else if(width == 16) {
            emit(as, value);
            emit(as, value >> 8);
            if(value >= 65536) {
                asm_err(as, "Warning: value (%zd) > 65535 output as word value", value);
            }
        } else if(width == 32) {
            emit(as, value);
            emit(as, value >> 8);
            emit(as, value >> 16);
            emit(as, value >> 24);
            if(value >= 0X100000000) {
                asm_err(as, "Warning: value ($%zx) > $FFFFFFFF output as dword value", value);
            }
        } else if(width == 64) {
            emit(as, value);
            emit(as, value >> 8);
            emit(as, value >> 16);
            emit(as, value >> 24);
            emit(as, value >> 32);
            emit(as, value >> 40);
            emit(as, value >> 48);
            emit(as, value >> 56);
            if(value >= 0X8000000000000000) {
                asm_err(as, "Warning: value ($%zx) > $7FFFFFFFFFFFFFFF output as qword value", value);
            }
        }
    }
}

void write_values(ASSEMBLER *as, int width, int order) {
    do {
        write_bytes(as, evaluate_expression(as), width, order);
    } while(as->current_token.op == ',');
}

//----------------------------------------------------------------------------
// Symbol storage / lookup
int anonymous_symbol_lookup(ASSEMBLER *as, uint16_t *address, int direction) {
    // Ensure the array has items
    if(as->anon_symbols.items == 0) {
        *address = 0xFFFF;
        return 0;                                           // Not found
    }

    int low = 0;
    int high = (int) as->anon_symbols.items - 1;
    int exact_match_index = -1;
    int closest_smaller_index = -1;
    int target_index;

    // Perform binary search to find the closest smaller or equal label
    while(low <= high) {
        int mid = (low + high) / 2;
        uint16_t value = *ARRAY_GET(&as->anon_symbols, uint16_t, mid);

        if(value == *address) {
            exact_match_index = mid;                        // Exact match found, not sure this can happen?
            break;
        } else if(value < *address) {
            closest_smaller_index = mid;
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }

    if(exact_match_index != -1) {
        // If we have an exact match
        target_index = exact_match_index + direction;
    } else {
        // Determine the target_index with direction
        if(direction < 0) {
            // For negative direction: Start at the label greater than
            // the closest_smaller_index
            target_index = closest_smaller_index + 1 + direction;
        } else {
            // For zero or positive direction: Start at the closest_smaller_index
            target_index = closest_smaller_index + direction;
        }
    }

    // Check bounds
    if(target_index < 0 || target_index >= (int) as->anon_symbols.items) {
        // Error out of bounds
        *address = 0xFFFF;
        return 0;
    }

    // Return the address at the target index
    *address = *ARRAY_GET(&as->anon_symbols, uint16_t, target_index);
    return 1;
}

void symbol_clear(ASSEMBLER *as, const char *symbol_name, uint32_t symbol_name_length) {
    uint32_t name_hash = util_fnv_1a_hash(symbol_name, symbol_name_length);
    uint8_t bucket = name_hash & 0xff;
    DYNARRAY *bucket_array = &as->symbol_table[bucket];
    SYMBOL_LABEL *sl = symbol_lookup(as, name_hash, symbol_name, symbol_name_length);
    if(sl) {
        array_remove(bucket_array, sl);
    }
}

SYMBOL_LABEL *symbol_lookup(ASSEMBLER *as, uint32_t name_hash, const char *symbol_name, uint32_t symbol_name_length) {
    uint8_t bucket = name_hash & 0xff;
    DYNARRAY *bucket_array = &as->symbol_table[bucket];
    for(size_t i = 0; i < bucket_array->items; i++) {
        SYMBOL_LABEL *sl = ARRAY_GET(bucket_array, SYMBOL_LABEL, i);
        if(sl->symbol_hash == name_hash && !strnicmp(symbol_name, sl->symbol_name, symbol_name_length)) {
            return sl;
        }
    }
    return 0;
}

int symbol_sort(const void *lhs, const void *rhs) {
    return (uint16_t)(((SYMBOL_LABEL *) lhs)->symbol_value) - (uint16_t)(((SYMBOL_LABEL *) rhs)->symbol_value);
}

SYMBOL_LABEL *symbol_store(ASSEMBLER *as, const char *symbol_name, uint32_t symbol_name_length, SYMBOL_TYPE symbol_type, uint64_t value) {
    uint32_t name_hash = util_fnv_1a_hash(symbol_name, symbol_name_length);
    SYMBOL_LABEL *sl = symbol_lookup(as, name_hash, symbol_name, symbol_name_length);
    if(sl) {
        if(sl->symbol_type == SYMBOL_UNKNOWN) {
            sl->symbol_type = symbol_type;
            sl->symbol_value = value;
        } else {
            if(sl->symbol_type != symbol_type) {
                // Symbol changing type error
                asm_err(as, "Symbol %.*s can't be address and variable type", symbol_name_length, symbol_name);
            }
            if(sl->symbol_type == SYMBOL_VARIABLE) {
                // Variables can change value along the way
                sl->symbol_value = value;
            } else if(sl->symbol_value != value) {
                // Addresses may not change value
                asm_err(as, "Multiple address labels have name %.*s", symbol_name_length, symbol_name);
            }
        }
    } else {
        // Create a new entry for a previously unknown symbol
        SYMBOL_LABEL new_sl;
        new_sl.symbol_type = symbol_type;
        new_sl.symbol_hash = name_hash;
        new_sl.symbol_length = symbol_name_length;
        new_sl.symbol_name = symbol_name;
        new_sl.symbol_value = value;
        new_sl.symbol_width = 0;
        uint8_t bucket = name_hash & 0xff;
        DYNARRAY *bucket_array = &as->symbol_table[bucket];
        ARRAY_ADD(bucket_array, new_sl);
        sl = ARRAY_GET(bucket_array, SYMBOL_LABEL, bucket_array->items - 1);
    }
    return sl;
}

//----------------------------------------------------------------------------
// Token helpers
void expect(ASSEMBLER *as, char op) {
    if(as->current_token.type != TOKEN_OP || as->current_token.op != op) {
        asm_err(as, "Expected '%c'", op);
    }
    next_token(as);
}

const char *find_delimiter(ASSEMBLER *as, const char *delimitors) {
    while(as->token_start < as->input) {
        const char *c = delimitors;
        while(*c && *as->token_start != *c) {
            c++;
        }
        if(*c) {
            return as->token_start;
        }
        as->token_start++;
    }
    return 0;
}

int match(ASSEMBLER *as, int number, ...) {
    va_list args;
    va_start(args, number);

    // For all match arguments
    for(int i = 0; i < number; i++) {
        int j = 0;
        // Get the argument
        const char *str = va_arg(args, const char *);
        // Compare token start to the argument string
        while(*str && as->token_start[j] && *str == as->token_start[j]) {
            str++;
            j++;
        }
        if(!(*str)) {
            // If !*str it was a match
            as->token_start += j;
            va_end(args);
            return i;
        }
    }
    va_end(args);
    return -1;
}

void append_bytes(char **buf, size_t *len, size_t *cap, const char *src, size_t n) {
    if(n == 0) {
        return;
    }
    if(*len + n + 1 > *cap) {
        size_t newcap = (*cap == 0) ? 256 : *cap;
        while(*len + n + 1 > newcap) {
            newcap *= 2;
        }
        char *nb = (char *)realloc(*buf, newcap);
        if(!nb) {
            return;
        }
        *buf = nb;
        *cap = newcap;
    }
    memcpy(*buf + *len, src, n);
    *len += n;
    (*buf)[*len] = '\0';
}

void find_ab_passing_over_c(ASSEMBLER *as, const char *a, const char *b, const char *c) {
    int nest_level = 1;
    size_t a_len = strlen(a);
    size_t b_len = b ? strlen(b) : 0;
    size_t c_len = strlen(c);
    do {
        get_token(as);
        if(*as->input && !strnicmp(c, as->token_start, c_len)) {
            find_ab_passing_over_c(as, a, NULL, c);
            continue;
        }
        if(nest_level && !strnicmp(a, as->token_start, a_len)) {
            if(!(--nest_level)) {
                break;
            }
            continue;
        }
        if(nest_level && b_len && !strnicmp(b, as->token_start, b_len)) {
            if(!(--nest_level)) {
                break;
            }
            continue;
        }
    } while(*as->input);
    if(as->input == as->token_start) {
        get_token(as);
    }
}

MACRO_ARG parse_macro_args(const char **pp) {
    const char *p = *pp;

    // trim leading space
    while(*p == ' ' || *p == '\t') {
        p++;
    }

    const char *start = p;
    int parens = 0;
    int in_string = 0;

    // lift the token, respecting '"'s - ','s have to be quoted, ie
    // my_macro "lda ($50),y", sta $25
    // will work, and the inner ',' isn't mistaken as a arg seperator
    while(*p) {
        char c = *p;

        if(!in_string) {
            if(c == ';' || c == '\n' || c == '\r') {
                break;
            }
            if(c == ',' && parens == 0) {
                break;
            }
            if(c == '(') {
                parens++;
            } else if(c == ')' && parens > 0) {
                parens--;
            }
            if(c == '"') {
                in_string = 1;
                if(p == start) {
                    start++;
                }
            }
        } else {
            if(c == '"' && p[-1] != '\\') {
                in_string = 0;
                break;
            }
            if(c == '\n' || c == '\r') {
                break;
            }
        }

        p++;
    }

    const char *end = p;

    // Skip the closing quote after setting the length right
    if(*p == '"') {
        p++;
    }

    // trim trailing space
    while(end > start && (end[-1] == ' ' || end[-1] == '\t')) {
        end--;
    }

    MACRO_ARG a;
    a.text = start;
    a.text_length = (int)(end - start);

    *pp = p;
    return a;
}

//----------------------------------------------------------------------------
// Assembly generator helpers
void decode_abs_rel_zp_opcode(ASSEMBLER *as) {
    // Relative (branch) instructions have their opcodes in zero page as well
    int relative = as->opcode_info.width == 1;
    as->opcode_info.addressing_mode = ADDRESS_MODE_ZEROPAGE;
    if(!relative) {
        // Not relative, so may be absolute based on operand size
        if(as->opcode_info.value >= 256 || as->opcode_info.width > 8) {
            as->opcode_info.addressing_mode = ADDRESS_MODE_ABSOLUTE;
            as->opcode_info.width = 16;
        }
        if(as->current_token.op == ',') {
            // Make sure a comma is followed by x or y
            get_token(as);
            switch(tolower(*as->token_start)) {
                case 'x':
                    as->opcode_info.addressing_mode++;
                    break;
                case 'y':
                    if(as->opcode_info.width < 16 && as->opcode_info.addressing_mode == ADDRESS_MODE_ZEROPAGE) {
                        // lda 23, y is valid, but it's 16 bit, not ZP, it's absolute
                        as->opcode_info.addressing_mode = ADDRESS_MODE_ABSOLUTE_Y;
                        as->opcode_info.width = 16;
                    } else {
                        as->opcode_info.addressing_mode += 2;
                    }
                    break;
                default:
                    asm_err(as, "Unexpected ,%c", *as->token_start);
                    break;
            }
        }
    }
    write_opcode(as);
}

int is_indirect(ASSEMBLER *as, char *reg) {
    const char *c = as->token_start;
    int brackets = 1;
    // Only called when it's known there are (as)'s on the line
    // Scan forward counting brackets and looking for a ,
    while(*c && *c != ',' && *c != ';' && !util_is_newline(*c)) {
        if(*c == '(') {
            brackets++;
        } else if(*c == ')') {
            brackets--;
        }
        c++;
    }
    // No comma, not indexed
    if(*c == ',') {
        // Comma in bracket is x-indexed
        if(brackets != 0) {
            *reg = 'x';
            return ADDRESS_MODE_INDIRECT_X;
        } else {
            // Comma outside brackets is y-indexed
            *reg = 'y';
            return ADDRESS_MODE_INDIRECT_Y;
        }
    } else if(!brackets) {
        return ADDRESS_MODE_INDIRECT;
    }
    return 0;
}

//----------------------------------------------------------------------------
// Token identification
int is_address(ASSEMBLER *as) {
    if(*as->token_start != '*') {
        return 0;
    }
    return 1;
}

int is_dot_command(ASSEMBLER *as) {
    OPCODEINFO *oi;
    if(*as->token_start != '.' || !(oi = in_word_set(as->token_start, as->input - as->token_start))) {
        return 0;
    }
    as->opcode_info = *oi;
    return 1;
}

int is_label(ASSEMBLER *as) {
    if(*(as->input - 1) != ':') {
        return 0;
    }
    return 1;
}

// In this case, also do the work since finding the macro is
// already a significant effort
int is_macro_parse_macro(ASSEMBLER *as) {
    size_t i;
    int name_length = (int)(as->input - as->token_start);

    for(i = 0; i < as->macros.items; i++) {
        MACRO *macro = ARRAY_GET(&as->macros, MACRO, i);
        if(name_length == macro->macro_name_length && 0 == strnicmp(as->token_start, macro->macro_name, name_length)) {

            const size_t argc = macro->macro_parameters.items;

            // Get the raw parameters to the macro
            MACRO_ARG *args = NULL;
            if(argc) {
                args = (MACRO_ARG *)calloc(argc, sizeof(MACRO_ARG));
                if(!args) {
                    asm_err(as, "Out of memory");
                    return 1;
                }
            }

            const char *p = as->input; // points after macro name token
            for(size_t ai = 0; ai < argc; ai++) {
                // If at end-of-line or at a comment, remaining args are empty
                while(*p == ' ' || *p == '\t') {
                    p++;
                }
                if(*p == '\0' || *p == ';' || *p == '\n' || *p == '\r') {
                    args[ai].text = NULL;
                    args[ai].text_length = 0;
                    continue;
                }
                args[ai] = parse_macro_args(&p);
                // skip comma
                if(*p == ',') {
                    p++;    
                }
            }

            // Advance input to end-of-line, past all macro parameters so parsing can continue
            while(*p && *p != '\n' && *p != '\r') {
                if(*p == ';') {
                    break;
                }
                p++;
            }
            as->input = p;
            as->token_start = p;
            input_stack_push(as);

            // Expand macro body using get_token()
            const char *body_start = macro->macro_body_input.input;
            const char *body_end   = macro->macro_body_end;

            if(!body_start || !body_end || body_end < body_start) {
                free(args);
                asm_err(as, "Macro body end not set for %.*s", macro->macro_name_length, macro->macro_name);
                return 1;
            }

            // Set up to parse macro body
            as->input = as->token_start = body_start;
            as->next_line_count = 0;

            char *out = NULL;
            size_t out_len = 0, out_cap = 0;

            const char *cursor = body_start;

            while(as->input < body_end && *as->input) {
                get_token(as); // advances as->input, sets as->token_start

                const char *ts = as->token_start;
                const char *te = as->input;

                if(ts > body_end) {
                    ts = body_end;
                }
                if(te > body_end) {
                    te = body_end;
                }

                // Copy bytes before token
                if(cursor < ts) {
                    append_bytes(&out, &out_len, &out_cap, cursor, (size_t)(ts - cursor));
                }

                // Copy or substitute token bytes
                if(ts < te) {
                    if(is_variable(as)) {
                        int substituted = 0;
                        for(size_t mi = 0; mi < argc; mi++) {
                            MACRO_VARIABLE *mv = ARRAY_GET(&macro->macro_parameters, MACRO_VARIABLE, mi);
                            if(mv->variable_name_length == (int)(te - ts) &&
                                    0 == strnicmp(ts, mv->variable_name, (size_t)(te - ts))) {
                                // Substitute raw argument text
                                if(args[mi].text && args[mi].text_length > 0) {
                                    append_bytes(&out, &out_len, &out_cap, args[mi].text, (size_t)args[mi].text_length);
                                }
                                substituted = 1;
                                break;
                            }
                        }
                        if(!substituted) {
                            append_bytes(&out, &out_len, &out_cap, ts, (size_t)(te - ts));
                        }
                    } else {
                        append_bytes(&out, &out_len, &out_cap, ts, (size_t)(te - ts));
                    }
                }

                cursor = te;
            }

            // Copy any trailing bytes up to body_end
            if(cursor < body_end) {
                append_bytes(&out, &out_len, &out_cap, cursor, (size_t)(body_end - cursor));
            }
            // Add the body to the list
            // Note that these have to stay past pass 1 so that
            // symbols in pass 2, from here in pass 1, still exist
            // I could reuse the buffer in pass 2 if I hash the sig
            // or I can just live with all of these buffers, which
            // is what I currently choose to do
            ARRAY_ADD(&as->macro_buffers, out);


            free(args);

            if(!out) {
                asm_err(as, "Out of memory");
                return 1;
            }

            // Ensure buffer ends with newline
            if(out_len == 0 || out[out_len - 1] != '\n') {
                append_bytes(&out, &out_len, &out_cap, "\n", 1);
            }

            // Activate expanded macro buffer directly
            as->current_file = macro->macro_body_input.current_file;
            as->current_line = macro->macro_body_input.current_line;
            as->line_start   = out;
            as->next_line_start = out;
            as->next_line_count = 0;
            as->input = as->token_start = out;

            return 1;
        }
    }
    return 0;
}

int is_opcode(ASSEMBLER *as) {
    OPCODEINFO *oi;
    if(as->input - as->token_start != 3 || *as->token_start == '.' || !(oi = in_word_set(as->token_start, 3))) {
        return 0;
    }
    as->opcode_info = *oi;
    return 1;
}

int is_variable(ASSEMBLER *as) {
    // Variable start with [a-Z] or _
    if(*as->token_start != '_' && !isalpha(*as->token_start)) {
        return 0;
    }
    const char *c = as->token_start;
    // an can contain same plus [0-9]
    while(c < as->input && (*c == '_' || isalnum(*c))) {
        c++;
    }
    if(c != as->input) {
        return 0;
    }
    return 1;
}

int is_valid_instruction_only(ASSEMBLER *as) {
    switch(as->opcode_info.opcode_id) {
        case GPERF_OPCODE_ASL:
        case GPERF_OPCODE_LSR:
        case GPERF_OPCODE_ROL:
        case GPERF_OPCODE_ROR:
            if(as->current_token.type == TOKEN_VAR) {
                if(as->current_token.name_length == 1 && tolower(*as->token_start) == 'a') {
                    next_token(as);
                }
            }
            return as->current_token.type == TOKEN_END;

        default:
            if(!as->opcode_info.width) {
                return 1;
            }
    }
    return 0;
}

//----------------------------------------------------------------------------
// Parse DOT commands
void parse_dot_else(ASSEMBLER *as) {
    if(as->if_active) {
        as->if_active--;
    } else {
        asm_err(as, ".else without .if");
    }
    find_ab_passing_over_c(as, ".endif", NULL, ".if");
    if(!*as->input) {
        asm_err(as, ".else without .endif");
    }
}

void parse_dot_endfor(ASSEMBLER *as) {
    if(as->loop_stack.items) {
        const char *post_loop = as->input;
        FOR_LOOP *for_loop = ARRAY_GET(&as->loop_stack, FOR_LOOP, as->loop_stack.items - 1);
        int same_file = stricmp(for_loop->loop_start_file, as->current_file) == 0;
        int loop_iterations = ++for_loop->iterations;
        as->token_start = as->input = for_loop->loop_adjust_start;
        evaluate_expression(as);
        as->token_start = as->input = for_loop->loop_condition_start;
        if(loop_iterations < 65536 && same_file && evaluate_expression(as)) {
            as->token_start = as->input = for_loop->loop_body_start;
            as->current_line = for_loop->body_line;
        } else {
            // Pop the for loop from the stack
            as->loop_stack.items--;
            as->token_start = as->input = post_loop;
            if(!same_file) {
                asm_err(as, ".endfor matches .for in file %s, body at line %zd", for_loop->loop_start_file, for_loop->body_line);
            } else if(loop_iterations >= 65536) {
                asm_err(as, "Exiting .for loop with body at line %zd, which has iterated 64K times", for_loop->body_line);
            }
            as->current_line--;
        }
    } else {
        asm_err(as, ".endfor without a matching .for");
    }
}

void parse_dot_endif(ASSEMBLER *as) {
    if(as->if_active) {
        as->if_active--;
    } else {
        asm_err(as, ".endif with no .if");
    }
}

void parse_dot_endmacro(ASSEMBLER *as) {
    if(!input_stack_empty(as)) {
        // Do not free the macro buffer here, there might be a reference to a symbol name
        // Return to the parse point where the macro was called
        input_stack_pop(as);
    } else {
        asm_err(as, ".endmacro but not running a macro");
    }
}

void parse_dot_for(ASSEMBLER *as) {
    FOR_LOOP for_loop;
    evaluate_expression(as);                                  // assignment
    for_loop.iterations = 0;
    for_loop.loop_condition_start = as->input;
    for_loop.loop_start_file = as->current_file;
    // evaluate the condition
    if(!evaluate_expression(as)) {
        find_ab_passing_over_c(as, ".endfor", NULL, ".for");
        if(!*as->input) {
            // failed so find .endfor (must be in same file)
            asm_err(as, ".for without .endfor");
        }
    } else {
        // condition success
        for_loop.loop_adjust_start = as->input;
        // skip past the adjust condition
        do {
            get_token(as);
        } while(*as->input && as->token_start != as->input);
        // to find the loop body
        for_loop.loop_body_start = as->input;
        for_loop.body_line = as->current_line + as->next_line_count;
        ARRAY_ADD(&as->loop_stack, for_loop);
    }
}

void parse_dot_if(ASSEMBLER *as) {
    if(!evaluate_expression(as)) {
        find_ab_passing_over_c(as, ".endif", ".else", ".if");
        if(!*as->input) {
            // failed so find .else or .endif (must be in same file)
            asm_err(as, ".if without .endif");
        } else {
            // If not endif, it's else and that needs an endif
            if(tolower(as->token_start[2]) != 'n') {
                as->if_active++;
                // I could add a .elseif and call parse_dot_if?
            }
        }
    } else {
        // condition success
        as->if_active++;
    }
}

void process_dot_incbin(ASSEMBLER *as) {
    get_token(as);
    if(*as->token_start != '"') {
        asm_err(as, "include expects a \" enclosed string as a parameter");
    } else {
        UTIL_FILE new_file;
        char file_name[PATH_MAX];
        size_t string_length = as->input - as->token_start - 2;
        strncpy(file_name, as->token_start + 1, string_length);
        file_name[string_length] = '\0';

        // See if the file had previously been loaded
        UTIL_FILE *f = include_files_find_file(as, file_name);
        if(!f) {
            // If not, it's a new file, load it
            memset(&new_file, 0, sizeof(UTIL_FILE));

            if(A2_OK == util_file_load(&new_file, file_name, "rb")) {
                // Success, add to list of loaded files and assign it to f
                if(A2_OK != ARRAY_ADD(&as->include_files.included_files, new_file)) {
                    asm_err(as, "Out of memory");
                }
                f = &new_file;
            } else {
                asm_err(as, ".incbin could not load the file %s", file_name);
            }
        }
        if(f) {
            // if is now in memory, either previously or newly loaded
            size_t data_size = f->file_size;
            uint8_t *data = (uint8_t *)f->file_data;
            while(data_size--) {
                emit(as, *data++);
            }
        }
    }
}

void process_dot_include(ASSEMBLER *as) {
    get_token(as);
    if(*as->token_start != '"') {
        asm_err(as, "include expects a \" enclosed string as a parameter");
    } else {
        char file_name[PATH_MAX];
        size_t string_length = as->input - as->token_start - 2;
        strncpy(file_name, as->token_start + 1, string_length);
        file_name[string_length] = '\0';
        include_files_push(as, file_name);
    }
}

void process_dot_macro(ASSEMBLER *as) {
    int macro_okay = 1;
    MACRO macro;
    ARRAY_INIT(&macro.macro_parameters, MACRO_VARIABLE);
    next_token(as);   // Get to the name
    if(as->current_token.type != TOKEN_VAR) {
        macro.macro_name = NULL;
        macro.macro_name_length = 0;
        macro_okay = 0;
        asm_err(as, "Macro has no name");
    } else {
        size_t i;
        macro.macro_name = as->token_start;
        macro.macro_name_length = (int)(as->input - as->token_start);
        // Make sure no other macro by this name exists
        for(i = 0; i < as->macros.items; i++) {
            MACRO *existing_macro = ARRAY_GET(&as->macros, MACRO, i);
            if(existing_macro->macro_name_length == macro.macro_name_length && 0 == strnicmp(existing_macro->macro_name, macro.macro_name, macro.macro_name_length)) {
                macro_okay = 0;
                asm_err(as, "Macro with name %.*s has already been defined", macro.macro_name_length, macro.macro_name);
            }
        }
    }
    // Add macro parameters, if any
    while(as->current_token.type == TOKEN_VAR) {
        next_token(as);
        if(as->current_token.op == ',') {
            next_token(as);
        }
        if(as->current_token.type != TOKEN_END) {
            MACRO_VARIABLE mv;
            mv.variable_name = as->token_start;
            mv.variable_name_length = (int)(as->input - as->token_start);
            ARRAY_ADD(&macro.macro_parameters, mv);
        }
    }
    // After parameters it must be the end of the line
    if(!(util_is_newline(as->current_token.op) || as->current_token.op == ';')) {
        macro_okay = 0;
        asm_err(as, "Macro defenition error");
    }
    // Save the parse point as the macro body start point
    input_stack_push(as);
    macro.macro_body_input = *ARRAY_GET(&as->input_stack, INPUT_STACK, as->input_stack.items - 1);
    input_stack_pop(as);
    // Look for .endmacro, ignoring the macro body
    find_ab_passing_over_c(as, ".endmacro", NULL, ".macro");
    macro.macro_body_end = as->input;
    if(!(*as->input)) {
        macro_okay = 0;
        asm_err(as, ".macro %.*s L%05zu, with no .endmacro\n", macro.macro_name_length, macro.macro_name, macro.macro_body_input.current_line);
    }
    // If there were no errors, add the macro to the list of macros
    if(macro_okay) {
        ARRAY_ADD(&as->macros, macro);
    }
}

void process_dot_org(ASSEMBLER *as) {
    uint64_t value = evaluate_expression(as);
    if(as->current_address > value) {
        asm_err(as, "Assigning address %04X when address is already %04X error", value, as->current_address);
    } else {
        as->current_address = value;
        if(value < as->start_address) {
            as->start_address = value;
        }
    }
}

void process_dot_strcode(ASSEMBLER *as) {
    // Encoding doesn't matter till 2nd pass
    if(as->pass == 2) {
        get_token(as);                                        // skip past .strcode
        // Mark the expression start
        as->strcode = as->token_start;
    }
    // Find the end of the .strcode expression
    do {
        next_token(as);
    } while(as->current_token.type != TOKEN_END);
}

void process_dot_string(ASSEMBLER *as) {
    uint64_t value;
    SYMBOL_LABEL *sl;
    if(as->strcode) {
        // Add the _ variable if it doesn't yet exist and get a handle to the storage
        sl = symbol_store(as, "_", 1, SYMBOL_VARIABLE, 0);
    }
    do {
        // Get to a token to process
        next_token(as);
        // String data
        if(as->current_token.type == TOKEN_OP && as->current_token.op == '"') {
            // Loop over the string but stop before the closing quote
            while(as->token_start < (as->input - 1)) {
                // Handle quoted numbers (no \n or \t handling here)
                if(*as->token_start == '\\') {
                    as->token_start++;
                    if(*as->token_start == '\\') {
                        value = '\\';
                        as->token_start++;
                    } else if(tolower(*as->token_start) == 'x') {
                        as->token_start++;
                        value = strtoll(as->token_start, (char **) &as->token_start, 16);
                    } else if(*as->token_start == '%') {
                        as->token_start++;
                        value = strtoll(as->token_start, (char **) &as->token_start, 2);
                    } else if(*as->token_start == '0') {
                        value = strtoll(as->token_start, (char **) &as->token_start, 8);
                    } else if(*as->token_start >= '1' && *as->token_start <= '9') {
                        value = strtoll(as->token_start, (char **) &as->token_start, 10);
                    } else {
                        // Quote ascii to skip passing it through .strcode
                        value = *as->token_start++;
                    }
                    if(value >= 256) {
                        asm_err(as, "Escape value %ld not between 0 and 255", value);
                    }
                    emit(as, value);
                } else {
                    // Regular characters can go through the strcode process
                    int character = *as->token_start;

                    if(as->strcode) {
                        // Set the variable with the value of the string character
                        sl->symbol_value = character;
                        // Save the parse state to parse the expression
                        input_stack_push(as);
                        as->input = as->token_start = as->strcode;
                        // Evaluate the expression
                        character = evaluate_expression(as);
                        // Restore the parse state into the string
                        input_stack_pop(as);
                    }
                    // Write the resultant character out
                    emit(as, character);
                    as->token_start++;
                }
            }
            next_token(as);
        } else {
            // Non-string data must be byte expression (and 1st token already
            // loaded so use parse_ not evaluate_)
            value = parse_expression(as);
            emit(as, value);
            if(value >= 256) {
                asm_err(as, "Values %ld not between 0 and 255", value);
            }
        }
    } while(as->current_token.op == ',');
}

//----------------------------------------------------------------------------
// Non-expression Parse routines
void parse_address(ASSEMBLER *as) {
    // Address assignment like `* = EXPRESSION` or modification `* += EXPRESSION`
    uint16_t address;
    int op;
    get_token(as);
    if(-1 == (op = match(as, 3, "+=", "+", "="))) {
        asm_err(as, "Address assign error");
        return;
    }
    if(0 == op) {
        // += returned just the + so get the ='s
        next_token(as);
    }
    int64_t value = evaluate_expression(as);
    switch(op) {
        case 0:
        case 1:
            address = as->current_address + value;
            break;
        case 2:
            address = value;
            break;
    }
    if(as->current_address > address) {
        asm_err(as, "Assigning address %04X when address is already %04X error", address, as->current_address);
    } else {
        as->current_address = address;
        if(address < as->start_address) {
            as->start_address = address;
        }
    }
}

uint16_t parse_anonymous_address(ASSEMBLER *as) {
    // Anonymous label
    next_token(as);
    char op = as->current_token.op;
    int direction = 0;
    while(op == as->current_token.op && as->current_token.type == TOKEN_OP) {
        direction++;
        next_token(as);
    }
    switch(op) {
        case '+':
            break;
        case '-':
            direction = -direction;
            break;
        default:
            asm_err(as, "Unexpected symbol after anonymous : (%c)", op);
            break;
    }
    // The opcode has not been emitted so + 1
    uint16_t address = as->current_address + 1;
    if(!anonymous_symbol_lookup(as, &address, direction)) {
        asm_err(as, "Invalid anonymous label address");
    }
    return address;
}

void parse_dot_command(ASSEMBLER *as) {
    switch(as->opcode_info.opcode_id) {
        case GPERF_DOT_6502:
            as->valid_opcodes = 1;
            break;
        case GPERF_DOT_65c02:
            as->valid_opcodes = 0;
            break;
        case GPERF_DOT_ALIGN: {
                uint64_t value = evaluate_expression(as);
                as->current_address = (as->current_address + (value - 1)) & ~(value - 1);
            }
            break;
        case GPERF_DOT_BYTE:
            write_values(as, 8, BYTE_ORDER_LO);
            break;
        case GPERF_DOT_DROW:
            write_values(as, 16, BYTE_ORDER_HI);
            break;
        case GPERF_DOT_DROWD:
            write_values(as, 32, BYTE_ORDER_HI);
            break;
        case GPERF_DOT_DROWQ:
            write_values(as, 64, BYTE_ORDER_HI);
            break;
        case GPERF_DOT_DWORD:
            write_values(as, 32, BYTE_ORDER_LO);
            break;
        case GPERF_DOT_ELSE:
            parse_dot_else(as);
            break;
        case GPERF_DOT_ENDFOR:
            parse_dot_endfor(as);
            break;
        case GPERF_DOT_ENDIF:
            parse_dot_endif(as);
            break;
        case GPERF_DOT_ENDMACRO:
            parse_dot_endmacro(as);
            break;
        case GPERF_DOT_FOR:
            parse_dot_for(as);
            break;
        case GPERF_DOT_IF:
            parse_dot_if(as);
            break;
        case GPERF_DOT_INCBIN:
            process_dot_incbin(as);
            break;
        case GPERF_DOT_INCLUDE:
            process_dot_include(as);
            break;
        case GPERF_DOT_MACRO:
            process_dot_macro(as);
            break;
        case GPERF_DOT_ORG:
            process_dot_org(as);
            break;
        case GPERF_DOT_QWORD:
            write_values(as, 64, BYTE_ORDER_LO);
            break;
        case GPERF_DOT_STRCODE:
            process_dot_strcode(as);
            break;
        case GPERF_DOT_STRING:
            process_dot_string(as);
            break;
        case GPERF_DOT_WORD:
            write_values(as, 16, BYTE_ORDER_LO);
            break;
        default:
            asm_err(as, "opcode with id:%d not understood", as->opcode_info.opcode_id);
    }
}

void parse_label(ASSEMBLER *as) {
    const char *symbol_name = as->token_start;
    uint32_t name_length = as->input - 1 - as->token_start;
    if(!name_length) {
        // No name = anonymous label
        if(as->pass == 1) {
            // Only add anonymous labels in pass 1 (so there's no doubling up)
            ARRAY_ADD(&as->anon_symbols, as->current_address);
        }
    } else {
        symbol_store(as, symbol_name, name_length, SYMBOL_ADDRESS, as->current_address);
    }
}

void parse_opcode(ASSEMBLER *as) {
    next_token(as);
    if(is_valid_instruction_only(as)) {
        // Implied
        if(as->valid_opcodes && !asm_opcode_type[as->opcode_info.opcode_id][as->opcode_info.addressing_mode]) {
            asm_err(as, "Opcode %.3s with mode %s only valid in 65c02", as->opcode_info.mnemonic, address_mode_txt[as->opcode_info.addressing_mode]);
        }
        emit(as, asm_opcode[as->opcode_info.opcode_id][ADDRESS_MODE_ACCUMULATOR]);
    } else {
        int processed = 0;
        switch(as->current_token.op) {
            case '#':
                // Immediate
                as->opcode_info.value = evaluate_expression(as);
                as->opcode_info.addressing_mode = ADDRESS_MODE_IMMEDIATE;
                write_opcode(as);
                processed = 1;
                break;
            case '(': {
                    char reg;
                    int indirect = is_indirect(as, &reg);
                    if(indirect) {
                        // Already inside bracket
                        if(reg == 'x') {
                            // , in ,x ends expression so evaluate to ignore active open (
                            as->opcode_info.value = evaluate_expression(as);
                        } else {
                            // start with the "active" ( and end in ) so parse
                            as->opcode_info.value = parse_expression(as);
                        }
                        as->opcode_info.addressing_mode = indirect;
                        write_opcode(as);
                        if(indirect != ADDRESS_MODE_INDIRECT) {
                            // Indirect x or y need extra steps
                            next_token(as);
                            // Make sure it was ,x or ,y
                            if(tolower(*as->token_start) != reg) {
                                asm_err(as, "Expected ,%c", reg);
                            }
                            next_token(as);
                            // If it was ,x go past the closing )
                            if(as->current_token.op == ')') {
                                next_token(as);
                            }
                        }
                        processed = 1;
                    }
                }
                break;
        }
        if(!processed) {
            // general case
            if(as->current_token.type == TOKEN_END) {
                asm_err(as, "Opcode %.3s with mode %s expects an operand", as->opcode_info.mnemonic, address_mode_txt[as->opcode_info.addressing_mode]);
            }
            as->opcode_info.value = parse_expression(as);
            if(as->opcode_info.width >= 8 && as->expression_size > 8) {
                as->opcode_info.width = 16;                 //as->expression_size;
            }
            decode_abs_rel_zp_opcode(as);
        }
    }
}

void parse_variable(ASSEMBLER *as) {
    const char *symbol_name = as->token_start;
    uint32_t name_length = as->input - as->token_start;
    next_token(as);
    expect(as, '=');
    symbol_store(as, symbol_name, name_length, SYMBOL_VARIABLE, parse_expression(as));
}

//----------------------------------------------------------------------------
// Assembler start and end routines
int assembler_init(ASSEMBLER *as, ERRORLOG *errorlog, void *user, output_byte ob) {

    memset(as, 0, sizeof(ASSEMBLER));
    as->errorlog = errorlog;
    as->cb_assembler_ctx.user = user;
    as->cb_assembler_ctx.output_byte = ob;

    ARRAY_INIT(&as->anon_symbols, uint16_t);
    ARRAY_INIT(&as->loop_stack, FOR_LOOP);
    ARRAY_INIT(&as->macros, MACRO);
    ARRAY_INIT(&as->macro_buffers, char *);
    ARRAY_INIT(&as->input_stack, INPUT_STACK);

    int bucket;
    as->symbol_table = (DYNARRAY *)malloc(sizeof(DYNARRAY) * 256);
    for(bucket = 0; bucket < 256; bucket++) {
        ARRAY_INIT(&as->symbol_table[bucket], SYMBOL_LABEL);
    }

    include_files_init(as);

    as->start_address = -1;
    as->last_address = 0;
    as->pass = 0;
    return A2_OK;
}

int assembler_assemble(ASSEMBLER *as, const char *input_file, uint16_t address) {
    as->pass = 0;
    as->current_file = input_file;
    char start_path[PATH_MAX];
    const char *name = util_strrtok(input_file, "\\/");
    if(name) {
        util_dir_get_current(start_path, PATH_MAX);
        char file_path[PATH_MAX];
        int len = name - input_file;
        len = len > PATH_MAX ? PATH_MAX : len;
        strncpy(file_path, input_file, len);
        file_path[len] = '\0';
        util_dir_change(file_path);
        input_file = name + 1;
    }
    while(as->pass < 2) {
        if(A2_OK != include_files_push(as, input_file)) {
            return A2_ERR;
        }
        as->current_file = ARRAY_GET(&as->include_files.included_files, UTIL_FILE, 0)->file_display_name;
        as->current_address = address;
        as->pass++;
        while(as->pass < 3) {
            do {
                // a newline returns an empty token so keep
                // getting tokens till there's something to process
                get_token(as);
            } while(as->token_start == as->input && *as->input);

            if(as->token_start == as->input) {
                // Make sure all opened constructs were closed as well
                if(as->if_active) {
                    as->if_active = 0;
                    asm_err(as, ".if without .endif");
                }
                if(as->loop_stack.items) {
                    asm_err(as, ".for L:%05zu, without .endfor", ARRAY_GET(&as->loop_stack, FOR_LOOP, as->loop_stack.items - 1)->body_line - 1);
                    as->loop_stack.items = 0;
                }
                // The end of the file has been reached so if it was an included file
                // pop to the parent, or end
                if(A2_OK == include_files_pop(as)) {
                    continue;
                }
                break;
            }

            // Prioritize specific checks to avoid ambiguity
            if(is_opcode(as)) {                               // Opcode first
                parse_opcode(as);
            } else if(is_dot_command(as)) {                   // Dot commands next
                parse_dot_command(as);
            } else if(is_label(as)) {                         // Labels (endings in ":")
                parse_label(as);
            } else if(is_address(as)) {                       // Address (starting with "*")
                parse_address(as);
            } else if(is_macro_parse_macro(as)) {             // Macros are like variables but can be found
                continue;
            } else if(is_variable(as)) {                      // Variables are last (followed by "=")
                // parse_variable(as);
                as->current_token.type = TOKEN_VAR;         // Variable Name
                as->current_token.name = as->token_start;
                as->current_token.name_length = as->input - as->token_start;
                as->current_token.name_hash = util_fnv_1a_hash(as->current_token.name, as->current_token.name_length);
                parse_expression(as);
            } else {
                asm_err(as, "Unknown token");
            }
        }
        // Flush the macros between passes so they can be re-parsed and
        // errors reported properly om the second pass
        flush_macros(as);
    }
    if(name) {
        util_dir_change(start_path);
    }
    return A2_OK;
}

void assembler_shutdown(ASSEMBLER *as) {
    include_files_cleanup(as);
    for(int i = 0; i < 256; i++) {
        array_free(&as->symbol_table[i]);
    }
    free(as->symbol_table);
    // free active macro buffer (if any)
    while(as->macro_buffers.items) {
        char *buf = *ARRAY_GET(&as->macro_buffers, char*, as->macro_buffers.items - 1);
        free(buf);
        as->macro_buffers.items--;
    }
    array_free(&as->input_stack);
    array_free(&as->macros);
    array_free(&as->macro_buffers);
    array_free(&as->loop_stack);
    array_free(&as->anon_symbols);
}

