#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    CPU_6502,
    CPU_65c02,
};

typedef struct CPU {
    uint16_t pc;
    uint16_t opcode_pc;
    uint16_t sp;
    uint8_t A, X, Y;
    union {
        struct {
            uint8_t C: 1;
            uint8_t Z: 1;
            uint8_t I: 1;
            uint8_t D: 1;
            uint8_t B: 1;
            uint8_t E: 1;
            uint8_t V: 1;
            uint8_t N: 1;
        };
        uint8_t flags;
    };
    union {
        struct {
            uint8_t address_lo;
            uint8_t address_hi;
        };
        uint16_t address_16;
    };
    union {
        struct {
            uint8_t scratch_lo;
            uint8_t scratch_hi;
        };
        uint16_t scratch_16;
    };
    struct {
        uint8_t page_fault: 1;
    };
    uint8_t irq_defer;
    uint8_t irq_defer_i;
    uint8_t opcode_active;
    uint32_t class;
    uint64_t cycles;
    uint64_t irq_entries;
    uint64_t nmi_entries;
} CPU;

typedef uint8_t (*cpu65_read_fn)(void *user, uint16_t address);
typedef void (*cpu65_write_fn)(void *user, uint16_t address, uint8_t value);
typedef uint8_t (*cpu65_irq_pending_fn)(void *user);
typedef uint8_t (*cpu65_nmi_pending_fn)(void *user);

/* Semantic role of the access currently being issued through the memory
   callbacks. This is trace metadata only: it does not change CPU execution or
   bus ownership. */
typedef enum cpu65_bus_access_kind {
    CPU65_BUS_ACCESS_DATA_READ = 0,
    CPU65_BUS_ACCESS_DATA_WRITE,
    CPU65_BUS_ACCESS_OPCODE_FETCH,
    CPU65_BUS_ACCESS_OPERAND_READ,
    CPU65_BUS_ACCESS_DUMMY_READ,
    CPU65_BUS_ACCESS_RMW_DUMMY_WRITE,
    CPU65_BUS_ACCESS_STACK_READ,
    CPU65_BUS_ACCESS_STACK_WRITE,
    CPU65_BUS_ACCESS_VECTOR_READ
} cpu65_bus_access_kind;

typedef enum cpu65_interrupt_kind {
    CPU65_INTERRUPT_NONE = 0,
    CPU65_INTERRUPT_NMI,
    CPU65_INTERRUPT_IRQ
} cpu65_interrupt_kind;

typedef struct cpu65_t {
    CPU cpu;
    void *user;
    cpu65_read_fn read;
    cpu65_write_fn write;
    cpu65_irq_pending_fn irq_pending;
    cpu65_nmi_pending_fn nmi_pending;
    cpu65_bus_access_kind bus_access_kind;
    uint8_t micro_active;
    uint8_t micro_opcode;
    uint8_t micro_phase;
    uint8_t micro_branch_taken;
    uint16_t micro_target;
    uint16_t micro_interrupt_vector;
    uint8_t micro_is_interrupt;
} cpu65_t;

