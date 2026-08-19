/* 6502 CPU core, adapted from c64m c6510 (itself from a2m). */

#include "cpu65.h"
#include "cpu65_inln.h"

#include <assert.h>
#include <string.h>

void cpu65_init(cpu65_t *m, void *user, cpu65_read_fn read, cpu65_write_fn write) {
    assert(m);
    assert(read);
    assert(write);

    memset(m, 0, sizeof(*m));
    m->user = user;
    m->read = read;
    m->write = write;
    m->cpu.class = CPU_6502;
    m->cpu.sp = 0x100;
}

void cpu65_set_irq_pending_callback(cpu65_t *m, cpu65_irq_pending_fn irq_pending) {
    assert(m);
    m->irq_pending = irq_pending;
}

void cpu65_set_nmi_pending_callback(cpu65_t *m, cpu65_nmi_pending_fn nmi_pending) {
    assert(m);
    m->nmi_pending = nmi_pending;
}

void cpu65_reset(cpu65_t *m) {
    assert(m);
    assert(m->read);

    m->cpu.pc = (uint16_t)(m->read(m->user, 0xfffc) | ((uint16_t)m->read(m->user, 0xfffd) << 8));
    m->cpu.sp = 0x100;
    m->cpu.I = 1;
    m->cpu.D = 0;
    m->cpu.B = 0;
    m->cpu.E = 1;
    m->cpu.irq_defer = 0;
    m->cpu.irq_defer_i = 0;
    m->cpu.opcode_active = 0;
    m->cpu.irq_entries = 0;
    m->cpu.nmi_entries = 0;
    m->micro_active = 0;
    m->micro_opcode = 0;
    m->micro_phase = 0;
    m->micro_branch_taken = 0;
    m->micro_target = 0;
    m->micro_interrupt_vector = 0;
    m->micro_is_interrupt = 0;
}

void cpu65_set_overflow(cpu65_t *m) {
    assert(m);
    m->cpu.V = 1;
}

