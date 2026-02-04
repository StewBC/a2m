/* ANSI-C code produced by gperf version 3.3 */
/* Command-line: gperf -c --ignore-case gperf/asm6502.gperf  */
/* Computed positions: -k'1-2,5,$' */

#if !((' ' == 32) && ('!' == 33) && ('"' == 34) && ('#' == 35) \
      && ('%' == 37) && ('&' == 38) && ('\'' == 39) && ('(' == 40) \
      && (')' == 41) && ('*' == 42) && ('+' == 43) && (',' == 44) \
      && ('-' == 45) && ('.' == 46) && ('/' == 47) && ('0' == 48) \
      && ('1' == 49) && ('2' == 50) && ('3' == 51) && ('4' == 52) \
      && ('5' == 53) && ('6' == 54) && ('7' == 55) && ('8' == 56) \
      && ('9' == 57) && (':' == 58) && (';' == 59) && ('<' == 60) \
      && ('=' == 61) && ('>' == 62) && ('?' == 63) && ('A' == 65) \
      && ('B' == 66) && ('C' == 67) && ('D' == 68) && ('E' == 69) \
      && ('F' == 70) && ('G' == 71) && ('H' == 72) && ('I' == 73) \
      && ('J' == 74) && ('K' == 75) && ('L' == 76) && ('M' == 77) \
      && ('N' == 78) && ('O' == 79) && ('P' == 80) && ('Q' == 81) \
      && ('R' == 82) && ('S' == 83) && ('T' == 84) && ('U' == 85) \
      && ('V' == 86) && ('W' == 87) && ('X' == 88) && ('Y' == 89) \
      && ('Z' == 90) && ('[' == 91) && ('\\' == 92) && (']' == 93) \
      && ('^' == 94) && ('_' == 95) && ('a' == 97) && ('b' == 98) \
      && ('c' == 99) && ('d' == 100) && ('e' == 101) && ('f' == 102) \
      && ('g' == 103) && ('h' == 104) && ('i' == 105) && ('j' == 106) \
      && ('k' == 107) && ('l' == 108) && ('m' == 109) && ('n' == 110) \
      && ('o' == 111) && ('p' == 112) && ('q' == 113) && ('r' == 114) \
      && ('s' == 115) && ('t' == 116) && ('u' == 117) && ('v' == 118) \
      && ('w' == 119) && ('x' == 120) && ('y' == 121) && ('z' == 122) \
      && ('{' == 123) && ('|' == 124) && ('}' == 125) && ('~' == 126))
/* The character set is not based on ISO-646.  */
#error "gperf generated tables don't work with this execution character set. Please report a bug to <bug-gperf@gnu.org>."
#endif

#line 1 "gperf/asm6502.gperf"

#include "asm_lib.h"
#line 7 "gperf/asm6502.gperf"
struct OPCODEINFO;

#define TOTAL_KEYWORDS 101
#define MIN_WORD_LENGTH 3
#define MAX_WORD_LENGTH 10
#define MIN_HASH_VALUE 17
#define MAX_HASH_VALUE 268
/* maximum key range = 252, duplicates = 0 */

#ifndef GPERF_DOWNCASE
#define GPERF_DOWNCASE 1
static unsigned char gperf_downcase[256] =
  {
      0,   1,   2,   3,   4,   5,   6,   7,   8,   9,  10,  11,  12,  13,  14,
     15,  16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,  29,
     30,  31,  32,  33,  34,  35,  36,  37,  38,  39,  40,  41,  42,  43,  44,
     45,  46,  47,  48,  49,  50,  51,  52,  53,  54,  55,  56,  57,  58,  59,
     60,  61,  62,  63,  64,  97,  98,  99, 100, 101, 102, 103, 104, 105, 106,
    107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121,
    122,  91,  92,  93,  94,  95,  96,  97,  98,  99, 100, 101, 102, 103, 104,
    105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119,
    120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134,
    135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 149,
    150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160, 161, 162, 163, 164,
    165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175, 176, 177, 178, 179,
    180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191, 192, 193, 194,
    195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208, 209,
    210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223, 224,
    225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239,
    240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254,
    255
  };
