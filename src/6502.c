// Apple ][+ emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#include "header.h"

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
    UND_80  , STA_X_ind, UND_82 , UND_83, STY_zpg  , STA_zpg  , STX_zpg  , UND_87, DEY     , UND_3D   , TXA     , UND_8B, STY_abs  , STA_abs  , STX_abs  , UND_8F, // 8
    BCC_rel , STA_ind_Y, UND_92 , UND_93, STY_zpg_X, STA_zpg_X, STX_zpg_Y, UND_97, TYA     , STA_abs_Y, TXS     , UND_9B, UND_9C   , STA_abs_X, UND_45   , UND_9F, // 9
    LDY_imm , LDA_X_ind, LDX_imm, UND_A3, LDY_zpg  , LDA_zpg  , LDX_zpg  , UND_A7, TAY     , LDA_imm  , TAX     , UND_AB, LDY_abs  , LDA_abs  , LDX_abs  , UND_AF, // A
    BCS_rel , LDA_ind_Y, UND_B2 , UND_B3, LDY_zpg_X, LDA_zpg_X, LDX_zpg_Y, UND_B7, CLV     , LDA_abs_Y, TSX     , UND_BB, LDY_abs_X, LDA_abs_X, LDX_abs_Y, UND_BF, // B
    CPY_imm , CMP_X_ind, UND_C2 , UND_C3, CPY_zpg  , CMP_zpg  , DEC_zpg  , UND_C7, INY     , CMP_imm  , DEX     , UND_CB, CPY_abs  , CMP_abs  , DEC_abs  , UND_CF, // C
    BNE_rel , CMP_ind_Y, UND_D2 , UND_D3, UND_D4   , CMP_zpg_X, DEC_zpg_X, UND_D7, CLD     , CMP_abs_Y, UND_DA  , UND_DB, UND_DC   , CMP_abs_X, DEC_abs_X, UND_DF, // D
    CPX_imm , SBC_X_ind, UND_E2 , UND_E3, CPX_zpg  , SBC_zpg  , INC_zpg  , UND_E7, INX     , SBC_imm  , NOP     , UND_EB, CPX_abs  , SBC_abs  , INC_abs  , UND_EF, // E
    BEQ_rel , SBC_ind_Y, UND_F2 , UND_F3, UND_F4   , SBC_zpg_X, INC_zpg_X, UND_F7, SED     , SBC_abs_Y, UND_FA  , UND_FB, UND_FC   , SBC_abs_X, INC_abs_X, UND_FF, // F
} OP_6502;

