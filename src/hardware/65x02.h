// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

// These values are for the 65x02 address space - so 64K.
// This is not the RAM the machine has, just what the CPU can see at any one time
#define BANK_SIZE       (64*1024)
#define PAGE_SIZE       (256)
#define NUM_PAGES       ((BANK_SIZE)/(PAGE_SIZE))

// A ROM_BLOCK tracks a block of rom bytes
typedef struct {
    uint32_t address;
    uint32_t length;
    uint8_t *bytes;
} ROM_BLOCK;

// ROM contains an array of ROM_BLOCKs which may (or not) be mapped into the 6502's 64K
typedef struct {
    ROM_BLOCK *blocks;
    uint16_t num_blocks;
} ROMS;

// When mapping ram into the 6502 address space, this is used to map read or write ram
typedef enum {
    PAGE_MAP_READ,
    PAGE_MAP_WRITE
} PAGE_MAP_TYPE;

// Pages is a bundle of arrays.  Each array has pointers to bytes which should
// be seen as PAGE_SIZE in length (256 bytes)
typedef struct {
    uint16_t num_pages;
    uint8_t **read_pages;                       // array of page bytes mapped for reading
    uint8_t **write_pages;                      // array of page bytes mapped for writing
    uint64_t **last_write_pages;                // array of page 64 bit entries for tracking write addresses
} PAGES;

typedef struct {
    uint8_t *RAM_MAIN;                          // The ram_size memory - addressable in max 64k chunks
    uint8_t *RAM_WATCH;                         // 64K watch map for IO / Breakpoints. See WATCH_MASK
    uint8_t *RAM_LC;                            // LC Ram (Always 2*16 for ease with //e)
    uint64_t *RAM_LAST_WRITE;                   // 64-bit array of ram size
    uint64_t *RAM_LC_LAST_WRITE;                // 64-bit array of 32K (LC ram size)
} RAM;

enum {
    CPU_6502,
    CPU_65c02,
};

/* The 6502 internals*/
typedef struct {
    uint16_t pc;                                // Program counter
    uint16_t opcode_pc;                         // for last write, track last opcode pc
    uint16_t sp;                                // Stack pointer
    uint8_t A, X, Y;                            // 8 bit registers
    union {
        struct {
            uint8_t C: 1;                       // carry
            uint8_t Z: 1;                       // zero
            uint8_t I: 1;                       // Interrupt Disable
            uint8_t D: 1;                       // BCD mode
            uint8_t B: 1;                       // Break
            uint8_t E: 1;                       // Extra (almost unused)
            uint8_t V: 1;                       // Overflow
            uint8_t N: 1;                       // Negative
        };
        uint8_t flags;
    };
    union {
        struct {
            uint8_t address_lo;
            uint8_t address_hi;
        };
        uint16_t address_16;                    // For Emulation - Usually where bytes will be fetched
    };
    union {
        struct {
            uint8_t scratch_lo;
            uint8_t scratch_hi;
        };
        uint16_t scratch_16;                    // For Emulation - Placeholder
    };
    struct {
        uint8_t page_fault: 1;                  // During stages where a page-fault could happen, denotes fault
    };
    uint32_t class;                             // CPU_6502 or CPU_65c02
    uint64_t cycles;                            // Total count of cycles the cpu has executed
} CPU;