#endif

#ifndef GPERF_CASE_STRNCMP
#define GPERF_CASE_STRNCMP 1
static int
gperf_case_strncmp (register const char *s1, register const char *s2, register size_t n)
{
  for (; n > 0;)
    {
      unsigned char c1 = gperf_downcase[(unsigned char)*s1++];
      unsigned char c2 = gperf_downcase[(unsigned char)*s2++];
      if (c1 != 0 && c1 == c2)
        {
          n--;
          continue;
        }
      return (int)c1 - (int)c2;
    }
  return 0;
}
#endif

#ifdef __GNUC__
__inline
#else
#ifdef __cplusplus
inline
#endif
#endif
static unsigned int
hash (register const char *str, register size_t len)
{
  static unsigned short asso_values[] =
    {
      269, 269, 269, 269, 269, 269, 269, 269, 269, 269,
      269, 269, 269, 269, 269, 269, 269, 269, 269, 269,
      269, 269, 269, 269, 269, 269, 269, 269, 269, 269,
      269, 269,   5, 269, 269, 269, 269, 269, 269, 269,
      269, 269, 269, 269, 269, 269,  10, 269,  45, 269,
        5, 269, 269, 269, 269,   5, 269, 269, 269, 269,
      269, 269, 269, 269, 269, 105,  50,  20,  15,   5,
        0,   5, 269,  25,  27,  20,  82,  65,  75,   2,
       60, 102,   0,   5,   0,  92,  55,   1,  52, 120,
       10, 269, 269, 269, 269, 269, 269, 105,  50,  20,
       15,   5,   0,   5, 269,  25,  27,  20,  82,  65,
       75,   2,  60, 102,   0,   5,   0,  92,  55,   1,
       52, 120,  10, 269, 269, 269, 269, 269, 269, 269,
      269, 269, 269, 269, 269, 269, 269, 269, 269, 269,
      269, 269, 269, 269, 269, 269, 269, 269, 269, 269,
      269, 269, 269, 269, 269, 269, 269, 269, 269, 269,
      269, 269, 269, 269, 269, 269, 269, 269, 269, 269,
      269, 269, 269, 269, 269, 269, 269, 269, 269, 269,
      269, 269, 269, 269, 269, 269, 269, 269, 269, 269,
      269, 269, 269, 269, 269, 269, 269, 269, 269, 269,
      269, 269, 269, 269, 269, 269, 269, 269, 269, 269,
      269, 269, 269, 269, 269, 269, 269, 269, 269, 269,
      269, 269, 269, 269, 269, 269, 269, 269, 269, 269,
      269, 269, 269, 269, 269, 269, 269, 269, 269, 269,
      269, 269, 269, 269, 269, 269, 269, 269, 269, 269,
      269, 269, 269, 269, 269, 269, 269
    };
  register unsigned int hval = len;

  switch (hval)
    {
      default:
        hval += asso_values[(unsigned char)str[4]];
#if (defined __cplusplus && (__cplusplus >= 201703L || (__cplusplus >= 201103L && defined __clang__ && __clang_major__ + (__clang_minor__ >= 9) > 3))) || (defined __STDC_VERSION__ && __STDC_VERSION__ >= 202000L && ((defined __GNUC__ && __GNUC__ >= 10) || (defined __clang__ && __clang_major__ >= 9)))
      [[fallthrough]];
#elif (defined __GNUC__ && __GNUC__ >= 7) || (defined __clang__ && __clang_major__ >= 10)
      __attribute__ ((__fallthrough__));
#endif
      /*FALLTHROUGH*/
      case 4:
      case 3:
      case 2:
        hval += asso_values[(unsigned char)str[1]+1];
#if (defined __cplusplus && (__cplusplus >= 201703L || (__cplusplus >= 201103L && defined __clang__ && __clang_major__ + (__clang_minor__ >= 9) > 3))) || (defined __STDC_VERSION__ && __STDC_VERSION__ >= 202000L && ((defined __GNUC__ && __GNUC__ >= 10) || (defined __clang__ && __clang_major__ >= 9)))
      [[fallthrough]];
#elif (defined __GNUC__ && __GNUC__ >= 7) || (defined __clang__ && __clang_major__ >= 10)
      __attribute__ ((__fallthrough__));
#endif
      /*FALLTHROUGH*/
      case 1:
        hval += asso_values[(unsigned char)str[0]];
        break;
    }
  return hval + asso_values[(unsigned char)str[len - 1]];
}