void machine_run_opcode_6502(APPLE2 *m) {
    uint8_t opcode = read_from_memory(m, m->cpu.pc);
    CYCLE(m);
    m->cpu.pc++;
    switch(opcode) {
        case BRK:       { al_read_pc(m); pc_hi_to_stack(m); pc_lo_to_stack(m); php(m); brk_6502(m); } break; // 00
        case ORA_X_ind: { mixa(m); ora_a16(m); } break; // 01
        case UND_02:    { read_pc(m); } break; // 02
        case UND_03:    { read_pc(m); } break; // 03
        case UND_04:    { read_pc(m); } break; // 04
        case ORA_zpg:   { al_read_pc(m); ora_a16(m); } break; // 05
        case ASL_zpg:   { mrw(m); asl_a16(m); } break; // 06
        case UND_07:    { read_pc(m); } break; // 07
        case PHP:       { read_pc(m); php(m); } break; // 08
        case ORA_imm:   { ora_imm(m); } break; // 09
        case ASL_A:     { asl_a(m); } break; // 0A
        case UND_0B:    { read_pc(m); } break; // 0B
        case UND_0C:    { read_pc(m); } break; // 0C
        case ORA_abs:   { a(m); ora_a16(m); } break; // 0D
        case ASL_abs:   { arw(m); asl_a16(m); } break; // 0E
        case UND_0F:    { read_pc(m); } break; // 0F
        case BPL_rel:   { bpl(m); } break; // 10
        case ORA_ind_Y: { miy(m); ora_a16(m); } break; // 11
        case UND_12:    { read_pc(m); } break; // 12
        case UND_13:    { read_pc(m); } break; // 13
        case UND_14:    { read_pc(m); } break; // 14
        case ORA_zpg_X: { mix(m); ora_a16(m); } break; // 15
        case ASL_zpg_X: { mixrw(m); asl_a16(m); } break; // 16
        case UND_17:    { read_pc(m); } break; // 17
        case CLC:       { clc(m); } break; // 18
        case ORA_abs_Y: { aiy(m); ora_a16(m); } break; // 19
        case UND_1A:    { read_pc(m); } break; // 1A
        case UND_1B:    { read_pc(m); } break; // 1B
        case UND_1C:    { read_pc(m); } break; // 1C
        case ORA_abs_X: { aix(m); ora_a16(m); } break; // 1D
        case ASL_abs_X: { aipxrw(m); asl_a16(m); } break; // 1E
        case UND_1F:    { read_pc(m); } break; // 1F
        case JSR_abs:   { al_read_pc(m); read_sp(m); pc_hi_to_stack(m); pc_lo_to_stack(m); jsr_a16(m); } break; // 20
        case AND_X_ind: { mixa(m); and_a16(m); } break; // 21
        case UND_22:    { read_pc(m); } break; // 22
        case UND_23:    { read_pc(m); } break; // 23
        case BIT_zpg:   { al_read_pc(m); bit_a16(m); } break; // 24
        case AND_zpg:   { al_read_pc(m); and_a16(m); } break; // 25
        case ROL_zpg:   { mrw(m); rol_a16(m); } break; // 26
        case UND_27:    { read_pc(m); } break; // 27
        case PLP:       { read_pc(m); read_sp(m); plp(m); } break; // 28
        case AND_imm:   { and_imm(m); } break; // 29
        case ROL_A:     { rol_a(m); } break; // 2A
        case UND_2B:    { read_pc(m); } break; // 2B
        case BIT_abs:   { a(m); bit_a16(m); } break; // 2C
        case AND_abs:   { a(m); and_a16(m); } break; // 2D
        case ROL_abs:   { arw(m); rol_a16(m); } break; // 2E
        case UND_2F:    { read_pc(m); } break; // 2F
        case BMI_rel:   { bmi(m); } break; // 30
        case AND_ind_Y: { miy(m); and_a16(m); } break; // 31
        case UND_32:    { read_pc(m); } break; // 32
        case UND_33:    { read_pc(m); } break; // 33
        case UND_34:    { read_pc(m); } break; // 34
        case AND_zpg_X: { mix(m); and_a16(m); } break; // 35
        case ROL_zpg_X: { mixrw(m); rol_a16(m); } break; // 36
        case UND_37:    { read_pc(m); } break; // 37
        case SEC:       { sec(m); } break; // 38
        case AND_abs_Y: { aiy(m); and_a16(m); } break; // 39
        case UND_3A:    { read_pc(m); } break; // 3A
        case UND_3B:    { read_pc(m); } break; // 3B
        case UND_3C:    { read_pc(m); } break; // 3C
        case AND_abs_X: { aix(m); and_a16(m); } break; // 3D
        case ROL_abs_X: { aipxrw(m); rol_a16(m); } break; // 3E
        case UND_3F:    { read_pc(m); } break; // 3F
        case RTI:       { read_pc(m); read_sp(m); p_from_stack(m); al_from_stack(m); rti(m); } break; // 40
        case EOR_X_ind: { mixa(m); eor_a16(m); } break; // 41
        case UND_42:    { read_pc(m); } break; // 42
        case UND_43:    { read_pc(m); } break; // 43
        case UND_44:    { read_pc(m); } break; // 44
        case EOR_zpg:   { al_read_pc(m); eor_a16(m); } break; // 45
        case LSR_zpg:   { mrw(m); lsr_a16(m); } break; // 46
        case UND_47:    { read_pc(m); } break; // 47
        case PHA:       { read_pc(m); pha(m); } break; // 48
        case EOR_imm:   { eor_imm(m); } break; // 49
        case LSR_A:     { lsr_a(m); } break; // 4A
        case UND_4B:    { read_pc(m); } break; // 4B
        case JMP_abs:   { a(m); jmp_a16(m); } break; // 4C
        case EOR_abs:   { a(m); eor_a16(m); } break; // 4D
        case LSR_abs:   { arw(m); lsr_a16(m); } break; // 4E
        case UND_4F:    { read_pc(m); } break; // 4F
        case BVC_rel:   { bvc(m); } break; // 50
        case EOR_ind_Y: { miy(m); eor_a16(m); } break; // 51
        case UND_52:    { read_pc(m); } break; // 52
        case UND_53:    { read_pc(m); } break; // 53
        case UND_54:    { read_pc(m); } break; // 54
        case EOR_zpg_X: { mix(m); eor_a16(m); } break; // 55
        case LSR_zpg_X: { mixrw(m); lsr_a16(m); } break; // 56
        case UND_57:    { read_pc(m); } break; // 57
        case CLI:       { cli(m); } break; // 58
        case EOR_abs_Y: { aiy(m); eor_a16(m); } break; // 59
        case UND_5A:    { read_pc(m); } break; // 5A
        case UND_5B:    { read_pc(m); } break; // 5B
        case UND_5C:    { read_pc(m); } break; // 5C
        case EOR_abs_X: { aix(m); eor_a16(m); } break; // 5D
        case LSR_abs_X: { aipxrw(m); lsr_a16(m); } break; // 5E
        case UND_5F:    { read_pc(m); } break; // 5F
        case RTS:       { read_pc(m); read_sp(m); al_from_stack(m); ah_from_stack(m); rts(m); } break; // 60
        case ADC_X_ind: { mixa(m); adc_a16(m); } break; // 61
        case UND_62:    { read_pc(m); } break; // 62
        case UND_63:    { read_pc(m); } break; // 63
        case UND_64:    { read_pc(m); } break; // 64
        case ADC_zpg:   { al_read_pc(m); adc_a16(m); } break; // 65
        case ROR_zpg:   { mrw(m); ror_a16(m); } break; // 66
        case UND_67:    { read_pc(m); } break; // 67
        case PLA:       { read_pc(m); read_sp(m); pla(m); } break; // 68
        case ADC_imm:   { adc_imm(m); } break; // 69
        case ROR_A:     { ror_a(m); } break; // 6A
        case UND_6B:    { read_pc(m); } break; // 6B
        case JMP_ind:   { ar(m); jmp_ind(m); } break; // 6C
        case ADC_abs:   { a(m); adc_a16(m); } break; // 6D
        case ROR_abs:   { arw(m); ror_a16(m); } break; // 6E
        case UND_6F:    { read_pc(m); } break; // 6F
        case BVS_rel:   { bvs(m); } break; // 70
        case ADC_ind_Y: { miy(m); adc_a16(m); } break; // 71
        case UND_72:    { read_pc(m); } break; // 72
        case UND_73:    { read_pc(m); } break; // 73
        case UND_74:    { read_pc(m); } break; // 74
        case ADC_zpg_X: { mix(m); adc_a16(m); } break; // 75
        case ROR_zpg_X: { mixrw(m); ror_a16(m); } break; // 76
        case UND_77:    { read_pc(m); } break; // 77
        case SEI:       { sei(m); } break; // 78
        case ADC_abs_Y: { aiy(m); adc_a16(m);} break; // 79
        case UND_7A:    { read_pc(m); } break; // 7A
        case UND_7B:    { read_pc(m); } break; // 7B
        case UND_7C:    { read_pc(m); } break; // 7C
        case ADC_abs_X: { aix(m); adc_a16(m); } break; // 7D
        case ROR_abs_X: { aipxrw(m); ror_a16(m); } break; // 7E
        case UND_7F:    { read_pc(m); } break; // 7F
        case UND_80:    { read_pc(m); } break; // 80
        case STA_X_ind: { mixa(m); sta_a16(m); } break; // 81
        case UND_82:    { read_pc(m); } break; // 82
        case UND_83:    { read_pc(m); } break; // 83
        case STY_zpg:   { al_read_pc(m); sty_a16(m); } break; // 84
        case STA_zpg:   { al_read_pc(m); sta_a16(m); } break; // 85
        case STX_zpg:   { al_read_pc(m); stx_a16(m); } break; // 86
        case UND_87:    { read_pc(m); } break; // 87
        case DEY:       { dey(m); } break; // 88
        case UND_3D:    { read_pc(m); } break; // 89
        case TXA:       { txa(m); } break; // 8A
        case UND_8B:    { read_pc(m); } break; // 8B
        case STY_abs:   { a(m); sty_a16(m); } break; // 8C
        case STA_abs:   { a(m); sta_a16(m); } break; // 8D
        case STX_abs:   { a(m); stx_a16(m); } break; // 8E
        case UND_8F:    { read_pc(m); } break; // 8F
        case BCC_rel:   { bcc(m); } break; // 90
        case STA_ind_Y: { miyr(m); sta_a16(m); } break; // 91
        case UND_92:    { read_pc(m); } break; // 92
        case UND_93:    { read_pc(m); } break; // 93
        case STY_zpg_X: { mix(m); sty_a16(m); } break; // 94
        case STA_zpg_X: { mix(m); sta_a16(m); } break; // 95
        case STX_zpg_Y: { mizy(m); stx_a16(m); } break; // 96
        case UND_97:    { read_pc(m); } break; // 97
        case TYA:       { tya(m); } break; // 98
        case STA_abs_Y: { aiyr(m); sta_a16(m); } break; // 99
        case TXS:       { txs(m); } break; // 9A
        case UND_9B:    { read_pc(m); } break; // 9B
        case UND_9C:    { read_pc(m); } break; // 9C
        case STA_abs_X: { aipxr(m); sta_a16(m); } break; // 9D
        case UND_45:    { read_pc(m); } break; // 9E
        case UND_9F:    { read_pc(m); } break; // 9F
        case LDY_imm:   { ldy_imm(m); } break; // A0
        case LDA_X_ind: { mixa(m); lda_a16(m); } break; // A1
        case LDX_imm:   { ldx_imm(m); } break; // A2
        case UND_A3:    { read_pc(m); } break; // A3
        case LDY_zpg:   { al_read_pc(m); ldy_a16(m); } break; // A4
        case LDA_zpg:   { al_read_pc(m); lda_a16(m); } break; // A5
        case LDX_zpg:   { al_read_pc(m); ldx_a16(m); } break; // A6
        case UND_A7:    { read_pc(m); } break; // A7
        case TAY:       { tay(m); } break; // A8
        case LDA_imm:   { lda_imm(m); } break; // A9
        case TAX:       { tax(m); } break; // AA
        case UND_AB:    { read_pc(m); } break; // AB
        case LDY_abs:   { a(m); ldy_a16(m); } break; // AC
        case LDA_abs:   { a(m); lda_a16(m); } break; // AD
        case LDX_abs:   { a(m); ldx_a16(m); } break; // AE
        case UND_AF:    { read_pc(m); } break; // AF
        case BCS_rel:   { bcs(m); } break; // B0
        case LDA_ind_Y: { miy(m); lda_a16(m); } break; // B1
        case UND_B2:    { read_pc(m); } break; // B2
        case UND_B3:    { read_pc(m); } break; // B3
        case LDY_zpg_X: { mix(m); ldy_a16(m); } break; // B4
        case LDA_zpg_X: { mix(m); lda_a16(m); } break; // B5
        case LDX_zpg_Y: { mizy(m); ldx_a16(m); } break; // B6
        case UND_B7:    { read_pc(m); } break; // B7
        case CLV:       { clv(m); } break; // B8
        case LDA_abs_Y: { aiy(m); lda_a16(m); } break; // B9
        case TSX:       { tsx(m); } break; // BA
        case UND_BB:    { read_pc(m); } break; // BB
        case LDY_abs_X: { aix(m); ldy_a16(m); } break; // BC
        case LDA_abs_X: { aix(m); lda_a16(m); } break; // BD
        case LDX_abs_Y: { aiy(m); ldx_a16(m); } break; // BE
        case UND_BF:    { read_pc(m); } break; // BF
        case CPY_imm:   { cpy_imm(m); } break; // C0
        case CMP_X_ind: { mixa(m); cmp_a16(m); } break; // C1
        case UND_C2:    { read_pc(m); } break; // C2
        case UND_C3:    { read_pc(m); } break; // C3
        case CPY_zpg:   { al_read_pc(m); cpy_a16(m); } break; // C4
        case CMP_zpg:   { al_read_pc(m); cmp_a16(m); } break; // C5
        case DEC_zpg:   { mrw(m); dec_a16(m); } break; // C6
        case UND_C7:    { read_pc(m); } break; // C7
        case INY:       { iny(m); } break; // C8
        case CMP_imm:   { cmp_imm(m); } break; // C9
        case DEX:       { dex(m); } break; // CA
        case UND_CB:    { read_pc(m); } break; // CB
        case CPY_abs:   { a(m); cpy_a16(m); } break; // CC
        case CMP_abs:   { a(m); cmp_a16(m); } break; // CD
        case DEC_abs:   { arw(m); dec_a16(m); } break; // CE
        case UND_CF:    { read_pc(m); } break; // CF
        case BNE_rel:   { bne(m); } break; // D0
        case CMP_ind_Y: { miy(m); cmp_a16(m); } break; // D1
        case UND_D2:    { read_pc(m); } break; // D2
        case UND_D3:    { read_pc(m); } break; // D3
        case UND_D4:    { read_pc(m); } break; // D4
        case CMP_zpg_X: { mix(m); cmp_a16(m); } break; // D5
        case DEC_zpg_X: { mixrw(m); dec_a16(m); } break; // D6
        case UND_D7:    { read_pc(m); } break; // D7
        case CLD:       { cld(m); } break; // D8
        case CMP_abs_Y: { aiy(m); cmp_a16(m); } break; // D9
        case UND_DA:    { read_pc(m); } break; // DA
        case UND_DB:    { read_pc(m); } break; // DB
        case UND_DC:    { read_pc(m); } break; // DC
        case CMP_abs_X: { aix(m); cmp_a16(m); } break; // DD
        case DEC_abs_X: { aipxrw(m); dec_a16(m); } break; // DE
        case UND_DF:    { read_pc(m); } break; // DF
        case CPX_imm:   { cpx_imm(m); } break; // E0
        case SBC_X_ind: { mixa(m); sbc_a16(m); } break; // E1
        case UND_E2:    { read_pc(m); } break; // E2
        case UND_E3:    { read_pc(m); } break; // E3
        case CPX_zpg:   { al_read_pc(m); cpx_a16(m); } break; // E4
        case SBC_zpg:   { al_read_pc(m); sbc_a16(m); } break; // E5
        case INC_zpg:   { mrw(m); inc_a16(m); } break; // E6
        case UND_E7:    { read_pc(m); } break; // E7
        case INX:       { inx(m); } break; // E8
        case SBC_imm:   { sbc_imm(m); } break; // E9
        case NOP:       { read_pc(m); } break; // EA
        case UND_EB:    { read_pc(m); } break; // EB
        case CPX_abs:   { a(m); cpx_a16(m); } break; // EC
        case SBC_abs:   { a(m); sbc_a16(m); } break; // ED
        case INC_abs:   { arw(m); inc_a16(m); } break; // EE
        case UND_EF:    { read_pc(m); } break; // EF
        case BEQ_rel:   { beq(m); } break; // F0
        case SBC_ind_Y: { miy(m); sbc_a16(m); } break; // F1
        case UND_F2:    { read_pc(m); } break; // F2
        case UND_F3:    { read_pc(m); } break; // F3
        case UND_F4:    { read_pc(m); } break; // F4
        case SBC_zpg_X: { mix(m); sbc_a16(m); } break; // F5
        case INC_zpg_X: { mixrw(m); inc_a16(m); } break; // F6
        case UND_F7:    { read_pc(m); } break; // F7
        case SED:       { sed(m); } break; // F8
        case SBC_abs_Y: { aiy(m); sbc_a16(m); } break; // F9
        case UND_FA:    { read_pc(m); } break; // FA
        case UND_FB:    { read_pc(m); } break; // FB
        case UND_FC:    { read_pc(m); } break; // FC
        case SBC_abs_X: { aix(m); sbc_a16(m); } break; // FD
        case INC_abs_X: { aipxrw(m); inc_a16(m); } break; // FE
        case UND_FF:    { read_pc(m); } break; // FF
   }
}

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