typedef enum {
//  0         1          2        3       4          5          6          7       8         9          A         B       C          D          E          F
    BRK     , ORA_X_ind, UND_02 , UND_03, UND_04   , ORA_zpg  , ASL_zpg  , UND_07, PHP     , ORA_imm  , ASL_A   , UND_0B, UND_0C   , ORA_abs  , ASL_abs  , UND_0F, // 0
    BPL_rel , ORA_ind_Y, UND_12 , UND_13, UND_14   , ORA_zpg_X, ASL_zpg_X, UND_17, CLC     , ORA_abs_Y, UND_1A  , UND_1B, UND_1C   , ORA_abs_X, ASL_abs_X, UND_1F, // 1
    JSR_abs , AND_X_ind, UND_22 , UND_23, BIT_zpg  , AND_zpg  , ROL_zpg  , UND_27, PLP     , AND_imm  , ROL_A   , UND_2B, BIT_abs  , AND_abs  , ROL_abs  , UND_2F, // 2
    BMI_rel , AND_ind_Y, UND_32 , UND_33, UND_34   , AND_zpg_X, ROL_zpg_X, UND_37, SEC     , AND_abs_Y, UND_3A  , UND_3B, UND_3C   , AND_abs_X, ROL_abs_X, UND_3F, // 3
    RTI     , EOR_X_ind, UND_42 , UND_43, UND_44   , EOR_zpg  , LSR_zpg  , UND_47, PHA     , EOR_imm  , LSR_A   , UND_4B, JMP_abs  , EOR_abs  , LSR_abs  , UND_4F, // 4
    BVC_rel , EOR_ind_Y, UND_52 , UND_53, UND_54   , EOR_zpg_X, LSR_zpg_X, UND_57, CLI     , EOR_abs_Y, UND_5A  , UND_5B, UND_5C   , EOR_abs_X, LSR_abs_X, UND_5F, // 5
    RTS     , ADC_X_ind, UND_62 , UND_63, UND_64   , ADC_zpg  , ROR_zpg  , UND_67, PLA     , ADC_imm  , ROR_A   , UND_6B, JMP_ind  , ADC_abs  , ROR_abs  , UND_6F, // 6
    BVS_rel , ADC_ind_Y, UND_72 , UND_73, UND_74   , ADC_zpg_X, ROR_zpg_X, UND_77, SEI     , ADC_abs_Y, UND_7A  , UND_7B, UND_7C   , ADC_abs_X, ROR_abs_X, UND_7F, // 7
    UND_80  , STA_X_ind, UND_82 , UND_83, STY_zpg  , STA_zpg  , STX_zpg  , UND_87, DEY     , UND_89   , TXA     , UND_8B, STY_abs  , STA_abs  , STX_abs  , UND_8F, // 8
    BCC_rel , STA_ind_Y, UND_92 , UND_93, STY_zpg_X, STA_zpg_X, STX_zpg_Y, UND_97, TYA     , STA_abs_Y, TXS     , UND_9B, UND_9C   , STA_abs_X, UND_9E   , UND_9F, // 9
    LDY_imm , LDA_X_ind, LDX_imm, UND_A3, LDY_zpg  , LDA_zpg  , LDX_zpg  , UND_A7, TAY     , LDA_imm  , TAX     , UND_AB, LDY_abs  , LDA_abs  , LDX_abs  , UND_AF, // A
    BCS_rel , LDA_ind_Y, UND_B2 , UND_B3, LDY_zpg_X, LDA_zpg_X, LDX_zpg_Y, UND_B7, CLV     , LDA_abs_Y, TSX     , UND_BB, LDY_abs_X, LDA_abs_X, LDX_abs_Y, UND_BF, // B
    CPY_imm , CMP_X_ind, UND_C2 , UND_C3, CPY_zpg  , CMP_zpg  , DEC_zpg  , UND_C7, INY     , CMP_imm  , DEX     , UND_CB, CPY_abs  , CMP_abs  , DEC_abs  , UND_CF, // C
    BNE_rel , CMP_ind_Y, UND_D2 , UND_D3, UND_D4   , CMP_zpg_X, DEC_zpg_X, UND_D7, CLD     , CMP_abs_Y, UND_DA  , UND_DB, UND_DC   , CMP_abs_X, DEC_abs_X, UND_DF, // D
    CPX_imm , SBC_X_ind, UND_E2 , UND_E3, CPX_zpg  , SBC_zpg  , INC_zpg  , UND_E7, INX     , SBC_imm  , NOP     , UND_EB, CPX_abs  , SBC_abs  , INC_abs  , UND_EF, // E
    BEQ_rel , SBC_ind_Y, UND_F2 , UND_F3, UND_F4   , SBC_zpg_X, INC_zpg_X, UND_F7, SED     , SBC_abs_Y, UND_FA  , UND_FB, UND_FC   , SBC_abs_X, INC_abs_X, UND_FF, // F
} OP_6502;

typedef enum {
    TSB_zpg     = 0x04,
    TSB_abs     = 0x0C,
    ORA_ind_zp  = 0x12,
    TRB_zpg     = 0x14,
    INA         = 0x1A,
    TRB_abs     = 0x1C,
    AND_ind_zp  = 0x32,
    BIT_zpg_x   = 0x34,
    DEA         = 0x3A,
    BIT_abs_x   = 0x3C,
    EOR_ind_zp  = 0x52,
    PHY         = 0x5A,
    STZ_zpg     = 0x64,
    ADC_ind_zp  = 0x72,
    STZ_zpg_x   = 0x74,
    PLY         = 0x7A,
    JMP_ind_x   = 0x7C,
    BRA         = 0x80,
    BIT_imm     = 0x89,
    STA_ind_zp  = 0x92,
    STZ_abs     = 0x9C,
    STZ_abs_x   = 0x9E,
    LDA_ind_zp  = 0xB2,
    CMP_ind_zp  = 0xD2,
    PHX         = 0xDA,
    SBC_ind_zp  = 0xF2,
    PLX         = 0xFA,
} OP_65c02;

// Forward declarations
typedef struct APPLE2 APPLE2;

// Configure the ram, ROM and bytes setup (what is mapped in)
uint8_t rom_init(ROMS *rom, uint16_t num_blocks);
void rom_add(ROMS *rom, uint8_t block_num, uint32_t address, uint32_t length, uint8_t *bytes);
uint8_t pages_init(PAGES *pages, uint32_t length);
void pages_map(PAGES *pages, PAGE_MAP_TYPE map, uint32_t address, uint32_t length, RAM *ram);
void pages_map_lc(PAGES *pages, PAGE_MAP_TYPE map_type, uint32_t address, uint32_t length, uint32_t from, RAM *ram);
void pages_map_rom(PAGES *pages, uint32_t address, uint32_t length, uint8_t *rom_bytes);
void pages_map_rom_block(PAGES *pages, ROM_BLOCK *block);

// 1 time init
void cpu_init(APPLE2 *m);

// Step the apple2 a single opcode
extern size_t (*machine_run_opcode)(APPLE2 *m);
size_t machine_run_opcode_6502(APPLE2 *m);
size_t machine_run_opcode_65c02(APPLE2 *m);
