// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "common.h"
#include "hardware_lib.h"
#include "6502_inln.h"

// SQW - UND_ opcodes are not implemented properly
size_t machine_run_opcode_6502(APPLE2 *m) {
    size_t start_cycle = m->cpu.cycles;
    if(a2_take_irq_if_pending(m)) {
        return m->cpu.cycles - start_cycle;
    }
    m->cpu.opcode_pc = m->cpu.pc;
    if(m->a2out_cb.cb_trace_ctx.cb_trace) {
        m->a2out_cb.cb_trace_ctx.cb_trace(m->a2out_cb.cb_trace_ctx.user);
    }
    uint8_t opcode = read_from_memory(m, m->cpu.pc);
    CYCLE(m);
    m->cpu.pc++;
    switch(opcode) {
        case BRK:       { al_read_pc(m); pc_hi_to_stack(m); pc_lo_to_stack(m); php(m); a2_brk(m); } break;                       // 00
        case ORA_X_ind: { mixa(m); ora_a16(m); } break;                          // 01
        case UND_02:    { jam(m); } break;                                       // 02 JAM/KIL (undocumented)
        case UND_03:    { mixa(m); sl_read_a16(m); sl_write_a16(m); slo_a16(m); } break; // 03 SLO (undocumented)
        case UND_04:    { al_read_pc(m); sl_read_a16(m); } break;                // 04 NOP zpg (undocumented)
        case ORA_zpg:   { al_read_pc(m); ora_a16(m); } break;                    // 05
        case ASL_zpg:   { mrw(m); asl_a16(m); } break;                           // 06
        case UND_07:    { mrw(m); slo_a16(m); } break;                           // 07 SLO (undocumented)
        case PHP:       { read_pc(m); php(m); } break;                           // 08
        case ORA_imm:   { ora_imm(m); } break;                                   // 09
        case ASL_A:     { asl_a(m); } break;                                     // 0A
        case UND_0B:    { anc_imm(m); } break;                                   // 0B ANC #imm (undocumented)
        case TSB_abs:   { a(m); sl_read_a16(m); } break;                         // 0C NOP abs (undocumented)
        case ORA_abs:   { a(m); ora_a16(m); } break;                             // 0D
        case ASL_abs:   { arw(m); asl_a16(m); } break;                           // 0E
        case UND_0F:    { arw(m); slo_a16(m); } break;                           // 0F SLO (undocumented)
        case BPL_rel:   { bpl(m); } break;                                       // 10
        case ORA_ind_Y: { miy(m); ora_a16(m); } break;                           // 11
        case UND_12:    { jam(m); } break;                                       // 12 JAM/KIL (undocumented)
        case UND_13:    { miyr(m); sl_read_a16(m); sl_write_a16(m); slo_a16(m); } break; // 13 SLO (undocumented)
        case UND_14:    { mix(m); sl_read_a16(m); } break;                       // 14 NOP zpg,X (undocumented)
        case ORA_zpg_X: { mix(m); ora_a16(m); } break;                           // 15
        case ASL_zpg_X: { mixrw(m); asl_a16(m); } break;                         // 16
        case UND_17:    { mixrw(m); slo_a16(m); } break;                         // 17 SLO (undocumented)
        case CLC:       { clc(m); } break;                                       // 18
        case ORA_abs_Y: { aiy(m); ora_a16(m); } break;                           // 19
        case INA:       { read_pc(m); } break;                                   // 1A NOP (undocumented)
        case UND_1B:    { aiyr(m); sl_read_a16(m); sl_write_a16(m); slo_a16(m); } break; // 1B SLO (undocumented)
        case UND_1C:    { aix(m); sl_read_a16(m); } break;                       // 1C NOP abs,X (undocumented)
        case ORA_abs_X: { aix(m); ora_a16(m); } break;                           // 1D
        case ASL_abs_X: { aixr_sel(m); asl_a16(m); } break;                      // 1E
        case UND_1F:    { aipxrw(m); slo_a16(m); } break;                        // 1F SLO (undocumented)
        case JSR_abs:   { al_read_pc(m); read_sp(m); pc_hi_to_stack(m); pc_lo_to_stack(m); jsr_a16(m); } break;           // 20
        case AND_X_ind: { mixa(m); and_a16(m); } break;                          // 21
        case UND_22:    { jam(m); } break;                                       // 22 JAM/KIL (undocumented)
        case UND_23:    { mixa(m); sl_read_a16(m); sl_write_a16(m); rla_a16(m); } break; // 23 RLA (undocumented)
        case BIT_zpg:   { al_read_pc(m); bit_a16(m); } break;                    // 24
        case AND_zpg:   { al_read_pc(m); and_a16(m); } break;                    // 25
        case ROL_zpg:   { mrw(m); rol_a16(m); } break;                           // 26
        case UND_27:    { mrw(m); rla_a16(m); } break;                           // 27 RLA (undocumented)
        case PLP:       { read_pc(m); read_sp(m); plp(m); } break;               // 28
        case AND_imm:   { and_imm(m); } break;                                   // 29
        case ROL_A:     { rol_a(m); } break;                                     // 2A
        case UND_2B:    { anc_imm(m); } break;                                   // 2B ANC #imm (undocumented)
        case BIT_abs:   { a(m); bit_a16(m); } break;                             // 2C
        case AND_abs:   { a(m); and_a16(m); } break;                             // 2D
        case ROL_abs:   { arw(m); rol_a16(m); } break;                           // 2E
        case UND_2F:    { arw(m); rla_a16(m); } break;                           // 2F RLA (undocumented)
        case BMI_rel:   { bmi(m); } break;                                       // 30
        case AND_ind_Y: { miy(m); and_a16(m); } break;                           // 31
        case UND_32:    { jam(m); } break;                                       // 32 JAM/KIL (undocumented)
        case UND_33:    { miyr(m); sl_read_a16(m); sl_write_a16(m); rla_a16(m); } break; // 33 RLA (undocumented)
        case UND_34:    { mix(m); sl_read_a16(m); } break;                       // 34 NOP zpg,X (undocumented)
        case AND_zpg_X: { mix(m); and_a16(m); } break;                           // 35
        case ROL_zpg_X: { mixrw(m); rol_a16(m); } break;                         // 36
        case UND_37:    { mixrw(m); rla_a16(m); } break;                         // 37 RLA (undocumented)
        case SEC:       { sec(m); } break;                                       // 38
        case AND_abs_Y: { aiy(m); and_a16(m); } break;                           // 39
        case UND_3A:    { read_pc(m); } break;                                   // 3A NOP (undocumented)
        case UND_3B:    { aiyr(m); sl_read_a16(m); sl_write_a16(m); rla_a16(m); } break; // 3B RLA (undocumented)
        case UND_3C:    { aix(m); sl_read_a16(m); } break;                       // 3C NOP abs,X (undocumented)
        case AND_abs_X: { aix(m); and_a16(m); } break;                           // 3D
        case ROL_abs_X: { aixr_sel(m); rol_a16(m); } break;                      // 3E
        case UND_3F:    { aipxrw(m); rla_a16(m); } break;                        // 3F RLA (undocumented)
        case RTI:       { read_pc(m); read_sp(m); p_from_stack(m); al_from_stack(m); rti(m); } break;                  // 40
        case EOR_X_ind: { mixa(m); eor_a16(m); } break;                          // 41
        case UND_42:    { jam(m); } break;                                       // 42 JAM/KIL (undocumented)
        case UND_43:    { mixa(m); sl_read_a16(m); sl_write_a16(m); sre_a16(m); } break; // 43 SRE (undocumented)
        case UND_44:    { al_read_pc(m); sl_read_a16(m); } break;                // 44 NOP zpg (undocumented)
        case EOR_zpg:   { al_read_pc(m); eor_a16(m); } break;                    // 45
        case LSR_zpg:   { mrw(m); lsr_a16(m); } break;                           // 46
        case UND_47:    { mrw(m); sre_a16(m); } break;                           // 47 SRE (undocumented)
        case PHA:       { read_pc(m); pha(m); } break;                           // 48
        case EOR_imm:   { eor_imm(m); } break;                                   // 49
        case LSR_A:     { lsr_a(m); } break;                                     // 4A
        case UND_4B:    { alr_imm(m); } break;                                   // 4B ALR #imm (undocumented)
        case JMP_abs:   { a(m); jmp_a16(m); } break;                             // 4C
        case EOR_abs:   { a(m); eor_a16(m); } break;                             // 4D
        case LSR_abs:   { arw(m); lsr_a16(m); } break;                           // 4E
        case UND_4F:    { arw(m); sre_a16(m); } break;                           // 4F SRE (undocumented)
        case BVC_rel:   { bvc(m); } break;                                       // 50
        case EOR_ind_Y: { miy(m); eor_a16(m); } break;                           // 51
        case UND_52:    { jam(m); } break;                                       // 52 JAM/KIL (undocumented)
        case UND_53:    { miyr(m); sl_read_a16(m); sl_write_a16(m); sre_a16(m); } break; // 53 SRE (undocumented)
        case UND_54:    { mix(m); sl_read_a16(m); } break;                       // 54 NOP zpg,X (undocumented)
        case EOR_zpg_X: { mix(m); eor_a16(m); } break;                           // 55
        case LSR_zpg_X: { mixrw(m); lsr_a16(m); } break;                         // 56
        case UND_57:    { mixrw(m); sre_a16(m); } break;                         // 57 SRE (undocumented)
        case CLI:       { cli(m); } break;                                       // 58
        case EOR_abs_Y: { aiy(m); eor_a16(m); } break;                           // 59
        case UND_5A:    { read_pc(m); } break;                                   // 5A NOP (undocumented)
        case UND_5B:    { aiyr(m); sl_read_a16(m); sl_write_a16(m); sre_a16(m); } break; // 5B SRE (undocumented)
        case UND_5C:    { aix(m); sl_read_a16(m); } break;                       // 5C NOP abs,X (undocumented)
        case EOR_abs_X: { aix(m); eor_a16(m); } break;                           // 5D
        case LSR_abs_X: { aixr_sel(m); lsr_a16(m); } break;                      // 5E
        case UND_5F:    { aipxrw(m); sre_a16(m); } break;                        // 5F SRE (undocumented)
        case RTS:       { read_pc(m); read_sp(m); al_from_stack(m); ah_from_stack(m); rts(m); } break;                 // 60
        case ADC_X_ind: { mixa(m); adc_a16(m); } break;                          // 61
        case UND_62:    { jam(m); } break;                                       // 62 JAM/KIL (undocumented)
        case UND_63:    { mixa(m); sl_read_a16(m); sl_write_a16(m); rra_a16(m); } break; // 63 RRA (undocumented)
        case UND_64:    { al_read_pc(m); sl_read_a16(m); } break;                // 64 NOP zpg (undocumented)
        case ADC_zpg:   { al_read_pc(m); adc_a16(m); } break;                    // 65
        case ROR_zpg:   { mrw(m); ror_a16(m); } break;                           // 66
        case UND_67:    { mrw(m); rra_a16(m); } break;                           // 67 RRA (undocumented)
        case PLA:       { read_pc(m); read_sp(m); pla(m); } break;               // 68
        case ADC_imm:   { adc_imm(m); } break;                                   // 69
        case ROR_A:     { ror_a(m); } break;                                     // 6A
        case UND_6B:    { arr_imm(m); } break;                                   // 6B ARR #imm (undocumented)
        case JMP_ind:   { ar(m); jmp_ind(m); } break;                            // 6C
        case ADC_abs:   { a(m); adc_a16(m); } break;                             // 6D
        case ROR_abs:   { arw(m); ror_a16(m); } break;                           // 6E
        case UND_6F:    { arw(m); rra_a16(m); } break;                           // 6F RRA (undocumented)
        case BVS_rel:   { bvs(m); } break;                                       // 70
        case ADC_ind_Y: { miy(m); adc_a16(m); } break;                           // 71
        case UND_72:    { jam(m); } break;                                       // 72 JAM/KIL (undocumented)
        case UND_73:    { miyr(m); sl_read_a16(m); sl_write_a16(m); rra_a16(m); } break; // 73 RRA (undocumented)
        case UND_74:    { mix(m); sl_read_a16(m); } break;                       // 74 NOP zpg,X (undocumented)
        case ADC_zpg_X: { mix(m); adc_a16(m); } break;                           // 75
        case ROR_zpg_X: { mixrw(m); ror_a16(m); } break;                         // 76
        case UND_77:    { mixrw(m); rra_a16(m); } break;                         // 77 RRA (undocumented)
        case SEI:       { sei(m); } break;                                       // 78
        case ADC_abs_Y: { aiy(m); adc_a16(m);} break;                            // 79
        case UND_7A:    { read_pc(m); } break;                                   // 7A NOP (undocumented)
        case UND_7B:    { aiyr(m); sl_read_a16(m); sl_write_a16(m); rra_a16(m); } break; // 7B RRA (undocumented)
        case UND_7C:    { aix(m); sl_read_a16(m); } break;                       // 7C NOP abs,X (undocumented)
        case ADC_abs_X: { aix(m); adc_a16(m); } break;                           // 7D
        case ROR_abs_X: { aixr_sel(m); ror_a16(m); } break;                      // 7E
        case UND_7F:    { aipxrw(m); rra_a16(m); } break;                        // 7F RRA (undocumented)
        case UND_80:    { al_read_pc(m); } break;                                // 80 NOP #imm (undocumented)
        case STA_X_ind: { mixa(m); sta_a16(m); } break;                          // 81
        case UND_82:    { al_read_pc(m); } break;                                // 82 NOP #imm (undocumented)
        case UND_83:    { mixa(m); sax_a16(m); } break;                          // 83 SAX (undocumented)
        case STY_zpg:   { al_read_pc(m); sty_a16(m); } break;                    // 84
        case STA_zpg:   { al_read_pc(m); sta_a16(m); } break;                    // 85
        case STX_zpg:   { al_read_pc(m); stx_a16(m); } break;                    // 86
        case UND_87:    { al_read_pc(m); sax_a16(m); } break;                    // 87 SAX (undocumented)
        case DEY:       { dey(m); } break;                                       // 88
        case UND_89:    { al_read_pc(m); } break;                                // 89 NOP #imm (undocumented)
        case TXA:       { txa(m); } break;                                       // 8A
        case UND_8B:    { xaa_imm(m); } break;                                   // 8B XAA/ANE #imm (undocumented)
        case STY_abs:   { a(m); sty_a16(m); } break;                             // 8C
        case STA_abs:   { a(m); sta_a16(m); } break;                             // 8D
        case STX_abs:   { a(m); stx_a16(m); } break;                             // 8E
        case UND_8F:    { a(m); sax_a16(m); } break;                             // 8F SAX (undocumented)
        case BCC_rel:   { bcc(m); } break;                                       // 90
        case STA_ind_Y: { miyr(m); sta_a16(m); } break;                          // 91
        case UND_92:    { jam(m); } break;                                       // 92 JAM/KIL (undocumented)
        case UND_93:    { miyr_und(m); ahx_a16(m); } break;                      // 93 AHX/SHA (undocumented)
        case STY_zpg_X: { mix(m); sty_a16(m); } break;                           // 94
        case STA_zpg_X: { mix(m); sta_a16(m); } break;                           // 95
        case STX_zpg_Y: { mizy(m); stx_a16(m); } break;                          // 96
        case UND_97:    { mizy(m); sax_a16(m); } break;                          // 97 SAX (undocumented)
        case TYA:       { tya(m); } break;                                       // 98
        case STA_abs_Y: { aiyr(m); sta_a16(m); } break;                          // 99
        case TXS:       { txs(m); } break;                                       // 9A
        case UND_9B:    { aiyr_und(m); shs_a16(m); } break;                      // 9B SHS/TAS (undocumented)
        case UND_9C:    { aipxr_und(m); shy_a16(m); } break;                     // 9C SHY (undocumented)
        case STA_abs_X: { aipxr(m); sta_a16(m); } break;                         // 9D
        case UND_9E:    { aiyr_und(m); shx_a16(m); } break;                      // 9E SHX (undocumented)
        case UND_9F:    { aiyr_und(m); ahx_a16(m); } break;                      // 9F AHX/SHA (undocumented)
        case LDY_imm:   { ldy_imm(m); } break;                                   // A0
        case LDA_X_ind: { mixa(m); lda_a16(m); } break;                          // A1
        case LDX_imm:   { ldx_imm(m); } break;                                   // A2
        case UND_A3:    { mixa(m); lax_a16(m); } break;                          // A3 LAX (undocumented)
        case LDY_zpg:   { al_read_pc(m); ldy_a16(m); } break;                    // A4
        case LDA_zpg:   { al_read_pc(m); lda_a16(m); } break;                    // A5
        case LDX_zpg:   { al_read_pc(m); ldx_a16(m); } break;                    // A6
        case UND_A7:    { al_read_pc(m); lax_a16(m); } break;                    // A7 LAX (undocumented)
        case TAY:       { tay(m); } break;                                       // A8
        case LDA_imm:   { lda_imm(m); } break;                                   // A9
        case TAX:       { tax(m); } break;                                       // AA
        case UND_AB:    { lax_imm_und(m); } break;                               // AB LAX #imm (undocumented)
        case LDY_abs:   { a(m); ldy_a16(m); } break;                             // AC
        case LDA_abs:   { a(m); lda_a16(m); } break;                             // AD
        case LDX_abs:   { a(m); ldx_a16(m); } break;                             // AE
        case UND_AF:    { a(m); lax_a16(m); } break;                             // AF LAX (undocumented)
        case BCS_rel:   { bcs(m); } break;                                       // B0
        case LDA_ind_Y: { miy(m); lda_a16(m); } break;                           // B1
        case UND_B2:    { jam(m); } break;                                       // B2 JAM/KIL (undocumented)
        case UND_B3:    { miy(m); lax_a16(m); } break;                           // B3 LAX (undocumented)
        case LDY_zpg_X: { mix(m); ldy_a16(m); } break;                           // B4
        case LDA_zpg_X: { mix(m); lda_a16(m); } break;                           // B5
        case LDX_zpg_Y: { mizy(m); ldx_a16(m); } break;                          // B6
        case UND_B7:    { mizy(m); lax_a16(m); } break;                          // B7 LAX (undocumented)
        case CLV:       { clv(m); } break;                                       // B8
        case LDA_abs_Y: { aiy(m); lda_a16(m); } break;                           // B9
        case TSX:       { tsx(m); } break;                                       // BA
        case UND_BB:    { aiy(m); las_a16(m); } break;                           // BB LAS/LAR (undocumented)
        case LDY_abs_X: { aix(m); ldy_a16(m); } break;                           // BC
        case LDA_abs_X: { aix(m); lda_a16(m); } break;                           // BD
        case LDX_abs_Y: { aiy(m); ldx_a16(m); } break;                           // BE
        case UND_BF:    { aiy(m); lax_a16(m); } break;                           // BF LAX (undocumented)
        case CPY_imm:   { cpy_imm(m); } break;                                   // C0
        case CMP_X_ind: { mixa(m); cmp_a16(m); } break;                          // C1
        case UND_C2:    { al_read_pc(m); } break;                                // C2 NOP #imm (undocumented)
        case UND_C3:    { mixa(m); sl_read_a16(m); sl_write_a16(m); dcp_a16(m); } break; // C3 DCP (undocumented)
        case CPY_zpg:   { al_read_pc(m); cpy_a16(m); } break;                    // C4
        case CMP_zpg:   { al_read_pc(m); cmp_a16(m); } break;                    // C5
        case DEC_zpg:   { mrw(m); dec_a16(m); } break;                           // C6
        case UND_C7:    { mrw(m); dcp_a16(m); } break;                           // C7 DCP (undocumented)
        case INY:       { iny(m); } break;                                       // C8
        case CMP_imm:   { cmp_imm(m); } break;                                   // C9
        case DEX:       { dex(m); } break;                                       // CA
        case UND_CB:    { axs_imm(m); } break;                                   // CB AXS/SBX #imm (undocumented)
        case CPY_abs:   { a(m); cpy_a16(m); } break;                             // CC
        case CMP_abs:   { a(m); cmp_a16(m); } break;                             // CD
        case DEC_abs:   { arw(m); dec_a16(m); } break;                           // CE
        case UND_CF:    { arw(m); dcp_a16(m); } break;                           // CF DCP (undocumented)
        case BNE_rel:   { bne(m); } break;                                       // D0
        case CMP_ind_Y: { miy(m); cmp_a16(m); } break;                           // D1
        case UND_D2:    { jam(m); } break;                                       // D2 JAM/KIL (undocumented)
        case UND_D3:    { miyr(m); sl_read_a16(m); sl_write_a16(m); dcp_a16(m); } break; // D3 DCP (undocumented)
        case UND_D4:    { mix(m); sl_read_a16(m); } break;                       // D4 NOP zpg,X (undocumented)
        case CMP_zpg_X: { mix(m); cmp_a16(m); } break;                           // D5
        case DEC_zpg_X: { mixrw(m); dec_a16(m); } break;                         // D6
        case UND_D7:    { mixrw(m); dcp_a16(m); } break;                         // D7 DCP (undocumented)
        case CLD:       { cld(m); } break;                                       // D8
        case CMP_abs_Y: { aiy(m); cmp_a16(m); } break;                           // D9
        case UND_DA:    { read_pc(m); } break;                                   // DA NOP (undocumented)
        case UND_DB:    { aiyr(m); sl_read_a16(m); sl_write_a16(m); dcp_a16(m); } break; // DB DCP (undocumented)
        case UND_DC:    { aix(m); sl_read_a16(m); } break;                       // DC NOP abs,X (undocumented)
        case CMP_abs_X: { aix(m); cmp_a16(m); } break;                           // DD
        case DEC_abs_X: { aipxrw(m); dec_a16(m); } break;                        // DE
        case UND_DF:    { aipxrw(m); dcp_a16(m); } break;                        // DF DCP (undocumented)
        case CPX_imm:   { cpx_imm(m); } break;                                   // E0
        case SBC_X_ind: { mixa(m); sbc_a16(m); } break;                          // E1
        case UND_E2:    { al_read_pc(m); } break;                                // E2 NOP #imm (undocumented)
        case UND_E3:    { mixa(m); sl_read_a16(m); sl_write_a16(m); isc_a16(m); } break; // E3 ISC/ISB (undocumented)
        case CPX_zpg:   { al_read_pc(m); cpx_a16(m); } break;                    // E4
        case SBC_zpg:   { al_read_pc(m); sbc_a16(m); } break;                    // E5
        case INC_zpg:   { mrw(m); inc_a16(m); } break;                           // E6
        case UND_E7:    { mrw(m); isc_a16(m); } break;                           // E7 ISC/ISB (undocumented)
        case INX:       { inx(m); } break;                                       // E8
        case SBC_imm:   { sbc_imm(m); } break;                                   // E9
        case NOP:       { read_pc(m); } break;                                   // EA
        case UND_EB:    { sbc_imm(m); } break;                                   // EB SBC #imm (undocumented)
        case CPX_abs:   { a(m); cpx_a16(m); } break;                             // EC
        case SBC_abs:   { a(m); sbc_a16(m); } break;                             // ED
        case INC_abs:   { arw(m); inc_a16(m); } break;                           // EE
        case UND_EF:    { arw(m); isc_a16(m); } break;                           // EF ISC/ISB (undocumented)
        case BEQ_rel:   { beq(m); } break;                                       // F0
        case SBC_ind_Y: { miy(m); sbc_a16(m); } break;                           // F1
        case UND_F2:    { jam(m); } break;                                       // F2 JAM/KIL (undocumented)
        case UND_F3:    { miyr(m); sl_read_a16(m); sl_write_a16(m); isc_a16(m); } break; // F3 ISC/ISB (undocumented)
        case UND_F4:    { mix(m); sl_read_a16(m); } break;                       // F4 NOP zpg,X (undocumented)
        case SBC_zpg_X: { mix(m); sbc_a16(m); } break;                           // F5
        case INC_zpg_X: { mixrw(m); inc_a16(m); } break;                         // F6
        case UND_F7:    { mixrw(m); isc_a16(m); } break;                         // F7 ISC/ISB (undocumented)
        case SED:       { sed(m); } break;                                       // F8
        case SBC_abs_Y: { aiy(m); sbc_a16(m); } break;                           // F9
        case UND_FA:    { read_pc(m); } break;                                   // FA NOP (undocumented)
        case UND_FB:    { aiyr(m); sl_read_a16(m); sl_write_a16(m); isc_a16(m); } break; // FB ISC/ISB (undocumented)
        case UND_FC:    { aix(m); sl_read_a16(m); } break;                       // FC NOP abs,X (undocumented)
        case SBC_abs_X: { aix(m); sbc_a16(m); } break;                           // FD
        case INC_abs_X: { aipxrw(m); inc_a16(m); } break;                        // FE
        case UND_FF:    { aipxrw(m); isc_a16(m); } break;                        // FF ISC/ISB (undocumented)
    }
    return m->cpu.cycles - start_cycle;
}