typedef enum {
//  0         1          2        3       4          5          6          7       8         9          A         B       C          D          E          F
    BRK     , ORA_X_ind, UND_02 , UND_03, UND_04   , ORA_zpg  , ASL_zpg  , UND_07, PHP     , ORA_imm  , ASL_A   , UND_0B, UND_0C   , ORA_abs  , ASL_abs  , UND_0F,
    BPL_rel , ORA_ind_Y, UND_12 , UND_13, UND_14   , ORA_zpg_X, ASL_zpg_X, UND_17, CLC     , ORA_abs_Y, UND_1A  , UND_1B, UND_1C   , ORA_abs_X, ASL_abs_X, UND_1F,
    JSR_abs , AND_X_ind, UND_22 , UND_23, BIT_zpg  , AND_zpg  , ROL_zpg  , UND_27, PLP     , AND_imm  , ROL_A   , UND_2B, BIT_abs  , AND_abs  , ROL_abs  , UND_2F,
    BMI_rel , AND_ind_Y, UND_32 , UND_33, UND_34   , AND_zpg_X, ROL_zpg_X, UND_37, SEC     , AND_abs_Y, UND_3A  , UND_3B, UND_3C   , AND_abs_X, ROL_abs_X, UND_3F,
    RTI     , EOR_X_ind, UND_42 , UND_43, UND_44   , EOR_zpg  , LSR_zpg  , UND_47, PHA     , EOR_imm  , LSR_A   , UND_4B, JMP_abs  , EOR_abs  , LSR_abs  , UND_4F,
    BVC_rel , EOR_ind_Y, UND_52 , UND_53, UND_54   , EOR_zpg_X, LSR_zpg_X, UND_57, CLI     , EOR_abs_Y, UND_5A  , UND_5B, UND_5C   , EOR_abs_X, LSR_abs_X, UND_5F,
    RTS     , ADC_X_ind, UND_62 , UND_63, UND_64   , ADC_zpg  , ROR_zpg  , UND_67, PLA     , ADC_imm  , ROR_A   , UND_6B, JMP_ind  , ADC_abs  , ROR_abs  , UND_6F,
    BVS_rel , ADC_ind_Y, UND_72 , UND_73, UND_74   , ADC_zpg_X, ROR_zpg_X, UND_77, SEI     , ADC_abs_Y, UND_7A  , UND_7B, UND_7C   , ADC_abs_X, ROR_abs_X, UND_7F,
    UND_80  , STA_X_ind, UND_82 , UND_83, STY_zpg  , STA_zpg  , STX_zpg  , UND_87, DEY     , UND_89   , TXA     , UND_8B, STY_abs  , STA_abs  , STX_abs  , UND_8F,
    BCC_rel , STA_ind_Y, UND_92 , UND_93, STY_zpg_X, STA_zpg_X, STX_zpg_Y, UND_97, TYA     , STA_abs_Y, TXS     , UND_9B, UND_9C   , STA_abs_X, UND_9E   , UND_9F,
    LDY_imm , LDA_X_ind, LDX_imm, UND_A3, LDY_zpg  , LDA_zpg  , LDX_zpg  , UND_A7, TAY     , LDA_imm  , TAX     , UND_AB, LDY_abs  , LDA_abs  , LDX_abs  , UND_AF,
    BCS_rel , LDA_ind_Y, UND_B2 , UND_B3, LDY_zpg_X, LDA_zpg_X, LDX_zpg_Y, UND_B7, CLV     , LDA_abs_Y, TSX     , UND_BB, LDY_abs_X, LDA_abs_X, LDX_abs_Y, UND_BF,
    CPY_imm , CMP_X_ind, UND_C2 , UND_C3, CPY_zpg  , CMP_zpg  , DEC_zpg  , UND_C7, INY     , CMP_imm  , DEX     , UND_CB, CPY_abs  , CMP_abs  , DEC_abs  , UND_CF,
    BNE_rel , CMP_ind_Y, UND_D2 , UND_D3, UND_D4   , CMP_zpg_X, DEC_zpg_X, UND_D7, CLD     , CMP_abs_Y, UND_DA  , UND_DB, UND_DC   , CMP_abs_X, DEC_abs_X, UND_DF,
    CPX_imm , SBC_X_ind, UND_E2 , UND_E3, CPX_zpg  , SBC_zpg  , INC_zpg  , UND_E7, INX     , SBC_imm  , NOP     , UND_EB, CPX_abs  , SBC_abs  , INC_abs  , UND_EF,
    BEQ_rel , SBC_ind_Y, UND_F2 , UND_F3, UND_F4   , SBC_zpg_X, INC_zpg_X, UND_F7, SED     , SBC_abs_Y, UND_FA  , UND_FB, UND_FC   , SBC_abs_X, INC_abs_X, UND_FF,
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

void cpu65_init(cpu65_t *m, void *user, cpu65_read_fn read, cpu65_write_fn write);
void cpu65_set_irq_pending_callback(cpu65_t *m, cpu65_irq_pending_fn irq_pending);
void cpu65_set_nmi_pending_callback(cpu65_t *m, cpu65_nmi_pending_fn nmi_pending);

void cpu65_reset(cpu65_t *m);
void cpu65_set_overflow(cpu65_t *m);
size_t cpu65_step(cpu65_t *m);

/* Resumable Phi2 path for documented NMOS 6502/6510 opcodes plus practical
   undocumented families. Unstable undocs and JAM fall back to cpu65_step(). */
bool cpu65_micro_can_begin(const cpu65_t *m, uint8_t opcode);
void cpu65_micro_begin(cpu65_t *m);
void cpu65_micro_begin_interrupt(cpu65_t *m, cpu65_interrupt_kind kind);
cpu65_interrupt_kind cpu65_micro_poll_interrupt(cpu65_t *m);
cpu65_bus_access_kind cpu65_micro_access_kind(const cpu65_t *m);
bool cpu65_micro_step(cpu65_t *m);
size_t cpu65_micro_cycles_remaining(const cpu65_t *m);