size_t cpu65_step(cpu65_t *m) {
    size_t start_cycle = m->cpu.cycles;
    m->cpu.opcode_active = 0;
    if(cpu65_take_nmi_if_pending(m)) {
        return m->cpu.cycles - start_cycle;
    }
    if(cpu65_take_irq_if_pending(m)) {
        return m->cpu.cycles - start_cycle;
    }
    m->cpu.opcode_pc = m->cpu.pc;
    m->cpu.opcode_active = 1;
    uint8_t opcode = read_opcode(m, m->cpu.pc);
    CYCLE(m);
    m->cpu.pc++;
    switch(opcode) {
        case BRK:       { al_read_pc(m); pc_hi_to_stack(m); pc_lo_to_stack(m); php(m); cpu65_brk(m); } break;                       // 00
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

/* Stable NMOS unofficial opcodes used by C64 software.  Deliberately exclude
   the data-bus/chip-dependent forms (XAA, AHX, SHX, SHY, TAS, LAS, LAX #imm)
   and JAM: their compatibility executor remains the explicit fallback. */
/* Opcode class for micro-step dispatch (O(1) instead of dual switch probes). */
enum {
    CPU65_OP_NONE = 0,
    CPU65_OP_DOC = 1,
    CPU65_OP_UND_PRACTICAL = 2,
    CPU65_OP_UND_NOP = 3
};

static uint8_t cpu65_micro_op_class[256];
static int cpu65_micro_op_class_ready;

static bool cpu65_micro_is_practical_undocumented(uint8_t opcode) {
    switch (opcode) {
    /* SLO, RLA, SRE, RRA, DCP, ISC: (zp,X), zp, abs, (zp),Y, zp,X, abs,Y, abs,X. */
    case UND_03: case UND_07: case UND_0F: case UND_13: case UND_17: case UND_1B: case UND_1F:
    case UND_23: case UND_27: case UND_2F: case UND_33: case UND_37: case UND_3B: case UND_3F:
    case UND_43: case UND_47: case UND_4F: case UND_53: case UND_57: case UND_5B: case UND_5F:
    case UND_63: case UND_67: case UND_6F: case UND_73: case UND_77: case UND_7B: case UND_7F:
    case UND_C3: case UND_C7: case UND_CF: case UND_D3: case UND_D7: case UND_DB: case UND_DF:
    case UND_E3: case UND_E7: case UND_EF: case UND_F3: case UND_F7: case UND_FB: case UND_FF:
    /* LAX and SAX, excluding unstable LAX #imm. */
    case UND_A3: case UND_A7: case UND_AF: case UND_B3: case UND_B7: case UND_BF:
    case UND_83: case UND_87: case UND_8F: case UND_97:
        return true;
    default:
        return false;
    }
}

/* Stable undocumented NOPs are common in compact C64 loaders.  They still
   perform their addressing reads, so executing them as one bulk instruction
   loses VIC/IEC-visible cycles even though they do not change registers. */
static bool cpu65_micro_is_undocumented_nop(uint8_t opcode) {
    return opcode == UND_44 || opcode == UND_80 ||
           opcode == UND_1C || opcode == UND_FC;
}

static void cpu65_micro_init_op_class(void) {
    unsigned op;
    if (cpu65_micro_op_class_ready) {
        return;
    }
    memset(cpu65_micro_op_class, 0, sizeof(cpu65_micro_op_class));
    for (op = 0; op < 256u; op++) {
        if (cpu65_micro_is_practical_undocumented((uint8_t)op)) {
            cpu65_micro_op_class[op] = (uint8_t)CPU65_OP_UND_PRACTICAL;
        } else if (cpu65_micro_is_undocumented_nop((uint8_t)op)) {
            cpu65_micro_op_class[op] = (uint8_t)CPU65_OP_UND_NOP;
        }
    }
    /* Documented set mirrors the previous can_begin switch (without undocs). */
#define CPU65_MARK_DOC(op) cpu65_micro_op_class[(op)] = (uint8_t)CPU65_OP_DOC
    CPU65_MARK_DOC(NOP);
    CPU65_MARK_DOC(LDA_imm); CPU65_MARK_DOC(LDX_imm); CPU65_MARK_DOC(LDY_imm);
    CPU65_MARK_DOC(LDA_abs); CPU65_MARK_DOC(LDX_abs); CPU65_MARK_DOC(LDY_abs);
    CPU65_MARK_DOC(STA_abs); CPU65_MARK_DOC(STX_abs); CPU65_MARK_DOC(STY_abs);
    CPU65_MARK_DOC(JMP_abs); CPU65_MARK_DOC(JMP_ind);
    CPU65_MARK_DOC(BPL_rel); CPU65_MARK_DOC(BMI_rel); CPU65_MARK_DOC(BVC_rel);
    CPU65_MARK_DOC(BVS_rel); CPU65_MARK_DOC(BCC_rel); CPU65_MARK_DOC(BCS_rel);
    CPU65_MARK_DOC(BNE_rel); CPU65_MARK_DOC(BEQ_rel);
    CPU65_MARK_DOC(JSR_abs); CPU65_MARK_DOC(RTS);
    CPU65_MARK_DOC(ORA_imm); CPU65_MARK_DOC(AND_imm); CPU65_MARK_DOC(EOR_imm);
    CPU65_MARK_DOC(ADC_imm); CPU65_MARK_DOC(SBC_imm); CPU65_MARK_DOC(CMP_imm);
    CPU65_MARK_DOC(CPX_imm); CPU65_MARK_DOC(CPY_imm);
    CPU65_MARK_DOC(CLC); CPU65_MARK_DOC(SEC); CPU65_MARK_DOC(CLI); CPU65_MARK_DOC(SEI);
    CPU65_MARK_DOC(CLD); CPU65_MARK_DOC(SED); CPU65_MARK_DOC(CLV);
    CPU65_MARK_DOC(INX); CPU65_MARK_DOC(INY); CPU65_MARK_DOC(DEX); CPU65_MARK_DOC(DEY);
    CPU65_MARK_DOC(TAX); CPU65_MARK_DOC(TAY); CPU65_MARK_DOC(TXA); CPU65_MARK_DOC(TYA);
    CPU65_MARK_DOC(TSX); CPU65_MARK_DOC(TXS);
    CPU65_MARK_DOC(ASL_A); CPU65_MARK_DOC(ROL_A); CPU65_MARK_DOC(LSR_A); CPU65_MARK_DOC(ROR_A);
    CPU65_MARK_DOC(PHA); CPU65_MARK_DOC(PHP); CPU65_MARK_DOC(PLA); CPU65_MARK_DOC(PLP);
    CPU65_MARK_DOC(RTI); CPU65_MARK_DOC(BRK);
    CPU65_MARK_DOC(LDA_zpg); CPU65_MARK_DOC(LDX_zpg); CPU65_MARK_DOC(LDY_zpg);
    CPU65_MARK_DOC(STA_zpg); CPU65_MARK_DOC(STX_zpg); CPU65_MARK_DOC(STY_zpg);
    CPU65_MARK_DOC(ORA_zpg); CPU65_MARK_DOC(AND_zpg); CPU65_MARK_DOC(EOR_zpg);
    CPU65_MARK_DOC(ADC_zpg); CPU65_MARK_DOC(SBC_zpg); CPU65_MARK_DOC(CMP_zpg);
    CPU65_MARK_DOC(CPX_zpg); CPU65_MARK_DOC(CPY_zpg);
    CPU65_MARK_DOC(BIT_zpg); CPU65_MARK_DOC(BIT_abs);
    CPU65_MARK_DOC(ORA_abs); CPU65_MARK_DOC(AND_abs); CPU65_MARK_DOC(EOR_abs);
    CPU65_MARK_DOC(ADC_abs); CPU65_MARK_DOC(SBC_abs); CPU65_MARK_DOC(CMP_abs);
    CPU65_MARK_DOC(CPX_abs); CPU65_MARK_DOC(CPY_abs);
    CPU65_MARK_DOC(ORA_zpg_X); CPU65_MARK_DOC(AND_zpg_X); CPU65_MARK_DOC(EOR_zpg_X);
    CPU65_MARK_DOC(ADC_zpg_X); CPU65_MARK_DOC(SBC_zpg_X); CPU65_MARK_DOC(CMP_zpg_X);
    CPU65_MARK_DOC(ORA_abs_X); CPU65_MARK_DOC(ORA_abs_Y); CPU65_MARK_DOC(ORA_X_ind);
    CPU65_MARK_DOC(ORA_ind_Y);
    CPU65_MARK_DOC(AND_abs_X); CPU65_MARK_DOC(AND_abs_Y); CPU65_MARK_DOC(AND_X_ind);
    CPU65_MARK_DOC(AND_ind_Y);
    CPU65_MARK_DOC(EOR_abs_X); CPU65_MARK_DOC(EOR_abs_Y); CPU65_MARK_DOC(EOR_X_ind);
    CPU65_MARK_DOC(EOR_ind_Y);
    CPU65_MARK_DOC(ADC_abs_X); CPU65_MARK_DOC(ADC_abs_Y); CPU65_MARK_DOC(ADC_X_ind);
    CPU65_MARK_DOC(ADC_ind_Y);
    CPU65_MARK_DOC(SBC_abs_X); CPU65_MARK_DOC(SBC_abs_Y); CPU65_MARK_DOC(SBC_X_ind);
    CPU65_MARK_DOC(SBC_ind_Y);
    CPU65_MARK_DOC(CMP_abs_X); CPU65_MARK_DOC(CMP_abs_Y); CPU65_MARK_DOC(CMP_X_ind);
    CPU65_MARK_DOC(CMP_ind_Y);
    CPU65_MARK_DOC(LDA_X_ind); CPU65_MARK_DOC(STA_X_ind);
    CPU65_MARK_DOC(LDA_ind_Y); CPU65_MARK_DOC(STA_ind_Y);
    CPU65_MARK_DOC(LDA_zpg_X); CPU65_MARK_DOC(LDX_zpg_Y); CPU65_MARK_DOC(LDY_zpg_X);
    CPU65_MARK_DOC(STA_zpg_X); CPU65_MARK_DOC(STX_zpg_Y); CPU65_MARK_DOC(STY_zpg_X);
    CPU65_MARK_DOC(LDA_abs_X); CPU65_MARK_DOC(LDA_abs_Y); CPU65_MARK_DOC(LDX_abs_Y);
    CPU65_MARK_DOC(LDY_abs_X); CPU65_MARK_DOC(STA_abs_X); CPU65_MARK_DOC(STA_abs_Y);
    CPU65_MARK_DOC(ASL_zpg); CPU65_MARK_DOC(ROL_zpg); CPU65_MARK_DOC(LSR_zpg);
    CPU65_MARK_DOC(ROR_zpg); CPU65_MARK_DOC(DEC_zpg); CPU65_MARK_DOC(INC_zpg);
    CPU65_MARK_DOC(ASL_abs); CPU65_MARK_DOC(ROL_abs); CPU65_MARK_DOC(LSR_abs);
    CPU65_MARK_DOC(ROR_abs); CPU65_MARK_DOC(DEC_abs); CPU65_MARK_DOC(INC_abs);
    CPU65_MARK_DOC(ASL_zpg_X); CPU65_MARK_DOC(ROL_zpg_X); CPU65_MARK_DOC(LSR_zpg_X);
    CPU65_MARK_DOC(ROR_zpg_X); CPU65_MARK_DOC(DEC_zpg_X); CPU65_MARK_DOC(INC_zpg_X);
    CPU65_MARK_DOC(ASL_abs_X); CPU65_MARK_DOC(ROL_abs_X); CPU65_MARK_DOC(LSR_abs_X);
    CPU65_MARK_DOC(ROR_abs_X); CPU65_MARK_DOC(DEC_abs_X); CPU65_MARK_DOC(INC_abs_X);
#undef CPU65_MARK_DOC
    cpu65_micro_op_class_ready = 1;
}

static bool cpu65_micro_is_undocumented_rmw(uint8_t opcode) {
    switch (opcode) {
    case UND_03: case UND_07: case UND_0F: case UND_13: case UND_17: case UND_1B: case UND_1F:
    case UND_23: case UND_27: case UND_2F: case UND_33: case UND_37: case UND_3B: case UND_3F:
    case UND_43: case UND_47: case UND_4F: case UND_53: case UND_57: case UND_5B: case UND_5F:
    case UND_63: case UND_67: case UND_6F: case UND_73: case UND_77: case UND_7B: case UND_7F:
    case UND_C3: case UND_C7: case UND_CF: case UND_D3: case UND_D7: case UND_DB: case UND_DF:
    case UND_E3: case UND_E7: case UND_EF: case UND_F3: case UND_F7: case UND_FB: case UND_FF:
        return true;
    default:
        return false;
    }
}

static bool cpu65_micro_is_lax(uint8_t opcode) {
    return opcode == UND_A3 || opcode == UND_A7 || opcode == UND_AF ||
           opcode == UND_B3 || opcode == UND_B7 || opcode == UND_BF;
}

/* SAX zp,Y (0x97) and LAX zp,Y (0xB7) are Y-indexed even though their low
   nibble (0x17) matches the X-indexed zero-page column of the RMW families.
   LAX abs,Y (0xBF) is handled separately via the ABS_Y mode mapping. */
static bool cpu65_micro_undoc_uses_y_index(uint8_t opcode) {
    return opcode == UND_97 || opcode == UND_B7;
}

typedef enum cpu65_undocumented_mode {
    CPU65_UND_MODE_X_IND,
    CPU65_UND_MODE_ZP,
    CPU65_UND_MODE_ABS,
    CPU65_UND_MODE_IND_Y,
    CPU65_UND_MODE_ZP_INDEXED,
    CPU65_UND_MODE_ABS_Y,
    CPU65_UND_MODE_ABS_X
} cpu65_undocumented_mode;

static cpu65_undocumented_mode cpu65_micro_undocumented_mode(uint8_t opcode) {
    /* LAX abs,Y (0xBF) reuses the 0x1f column but is Y-indexed, unlike the
       RMW abs,X families that share that column.  Route it to ABS_Y so it
       uses the Y register and the correct 4/5-cycle timing. */
    if (opcode == UND_BF) {
        return CPU65_UND_MODE_ABS_Y;
    }
    switch (opcode & 0x1fu) {
    case 0x03u: return CPU65_UND_MODE_X_IND;
    case 0x07u: return CPU65_UND_MODE_ZP;
    case 0x0fu: return CPU65_UND_MODE_ABS;
    case 0x13u: return CPU65_UND_MODE_IND_Y;
    case 0x17u: return CPU65_UND_MODE_ZP_INDEXED;
    case 0x1bu: return CPU65_UND_MODE_ABS_Y;
    default:    return CPU65_UND_MODE_ABS_X;
    }
}

bool cpu65_micro_can_begin(const cpu65_t *m, uint8_t opcode) {
    assert(m);
    (void)m;
    cpu65_micro_init_op_class();
    return cpu65_micro_op_class[opcode] != (uint8_t)CPU65_OP_NONE;
}

void cpu65_micro_begin(cpu65_t *m) {
    assert(m);
    assert(!m->micro_active);

    m->cpu.opcode_pc = m->cpu.pc;
    m->cpu.opcode_active = 1;
    m->micro_active = 1;
    m->micro_opcode = 0;
    m->micro_phase = 0;
    m->micro_branch_taken = 0;
    m->micro_target = 0;
    m->micro_interrupt_vector = 0;
    m->micro_is_interrupt = 0;
}

void cpu65_micro_begin_interrupt(cpu65_t *m, cpu65_interrupt_kind kind) {
    assert(m);
    assert(!m->micro_active);
    assert(kind == CPU65_INTERRUPT_NMI || kind == CPU65_INTERRUPT_IRQ);

    m->cpu.opcode_pc = m->cpu.pc;
    m->cpu.opcode_active = 0;
    m->micro_active = 1;
    m->micro_opcode = 0;
    m->micro_phase = 0;
    m->micro_branch_taken = 0;
    m->micro_target = 0;
    m->micro_interrupt_vector = kind == CPU65_INTERRUPT_NMI ? 0xfffau : 0xfffeu;
    m->micro_is_interrupt = 1;
}

cpu65_interrupt_kind cpu65_micro_poll_interrupt(cpu65_t *m) {
    uint8_t pending;
    uint8_t irq_disable;

    assert(m);

    if (cpu65_nmi_pending(m)) {
        return CPU65_INTERRUPT_NMI;
    }
    pending = cpu65_irq_pending(m);
    irq_disable = m->cpu.irq_defer ? m->cpu.irq_defer_i : m->cpu.I;
    if (!pending || irq_disable) {
        if (m->cpu.irq_defer) {
            m->cpu.irq_defer = 0;
        }
        return CPU65_INTERRUPT_NONE;
    }
    if (m->cpu.irq_defer) {
        m->cpu.irq_defer = 0;
    }
    return CPU65_INTERRUPT_IRQ;
}

cpu65_bus_access_kind cpu65_micro_access_kind(const cpu65_t *m) {
    assert(m);
    assert(m->micro_active);

    if (m->micro_phase == 0) {
        if (m->micro_is_interrupt) {
            return CPU65_BUS_ACCESS_DUMMY_READ;
        }
        return CPU65_BUS_ACCESS_OPCODE_FETCH;
    }

    if (m->micro_is_interrupt) {
        if (m->micro_phase == 1) return CPU65_BUS_ACCESS_DUMMY_READ;
        if (m->micro_phase < 5) return CPU65_BUS_ACCESS_STACK_WRITE;
        return CPU65_BUS_ACCESS_VECTOR_READ;
    }

    cpu65_micro_init_op_class();
    if (cpu65_micro_op_class[m->micro_opcode] ==
        (uint8_t)CPU65_OP_UND_PRACTICAL) {
        cpu65_undocumented_mode mode =
            cpu65_micro_undocumented_mode(m->micro_opcode);
        bool rmw = cpu65_micro_is_undocumented_rmw(m->micro_opcode);
        bool lax = cpu65_micro_is_lax(m->micro_opcode);

        if (m->micro_phase == 1) return CPU65_BUS_ACCESS_OPERAND_READ;
        switch (mode) {
        case CPU65_UND_MODE_X_IND:
            if (m->micro_phase == 2) return CPU65_BUS_ACCESS_DUMMY_READ;
            if (m->micro_phase < 5) return CPU65_BUS_ACCESS_DATA_READ;
            if (!rmw) return lax ? CPU65_BUS_ACCESS_DATA_READ : CPU65_BUS_ACCESS_DATA_WRITE;
            return m->micro_phase == 5 ? CPU65_BUS_ACCESS_DATA_READ :
                (m->micro_phase == 6 ? CPU65_BUS_ACCESS_RMW_DUMMY_WRITE : CPU65_BUS_ACCESS_DATA_WRITE);
        case CPU65_UND_MODE_ZP:
            if (m->micro_phase == 2) return lax ? CPU65_BUS_ACCESS_DATA_READ :
                (rmw ? CPU65_BUS_ACCESS_DATA_READ : CPU65_BUS_ACCESS_DATA_WRITE);
            return m->micro_phase == 3 ? CPU65_BUS_ACCESS_RMW_DUMMY_WRITE : CPU65_BUS_ACCESS_DATA_WRITE;
        case CPU65_UND_MODE_ABS:
            if (m->micro_phase < 3) return CPU65_BUS_ACCESS_OPERAND_READ;
            if (m->micro_phase == 3) return lax ? CPU65_BUS_ACCESS_DATA_READ :
                (rmw ? CPU65_BUS_ACCESS_DATA_READ : CPU65_BUS_ACCESS_DATA_WRITE);
            return m->micro_phase == 4 ? CPU65_BUS_ACCESS_RMW_DUMMY_WRITE : CPU65_BUS_ACCESS_DATA_WRITE;
        case CPU65_UND_MODE_IND_Y:
            if (m->micro_phase < 4) return CPU65_BUS_ACCESS_DATA_READ;
            if (m->micro_phase == 4) {
                return lax && !m->micro_branch_taken ? CPU65_BUS_ACCESS_DATA_READ :
                    CPU65_BUS_ACCESS_DUMMY_READ;
            }
            if (!rmw) return lax ? CPU65_BUS_ACCESS_DATA_READ : CPU65_BUS_ACCESS_DATA_WRITE;
            return m->micro_phase == 5 ? CPU65_BUS_ACCESS_DATA_READ :
                (m->micro_phase == 6 ? CPU65_BUS_ACCESS_RMW_DUMMY_WRITE : CPU65_BUS_ACCESS_DATA_WRITE);
        case CPU65_UND_MODE_ZP_INDEXED:
            if (m->micro_phase == 2) return CPU65_BUS_ACCESS_DUMMY_READ;
            if (m->micro_phase == 3) return lax ? CPU65_BUS_ACCESS_DATA_READ :
                (rmw ? CPU65_BUS_ACCESS_DATA_READ : CPU65_BUS_ACCESS_DATA_WRITE);
            return m->micro_phase == 4 ? CPU65_BUS_ACCESS_RMW_DUMMY_WRITE : CPU65_BUS_ACCESS_DATA_WRITE;
        case CPU65_UND_MODE_ABS_Y:
        case CPU65_UND_MODE_ABS_X:
            if (m->micro_phase < 3) return CPU65_BUS_ACCESS_OPERAND_READ;
            if (m->micro_phase == 3) {
                return lax && !m->micro_branch_taken ? CPU65_BUS_ACCESS_DATA_READ :
                    CPU65_BUS_ACCESS_DUMMY_READ;
            }
            if (!rmw) return lax ? CPU65_BUS_ACCESS_DATA_READ : CPU65_BUS_ACCESS_DATA_WRITE;
            if (m->micro_phase == 4) return CPU65_BUS_ACCESS_DATA_READ;
            return m->micro_phase == 5 ? CPU65_BUS_ACCESS_RMW_DUMMY_WRITE : CPU65_BUS_ACCESS_DATA_WRITE;
        }
    }

    if (cpu65_micro_op_class[m->micro_opcode] == (uint8_t)CPU65_OP_UND_NOP) {
        if (m->micro_opcode == UND_80) {
            return CPU65_BUS_ACCESS_OPERAND_READ;
        }
        if (m->micro_opcode == UND_44) {
            return m->micro_phase == 1 ? CPU65_BUS_ACCESS_OPERAND_READ :
                CPU65_BUS_ACCESS_DATA_READ;
        }
        if (m->micro_phase < 3) {
            return CPU65_BUS_ACCESS_OPERAND_READ;
        }
        if (m->micro_phase == 3 && m->micro_branch_taken) {
            return CPU65_BUS_ACCESS_DUMMY_READ;
        }
        return CPU65_BUS_ACCESS_DATA_READ;
    }

    switch (m->micro_opcode) {
    case NOP:
        return CPU65_BUS_ACCESS_DUMMY_READ;
    case LDA_imm:
    case LDX_imm:
    case LDY_imm:
        return CPU65_BUS_ACCESS_OPERAND_READ;
    case LDA_abs:
    case LDX_abs:
    case LDY_abs:
        return m->micro_phase < 3 ?
            CPU65_BUS_ACCESS_OPERAND_READ : CPU65_BUS_ACCESS_DATA_READ;
    case STA_abs:
    case STX_abs:
    case STY_abs:
        return m->micro_phase < 3 ?
            CPU65_BUS_ACCESS_OPERAND_READ : CPU65_BUS_ACCESS_DATA_WRITE;
    case JMP_abs:
        return CPU65_BUS_ACCESS_OPERAND_READ;
    case JMP_ind:
        return m->micro_phase < 3 ? CPU65_BUS_ACCESS_OPERAND_READ :
            CPU65_BUS_ACCESS_DATA_READ;
    case BPL_rel:
    case BMI_rel:
    case BVC_rel:
    case BVS_rel:
    case BCC_rel:
    case BCS_rel:
    case BNE_rel:
    case BEQ_rel:
        return m->micro_phase == 1 ?
            CPU65_BUS_ACCESS_OPERAND_READ : CPU65_BUS_ACCESS_DUMMY_READ;
    case JSR_abs:
        if (m->micro_phase == 1 || m->micro_phase == 5) {
            return CPU65_BUS_ACCESS_OPERAND_READ;
        }
        return m->micro_phase == 2 ?
            CPU65_BUS_ACCESS_DUMMY_READ : CPU65_BUS_ACCESS_STACK_WRITE;
    case RTS:
        return m->micro_phase < 3 ?
            CPU65_BUS_ACCESS_DUMMY_READ :
            (m->micro_phase < 5 ? CPU65_BUS_ACCESS_STACK_READ : CPU65_BUS_ACCESS_DUMMY_READ);
    case ORA_imm:
    case AND_imm:
    case EOR_imm:
    case ADC_imm:
    case SBC_imm:
    case CMP_imm:
    case CPX_imm:
    case CPY_imm:
        return CPU65_BUS_ACCESS_OPERAND_READ;
    case CLC:
    case SEC:
    case CLI:
    case SEI:
    case CLD:
    case SED:
    case CLV:
    case INX:
    case INY:
    case DEX:
    case DEY:
    case TAX:
    case TAY:
    case TXA:
    case TYA:
    case TSX:
    case TXS:
    case ASL_A:
    case ROL_A:
    case LSR_A:
    case ROR_A:
        return CPU65_BUS_ACCESS_DUMMY_READ;
    case PHA:
    case PHP:
        return m->micro_phase == 1 ?
            CPU65_BUS_ACCESS_DUMMY_READ : CPU65_BUS_ACCESS_STACK_WRITE;
    case PLA:
    case PLP:
        return m->micro_phase < 3 ?
            CPU65_BUS_ACCESS_DUMMY_READ : CPU65_BUS_ACCESS_STACK_READ;
    case RTI:
        return m->micro_phase < 3 ?
            CPU65_BUS_ACCESS_DUMMY_READ : CPU65_BUS_ACCESS_STACK_READ;
    case BRK:
        if (m->micro_phase == 1) return CPU65_BUS_ACCESS_OPERAND_READ;
        if (m->micro_phase < 5) return CPU65_BUS_ACCESS_STACK_WRITE;
        return CPU65_BUS_ACCESS_VECTOR_READ;
    case LDA_zpg:
    case LDX_zpg:
    case LDY_zpg:
        return m->micro_phase == 1 ?
            CPU65_BUS_ACCESS_OPERAND_READ : CPU65_BUS_ACCESS_DATA_READ;
    case STA_zpg:
    case STX_zpg:
    case STY_zpg:
        return m->micro_phase == 1 ?
            CPU65_BUS_ACCESS_OPERAND_READ : CPU65_BUS_ACCESS_DATA_WRITE;
    case ORA_zpg:
    case AND_zpg:
    case EOR_zpg:
    case ADC_zpg:
    case SBC_zpg:
    case CMP_zpg:
    case CPX_zpg:
    case CPY_zpg:
        return m->micro_phase == 1 ?
            CPU65_BUS_ACCESS_OPERAND_READ : CPU65_BUS_ACCESS_DATA_READ;
    case BIT_zpg:
        return m->micro_phase == 1 ?
            CPU65_BUS_ACCESS_OPERAND_READ : CPU65_BUS_ACCESS_DATA_READ;
    case BIT_abs:
    case ORA_abs:
    case AND_abs:
    case EOR_abs:
    case ADC_abs:
    case SBC_abs:
    case CMP_abs:
    case CPX_abs:
    case CPY_abs:
        return m->micro_phase < 3 ?
            CPU65_BUS_ACCESS_OPERAND_READ : CPU65_BUS_ACCESS_DATA_READ;
    case LDA_zpg_X:
    case LDX_zpg_Y:
    case LDY_zpg_X:
        if (m->micro_phase == 1) return CPU65_BUS_ACCESS_OPERAND_READ;
        return m->micro_phase == 2 ? CPU65_BUS_ACCESS_DUMMY_READ :
            CPU65_BUS_ACCESS_DATA_READ;
    case STA_zpg_X:
    case STX_zpg_Y:
    case STY_zpg_X:
        if (m->micro_phase == 1) return CPU65_BUS_ACCESS_OPERAND_READ;
        return m->micro_phase == 2 ? CPU65_BUS_ACCESS_DUMMY_READ :
            CPU65_BUS_ACCESS_DATA_WRITE;
    case ORA_zpg_X:
    case AND_zpg_X:
    case EOR_zpg_X:
    case ADC_zpg_X:
    case SBC_zpg_X:
    case CMP_zpg_X:
        if (m->micro_phase == 1) return CPU65_BUS_ACCESS_OPERAND_READ;
        return m->micro_phase == 2 ? CPU65_BUS_ACCESS_DUMMY_READ :
            CPU65_BUS_ACCESS_DATA_READ;
    case LDA_abs_X:
    case LDA_abs_Y:
    case LDX_abs_Y:
    case LDY_abs_X:
        if (m->micro_phase < 3) return CPU65_BUS_ACCESS_OPERAND_READ;
        if (m->micro_phase == 3 && m->micro_branch_taken) {
            return CPU65_BUS_ACCESS_DUMMY_READ;
        }
        return CPU65_BUS_ACCESS_DATA_READ;
    case STA_abs_X:
    case STA_abs_Y:
        if (m->micro_phase < 3) return CPU65_BUS_ACCESS_OPERAND_READ;
        return m->micro_phase == 3 ? CPU65_BUS_ACCESS_DUMMY_READ :
            CPU65_BUS_ACCESS_DATA_WRITE;
    case ORA_abs_X:
    case ORA_abs_Y:
    case AND_abs_X:
    case AND_abs_Y:
    case EOR_abs_X:
    case EOR_abs_Y:
    case ADC_abs_X:
    case ADC_abs_Y:
    case SBC_abs_X:
    case SBC_abs_Y:
    case CMP_abs_X:
    case CMP_abs_Y:
        if (m->micro_phase < 3) return CPU65_BUS_ACCESS_OPERAND_READ;
        if (m->micro_phase == 3 && m->micro_branch_taken) {
            return CPU65_BUS_ACCESS_DUMMY_READ;
        }
        return CPU65_BUS_ACCESS_DATA_READ;
    case LDA_X_ind:
    case ORA_X_ind:
    case AND_X_ind:
    case EOR_X_ind:
    case ADC_X_ind:
    case SBC_X_ind:
    case CMP_X_ind:
        if (m->micro_phase == 1) return CPU65_BUS_ACCESS_OPERAND_READ;
        if (m->micro_phase == 2) return CPU65_BUS_ACCESS_DUMMY_READ;
        return CPU65_BUS_ACCESS_DATA_READ;
    case STA_X_ind:
        if (m->micro_phase == 1) return CPU65_BUS_ACCESS_OPERAND_READ;
        if (m->micro_phase == 2) return CPU65_BUS_ACCESS_DUMMY_READ;
        return m->micro_phase < 5 ? CPU65_BUS_ACCESS_DATA_READ :
            CPU65_BUS_ACCESS_DATA_WRITE;
    case LDA_ind_Y:
    case ORA_ind_Y:
    case AND_ind_Y:
    case EOR_ind_Y:
    case ADC_ind_Y:
    case SBC_ind_Y:
    case CMP_ind_Y:
        if (m->micro_phase == 1) return CPU65_BUS_ACCESS_OPERAND_READ;
        if (m->micro_phase < 3) return CPU65_BUS_ACCESS_DATA_READ;
        if (m->micro_phase == 3 && m->micro_branch_taken) {
            return CPU65_BUS_ACCESS_DUMMY_READ;
        }
        return CPU65_BUS_ACCESS_DATA_READ;
    case STA_ind_Y:
        if (m->micro_phase == 1) return CPU65_BUS_ACCESS_OPERAND_READ;
        if (m->micro_phase < 3) return CPU65_BUS_ACCESS_DATA_READ;
        return m->micro_phase == 3 ? CPU65_BUS_ACCESS_DUMMY_READ :
            CPU65_BUS_ACCESS_DATA_WRITE;
    case ASL_zpg:
    case ROL_zpg:
    case LSR_zpg:
    case ROR_zpg:
    case DEC_zpg:
    case INC_zpg:
        if (m->micro_phase == 1) return CPU65_BUS_ACCESS_OPERAND_READ;
        if (m->micro_phase == 2) return CPU65_BUS_ACCESS_DATA_READ;
        return m->micro_phase == 3 ? CPU65_BUS_ACCESS_RMW_DUMMY_WRITE :
            CPU65_BUS_ACCESS_DATA_WRITE;
    case ASL_abs:
    case ROL_abs:
    case LSR_abs:
    case ROR_abs:
    case DEC_abs:
    case INC_abs:
        if (m->micro_phase < 3) return CPU65_BUS_ACCESS_OPERAND_READ;
        if (m->micro_phase == 3) return CPU65_BUS_ACCESS_DATA_READ;
        return m->micro_phase == 4 ? CPU65_BUS_ACCESS_RMW_DUMMY_WRITE :
            CPU65_BUS_ACCESS_DATA_WRITE;
    case ASL_zpg_X:
    case ROL_zpg_X:
    case LSR_zpg_X:
    case ROR_zpg_X:
    case DEC_zpg_X:
    case INC_zpg_X:
        if (m->micro_phase == 1) return CPU65_BUS_ACCESS_OPERAND_READ;
        if (m->micro_phase == 2) return CPU65_BUS_ACCESS_DUMMY_READ;
        if (m->micro_phase == 3) return CPU65_BUS_ACCESS_DATA_READ;
        return m->micro_phase == 4 ? CPU65_BUS_ACCESS_RMW_DUMMY_WRITE :
            CPU65_BUS_ACCESS_DATA_WRITE;
    case ASL_abs_X:
    case ROL_abs_X:
    case LSR_abs_X:
    case ROR_abs_X:
    case DEC_abs_X:
    case INC_abs_X:
        if (m->micro_phase < 3) return CPU65_BUS_ACCESS_OPERAND_READ;
        if (m->micro_phase == 3) return CPU65_BUS_ACCESS_DUMMY_READ;
        if (m->micro_phase == 4) return CPU65_BUS_ACCESS_DATA_READ;
        return m->micro_phase == 5 ? CPU65_BUS_ACCESS_RMW_DUMMY_WRITE :
            CPU65_BUS_ACCESS_DATA_WRITE;
    default:
        return CPU65_BUS_ACCESS_DATA_READ;
    }
}

static uint8_t cpu65_micro_rmw_value(cpu65_t *m) {
    uint8_t value = m->cpu.scratch_lo;

    if (cpu65_micro_is_undocumented_rmw(m->micro_opcode)) {
        switch (m->micro_opcode & 0xe0u) {
        case 0x00u: /* SLO = ASL then ORA */
            m->cpu.C = (value & 0x80u) != 0;
            value <<= 1;
            set_register_to_value(m, &m->cpu.A, (uint8_t)(m->cpu.A | value));
            break;
        case 0x20u: { /* RLA = ROL then AND */
            uint8_t carry = (value & 0x80u) != 0;
            value = (uint8_t)((value << 1) | m->cpu.C);
            m->cpu.C = carry;
            set_register_to_value(m, &m->cpu.A, (uint8_t)(m->cpu.A & value));
            break;
        }
        case 0x40u: /* SRE = LSR then EOR */
            m->cpu.C = value & 0x01u;
            value >>= 1;
            set_register_to_value(m, &m->cpu.A, (uint8_t)(m->cpu.A ^ value));
            break;
        case 0x60u: { /* RRA = ROR then ADC */
            uint8_t carry = value & 0x01u;
            value = (uint8_t)((value >> 1) | (m->cpu.C << 7));
            m->cpu.C = carry;
            add_value_to_accumulator(m, value);
            break;
        }
        case 0xc0u: /* DCP = DEC then CMP */
            value--;
            compare_bytes(m, m->cpu.A, value);
            break;
        default: /* ISC = INC then SBC */
            value++;
            subtract_value_from_accumulator(m, value);
            break;
        }
        m->cpu.scratch_hi = value;
        return value;
    }

    switch (m->micro_opcode) {
    case ASL_zpg:
    case ASL_abs:
    case ASL_zpg_X:
    case ASL_abs_X:
        m->cpu.C = (value & 0x80u) != 0;
        value <<= 1;
        break;
    case ROL_zpg:
    case ROL_abs:
    case ROL_zpg_X:
    case ROL_abs_X: {
        uint8_t carry = (value & 0x80u) != 0;
        value = (uint8_t)((value << 1) | m->cpu.C);
        m->cpu.C = carry;
        break;
    }
    case LSR_zpg:
    case LSR_abs:
    case LSR_zpg_X:
    case LSR_abs_X:
        m->cpu.C = value & 0x01u;
        value >>= 1;
        break;
    case ROR_zpg:
    case ROR_abs:
    case ROR_zpg_X:
    case ROR_abs_X: {
        uint8_t carry = value & 0x01u;
        value = (uint8_t)((value >> 1) | (m->cpu.C << 7));
        m->cpu.C = carry;
        break;
    }
    case DEC_zpg:
    case DEC_abs:
    case DEC_zpg_X:
    case DEC_abs_X:
        value--;
        break;
    default:
        value++;
        break;
    }
    set_register_to_value(m, &m->cpu.scratch_hi, value);
    return value;
}

static void cpu65_micro_apply_read_operation(cpu65_t *m, uint8_t value) {
    switch (m->micro_opcode) {
    case ORA_zpg:
    case ORA_abs:
    case ORA_zpg_X:
    case ORA_abs_X:
    case ORA_abs_Y:
    case ORA_X_ind:
    case ORA_ind_Y:
        set_register_to_value(m, &m->cpu.A, (uint8_t)(m->cpu.A | value));
        break;
    case AND_zpg:
    case AND_abs:
    case AND_zpg_X:
    case AND_abs_X:
    case AND_abs_Y:
    case AND_X_ind:
    case AND_ind_Y:
        set_register_to_value(m, &m->cpu.A, (uint8_t)(m->cpu.A & value));
        break;
    case EOR_zpg:
    case EOR_abs:
    case EOR_zpg_X:
    case EOR_abs_X:
    case EOR_abs_Y:
    case EOR_X_ind:
    case EOR_ind_Y:
        set_register_to_value(m, &m->cpu.A, (uint8_t)(m->cpu.A ^ value));
        break;
    case ADC_zpg:
    case ADC_abs:
    case ADC_zpg_X:
    case ADC_abs_X:
    case ADC_abs_Y:
    case ADC_X_ind:
    case ADC_ind_Y:
        add_value_to_accumulator(m, value);
        break;
    case SBC_zpg:
    case SBC_abs:
    case SBC_zpg_X:
    case SBC_abs_X:
    case SBC_abs_Y:
    case SBC_X_ind:
    case SBC_ind_Y:
        subtract_value_from_accumulator(m, value);
        break;
    case CMP_zpg:
    case CMP_abs:
    case CMP_zpg_X:
    case CMP_abs_X:
    case CMP_abs_Y:
    case CMP_X_ind:
    case CMP_ind_Y:
        compare_bytes(m, m->cpu.A, value);
        break;
    case CPX_zpg:
    case CPX_abs:
        compare_bytes(m, m->cpu.X, value);
        break;
    case CPY_zpg:
    case CPY_abs:
        compare_bytes(m, m->cpu.Y, value);
        break;
    case BIT_zpg:
    case BIT_abs:
        m->cpu.Z = (m->cpu.A & value) == 0;
        m->cpu.N = (value & 0x80u) != 0;
        m->cpu.V = (value & 0x40u) != 0;
        break;
    default:
        assert(!"unsupported resumable read operation");
        break;
    }
}

static bool cpu65_micro_uses_y_index(uint8_t opcode) {
    switch (opcode) {
    case LDA_abs_Y:
    case LDX_abs_Y:
    case STA_abs_Y:
    case ORA_abs_Y:
    case AND_abs_Y:
    case EOR_abs_Y:
    case ADC_abs_Y:
    case SBC_abs_Y:
    case CMP_abs_Y:
        return true;
    default:
        return false;
    }
}

static void cpu65_micro_set_lax(cpu65_t *m, uint8_t value) {
    m->cpu.A = value;
    set_register_to_value(m, &m->cpu.X, value);
}

/* Execute one post-fetch cycle of the stable practical undocumented set.
   These opcodes share the NMOS bus templates of their documented cousins;
   only the final register/memory operation differs. */
static bool cpu65_micro_step_practical_undocumented(cpu65_t *m) {
    cpu65_undocumented_mode mode = cpu65_micro_undocumented_mode(m->micro_opcode);
    bool rmw = cpu65_micro_is_undocumented_rmw(m->micro_opcode);
    bool lax = cpu65_micro_is_lax(m->micro_opcode);
    uint8_t value;

    if (m->micro_phase == 1) {
        m->cpu.scratch_lo = read_operand(m, m->cpu.pc);
        CYCLE(m);
        m->cpu.pc++;
        m->micro_phase++;
        return false;
    }

    switch (mode) {
    case CPU65_UND_MODE_ZP:
        if (m->micro_phase == 2) {
            m->cpu.address_lo = m->cpu.scratch_lo;
            m->cpu.address_hi = 0;
            if (rmw) {
                m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
                CYCLE(m); m->micro_phase++; return false;
            }
            if (lax) cpu65_micro_set_lax(m, read_from_memory(m, m->cpu.address_16));
            else write_to_memory(m, m->cpu.address_16, (uint8_t)(m->cpu.A & m->cpu.X));
            CYCLE(m); return true;
        }
        if (m->micro_phase == 3) {
            m->bus_access_kind = CPU65_BUS_ACCESS_RMW_DUMMY_WRITE;
            m->write(m->user, m->cpu.address_16, m->cpu.scratch_lo);
            CYCLE(m); m->micro_phase++; return false;
        }
        write_to_memory(m, m->cpu.address_16, cpu65_micro_rmw_value(m));
        CYCLE(m); return true;

    case CPU65_UND_MODE_ZP_INDEXED:
        if (m->micro_phase == 2) {
            (void)read_dummy(m, m->cpu.scratch_lo);
            m->cpu.address_lo = (uint8_t)(m->cpu.scratch_lo +
                (cpu65_micro_undoc_uses_y_index(m->micro_opcode) ? m->cpu.Y : m->cpu.X));
            m->cpu.address_hi = 0;
            CYCLE(m); m->micro_phase++; return false;
        }
        if (m->micro_phase == 3) {
            if (rmw) {
                m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
                CYCLE(m); m->micro_phase++; return false;
            }
            if (lax) cpu65_micro_set_lax(m, read_from_memory(m, m->cpu.address_16));
            else write_to_memory(m, m->cpu.address_16, (uint8_t)(m->cpu.A & m->cpu.X));
            CYCLE(m); return true;
        }
        if (m->micro_phase == 4) {
            m->bus_access_kind = CPU65_BUS_ACCESS_RMW_DUMMY_WRITE;
            m->write(m->user, m->cpu.address_16, m->cpu.scratch_lo);
            CYCLE(m); m->micro_phase++; return false;
        }
        write_to_memory(m, m->cpu.address_16, cpu65_micro_rmw_value(m));
        CYCLE(m); return true;

    case CPU65_UND_MODE_ABS:
        if (m->micro_phase == 2) {
            m->cpu.address_lo = m->cpu.scratch_lo;
            m->cpu.address_hi = read_operand(m, m->cpu.pc);
            CYCLE(m); m->cpu.pc++; m->micro_phase++; return false;
        }
        if (m->micro_phase == 3) {
            if (rmw) {
                m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
                CYCLE(m); m->micro_phase++; return false;
            }
            if (lax) cpu65_micro_set_lax(m, read_from_memory(m, m->cpu.address_16));
            else write_to_memory(m, m->cpu.address_16, (uint8_t)(m->cpu.A & m->cpu.X));
            CYCLE(m); return true;
        }
        if (m->micro_phase == 4) {
            m->bus_access_kind = CPU65_BUS_ACCESS_RMW_DUMMY_WRITE;
            m->write(m->user, m->cpu.address_16, m->cpu.scratch_lo);
            CYCLE(m); m->micro_phase++; return false;
        }
        write_to_memory(m, m->cpu.address_16, cpu65_micro_rmw_value(m));
        CYCLE(m); return true;

    case CPU65_UND_MODE_X_IND:
        if (m->micro_phase == 2) {
            (void)read_dummy(m, m->cpu.scratch_lo);
            m->micro_target = (uint8_t)(m->cpu.scratch_lo + m->cpu.X);
            CYCLE(m); m->micro_phase++; return false;
        }
        if (m->micro_phase == 3) {
            m->cpu.address_lo = read_from_memory(m, m->micro_target);
            CYCLE(m); m->micro_phase++; return false;
        }
        if (m->micro_phase == 4) {
            m->cpu.address_hi = read_from_memory(m, (uint8_t)(m->micro_target + 1u));
            CYCLE(m); m->micro_phase++; return false;
        }
        if (m->micro_phase == 5) {
            if (rmw) {
                m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
                CYCLE(m); m->micro_phase++; return false;
            }
            if (lax) cpu65_micro_set_lax(m, read_from_memory(m, m->cpu.address_16));
            else write_to_memory(m, m->cpu.address_16, (uint8_t)(m->cpu.A & m->cpu.X));
            CYCLE(m); return true;
        }
        if (m->micro_phase == 6) {
            m->bus_access_kind = CPU65_BUS_ACCESS_RMW_DUMMY_WRITE;
            m->write(m->user, m->cpu.address_16, m->cpu.scratch_lo);
            CYCLE(m); m->micro_phase++; return false;
        }
        write_to_memory(m, m->cpu.address_16, cpu65_micro_rmw_value(m));
        CYCLE(m); return true;

    case CPU65_UND_MODE_IND_Y:
        if (m->micro_phase == 2) {
            m->cpu.address_lo = read_from_memory(m, m->cpu.scratch_lo);
            CYCLE(m); m->micro_phase++; return false;
        }
        if (m->micro_phase == 3) {
            uint16_t base;
            m->cpu.address_hi = read_from_memory(m, (uint8_t)(m->cpu.scratch_lo + 1u));
            CYCLE(m);
            base = m->cpu.address_16;
            m->micro_target = (uint16_t)(base + m->cpu.Y);
            m->micro_branch_taken = (uint16_t)m->cpu.address_lo + m->cpu.Y > 0xffu;
            m->micro_phase++; return false;
        }
        if (m->micro_phase == 4) {
            if (lax && !m->micro_branch_taken) {
                m->micro_phase++;
                return cpu65_micro_step_practical_undocumented(m);
            }
            (void)read_dummy(m, (uint16_t)((m->cpu.address_16 & 0xff00u) |
                                            (m->micro_target & 0x00ffu)));
            CYCLE(m); m->micro_phase++; return false;
        }
        if (m->micro_phase == 5) {
            if (rmw) {
                m->cpu.scratch_lo = read_from_memory(m, m->micro_target);
                CYCLE(m); m->micro_phase++; return false;
            }
            cpu65_micro_set_lax(m, read_from_memory(m, m->micro_target));
            CYCLE(m); return true;
        }
        if (m->micro_phase == 6) {
            m->bus_access_kind = CPU65_BUS_ACCESS_RMW_DUMMY_WRITE;
            m->write(m->user, m->micro_target, m->cpu.scratch_lo);
            CYCLE(m); m->micro_phase++; return false;
        }
        write_to_memory(m, m->micro_target, cpu65_micro_rmw_value(m));
        CYCLE(m); return true;

    case CPU65_UND_MODE_ABS_Y:
    case CPU65_UND_MODE_ABS_X:
        if (m->micro_phase == 2) {
            uint16_t base;
            uint8_t index = mode == CPU65_UND_MODE_ABS_Y ? m->cpu.Y : m->cpu.X;
            m->cpu.address_lo = m->cpu.scratch_lo;
            m->cpu.address_hi = read_operand(m, m->cpu.pc);
            CYCLE(m); m->cpu.pc++;
            base = m->cpu.address_16;
            m->micro_target = (uint16_t)(base + index);
            m->micro_branch_taken = (uint16_t)m->cpu.address_lo + index > 0xffu;
            m->micro_phase++; return false;
        }
        if (m->micro_phase == 3) {
            if (lax && !m->micro_branch_taken) {
                m->micro_phase++;
                return cpu65_micro_step_practical_undocumented(m);
            }
            (void)read_dummy(m, (uint16_t)((m->cpu.address_16 & 0xff00u) |
                                            (m->micro_target & 0x00ffu)));
            CYCLE(m); m->micro_phase++; return false;
        }
        if (m->micro_phase == 4) {
            if (rmw) {
                m->cpu.scratch_lo = read_from_memory(m, m->micro_target);
                CYCLE(m); m->micro_phase++; return false;
            }
            cpu65_micro_set_lax(m, read_from_memory(m, m->micro_target));
            CYCLE(m); return true;
        }
        if (m->micro_phase == 5) {
            m->bus_access_kind = CPU65_BUS_ACCESS_RMW_DUMMY_WRITE;
            m->write(m->user, m->micro_target, m->cpu.scratch_lo);
            CYCLE(m); m->micro_phase++; return false;
        }
        write_to_memory(m, m->micro_target, cpu65_micro_rmw_value(m));
        CYCLE(m); return true;
    }
    return false;
}

bool cpu65_micro_step(cpu65_t *m) {
    uint8_t value;

    assert(m);
    assert(m->micro_active);

    if (m->micro_is_interrupt) {
        if (m->micro_phase == 0) {
            (void)read_dummy(m, m->cpu.pc);
        } else if (m->micro_phase == 1) {
            (void)read_dummy(m, m->cpu.sp);
        } else if (m->micro_phase == 2) {
            write_stack(m, m->cpu.sp, (uint8_t)(m->cpu.pc >> 8));
            if (--m->cpu.sp < 0x100u) m->cpu.sp += 0x100u;
        } else if (m->micro_phase == 3) {
            write_stack(m, m->cpu.sp, (uint8_t)m->cpu.pc);
            if (--m->cpu.sp < 0x100u) m->cpu.sp += 0x100u;
        } else if (m->micro_phase == 4) {
            write_stack(m, m->cpu.sp, (uint8_t)(m->cpu.flags & (uint8_t)~0x10u) | 0x20u);
            if (--m->cpu.sp < 0x100u) m->cpu.sp += 0x100u;
        } else if (m->micro_phase == 5) {
            m->cpu.address_lo = read_vector(m, m->micro_interrupt_vector);
        } else {
            m->cpu.address_hi = read_vector(m, (uint16_t)(m->micro_interrupt_vector + 1u));
        }
        CYCLE(m);
        if (m->micro_phase < 6) {
            m->micro_phase++;
            return false;
        }
        m->cpu.pc = m->cpu.address_16;
        m->cpu.I = 1;
        m->cpu.irq_entries += m->micro_interrupt_vector == 0xfffeu ? 1u : 0u;
        m->cpu.nmi_entries += m->micro_interrupt_vector == 0xfffau ? 1u : 0u;
        m->micro_active = 0;
        m->micro_is_interrupt = 0;
        m->micro_phase = 0;
        return true;
    }

    if (m->micro_phase == 0) {
        m->micro_opcode = read_opcode(m, m->cpu.pc);
        CYCLE(m);
        m->cpu.pc++;
        m->micro_phase = 1;
        return false;
    }

    cpu65_micro_init_op_class();
    {
        uint8_t cls = cpu65_micro_op_class[m->micro_opcode];
        if (cls == (uint8_t)CPU65_OP_UND_PRACTICAL) {
            if (!cpu65_micro_step_practical_undocumented(m)) {
                return false;
            }
            goto micro_complete;
        }
        if (cls == (uint8_t)CPU65_OP_UND_NOP) {
            /* fall into undocumented-nop body below */
        } else {
            /* Documented: skip undoc probes and use the main switch. */
            goto micro_documented;
        }
    }

    /* CPU65_OP_UND_NOP body */
    if (m->micro_opcode == UND_80) {
        (void)read_operand(m, m->cpu.pc);
        CYCLE(m);
        m->cpu.pc++;
        goto micro_complete;
    }
    if (m->micro_opcode == UND_44) {
        if (m->micro_phase == 1) {
            m->cpu.address_lo = read_operand(m, m->cpu.pc);
            CYCLE(m);
            m->cpu.pc++;
            m->micro_phase++;
            return false;
        }
        (void)read_from_memory(m, m->cpu.address_lo);
        CYCLE(m);
        goto micro_complete;
    }
    if (m->micro_phase == 1) {
        m->cpu.address_lo = read_operand(m, m->cpu.pc);
        CYCLE(m);
        m->cpu.pc++;
        m->micro_branch_taken = (uint16_t)m->cpu.address_lo + m->cpu.X > 0xffu;
        m->micro_phase++;
        return false;
    }
    if (m->micro_phase == 2) {
        uint16_t base;
        m->cpu.address_hi = read_operand(m, m->cpu.pc);
        CYCLE(m);
        m->cpu.pc++;
        base = m->cpu.address_16;
        m->micro_target = (uint16_t)(base + m->cpu.X);
        m->micro_phase++;
        return false;
    }
    if (m->micro_phase == 3 && m->micro_branch_taken) {
        (void)read_dummy(m, (uint16_t)((m->cpu.address_16 & 0xff00u) |
                                        (m->micro_target & 0x00ffu)));
        CYCLE(m);
        m->micro_phase++;
        return false;
    }
    (void)read_from_memory(m, m->micro_target);
    CYCLE(m);
    goto micro_complete;

micro_documented:
    switch (m->micro_opcode) {
    case NOP:
        (void)read_dummy(m, m->cpu.pc);
        CYCLE(m);
        break;
    case LDA_imm:
        value = read_operand(m, m->cpu.pc);
        CYCLE(m);
        m->cpu.pc++;
        set_register_to_value(m, &m->cpu.A, value);
        break;
    case LDX_imm:
        value = read_operand(m, m->cpu.pc);
        CYCLE(m);
        m->cpu.pc++;
        set_register_to_value(m, &m->cpu.X, value);
        break;
    case LDY_imm:
        value = read_operand(m, m->cpu.pc);
        CYCLE(m);
        m->cpu.pc++;
        set_register_to_value(m, &m->cpu.Y, value);
        break;
    case LDA_abs:
    case LDX_abs:
    case LDY_abs:
        if (m->micro_phase == 1) {
            m->cpu.address_lo = read_operand(m, m->cpu.pc);
            CYCLE(m);
            m->cpu.pc++;
            m->micro_phase++;
            return false;
        }
        if (m->micro_phase == 2) {
            m->cpu.address_hi = read_operand(m, m->cpu.pc);
            CYCLE(m);
            m->cpu.pc++;
            m->micro_phase++;
            return false;
        }
        value = read_from_memory(m, m->cpu.address_16);
        CYCLE(m);
        if (m->micro_opcode == LDX_abs) {
            set_register_to_value(m, &m->cpu.X, value);
        } else if (m->micro_opcode == LDY_abs) {
            set_register_to_value(m, &m->cpu.Y, value);
        } else {
            set_register_to_value(m, &m->cpu.A, value);
        }
        break;
    case STA_abs:
    case STX_abs:
    case STY_abs:
        if (m->micro_phase == 1) {
            m->cpu.address_lo = read_operand(m, m->cpu.pc);
            CYCLE(m);
            m->cpu.pc++;
            m->micro_phase++;
            return false;
        }
        if (m->micro_phase == 2) {
            m->cpu.address_hi = read_operand(m, m->cpu.pc);
            CYCLE(m);
            m->cpu.pc++;
            m->micro_phase++;
            return false;
        }
        if (m->micro_opcode == STX_abs) {
            write_to_memory(m, m->cpu.address_16, m->cpu.X);
        } else if (m->micro_opcode == STY_abs) {
            write_to_memory(m, m->cpu.address_16, m->cpu.Y);
        } else {
            write_to_memory(m, m->cpu.address_16, m->cpu.A);
        }
        CYCLE(m);
        break;
    case JMP_abs:
        if (m->micro_phase == 1) {
            m->cpu.address_lo = read_operand(m, m->cpu.pc);
            CYCLE(m);
            m->cpu.pc++;
            m->micro_phase++;
            return false;
        }
        m->cpu.address_hi = read_operand(m, m->cpu.pc);
        CYCLE(m);
        m->cpu.pc = m->cpu.address_16;
        break;
    case JMP_ind:
        if (m->micro_phase == 1) {
            m->cpu.address_lo = read_operand(m, m->cpu.pc);
            CYCLE(m);
            m->cpu.pc++;
            m->micro_phase++;
            return false;
        }
        if (m->micro_phase == 2) {
            m->cpu.address_hi = read_operand(m, m->cpu.pc);
            CYCLE(m);
            m->cpu.pc++;
            m->micro_target = m->cpu.address_16;
            m->micro_phase++;
            return false;
        }
        if (m->micro_phase == 3) {
            m->cpu.scratch_lo = read_from_memory(m, m->micro_target);
            CYCLE(m);
            m->micro_phase++;
            return false;
        }
        m->cpu.scratch_hi = read_from_memory(
            m, (uint16_t)((m->micro_target & 0xff00u) |
                           ((m->micro_target + 1u) & 0x00ffu)));
        CYCLE(m);
        m->cpu.pc = m->cpu.scratch_16;
        break;
    case BPL_rel:
    case BMI_rel:
    case BVC_rel:
    case BVS_rel:
    case BCC_rel:
    case BCS_rel:
    case BNE_rel:
    case BEQ_rel:
        if (m->micro_phase == 1) {
            value = read_operand(m, m->cpu.pc);
            CYCLE(m);
            m->cpu.pc++;
            m->micro_branch_taken =
                (m->micro_opcode == BPL_rel && !m->cpu.N) ||
                (m->micro_opcode == BMI_rel && m->cpu.N) ||
                (m->micro_opcode == BVC_rel && !m->cpu.V) ||
                (m->micro_opcode == BVS_rel && m->cpu.V) ||
                (m->micro_opcode == BCC_rel && !m->cpu.C) ||
                (m->micro_opcode == BCS_rel && m->cpu.C) ||
                (m->micro_opcode == BNE_rel && !m->cpu.Z) ||
                (m->micro_opcode == BEQ_rel && m->cpu.Z);
            m->cpu.scratch_lo = value;
            if (!m->micro_branch_taken) {
                break;
            }
            m->micro_phase++;
            return false;
        }
        if (m->micro_phase == 2) {
            uint16_t old_pc = m->cpu.pc;
            m->micro_target = (uint16_t)(old_pc + (int8_t)m->cpu.scratch_lo);
            (void)read_dummy(m, old_pc);
            CYCLE(m);
            if ((old_pc & 0xff00u) == (m->micro_target & 0xff00u)) {
                m->cpu.pc = m->micro_target;
                break;
            }
            m->micro_phase++;
            return false;
        }
        (void)read_dummy(m, (uint16_t)((m->cpu.pc & 0xff00u) | (m->micro_target & 0x00ffu)));
        CYCLE(m);
        m->cpu.pc = m->micro_target;
        break;
    case JSR_abs:
        if (m->micro_phase == 1) {
            m->cpu.address_lo = read_operand(m, m->cpu.pc);
            CYCLE(m);
            m->cpu.pc++;
            m->micro_phase++;
            return false;
        }
        if (m->micro_phase == 2) {
            (void)read_dummy(m, m->cpu.sp);
            CYCLE(m);
            m->micro_phase++;
            return false;
        }
        if (m->micro_phase == 3) {
            write_stack(m, m->cpu.sp, (uint8_t)(m->cpu.pc >> 8));
            CYCLE(m);
            if (--m->cpu.sp < 0x100u) {
                m->cpu.sp += 0x100u;
            }
            m->micro_phase++;
            return false;
        }
        if (m->micro_phase == 4) {
            write_stack(m, m->cpu.sp, (uint8_t)m->cpu.pc);
            CYCLE(m);
            if (--m->cpu.sp < 0x100u) {
                m->cpu.sp += 0x100u;
            }
            m->micro_phase++;
            return false;
        }
        m->cpu.address_hi = read_operand(m, m->cpu.pc);
        CYCLE(m);
        m->cpu.pc = m->cpu.address_16;
        break;
    case RTS:
        if (m->micro_phase == 1) {
            (void)read_dummy(m, m->cpu.pc);
            CYCLE(m);
            m->micro_phase++;
            return false;
        }
        if (m->micro_phase == 2) {
            (void)read_dummy(m, m->cpu.sp);
            CYCLE(m);
            m->micro_phase++;
            return false;
        }
        if (m->micro_phase == 3) {
            if (++m->cpu.sp >= 0x200u) {
                m->cpu.sp = 0x100u;
            }
            m->cpu.address_lo = read_stack(m, m->cpu.sp);
            CYCLE(m);
            m->micro_phase++;
            return false;
        }
        if (m->micro_phase == 4) {
            if (++m->cpu.sp >= 0x200u) {
                m->cpu.sp = 0x100u;
            }
            m->cpu.address_hi = read_stack(m, m->cpu.sp);
            CYCLE(m);
            m->micro_phase++;
            return false;
        }
        (void)read_dummy(m, m->cpu.address_16);
        CYCLE(m);
        m->cpu.pc = (uint16_t)(m->cpu.address_16 + 1u);
        break;
    case ORA_imm:
    case AND_imm:
    case EOR_imm:
    case ADC_imm:
    case SBC_imm:
    case CMP_imm:
    case CPX_imm:
    case CPY_imm:
        value = read_operand(m, m->cpu.pc);
        CYCLE(m);
        m->cpu.pc++;
        switch (m->micro_opcode) {
        case ORA_imm:
            set_register_to_value(m, &m->cpu.A, (uint8_t)(m->cpu.A | value));
            break;
        case AND_imm:
            set_register_to_value(m, &m->cpu.A, (uint8_t)(m->cpu.A & value));
            break;
        case EOR_imm:
            set_register_to_value(m, &m->cpu.A, (uint8_t)(m->cpu.A ^ value));
            break;
        case ADC_imm:
            add_value_to_accumulator(m, value);
            break;
        case SBC_imm:
            subtract_value_from_accumulator(m, value);
            break;
        case CMP_imm:
            compare_bytes(m, m->cpu.A, value);
            break;
        case CPX_imm:
            compare_bytes(m, m->cpu.X, value);
            break;
        default:
            compare_bytes(m, m->cpu.Y, value);
            break;
        }
        break;
    case CLC:
    case SEC:
    case CLI:
    case SEI:
    case CLD:
    case SED:
    case CLV:
        (void)read_dummy(m, m->cpu.pc);
        CYCLE(m);
        if (m->micro_opcode == CLC) m->cpu.C = 0;
        if (m->micro_opcode == SEC) m->cpu.C = 1;
        if (m->micro_opcode == CLI) {
            m->cpu.irq_defer = 1;
            m->cpu.irq_defer_i = 1;
            m->cpu.I = 0;
        }
        if (m->micro_opcode == SEI) {
            m->cpu.irq_defer = 0;
            m->cpu.irq_defer_i = 0;
            m->cpu.I = 1;
        }
        if (m->micro_opcode == CLD) m->cpu.D = 0;
        if (m->micro_opcode == SED) m->cpu.D = 1;
        if (m->micro_opcode == CLV) m->cpu.V = 0;
        break;
    case INX:
    case INY:
    case DEX:
    case DEY:
    case TAX:
    case TAY:
    case TXA:
    case TYA:
    case TSX:
    case TXS:
    case ASL_A:
    case ROL_A:
    case LSR_A:
    case ROR_A:
        (void)read_dummy(m, m->cpu.pc);
        CYCLE(m);
        if (m->micro_opcode == INX) set_register_to_value(m, &m->cpu.X, (uint8_t)(m->cpu.X + 1u));
        if (m->micro_opcode == INY) set_register_to_value(m, &m->cpu.Y, (uint8_t)(m->cpu.Y + 1u));
        if (m->micro_opcode == DEX) set_register_to_value(m, &m->cpu.X, (uint8_t)(m->cpu.X - 1u));
        if (m->micro_opcode == DEY) set_register_to_value(m, &m->cpu.Y, (uint8_t)(m->cpu.Y - 1u));
        if (m->micro_opcode == TAX) set_register_to_value(m, &m->cpu.X, m->cpu.A);
        if (m->micro_opcode == TAY) set_register_to_value(m, &m->cpu.Y, m->cpu.A);
        if (m->micro_opcode == TXA) set_register_to_value(m, &m->cpu.A, m->cpu.X);
        if (m->micro_opcode == TYA) set_register_to_value(m, &m->cpu.A, m->cpu.Y);
        if (m->micro_opcode == TSX) set_register_to_value(m, &m->cpu.X, (uint8_t)m->cpu.sp);
        if (m->micro_opcode == TXS) m->cpu.sp = (uint16_t)(0x100u | m->cpu.X);
        if (m->micro_opcode == ASL_A) {
            m->cpu.C = (m->cpu.A & 0x80u) != 0;
            set_register_to_value(m, &m->cpu.A, (uint8_t)(m->cpu.A << 1));
        }
        if (m->micro_opcode == ROL_A) {
            uint8_t carry = (m->cpu.A & 0x80u) != 0;
            set_register_to_value(m, &m->cpu.A, (uint8_t)((m->cpu.A << 1) | m->cpu.C));
            m->cpu.C = carry;
        }
        if (m->micro_opcode == LSR_A) {
            m->cpu.C = m->cpu.A & 0x01u;
            set_register_to_value(m, &m->cpu.A, (uint8_t)(m->cpu.A >> 1));
        }
        if (m->micro_opcode == ROR_A) {
            uint8_t carry = m->cpu.A & 0x01u;
            set_register_to_value(m, &m->cpu.A, (uint8_t)((m->cpu.A >> 1) | (m->cpu.C << 7)));
            m->cpu.C = carry;
        }
        break;
    case PHA:
    case PHP:
        if (m->micro_phase == 1) {
            (void)read_dummy(m, m->cpu.pc);
            CYCLE(m);
            m->micro_phase++;
            return false;
        }
        write_stack(m, m->cpu.sp,
            m->micro_opcode == PHA ? m->cpu.A : (uint8_t)(m->cpu.flags | 0x10u));
        CYCLE(m);
        if (--m->cpu.sp < 0x100u) {
            m->cpu.sp += 0x100u;
        }
        break;
    case PLA:
    case PLP:
        if (m->micro_phase == 1) {
            (void)read_dummy(m, m->cpu.pc);
            CYCLE(m);
            m->micro_phase++;
            return false;
        }
        if (m->micro_phase == 2) {
            (void)read_dummy(m, m->cpu.sp);
            CYCLE(m);
            m->micro_phase++;
            return false;
        }
        if (++m->cpu.sp >= 0x200u) {
            m->cpu.sp = 0x100u;
        }
        value = read_stack(m, m->cpu.sp);
        CYCLE(m);
        if (m->micro_opcode == PLA) {
            set_register_to_value(m, &m->cpu.A, value);
        } else {
            m->cpu.flags = (uint8_t)((value & (uint8_t)~0x10u) | 0x20u);
        }
        break;
    case RTI:
        if (m->micro_phase == 1) {
            (void)read_dummy(m, m->cpu.pc);
            CYCLE(m);
            m->micro_phase++;
            return false;
        }
        if (m->micro_phase == 2) {
            (void)read_dummy(m, m->cpu.sp);
            CYCLE(m);
            m->micro_phase++;
            return false;
        }
        if (++m->cpu.sp >= 0x200u) {
            m->cpu.sp = 0x100u;
        }
        value = read_stack(m, m->cpu.sp);
        CYCLE(m);
        if (m->micro_phase == 3) {
            m->cpu.flags = (uint8_t)((value & (uint8_t)~0x10u) | 0x20u);
            m->micro_phase++;
            return false;
        }
        if (m->micro_phase == 4) {
            m->cpu.address_lo = value;
            m->micro_phase++;
            return false;
        }
        m->cpu.address_hi = value;
        m->cpu.pc = m->cpu.address_16;
        break;
    case BRK:
        if (m->micro_phase == 1) {
            (void)read_operand(m, m->cpu.pc);
            CYCLE(m);
            m->cpu.pc++;
            m->micro_phase++;
            return false;
        }
        if (m->micro_phase == 2 || m->micro_phase == 3 || m->micro_phase == 4) {
            uint8_t pushed = m->micro_phase == 2 ? (uint8_t)(m->cpu.pc >> 8) :
                (m->micro_phase == 3 ? (uint8_t)m->cpu.pc : (uint8_t)(m->cpu.flags | 0x10u));
            write_stack(m, m->cpu.sp, pushed);
            CYCLE(m);
            if (--m->cpu.sp < 0x100u) {
                m->cpu.sp += 0x100u;
            }
            m->micro_phase++;
            return false;
        }
        if (m->micro_phase == 5) {
            m->cpu.address_lo = read_vector(m, 0xfffeu);
            CYCLE(m);
            m->micro_phase++;
            return false;
        }
        m->cpu.address_hi = read_vector(m, 0xffffu);
        CYCLE(m);
        m->cpu.pc = m->cpu.address_16;
        m->cpu.I = 1;
        break;
    case LDA_zpg:
    case LDX_zpg:
    case LDY_zpg:
    case STA_zpg:
    case STX_zpg:
    case STY_zpg:
        if (m->micro_phase == 1) {
            m->cpu.address_lo = read_operand(m, m->cpu.pc);
            m->cpu.address_hi = 0;
            CYCLE(m);
            m->cpu.pc++;
            m->micro_phase++;
            return false;
        }
        if (m->micro_opcode == LDA_zpg) {
            value = read_from_memory(m, m->cpu.address_16);
            CYCLE(m);
            set_register_to_value(m, &m->cpu.A, value);
        } else if (m->micro_opcode == LDX_zpg) {
            value = read_from_memory(m, m->cpu.address_16);
            CYCLE(m);
            set_register_to_value(m, &m->cpu.X, value);
        } else if (m->micro_opcode == LDY_zpg) {
            value = read_from_memory(m, m->cpu.address_16);
            CYCLE(m);
            set_register_to_value(m, &m->cpu.Y, value);
        } else if (m->micro_opcode == STA_zpg) {
            write_to_memory(m, m->cpu.address_16, m->cpu.A);
            CYCLE(m);
        } else if (m->micro_opcode == STX_zpg) {
            write_to_memory(m, m->cpu.address_16, m->cpu.X);
            CYCLE(m);
        } else {
            write_to_memory(m, m->cpu.address_16, m->cpu.Y);
            CYCLE(m);
        }
        break;
    case LDA_zpg_X:
    case LDX_zpg_Y:
    case LDY_zpg_X:
    case STA_zpg_X:
    case STX_zpg_Y:
    case STY_zpg_X:
        if (m->micro_phase == 1) {
            m->cpu.address_lo = read_operand(m, m->cpu.pc);
            m->cpu.address_hi = 0;
            CYCLE(m);
            m->cpu.pc++;
            m->micro_phase++;
            return false;
        }
        if (m->micro_phase == 2) {
            (void)read_dummy(m, m->cpu.address_16);
            m->cpu.address_lo = (uint8_t)(m->cpu.address_lo +
                ((m->micro_opcode == LDX_zpg_Y || m->micro_opcode == STX_zpg_Y) ?
                    m->cpu.Y : m->cpu.X));
            CYCLE(m);
            m->micro_phase++;
            return false;
        }
        if (m->micro_opcode == LDA_zpg_X) {
            value = read_from_memory(m, m->cpu.address_16);
            CYCLE(m);
            set_register_to_value(m, &m->cpu.A, value);
        } else if (m->micro_opcode == LDX_zpg_Y) {
            value = read_from_memory(m, m->cpu.address_16);
            CYCLE(m);
            set_register_to_value(m, &m->cpu.X, value);
        } else if (m->micro_opcode == LDY_zpg_X) {
            value = read_from_memory(m, m->cpu.address_16);
            CYCLE(m);
            set_register_to_value(m, &m->cpu.Y, value);
        } else if (m->micro_opcode == STA_zpg_X) {
            write_to_memory(m, m->cpu.address_16, m->cpu.A);
            CYCLE(m);
        } else if (m->micro_opcode == STX_zpg_Y) {
            write_to_memory(m, m->cpu.address_16, m->cpu.X);
            CYCLE(m);
        } else {
            write_to_memory(m, m->cpu.address_16, m->cpu.Y);
            CYCLE(m);
        }
        break;
    case ORA_zpg:
    case AND_zpg:
    case EOR_zpg:
    case ADC_zpg:
    case SBC_zpg:
    case CMP_zpg:
    case CPX_zpg:
    case CPY_zpg:
    case BIT_zpg:
        if (m->micro_phase == 1) {
            m->cpu.address_lo = read_operand(m, m->cpu.pc);
            m->cpu.address_hi = 0;
            CYCLE(m);
            m->cpu.pc++;
            m->micro_phase++;
            return false;
        }
        value = read_from_memory(m, m->cpu.address_16);
        CYCLE(m);
        cpu65_micro_apply_read_operation(m, value);
        break;
    case ORA_zpg_X:
    case AND_zpg_X:
    case EOR_zpg_X:
    case ADC_zpg_X:
    case SBC_zpg_X:
    case CMP_zpg_X:
        if (m->micro_phase == 1) {
            m->cpu.address_lo = read_operand(m, m->cpu.pc);
            m->cpu.address_hi = 0;
            CYCLE(m);
            m->cpu.pc++;
            m->micro_phase++;
            return false;
        }
        if (m->micro_phase == 2) {
            (void)read_dummy(m, m->cpu.address_16);
            m->cpu.address_lo = (uint8_t)(m->cpu.address_lo + m->cpu.X);
            CYCLE(m);
            m->micro_phase++;
            return false;
        }
        value = read_from_memory(m, m->cpu.address_16);
        CYCLE(m);
        cpu65_micro_apply_read_operation(m, value);
        break;
    case BIT_abs:
    case ORA_abs:
    case AND_abs:
    case EOR_abs:
    case ADC_abs:
    case SBC_abs:
    case CMP_abs:
    case CPX_abs:
    case CPY_abs:
        if (m->micro_phase == 1) {
            m->cpu.address_lo = read_operand(m, m->cpu.pc);
            CYCLE(m);
            m->cpu.pc++;
            m->micro_phase++;
            return false;
        }
        if (m->micro_phase == 2) {
            m->cpu.address_hi = read_operand(m, m->cpu.pc);
            CYCLE(m);
            m->cpu.pc++;
            m->micro_phase++;
            return false;
        }
        value = read_from_memory(m, m->cpu.address_16);
        CYCLE(m);
        cpu65_micro_apply_read_operation(m, value);
        break;
    case LDA_abs_X:
    case LDA_abs_Y:
    case LDX_abs_Y:
    case LDY_abs_X:
    case STA_abs_X:
    case STA_abs_Y:
    case ORA_abs_X:
    case ORA_abs_Y:
    case AND_abs_X:
    case AND_abs_Y:
    case EOR_abs_X:
    case EOR_abs_Y:
    case ADC_abs_X:
    case ADC_abs_Y:
    case SBC_abs_X:
    case SBC_abs_Y:
    case CMP_abs_X:
    case CMP_abs_Y:
        if (m->micro_phase == 1) {
            uint8_t index;

            m->cpu.address_lo = read_operand(m, m->cpu.pc);
            CYCLE(m);
            m->cpu.pc++;
            index = cpu65_micro_uses_y_index(m->micro_opcode) ? m->cpu.Y : m->cpu.X;
            m->micro_branch_taken = (uint16_t)m->cpu.address_lo + index > 0xffu;
            m->micro_phase++;
            return false;
        }
        if (m->micro_phase == 2) {
            uint16_t base;
            uint8_t index;

            m->cpu.address_hi = read_operand(m, m->cpu.pc);
            CYCLE(m);
            m->cpu.pc++;
            base = m->cpu.address_16;
            index = cpu65_micro_uses_y_index(m->micro_opcode) ? m->cpu.Y : m->cpu.X;
            m->micro_target = (uint16_t)(base + index);
            m->micro_phase++;
            return false;
        }
        if (m->micro_phase == 3 &&
            (m->micro_branch_taken || m->micro_opcode == STA_abs_X || m->micro_opcode == STA_abs_Y)) {
            (void)read_dummy(m, (uint16_t)((m->cpu.address_16 & 0xff00u) |
                                            (m->micro_target & 0x00ffu)));
            CYCLE(m);
            m->micro_phase++;
            return false;
        }
        if (m->micro_opcode == ORA_abs_X || m->micro_opcode == ORA_abs_Y ||
            m->micro_opcode == AND_abs_X || m->micro_opcode == AND_abs_Y ||
            m->micro_opcode == EOR_abs_X || m->micro_opcode == EOR_abs_Y ||
            m->micro_opcode == ADC_abs_X || m->micro_opcode == ADC_abs_Y ||
            m->micro_opcode == SBC_abs_X || m->micro_opcode == SBC_abs_Y ||
            m->micro_opcode == CMP_abs_X || m->micro_opcode == CMP_abs_Y) {
            value = read_from_memory(m, m->micro_target);
            CYCLE(m);
            cpu65_micro_apply_read_operation(m, value);
        } else if (m->micro_opcode == LDA_abs_X || m->micro_opcode == LDA_abs_Y) {
            value = read_from_memory(m, m->micro_target);
            CYCLE(m);
            set_register_to_value(m, &m->cpu.A, value);
        } else if (m->micro_opcode == LDX_abs_Y) {
            value = read_from_memory(m, m->micro_target);
            CYCLE(m);
            set_register_to_value(m, &m->cpu.X, value);
        } else if (m->micro_opcode == LDY_abs_X) {
            value = read_from_memory(m, m->micro_target);
            CYCLE(m);
            set_register_to_value(m, &m->cpu.Y, value);
        } else {
            write_to_memory(m, m->micro_target, m->cpu.A);
            CYCLE(m);
        }
        break;
    case LDA_X_ind:
    case STA_X_ind:
    case ORA_X_ind:
    case AND_X_ind:
    case EOR_X_ind:
    case ADC_X_ind:
    case SBC_X_ind:
    case CMP_X_ind:
        if (m->micro_phase == 1) {
            m->cpu.scratch_lo = read_operand(m, m->cpu.pc);
            CYCLE(m);
            m->cpu.pc++;
            m->micro_phase++;
            return false;
        }
        if (m->micro_phase == 2) {
            (void)read_dummy(m, m->cpu.scratch_lo);
            m->micro_target = (uint8_t)(m->cpu.scratch_lo + m->cpu.X);
            CYCLE(m);
            m->micro_phase++;
            return false;
        }
        if (m->micro_phase == 3) {
            m->cpu.address_lo = read_from_memory(m, m->micro_target);
            CYCLE(m);
            m->micro_phase++;
            return false;
        }
        if (m->micro_phase == 4) {
            m->cpu.address_hi = read_from_memory(m, (uint8_t)(m->micro_target + 1u));
            CYCLE(m);
            m->micro_phase++;
            return false;
        }
        if (m->micro_opcode == STA_X_ind) {
            write_to_memory(m, m->cpu.address_16, m->cpu.A);
            CYCLE(m);
        } else {
            value = read_from_memory(m, m->cpu.address_16);
            CYCLE(m);
            if (m->micro_opcode == LDA_X_ind) {
                set_register_to_value(m, &m->cpu.A, value);
            } else {
                cpu65_micro_apply_read_operation(m, value);
            }
        }
        break;
    case LDA_ind_Y:
    case STA_ind_Y:
    case ORA_ind_Y:
    case AND_ind_Y:
    case EOR_ind_Y:
    case ADC_ind_Y:
    case SBC_ind_Y:
    case CMP_ind_Y:
        if (m->micro_phase == 1) {
            m->cpu.scratch_lo = read_operand(m, m->cpu.pc);
            CYCLE(m);
            m->cpu.pc++;
            m->micro_phase++;
            return false;
        }
        if (m->micro_phase == 2) {
            m->cpu.address_lo = read_from_memory(m, m->cpu.scratch_lo);
            CYCLE(m);
            m->micro_branch_taken = (uint16_t)m->cpu.address_lo + m->cpu.Y > 0xffu;
            m->micro_phase++;
            return false;
        }
        if (m->micro_phase == 3) {
            uint16_t base;

            m->cpu.address_hi = read_from_memory(m, (uint8_t)(m->cpu.scratch_lo + 1u));
            CYCLE(m);
            base = m->cpu.address_16;
            m->micro_target = (uint16_t)(base + m->cpu.Y);
            m->micro_phase++;
            return false;
        }
        if (m->micro_phase == 4 &&
            (m->micro_branch_taken || m->micro_opcode == STA_ind_Y)) {
            (void)read_dummy(m, (uint16_t)((m->cpu.address_16 & 0xff00u) |
                                            (m->micro_target & 0x00ffu)));
            CYCLE(m);
            m->micro_phase++;
            return false;
        }
        if (m->micro_opcode == STA_ind_Y) {
            write_to_memory(m, m->micro_target, m->cpu.A);
            CYCLE(m);
        } else {
            value = read_from_memory(m, m->micro_target);
            CYCLE(m);
            if (m->micro_opcode == LDA_ind_Y) {
                set_register_to_value(m, &m->cpu.A, value);
            } else {
                cpu65_micro_apply_read_operation(m, value);
            }
        }
        break;
    case ASL_zpg:
    case ROL_zpg:
    case LSR_zpg:
    case ROR_zpg:
    case DEC_zpg:
    case INC_zpg:
        if (m->micro_phase == 1) {
            m->cpu.address_lo = read_operand(m, m->cpu.pc);
            m->cpu.address_hi = 0;
            CYCLE(m);
            m->cpu.pc++;
            m->micro_phase++;
            return false;
        }
        if (m->micro_phase == 2) {
            m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
            CYCLE(m);
            m->micro_phase++;
            return false;
        }
        if (m->micro_phase == 3) {
            m->bus_access_kind = CPU65_BUS_ACCESS_RMW_DUMMY_WRITE;
            m->write(m->user, m->cpu.address_16, m->cpu.scratch_lo);
            CYCLE(m);
            m->micro_phase++;
            return false;
        }
        write_to_memory(m, m->cpu.address_16, cpu65_micro_rmw_value(m));
        CYCLE(m);
        break;
    case ASL_zpg_X:
    case ROL_zpg_X:
    case LSR_zpg_X:
    case ROR_zpg_X:
    case DEC_zpg_X:
    case INC_zpg_X:
        if (m->micro_phase == 1) {
            m->cpu.address_lo = read_operand(m, m->cpu.pc);
            m->cpu.address_hi = 0;
            CYCLE(m);
            m->cpu.pc++;
            m->micro_phase++;
            return false;
        }
        if (m->micro_phase == 2) {
            (void)read_dummy(m, m->cpu.address_16);
            m->cpu.address_lo = (uint8_t)(m->cpu.address_lo + m->cpu.X);
            CYCLE(m);
            m->micro_phase++;
            return false;
        }
        if (m->micro_phase == 3) {
            m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
            CYCLE(m);
            m->micro_phase++;
            return false;
        }
        if (m->micro_phase == 4) {
            m->bus_access_kind = CPU65_BUS_ACCESS_RMW_DUMMY_WRITE;
            m->write(m->user, m->cpu.address_16, m->cpu.scratch_lo);
            CYCLE(m);
            m->micro_phase++;
            return false;
        }
        write_to_memory(m, m->cpu.address_16, cpu65_micro_rmw_value(m));
        CYCLE(m);
        break;
    case ASL_abs:
    case ROL_abs:
    case LSR_abs:
    case ROR_abs:
    case DEC_abs:
    case INC_abs:
        if (m->micro_phase == 1) {
            m->cpu.address_lo = read_operand(m, m->cpu.pc);
            CYCLE(m);
            m->cpu.pc++;
            m->micro_phase++;
            return false;
        }
        if (m->micro_phase == 2) {
            m->cpu.address_hi = read_operand(m, m->cpu.pc);
            CYCLE(m);
            m->cpu.pc++;
            m->micro_phase++;
            return false;
        }
        if (m->micro_phase == 3) {
            m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
            CYCLE(m);
            m->micro_phase++;
            return false;
        }
        if (m->micro_phase == 4) {
            m->bus_access_kind = CPU65_BUS_ACCESS_RMW_DUMMY_WRITE;
            m->write(m->user, m->cpu.address_16, m->cpu.scratch_lo);
            CYCLE(m);
            m->micro_phase++;
            return false;
        }
        write_to_memory(m, m->cpu.address_16, cpu65_micro_rmw_value(m));
        CYCLE(m);
        break;
    case ASL_abs_X:
    case ROL_abs_X:
    case LSR_abs_X:
    case ROR_abs_X:
    case DEC_abs_X:
    case INC_abs_X:
        if (m->micro_phase == 1) {
            m->cpu.address_lo = read_operand(m, m->cpu.pc);
            CYCLE(m);
            m->cpu.pc++;
            m->micro_phase++;
            return false;
        }
        if (m->micro_phase == 2) {
            m->cpu.address_hi = read_operand(m, m->cpu.pc);
            CYCLE(m);
            m->cpu.pc++;
            m->micro_target = (uint16_t)(m->cpu.address_16 + m->cpu.X);
            m->micro_phase++;
            return false;
        }
        if (m->micro_phase == 3) {
            (void)read_dummy(m, (uint16_t)((m->cpu.address_16 & 0xff00u) |
                                            (m->micro_target & 0x00ffu)));
            CYCLE(m);
            m->micro_phase++;
            return false;
        }
        if (m->micro_phase == 4) {
            m->cpu.scratch_lo = read_from_memory(m, m->micro_target);
            CYCLE(m);
            m->micro_phase++;
            return false;
        }
        if (m->micro_phase == 5) {
            m->bus_access_kind = CPU65_BUS_ACCESS_RMW_DUMMY_WRITE;
            m->write(m->user, m->micro_target, m->cpu.scratch_lo);
            CYCLE(m);
            m->micro_phase++;
            return false;
        }
        write_to_memory(m, m->micro_target, cpu65_micro_rmw_value(m));
        CYCLE(m);
        break;
    default:
        assert(!"unsupported resumable 6510 opcode");
        break;
    }

micro_complete:
    m->cpu.opcode_active = 0;
    m->micro_active = 0;
    m->micro_phase = 0;
    return true;
}

size_t cpu65_micro_cycles_remaining(const cpu65_t *m) {
    assert(m);

    if (!m->micro_active) {
        return 0;
    }
    if (m->micro_is_interrupt) {
        return (size_t)(7u - m->micro_phase);
    }
    if (m->micro_phase == 0) {
        return 1;
    }
    if (cpu65_micro_is_practical_undocumented(m->micro_opcode)) {
        cpu65_undocumented_mode mode =
            cpu65_micro_undocumented_mode(m->micro_opcode);
        bool rmw = cpu65_micro_is_undocumented_rmw(m->micro_opcode);
        bool lax = cpu65_micro_is_lax(m->micro_opcode);
        size_t total_cycles;

        switch (mode) {
        case CPU65_UND_MODE_ZP: total_cycles = rmw ? 5u : 3u; break;
        case CPU65_UND_MODE_ZP_INDEXED: total_cycles = rmw ? 6u : 4u; break;
        case CPU65_UND_MODE_ABS: total_cycles = rmw ? 6u : 4u; break;
        case CPU65_UND_MODE_X_IND: total_cycles = rmw ? 8u : 6u; break;
        case CPU65_UND_MODE_IND_Y:
            total_cycles = rmw ? 8u : (lax && !m->micro_branch_taken ? 5u : 6u);
            break;
        case CPU65_UND_MODE_ABS_Y:
            total_cycles = rmw ? 7u : (lax && !m->micro_branch_taken ? 4u : 5u);
            break;
        default: total_cycles = 7u; break;
        }
        return total_cycles > m->micro_phase ? total_cycles - m->micro_phase : 0;
    }
    if (cpu65_micro_is_undocumented_nop(m->micro_opcode)) {
        if (m->micro_opcode == UND_80) return 1;
        if (m->micro_opcode == UND_44) return (size_t)(3u - m->micro_phase);
        return (size_t)((m->micro_branch_taken ? 5u : 4u) - m->micro_phase);
    }

    switch (m->micro_opcode) {
    case NOP:
    case LDA_imm:
    case LDX_imm:
    case LDY_imm:
        return 1;
    case LDA_abs:
    case LDX_abs:
    case LDY_abs:
    case STA_abs:
    case STX_abs:
    case STY_abs:
        return (size_t)(4u - m->micro_phase);
    case JMP_abs:
        return (size_t)(3u - m->micro_phase);
    case JMP_ind:
        return (size_t)(5u - m->micro_phase);
    case BPL_rel:
    case BMI_rel:
    case BVC_rel:
    case BVS_rel:
    case BCC_rel:
    case BCS_rel:
    case BNE_rel:
    case BEQ_rel:
        return (size_t)(4u - m->micro_phase);
    case JSR_abs:
        return (size_t)(6u - m->micro_phase);
    case RTS:
        return (size_t)(6u - m->micro_phase);
    case ORA_imm:
    case AND_imm:
    case EOR_imm:
    case ADC_imm:
    case SBC_imm:
    case CMP_imm:
    case CPX_imm:
    case CPY_imm:
    case CLC:
    case SEC:
    case CLI:
    case SEI:
    case CLD:
    case SED:
    case CLV:
    case INX:
    case INY:
    case DEX:
    case DEY:
    case TAX:
    case TAY:
    case TXA:
    case TYA:
    case TSX:
    case TXS:
    case ASL_A:
    case ROL_A:
    case LSR_A:
    case ROR_A:
        return 1;
    case PHA:
    case PHP:
        return (size_t)(3u - m->micro_phase);
    case PLA:
    case PLP:
        return (size_t)(4u - m->micro_phase);
    case RTI:
        return (size_t)(6u - m->micro_phase);
    case BRK:
        return (size_t)(7u - m->micro_phase);
    case LDA_zpg:
    case LDX_zpg:
    case LDY_zpg:
    case STA_zpg:
    case STX_zpg:
    case STY_zpg:
        return (size_t)(3u - m->micro_phase);
    case ORA_zpg:
    case AND_zpg:
    case EOR_zpg:
    case ADC_zpg:
    case SBC_zpg:
    case CMP_zpg:
    case CPX_zpg:
    case CPY_zpg:
    case BIT_zpg:
        return (size_t)(3u - m->micro_phase);
    case BIT_abs:
    case ORA_abs:
    case AND_abs:
    case EOR_abs:
    case ADC_abs:
    case SBC_abs:
    case CMP_abs:
    case CPX_abs:
    case CPY_abs:
        return (size_t)(4u - m->micro_phase);
    case LDA_zpg_X:
    case LDX_zpg_Y:
    case LDY_zpg_X:
    case STA_zpg_X:
    case STX_zpg_Y:
    case STY_zpg_X:
    case ORA_zpg_X:
    case AND_zpg_X:
    case EOR_zpg_X:
    case ADC_zpg_X:
    case SBC_zpg_X:
    case CMP_zpg_X:
        return (size_t)(4u - m->micro_phase);
    case LDA_X_ind:
    case STA_X_ind:
    case ORA_X_ind:
    case AND_X_ind:
    case EOR_X_ind:
    case ADC_X_ind:
    case SBC_X_ind:
    case CMP_X_ind:
        return (size_t)(6u - m->micro_phase);
    case LDA_ind_Y:
    case ORA_ind_Y:
    case AND_ind_Y:
    case EOR_ind_Y:
    case ADC_ind_Y:
    case SBC_ind_Y:
    case CMP_ind_Y:
        if (m->micro_phase == 1) {
            return 4u;
        }
        return m->micro_branch_taken ? (size_t)(6u - m->micro_phase) :
            (size_t)(5u - m->micro_phase);
    case STA_ind_Y:
        return (size_t)(6u - m->micro_phase);
    case LDA_abs_X:
    case LDA_abs_Y:
    case LDX_abs_Y:
    case LDY_abs_X:
    case ORA_abs_X:
    case ORA_abs_Y:
    case AND_abs_X:
    case AND_abs_Y:
    case EOR_abs_X:
    case EOR_abs_Y:
    case ADC_abs_X:
    case ADC_abs_Y:
    case SBC_abs_X:
    case SBC_abs_Y:
    case CMP_abs_X:
    case CMP_abs_Y:
        return m->micro_branch_taken ? (size_t)(5u - m->micro_phase) :
            (size_t)(4u - m->micro_phase);
    case STA_abs_X:
    case STA_abs_Y:
        return (size_t)(5u - m->micro_phase);
    case ASL_zpg:
    case ROL_zpg:
    case LSR_zpg:
    case ROR_zpg:
    case DEC_zpg:
    case INC_zpg:
        return (size_t)(5u - m->micro_phase);
    case ASL_abs:
    case ROL_abs:
    case LSR_abs:
    case ROR_abs:
    case DEC_abs:
    case INC_abs:
        return (size_t)(6u - m->micro_phase);
    case ASL_zpg_X:
    case ROL_zpg_X:
    case LSR_zpg_X:
    case ROR_zpg_X:
    case DEC_zpg_X:
    case INC_zpg_X:
        return (size_t)(6u - m->micro_phase);
    case ASL_abs_X:
    case ROL_abs_X:
    case LSR_abs_X:
    case ROR_abs_X:
    case DEC_abs_X:
    case INC_abs_X:
        return (size_t)(7u - m->micro_phase);
    default:
        return 0;
    }
}