struct OPCODEINFO *
in_word_set (register const char *str, register size_t len)
{
#if (defined __GNUC__ && __GNUC__ + (__GNUC_MINOR__ >= 6) > 4) || (defined __clang__ && __clang_major__ >= 3)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
  static struct OPCODEINFO wordlist[] =
    {
      {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""},
      {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""},
#line 21 "gperf/asm6502.gperf"
      {".endfor",    GPERF_DOT_ENDFOR    , 0, 0, 0},
      {""},
#line 28 "gperf/asm6502.gperf"
      {".for",       GPERF_DOT_FOR       , 0, 0, 0},
#line 26 "gperf/asm6502.gperf"
      {".endrepeat", GPERF_DOT_ENDREPEAT , 0, 0, 0},
      {""},
#line 16 "gperf/asm6502.gperf"
      {".drow",      GPERF_DOT_DROW      , 0, 0, 0},
#line 97 "gperf/asm6502.gperf"
      {"sed",        GPERF_OPCODE_SED    , 0, 0, 0},
#line 38 "gperf/asm6502.gperf"
      {".res",       GPERF_DOT_RES       , 0, 0, 0},
#line 20 "gperf/asm6502.gperf"
      {".else",      GPERF_DOT_ELSE      , 0, 0, 0},
      {""},
#line 37 "gperf/asm6502.gperf"
      {".repeat",    GPERF_DOT_REPEAT    , 0, 0, 0},
#line 96 "gperf/asm6502.gperf"
      {"sec",        GPERF_OPCODE_SEC    , 0, 0, 0},
#line 27 "gperf/asm6502.gperf"
      {".endscope",  GPERF_DOT_ENDSCOPE  , 0, 0, 0},
#line 76 "gperf/asm6502.gperf"
      {"jsr",        GPERF_OPCODE_JSR    ,16, 0, 0},
#line 36 "gperf/asm6502.gperf"
      {".qword",     GPERF_DOT_QWORD     , 0, 0, 0},
#line 39 "gperf/asm6502.gperf"
      {".segdef",    GPERF_DOT_SEGDEF    , 0, 0, 0},
#line 98 "gperf/asm6502.gperf"
      {"sei",        GPERF_OPCODE_SEI    , 0, 0, 0},
      {""},
#line 10 "gperf/asm6502.gperf"
      {".6502     ", GPERF_DOT_6502      , 0, 0, 0},
#line 19 "gperf/asm6502.gperf"
      {".dword",     GPERF_DOT_DWORD     , 0, 0, 0},
#line 17 "gperf/asm6502.gperf"
      {".drowd",     GPERF_DOT_DROWD     , 0, 0, 0},
#line 67 "gperf/asm6502.gperf"
      {"dec",        GPERF_OPCODE_DEC    , 8, 0, 0},
      {""},
#line 29 "gperf/asm6502.gperf"
      {".if",        GPERF_DOT_IF        , 0, 0, 0},
#line 22 "gperf/asm6502.gperf"
      {".endif",     GPERF_DOT_ENDIF     , 0, 0, 0},
      {""},
#line 42 "gperf/asm6502.gperf"
      {".strcode",   GPERF_DOT_STRCODE   , 0, 0, 0},
      {""},
#line 15 "gperf/asm6502.gperf"
      {".byte",      GPERF_DOT_BYTE      , 0, 0, 0},
      {""},
#line 43 "gperf/asm6502.gperf"
      {".string",    GPERF_DOT_STRING    , 0, 0, 0},
#line 95 "gperf/asm6502.gperf"
      {"sbc",        GPERF_OPCODE_SBC    , 8, 0, 0},
      {""},
#line 72 "gperf/asm6502.gperf"
      {"inc",        GPERF_OPCODE_INC    , 8, 0, 0},
      {""}, {""},
#line 106 "gperf/asm6502.gperf"
      {"tsb",        GPERF_OPCODE_TSB    , 8, 0, 0},
      {""},
#line 107 "gperf/asm6502.gperf"
      {"tsx",        GPERF_OPCODE_TSX    , 0, 0, 0},
      {""}, {""},
#line 105 "gperf/asm6502.gperf"
      {"trb",        GPERF_OPCODE_TRB    , 8, 0, 0},
#line 58 "gperf/asm6502.gperf"
      {"bvs",        GPERF_OPCODE_BVS    , 1, 0, 0},
#line 53 "gperf/asm6502.gperf"
      {"bne",        GPERF_OPCODE_BNE    , 1, 0, 0},
      {""}, {""},
#line 92 "gperf/asm6502.gperf"
      {"ror",        GPERF_OPCODE_ROR    , 8, 0, 0},
      {""},
#line 12 "gperf/asm6502.gperf"
      {".addr",      GPERF_DOT_WORD      , 0, 0, 0},
      {""}, {""},
#line 70 "gperf/asm6502.gperf"
      {"eor",        GPERF_OPCODE_EOR    , 8, 0, 0},
      {""},
#line 68 "gperf/asm6502.gperf"
      {"dex",        GPERF_OPCODE_DEX    , 0, 0, 0},
      {""}, {""},
#line 49 "gperf/asm6502.gperf"
      {"bcs",        GPERF_OPCODE_BCS    , 1, 0, 0},
#line 57 "gperf/asm6502.gperf"
      {"bvc",        GPERF_OPCODE_BVC    , 1, 0, 0},
#line 11 "gperf/asm6502.gperf"
      {".65c02    ", GPERF_DOT_65c02     , 0, 0, 0},
      {""},
#line 25 "gperf/asm6502.gperf"
      {".endrep",    GPERF_DOT_ENDREPEAT , 0, 0, 0},
#line 56 "gperf/asm6502.gperf"
      {"brk",        GPERF_OPCODE_BRK    , 0, 0, 0},
#line 34 "gperf/asm6502.gperf"
      {".org",       GPERF_DOT_ORG       , 0, 0, 0},
#line 51 "gperf/asm6502.gperf"
      {"bit",        GPERF_OPCODE_BIT    , 8, 0, 0},
#line 41 "gperf/asm6502.gperf"
      {".scope",     GPERF_DOT_SCOPE     , 0, 0, 0},
#line 73 "gperf/asm6502.gperf"
      {"inx",        GPERF_OPCODE_INX    , 0, 0, 0},
#line 40 "gperf/asm6502.gperf"
      {".segment",   GPERF_DOT_SEGMENT   , 0, 0, 0},
      {""},
#line 80 "gperf/asm6502.gperf"
      {"lsr",        GPERF_OPCODE_LSR    , 0, 0, 0},
#line 23 "gperf/asm6502.gperf"
      {".endmacro",  GPERF_DOT_ENDMACRO  , 0, 0, 0},
      {""},
#line 48 "gperf/asm6502.gperf"
      {"bcc",        GPERF_OPCODE_BCC    , 1, 0, 0},
      {""}, {""}, {""}, {""},
#line 33 "gperf/asm6502.gperf"
      {".macro",     GPERF_DOT_MACRO     , 0, 0, 0},
      {""}, {""}, {""},
#line 44 "gperf/asm6502.gperf"
      {".word",      GPERF_DOT_WORD      , 0, 0, 0},
#line 24 "gperf/asm6502.gperf"
      {".endproc",   GPERF_DOT_ENDPROC   , 0, 0, 0},
      {""},
#line 94 "gperf/asm6502.gperf"
      {"rts",        GPERF_OPCODE_RTS    , 0, 0, 0},
      {""},
#line 14 "gperf/asm6502.gperf"
      {".asciiz",    GPERF_DOT_ASCIIZ    , 0, 0, 0},
#line 60 "gperf/asm6502.gperf"
      {"cld",        GPERF_OPCODE_CLD    , 0, 0, 0},
      {""},
#line 103 "gperf/asm6502.gperf"
      {"tax",        GPERF_OPCODE_TAX    , 0, 0, 0},
      {""}, {""},
#line 59 "gperf/asm6502.gperf"
      {"clc",        GPERF_OPCODE_CLC    , 0, 0, 0},
      {""},
#line 102 "gperf/asm6502.gperf"
      {"stz",        GPERF_OPCODE_STZ    , 8, 0, 0},
      {""}, {""},
#line 61 "gperf/asm6502.gperf"
      {"cli",        GPERF_OPCODE_CLI    , 0, 0, 0},
      {""},
#line 82 "gperf/asm6502.gperf"
      {"ora",        GPERF_OPCODE_ORA    , 8, 0, 0},
      {""}, {""},
#line 110 "gperf/asm6502.gperf"
      {"tya",        GPERF_OPCODE_TYA    , 0, 0, 0},
      {""},
#line 93 "gperf/asm6502.gperf"
      {"rti",        GPERF_OPCODE_RTI    , 0, 0, 0},
      {""}, {""},
#line 66 "gperf/asm6502.gperf"
      {"dea",        GPERF_OPCODE_DEA    , 0, 0, 0},
#line 18 "gperf/asm6502.gperf"
      {".drowq",     GPERF_DOT_DROWQ     , 0, 0, 0},
#line 46 "gperf/asm6502.gperf"
      {"and",        GPERF_OPCODE_AND    , 8, 0, 0},
      {""}, {""},
#line 109 "gperf/asm6502.gperf"
      {"txs",        GPERF_OPCODE_TXS    , 0, 0, 0},
      {""}, {""}, {""},
#line 31 "gperf/asm6502.gperf"
      {".include",   GPERF_DOT_INCLUDE   , 0, 0, 0},
#line 45 "gperf/asm6502.gperf"
      {"adc",        GPERF_OPCODE_ADC    , 8, 0, 0},
      {""},
#line 71 "gperf/asm6502.gperf"
      {"ina",        GPERF_OPCODE_INA    , 0, 0, 0},
      {""}, {""},
#line 69 "gperf/asm6502.gperf"
      {"dey",        GPERF_OPCODE_DEY    , 0, 0, 0},
      {""},
#line 85 "gperf/asm6502.gperf"
      {"phx",        GPERF_OPCODE_PHX    , 0, 0, 0},
      {""},
#line 78 "gperf/asm6502.gperf"
      {"ldx",        GPERF_OPCODE_LDX    , 8, 0, 0},
#line 62 "gperf/asm6502.gperf"
      {"clv",        GPERF_OPCODE_CLV    , 0, 0, 0},
      {""},
#line 91 "gperf/asm6502.gperf"
      {"rol",        GPERF_OPCODE_ROL    , 8, 0, 0},
#line 13 "gperf/asm6502.gperf"
      {".align",     GPERF_DOT_ALIGN     , 0, 0, 0},
      {""},
#line 84 "gperf/asm6502.gperf"
      {"php",        GPERF_OPCODE_PHP    , 0, 0, 0},
      {""},
#line 74 "gperf/asm6502.gperf"
      {"iny",        GPERF_OPCODE_INY    , 0, 0, 0},
      {""},
#line 100 "gperf/asm6502.gperf"
      {"stx",        GPERF_OPCODE_STX    , 8, 0, 0},
#line 52 "gperf/asm6502.gperf"
      {"bmi",        GPERF_OPCODE_BMI    , 1, 0, 0},
      {""},
#line 50 "gperf/asm6502.gperf"
      {"beq",        GPERF_OPCODE_BEQ    , 1, 0, 0},
      {""},
#line 35 "gperf/asm6502.gperf"
      {".proc",      GPERF_DOT_PROC      , 0, 0, 0},
#line 63 "gperf/asm6502.gperf"
      {"cmp",        GPERF_OPCODE_CMP    , 8, 0, 0},
      {""}, {""}, {""}, {""},
#line 55 "gperf/asm6502.gperf"
      {"bra",        GPERF_OPCODE_BRA    , 1, 0, 0},
      {""},
#line 75 "gperf/asm6502.gperf"
      {"jmp",        GPERF_OPCODE_JMP    ,16, 0, 0},
      {""}, {""}, {""},
#line 30 "gperf/asm6502.gperf"
      {".incbin",    GPERF_DOT_INCBIN    , 0, 0, 0},
      {""}, {""}, {""},
#line 104 "gperf/asm6502.gperf"
      {"tay",        GPERF_OPCODE_TAY    , 0, 0, 0},
      {""}, {""}, {""},
#line 64 "gperf/asm6502.gperf"
      {"cpx",        GPERF_OPCODE_CPX    , 8, 0, 0},
      {""}, {""},
#line 89 "gperf/asm6502.gperf"
      {"plx",        GPERF_OPCODE_PLX    , 0, 0, 0},
      {""}, {""}, {""}, {""}, {""}, {""}, {""},
#line 88 "gperf/asm6502.gperf"
      {"plp",        GPERF_OPCODE_PLP    , 0, 0, 0},
      {""},
#line 47 "gperf/asm6502.gperf"
      {"asl",        GPERF_OPCODE_ASL    , 8, 0, 0},
      {""}, {""},
#line 83 "gperf/asm6502.gperf"
      {"pha",        GPERF_OPCODE_PHA    , 0, 0, 0},
      {""},
#line 77 "gperf/asm6502.gperf"
      {"lda",        GPERF_OPCODE_LDA    , 8, 0, 0},
      {""}, {""},
#line 81 "gperf/asm6502.gperf"
      {"nop",        GPERF_OPCODE_NOP    , 0, 0, 0},
      {""}, {""}, {""}, {""}, {""}, {""},
#line 99 "gperf/asm6502.gperf"
      {"sta",        GPERF_OPCODE_STA    , 8, 0, 0},
      {""}, {""},
#line 86 "gperf/asm6502.gperf"
      {"phy",        GPERF_OPCODE_PHY    , 0, 0, 0},
      {""},
#line 79 "gperf/asm6502.gperf"
      {"ldy",        GPERF_OPCODE_LDY    , 8, 0, 0},
      {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""},
#line 101 "gperf/asm6502.gperf"
      {"sty",        GPERF_OPCODE_STY    , 8, 0, 0},
      {""}, {""}, {""}, {""}, {""}, {""}, {""},
#line 108 "gperf/asm6502.gperf"
      {"txa",        GPERF_OPCODE_TXA    , 0, 0, 0},
      {""}, {""}, {""}, {""},
#line 87 "gperf/asm6502.gperf"
      {"pla",        GPERF_OPCODE_PLA    , 0, 0, 0},
      {""}, {""}, {""},
#line 54 "gperf/asm6502.gperf"
      {"bpl",        GPERF_OPCODE_BPL    , 1, 0, 0},
      {""}, {""}, {""}, {""}, {""}, {""}, {""},
#line 65 "gperf/asm6502.gperf"
      {"cpy",        GPERF_OPCODE_CPY    , 8, 0, 0},
      {""}, {""},
#line 90 "gperf/asm6502.gperf"
      {"ply",        GPERF_OPCODE_PLY    , 0, 0, 0},
      {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""},
      {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""},
      {""},
#line 32 "gperf/asm6502.gperf"
      {".local",     GPERF_DOT_LOCAL     , 0, 0, 0}
    };
#if (defined __GNUC__ && __GNUC__ + (__GNUC_MINOR__ >= 6) > 4) || (defined __clang__ && __clang_major__ >= 3)
#pragma GCC diagnostic pop
#endif

  if (len <= MAX_WORD_LENGTH && len >= MIN_WORD_LENGTH)
    {
      register unsigned int key = hash (str, len);

      if (key <= MAX_HASH_VALUE)
        {
          register const char *s = wordlist[key].mnemonic;

          if ((((unsigned char)*str ^ (unsigned char)*s) & ~32) == 0 && !gperf_case_strncmp (str, s, len) && s[len] == '\0')
            return &wordlist[key];
        }
    }
  return (struct OPCODEINFO *) 0;
}
#line 111 "gperf/asm6502.gperf"