void machine_run_opcode_65c02(APPLE2 *m) {
    uint8_t opcode = read_from_memory(m, m->cpu.pc);
    CYCLE(m);
    m->cpu.pc++;
    switch(opcode) {
        case BRK:       { al_read_pc(m); pc_hi_to_stack(m); pc_lo_to_stack(m); php(m); brk_pc(m), brk_65c02(m); } break; // 00
        case ORA_X_ind: { mixa(m); ora_a16(m); } break; // 01
        case UND_02:    { al_read_pc(m); } break; // 02
        case UND_03:    { ; } break; // 03
        case TSB_zpg:   { mrr(m); tsb(m); } break; // 04 SQW
        case ORA_zpg:   { al_read_pc(m); ora_a16(m); } break; // 05
        case ASL_zpg:   { mrr(m); asl_a16(m); } break; // 06
        case UND_07:    { mix(m); } break; // 07
        case PHP:       { read_pc(m); php(m); } break; // 08
        case ORA_imm:   { ora_imm(m); } break; // 09
        case ASL_A:     { asl_a(m); } break; // 0A
        case UND_0B:    { ; } break; // 0B
        case TSB_abs:   { a(m); sl_read_a16(m); sl_read_a16(m); tsb(m); } break; // 0C
        case ORA_abs:   { a(m); ora_a16(m); } break; // 0D
        case ASL_abs:   { arr(m); asl_a16(m); } break; // 0E
        case UND_0F:    { a(m); } break; // 0F
        case BPL_rel:   { bpl(m); } break; // 10
        case ORA_ind_Y: { miy(m); ora_a16(m); } break; // 11
        case ORA_ind_zp:{ miz(m); ora_a16(m); } break; // 12
        case UND_13:    { ; } break; // 13
        case TRB_zpg:   { mrr(m); trb(m); } break; // 14
        case ORA_zpg_X: { mix(m); ora_a16(m); } break; // 15
        case ASL_zpg_X: { mixrr(m); asl_a16(m); } break; // 16
        case UND_17:    { mix(m); sl_read_a16(m); } break; // 17
        case CLC:       { clc(m); } break; // 18
        case ORA_abs_Y: { aiy(m); ora_a16(m); } break; // 19
        case INA:       { ina(m); } break; // 1A
        case UND_1B:    { ; } break; // 1B
        case TRB_abs:   { arr(m); trb(m); } break; // 1C
        case ORA_abs_X: { aix(m); ora_a16(m); } break; // 1D
        case ASL_abs_X: { aixrr(m); asl_a16(m); } break; // 1E
        case UND_1F:    { a(m); nop_pc(m, -1); } break; // 1F
        case JSR_abs:   { al_read_pc(m); read_sp(m); pc_hi_to_stack(m); pc_lo_to_stack(m); jsr_a16(m); } break; // 20
        case AND_X_ind: { mixa(m); and_a16(m); } break; // 21
        case UND_22:    { al_read_pc(m); } break; // 22
        case UND_23:    { ; } break; // 23
        case BIT_zpg:   { al_read_pc(m); bit_a16(m); } break; // 24
        case AND_zpg:   { al_read_pc(m); and_a16(m); } break; // 25
        case ROL_zpg:   { mrr(m); rol_a16(m); } break; // 26
        case UND_27:    { mix(m); } break; // 27
        case PLP:       { read_pc(m); read_sp(m); plp(m); } break; // 28
        case AND_imm:   { and_imm(m); } break; // 29
        case ROL_A:     { rol_a(m); } break; // 2A
        case UND_2B:    { ; } break; // 2B
        case BIT_abs:   { a(m); bit_a16(m); } break; // 2C
        case AND_abs:   { a(m); and_a16(m); } break; // 2D
        case ROL_abs:   { arr(m); rol_a16(m); } break; // 2E
        case UND_2F:    { a(m); } break; // 2F
        case BMI_rel:   { bmi(m); } break; // 30
        case AND_ind_Y: { miy(m); and_a16(m); } break; // 31
        case AND_ind_zp:{ miz(m); and_a16(m); } break; // 32
        case UND_33:    { ; } break; // 33
        case BIT_zpg_x: { mix(m); bit_a16(m); } break; // 34
        case AND_zpg_X: { mix(m); and_a16(m); } break; // 35
        case ROL_zpg_X: { mixrr(m); rol_a16(m); } break; // 36
        case UND_37:    { mix(m); sl_read_a16(m); } break; // 37
        case SEC:       { sec(m); } break; // 38
        case AND_abs_Y: { aiy(m); and_a16(m); } break; // 39
        case DEA:       { dea(m); } break; // 3A
        case UND_3B:    { ; } break; // 3B
        case BIT_abs_x: { aix(m); bit_a16(m); } break; // 3C
        case AND_abs_X: { aix(m); and_a16(m); } break; // 3D
        case ROL_abs_X: { aixrr(m); rol_a16(m); } break; // 3E
        case UND_3F:    { a(m); nop_pc(m, -1); } break; // 3F
        case RTI:       { read_pc(m); read_sp(m); p_from_stack(m); al_from_stack(m); rti(m); } break; // 40
        case EOR_X_ind: { mixa(m); eor_a16(m); } break; // 41
        case UND_42:    { al_read_pc(m); } break; // 42
        case UND_43:    { ; } break; // 43
        case UND_44:    { mix(m); } break; // 44
        case EOR_zpg:   { al_read_pc(m); eor_a16(m); } break; // 45
        case LSR_zpg:   { mrr(m); lsr_a16(m); } break; // 46
        case UND_47:    { mix(m); } break; // 47
        case PHA:       { read_pc(m); pha(m); } break; // 48
        case EOR_imm:   { eor_imm(m); } break; // 49
        case LSR_A:     { lsr_a(m); } break; // 4A
        case UND_4B:    { ; } break; // 4B
        case JMP_abs:   { a(m); jmp_a16(m); } break; // 4C
        case EOR_abs:   { a(m); eor_a16(m); } break; // 4D
        case LSR_abs:   { arr(m); lsr_a16(m); } break; // 4E
        case UND_4F:    { a(m); } break; // 4F
        case BVC_rel:   { bvc(m); } break; // 50
        case EOR_ind_Y: { miy(m); eor_a16(m); } break; // 51
        case EOR_ind_zp:{ miz(m); eor_a16(m); } break; // 52
        case UND_53:    { ; } break; // 53
        case UND_54:    { mix(m); sl_read_a16(m); } break; // 54
        case EOR_zpg_X: { mix(m); eor_a16(m); } break; // 55
        case LSR_zpg_X: { mixrr(m); lsr_a16(m); } break; // 56
        case UND_57:    { mix(m); sl_read_a16(m); } break; // 57
        case CLI:       { cli(m); } break; // 58
        case EOR_abs_Y: { aiy(m); eor_a16(m); } break; // 59
        case PHY:       { read_pc(m); phy(m); } break; // 5A
        case UND_5B:    { ; } break; // 5B
        case UND_5C:    { a(m); nop_pc(m, -1); } break; // 5C
        case EOR_abs_X: { aix(m); eor_a16(m); } break; // 5D
        case LSR_abs_X: { aixrr(m); lsr_a16(m); } break; // 5E
        case UND_5F:    { a(m); nop_pc(m, -1); } break; // 5F
        case RTS:       { read_pc(m); read_sp(m); al_from_stack(m); ah_from_stack(m); rts(m); } break; // 60
        case ADC_X_ind: { mixa(m); adc_a16(m); } break; // 61
        case UND_62:    { al_read_pc(m); } break; // 62
        case UND_63:    { ; } break; // 63
        case STZ_zpg:   { al_read_pc(m); stz_a16(m, 0); } break; // 64
        case ADC_zpg:   { al_read_pc(m); adc_a16(m); } break; // 65
        case ROR_zpg:   { mrr(m); ror_a16(m); } break; // 66
        case UND_67:    { mix(m); } break; // 67
        case PLA:       { read_pc(m); read_sp(m); pla(m); } break; // 68
        case ADC_imm:   { adc_imm(m); } break; // 69
        case ROR_A:     { ror_a(m); } break; // 6A
        case UND_6B:    { ; } break; // 6B
        case JMP_ind:   { ar(m); jmp_ind(m); } break; // 6C
        case ADC_abs:   { a(m); adc_a16(m); } break; // 6D
        case ROR_abs:   { arr(m); ror_a16(m); } break; // 6E
        case UND_6F:    { a(m); } break; // 6F
        case BVS_rel:   { bvs(m); } break; // 70
        case ADC_ind_Y: { miy(m); adc_a16(m); } break; // 71
        case ADC_ind_zp:{ miz(m); adc_a16(m); } break; // 72
        case UND_73:    { ; } break; // 73
        case STZ_zpg_x: { mix(m); stz_a16(m, 0); } break; // 74
        case ADC_zpg_X: { mix(m); adc_a16(m); } break; // 75
        case ROR_zpg_X: { mixrr(m); ror_a16(m); } break; // 76
        case UND_77:    { mix(m); sl_read_a16(m); } break; // 77
        case SEI:       { sei(m); } break; // 78
        case ADC_abs_Y: { aiy(m); adc_a16(m);} break; // 79
        case PLY:       { read_pc(m); read_sp(m); ply(m); } break; // 7A
        case UND_7B:    { ; } break; // 7B
        case JMP_ind_x: { jmp_ind_x(m); } break; // 7C
        case ADC_abs_X: { aix(m); adc_a16(m); } break; // 7D
        case ROR_abs_X: { aixrr(m); ror_a16(m); } break; // 7E
        case UND_7F:    { a(m); nop_pc(m, -1); } break; // 7F
        case BRA:       { bra(m); } break; // 80
        case STA_X_ind: { mixa(m); sta_a16(m); } break; // 81
        case UND_82:    { al_read_pc(m); } break; // 82
        case UND_83:    { ; } break; // 83
        case STY_zpg:   { al_read_pc(m); sty_a16(m); } break; // 84
        case STA_zpg:   { al_read_pc(m); sta_a16(m); } break; // 85
        case STX_zpg:   { al_read_pc(m); stx_a16(m); } break; // 86
        case UND_87:    { mix(m); } break; // 87
        case DEY:       { dey(m); } break; // 88
        case BIT_imm:   { bit_imm(m); } break; // 89
        case TXA:       { txa(m); } break; // 8A
        case UND_8B:    { ; } break; // 8B
        case STY_abs:   { a(m); sty_a16(m); } break; // 8C
        case STA_abs:   { a(m); sta_a16(m); } break; // 8D
        case STX_abs:   { a(m); stx_a16(m); } break; // 8E
        case UND_8F:    { a(m); } break; // 8F
        case BCC_rel:   { bcc(m); } break; // 90
        case STA_ind_Y: { miyr(m); sta_a16(m); } break; // 91
        case STA_ind_zp:{ miz(m); sta_a16(m); } break; // 92
        case UND_93:    { ; } break; // 93
        case STY_zpg_X: { mix(m); sty_a16(m); } break; // 94
        case STA_zpg_X: { mix(m); sta_a16(m); } break; // 95
        case STX_zpg_Y: { mizy(m); stx_a16(m); } break; // 96
        case UND_97:    { mix(m); sl_read_a16(m); } break; // 97
        case TYA:       { tya(m); } break; // 98
        case STA_abs_Y: { aiyr(m); sta_a16(m); } break; // 99
        case TXS:       { txs(m); } break; // 9A
        case UND_9B:    { ; } break; // 9B
        case STZ_abs:   { a(m); stz_a16(m, 0); } break; // 9C
        case STA_abs_X: { aipxr(m); sta_a16(m); } break; // 9D
        case STZ_abs_x: { aipxr(m); stz_a16(m, 0); } break; // 9E
        case UND_9F:    { a(m); nop_pc(m, -1); } break; // 9F
        case LDY_imm:   { ldy_imm(m); } break; // A0
        case LDA_X_ind: { mixa(m); lda_a16(m); } break; // A1
        case LDX_imm:   { ldx_imm(m); } break; // A2
        case UND_A3:    { ; } break; // A3
        case LDY_zpg:   { al_read_pc(m); ldy_a16(m); } break; // A4
        case LDA_zpg:   { al_read_pc(m); lda_a16(m); } break; // A5
        case LDX_zpg:   { al_read_pc(m); ldx_a16(m); } break; // A6
        case UND_A7:    { mix(m); } break; // A7
        case TAY:       { tay(m); } break; // A8
        case LDA_imm:   { lda_imm(m); } break; // A9
        case TAX:       { tax(m); } break; // AA
        case UND_AB:    { ; } break; // AB
        case LDY_abs:   { a(m); ldy_a16(m); } break; // AC
        case LDA_abs:   { a(m); lda_a16(m); } break; // AD
        case LDX_abs:   { a(m); ldx_a16(m); } break; // AE
        case UND_AF:    { a(m); } break; // AF
        case BCS_rel:   { bcs(m); } break; // B0
        case LDA_ind_Y: { miy(m); lda_a16(m); } break; // B1
        case LDA_ind_zp:{ miz(m); lda_a16(m); } break; // B2
        case UND_B3:    { ; } break; // B3
        case LDY_zpg_X: { mix(m); ldy_a16(m); } break; // B4
        case LDA_zpg_X: { mix(m); lda_a16(m); } break; // B5
        case LDX_zpg_Y: { mizy(m); ldx_a16(m); } break; // B6
        case UND_B7:    { mix(m); sl_read_a16(m); } break; // B7
        case CLV:       { clv(m); } break; // B8
        case LDA_abs_Y: { aiy(m); lda_a16(m); } break; // B9
        case TSX:       { tsx(m); } break; // BA
        case UND_BB:    { ; } break; // BB
        case LDY_abs_X: { aix(m); ldy_a16(m); } break; // BC
        case LDA_abs_X: { aix(m); lda_a16(m); } break; // BD
        case LDX_abs_Y: { aiy(m); ldx_a16(m); } break; // BE
        case UND_BF:    { a(m); nop_pc(m, -1); } break; // BF
        case CPY_imm:   { cpy_imm(m); } break; // C0
        case CMP_X_ind: { mixa(m); cmp_a16(m); } break; // C1
        case UND_C2:    { al_read_pc(m); } break; // C2
        case UND_C3:    { ; } break; // C3
        case CPY_zpg:   { al_read_pc(m); cpy_a16(m); } break; // C4
        case CMP_zpg:   { al_read_pc(m); cmp_a16(m); } break; // C5
        case DEC_zpg:   { mrr(m); dec_a16(m); } break; // C6
        case UND_C7:    { mix(m); } break; // C7
        case INY:       { iny(m); } break; // C8
        case CMP_imm:   { cmp_imm(m); } break; // C9
        case DEX:       { dex(m); } break; // CA
        case UND_CB:    { read_pc(m); } break; // CB
        case CPY_abs:   { a(m); cpy_a16(m); } break; // CC
        case CMP_abs:   { a(m); cmp_a16(m); } break; // CD
        case DEC_abs:   { arr(m); dec_a16(m); } break; // CE
        case UND_CF:    { a(m); } break; // CF
        case BNE_rel:   { bne(m); } break; // D0
        case CMP_ind_Y: { miy(m); cmp_a16(m); } break; // D1
        case CMP_ind_zp:{ miz(m); cmp_a16(m); } break; // D2
        case UND_D3:    { ; } break; // D3
        case UND_D4:    { mix(m); sl_read_a16(m); } break; // D4
        case CMP_zpg_X: { mix(m); cmp_a16(m); } break; // D5
        case DEC_zpg_X: { mixrr(m); dec_a16(m); } break; // D6
        case UND_D7:    { mix(m); sl_read_a16(m); } break; // D7
        case CLD:       { cld(m); } break; // D8
        case CMP_abs_Y: { aiy(m); cmp_a16(m); } break; // D9
        case PHX:       { read_pc(m); phx(m); } break; // DA
        case UND_DB:    { mix(m); sl_read_a16(m); } break; // DB
        case UND_DC:    { a(m); nop_pc(m, -1); } break; // DC
        case CMP_abs_X: { aix(m); cmp_a16(m); } break; // DD
        case DEC_abs_X: { aipxrr(m); dec_a16(m); } break; // DE
        case UND_DF:    { a(m); nop_pc(m, -1); } break; // DF
        case CPX_imm:   { cpx_imm(m); } break; // E0
        case SBC_X_ind: { mixa(m); sbc_a16(m); } break; // E1
        case UND_E2:    { al_read_pc(m); } break; // E2
        case UND_E3:    { ; } break; // E3
        case CPX_zpg:   { al_read_pc(m); cpx_a16(m); } break; // E4
        case SBC_zpg:   { al_read_pc(m); sbc_a16(m); } break; // E5
        case INC_zpg:   { mrr(m); inc_a16(m); } break; // E6
        case UND_E7:    { mix(m); } break; // E7
        case INX:       { inx(m); } break; // E8
        case SBC_imm:   { sbc_imm(m); } break; // E9
        case NOP:       { read_pc(m); } break; // EA
        case UND_EB:    { ; } break; // EB
        case CPX_abs:   { a(m); cpx_a16(m); } break; // EC
        case SBC_abs:   { a(m); sbc_a16(m); } break; // ED
        case INC_abs:   { arr(m); inc_a16(m); } break; // EE
        case UND_EF:    { a(m); } break; // EF
        case BEQ_rel:   { beq(m); } break; // F0
        case SBC_ind_Y: { miy(m); sbc_a16(m); } break; // F1
        case SBC_ind_zp:{ miz(m); sbc_a16(m); } break; // F2
        case UND_F3:    { ; } break; // F3
        case UND_F4:    { mix(m); sl_read_a16(m); } break; // F4
        case SBC_zpg_X: { mix(m); sbc_a16(m); } break; // F5
        case INC_zpg_X: { mixrr(m); inc_a16(m); } break; // F6
        case UND_F7:    { mix(m); sl_read_a16(m); } break; // F7
        case SED:       { sed(m); } break; // F8
        case SBC_abs_Y: { aiy(m); sbc_a16(m); } break; // F9
        case PLX:       { read_pc(m); read_sp(m); plx(m); } break; // FA
        case UND_FB:    { ; } break; // FB
        case UND_FC:    { a(m); nop_pc(m, -1); } break; // FC
        case SBC_abs_X: { aix(m); sbc_a16(m); } break; // FD
        case INC_abs_X: { aipxrr(m); inc_a16(m); } break; // FE
        case UND_FF:    { a(m); nop_pc(m, -1); } break; // FF
   }
}