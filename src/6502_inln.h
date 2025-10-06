// Apple ][+ emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#pragma once

#include <assert.h>

#define CYCLE(m)     m->cpu.cycles++

// Memory access
static inline uint8_t read_from_memory(APPLE2 *m, uint16_t address) {
    assert(address / PAGE_SIZE < m->read_pages.num_pages);
    uint8_t cb_mask = m->watch_pages.pages[address / PAGE_SIZE].bytes[address % PAGE_SIZE];
    if(cb_mask) {
        if(cb_mask & 2) {
            m->callback_breakpoint(m, address);
        }
        if(cb_mask & 1) {
            return m->callback_read(m, address);
        }
    }
    return m->read_pages.pages[address / PAGE_SIZE].bytes[address % PAGE_SIZE];
}

static inline uint8_t read_from_memory_debug(APPLE2 *m, uint16_t address) {
    assert(address / PAGE_SIZE < m->read_pages.num_pages);
    return m->read_pages.pages[address / PAGE_SIZE].bytes[address % PAGE_SIZE];
}

static inline void write_to_memory(APPLE2 *m, uint16_t address, uint8_t value) {
    uint16_t page = address / PAGE_SIZE;
    uint16_t offset = address % PAGE_SIZE;
    assert(page < m->write_pages.num_pages);
    uint8_t cb_mask = m->watch_pages.pages[page].bytes[offset];
    if(cb_mask) {
        if(cb_mask & 1) {
            m->callback_write(m, address, value);
        }
        if(cb_mask & 4) {
            m->callback_breakpoint(m, address);
        }
    }
    if(!(cb_mask & 1)) {
        m->write_pages.pages[page].bytes[offset] = value;
    }
}

// Setters
static inline void set_register_to_value(APPLE2 *m, uint8_t *reg, uint8_t value) {
    *reg = value;
    m->cpu.N = *reg & 0x80 ? 1 : 0;
    m->cpu.Z = *reg ? 0 : 1;
}

// Helper Functions
static inline void add_value_to_accumulator(APPLE2 *m, uint8_t value) {
    uint8_t a = m->cpu.A;
    m->cpu.scratch_16 = m->cpu.A + value + m->cpu.C;
    set_register_to_value(m, &m->cpu.A, m->cpu.scratch_lo);
    m->cpu.scratch_lo = (a & 0x0F) + (value & 0x0F) + m->cpu.C;
    m->cpu.V = ((a ^ m->cpu.A) & ~(a ^ value) & 0x80) != 0 ? 1 : 0;
    m->cpu.C = m->cpu.scratch_hi;
    if(m->cpu.D) {
        m->cpu.scratch_hi = (a >> 4) + (value >> 4);
        if (m->cpu.scratch_lo > 9) {
            m->cpu.scratch_lo += 6;
            m->cpu.scratch_hi++;
        }
        if (m->cpu.scratch_hi > 9) {
            m->cpu.scratch_hi += 6;
            m->cpu.C = 1;
        }
        m->cpu.A = (m->cpu.scratch_hi << 4) | (m->cpu.scratch_lo & 0x0F);
    }
}

static inline void compare_bytes(APPLE2 *m, uint8_t lhs, uint8_t rhs) {
    m->cpu.Z = (lhs == rhs) ? 1 : 0;
    m->cpu.C = (lhs >= rhs) ? 1 : 0;
    m->cpu.N = ((lhs - rhs) & 0x80) ? 1 : 0;
}

static inline uint8_t pull(APPLE2 *m) {
    if(++m->cpu.sp >= 0x200) {
        m->cpu.sp = 0x100;
    }
    return read_from_memory(m, m->cpu.sp);
}

static inline void push(APPLE2 *m, uint8_t value) {
    write_to_memory(m, m->cpu.sp, value);
    if(--m->cpu.sp < 0x100) {
        m->cpu.sp += 0x100;
    }
}

static inline void subtract_value_from_accumulator(APPLE2 *m, uint8_t value) {
    uint8_t a = m->cpu.A;
    m->cpu.C = 1 - m->cpu.C;
    m->cpu.scratch_16 = a - value - m->cpu.C;
    set_register_to_value(m, &m->cpu.A, m->cpu.scratch_lo);
    m->cpu.V = ((a ^ value) & (a ^ m->cpu.A) & 0x80) != 0 ? 1 : 0;
    if(m->cpu.D) {
        uint8_t lo = (a & 0x0F) - (value & 0x0F) - m->cpu.C;
        uint8_t hi = (a >> 4) - (value >> 4);
        if (lo & 0x10) {
            lo -= 6;
            hi--;
        }
        if (hi & 0xF0) {
            hi -= 6;
        }
        m->cpu.A = (hi << 4) | (lo & 0x0F);
    }
    m->cpu.C = m->cpu.scratch_16 < 0x100 ? 1 : 0;
}

// Stage Helpers
static inline void ah_from_stack(APPLE2 *m) {
    m->cpu.address_hi = pull(m);
    CYCLE(m);
}

static inline void ah_read_a16_sl2al(APPLE2 *m) {
    m->cpu.address_lo++;
    m->cpu.address_hi = read_from_memory(m, m->cpu.address_16);
    m->cpu.address_lo = m->cpu.scratch_lo;
    CYCLE(m);
}

static inline void ah_read_pc(APPLE2 *m) {
    m->cpu.address_hi = read_from_memory(m, m->cpu.pc);
    m->cpu.pc++;
    CYCLE(m);
}

static inline void al_from_stack(APPLE2 *m) {
    m->cpu.address_lo = pull(m);
    CYCLE(m);
}

static inline void al_read_pc(APPLE2 *m) {
    m->cpu.address_lo = read_from_memory(m, m->cpu.pc);
    m->cpu.address_hi = 0;
    m->cpu.pc++;
    CYCLE(m);
}

static inline void branch(APPLE2 *m) {
    read_from_memory(m, m->cpu.address_16);
    CYCLE(m);
    uint8_t lo = m->cpu.address_lo;
    m->cpu.address_lo += m->cpu.scratch_lo;
    if((lo + (int8_t)m->cpu.scratch_lo) & 0x100) {
        read_from_memory(m, m->cpu.address_16);
        CYCLE(m);
    }
    m->cpu.pc += (int8_t)m->cpu.scratch_lo;
}

static inline void p_from_stack(APPLE2 *m) {
    m->cpu.flags = (pull(m) & ~0b00010000) | 0b00100000;
    CYCLE(m);
}

static inline void pc_hi_to_stack(APPLE2 *m) {
    push(m, (m->cpu.pc >> 8) & 0xFF);
    CYCLE(m);
}

static inline void pc_lo_to_stack(APPLE2 *m) {
    push(m, m->cpu.pc & 0xFF);
    CYCLE(m);
}

static inline void read_a16_ind_x(APPLE2 *m) {
    read_from_memory(m, m->cpu.address_16);
    m->cpu.address_lo += m->cpu.X;
    CYCLE(m);
}

static inline void read_a16_ind_y(APPLE2 *m) {
    read_from_memory(m, m->cpu.address_16);
    m->cpu.address_lo += m->cpu.Y;
    CYCLE(m);
}

static inline void read_sp(APPLE2 *m) {
    read_from_memory(m, m->cpu.sp);
    CYCLE(m);
}

static inline void sl_read_a16(APPLE2 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
    CYCLE(m);
}

static inline void sl_write_a16(APPLE2 *m) {
    write_to_memory(m, m->cpu.address_16, m->cpu.scratch_lo);
    CYCLE(m);
}

// pipelines
static inline void a(APPLE2 *m) {
    al_read_pc(m);
    ah_read_pc(m);
}

static inline void ar(APPLE2 *m) {
    al_read_pc(m);
    ah_read_pc(m);
    sl_read_a16(m);
}

static inline void arw(APPLE2 *m) {
    al_read_pc(m);
    ah_read_pc(m);
    sl_read_a16(m);
    sl_write_a16(m);
}

static inline void aix(APPLE2 *m) {
    a(m);
    uint8_t lo = m->cpu.address_lo;
    m->cpu.address_lo += m->cpu.X;
    if(m->cpu.address_lo < lo) {
        if(m->cpu.class == CPU_6502) {
            read_from_memory(m, m->cpu.address_16);
        } else {
            read_from_memory(m, m->cpu.pc-1);
        }
        m->cpu.address_hi++;
        CYCLE(m);
    }
}

static inline void aixr(APPLE2 *m) {
    a(m);
    uint8_t lo = m->cpu.address_lo;
    m->cpu.address_lo += m->cpu.X;
    if(m->cpu.class == CPU_6502) {
        read_from_memory(m, m->cpu.address_16);
    } else {
        read_from_memory(m, m->cpu.pc-1);
    }
    CYCLE(m);
    if(m->cpu.address_lo < lo) {
        m->cpu.address_hi++;
    }
}

static inline void aipxrw(APPLE2 *m) {
    aixr(m);
    sl_read_a16(m);
    sl_write_a16(m);
}

static inline void aiy(APPLE2 *m) {
    a(m);
    uint8_t lo = m->cpu.address_lo;
    m->cpu.address_lo += m->cpu.Y;
    if(m->cpu.address_lo < lo) {
        if(m->cpu.class == CPU_6502) {
            read_from_memory(m, m->cpu.address_16);
        } else {
            read_from_memory(m, m->cpu.pc-1);
        }
        m->cpu.address_hi++;
        CYCLE(m);
    }
}

static inline void aiyr(APPLE2 *m) {
    a(m);
    uint8_t lo = m->cpu.address_lo;
    m->cpu.address_lo += m->cpu.Y;
    if(m->cpu.class == CPU_6502) {
        read_from_memory(m, m->cpu.address_16);
    } else {
        read_from_memory(m, m->cpu.pc-1);
    }
    CYCLE(m);
    if(m->cpu.address_lo < lo) {
        m->cpu.address_hi++;
    }
}

static inline void mix(APPLE2 *m) {
    al_read_pc(m);
    read_a16_ind_x(m);
}

static inline void mixa(APPLE2 *m) {
    mix(m);
    sl_read_a16(m);
    ah_read_a16_sl2al(m);
}

static inline void miy(APPLE2 *m) {
    al_read_pc(m);
    sl_read_a16(m);
    ah_read_a16_sl2al(m);
    uint8_t lo = m->cpu.address_lo;
    m->cpu.address_lo += m->cpu.Y;
    if(m->cpu.address_lo < lo) {
        if(m->cpu.class == CPU_6502) {
            read_from_memory(m, m->cpu.address_16);
        } else {
            read_from_memory(m, m->cpu.pc-1);
        }
        m->cpu.address_hi++;
        CYCLE(m);
    }
}

static inline void miyr(APPLE2 *m) {
    al_read_pc(m);
    sl_read_a16(m);
    ah_read_a16_sl2al(m);
    uint8_t lo = m->cpu.address_lo;
    m->cpu.address_lo += m->cpu.Y;
    if(m->cpu.class == CPU_6502) {
        read_from_memory(m, m->cpu.address_16);
    } else {
        read_from_memory(m, m->cpu.pc-1);
    }
    CYCLE(m);
    if(m->cpu.address_lo < lo) {
        m->cpu.address_hi++;
    }
}

static inline void mizy(APPLE2 *m) {
    al_read_pc(m);
    read_a16_ind_y(m);
}

static inline void mrw(APPLE2 *m) {
    al_read_pc(m);
    sl_read_a16(m);
    sl_write_a16(m);
}

static inline void mixrw(APPLE2 *m) {
    mix(m);
    sl_read_a16(m);
    sl_write_a16(m);
}

// Instructions
static inline void adc_a16(APPLE2 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
    add_value_to_accumulator(m, m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void adc_imm(APPLE2 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    add_value_to_accumulator(m, m->cpu.scratch_lo);
    m->cpu.pc++;
    CYCLE(m);
}

static inline void and_a16(APPLE2 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
    set_register_to_value(m, &m->cpu.A, m->cpu.A & m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void and_imm(APPLE2 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    set_register_to_value(m, &m->cpu.A, m->cpu.A & m->cpu.scratch_lo);
    m->cpu.pc++;
    CYCLE(m);
}

static inline void asl_a(APPLE2 *m) {
    read_from_memory(m, m->cpu.pc);
    m->cpu.C = m->cpu.A & 0x80 ? 1 : 0;
    set_register_to_value(m, &m->cpu.A, m->cpu.A <<= 1);
    CYCLE(m);
}

static inline void asl_a16(APPLE2 *m) {
    m->cpu.C = m->cpu.scratch_lo & 0x80 ? 1 : 0;
    set_register_to_value(m, &m->cpu.scratch_hi, m->cpu.scratch_lo << 1);
    write_to_memory(m, m->cpu.address_16, m->cpu.scratch_hi);
    CYCLE(m);
}

static inline void bcc(APPLE2 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    CYCLE(m);
    m->cpu.address_16 = ++m->cpu.pc;
    if(!m->cpu.C) {
        branch(m);
    }
}

static inline void bcs(APPLE2 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    CYCLE(m);
    m->cpu.address_16 = ++m->cpu.pc;
    if(m->cpu.C) {
        branch(m);
    }
}

static inline void beq(APPLE2 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    CYCLE(m);
    m->cpu.address_16 = ++m->cpu.pc;
    if(m->cpu.Z) {
        branch(m);
    }
}

static inline void bit_a16(APPLE2 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
    set_register_to_value(m, &m->cpu.scratch_hi, m->cpu.A & m->cpu.scratch_lo);
    m->cpu.flags &= 0b00111111;
    m->cpu.flags |= (m->cpu.scratch_lo & 0b11000000);
    CYCLE(m);
}

static inline void bmi(APPLE2 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    CYCLE(m);
    m->cpu.address_16 = ++m->cpu.pc;
    if(m->cpu.N) {
        branch(m);
    }
}

static inline void bne(APPLE2 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    CYCLE(m);
    m->cpu.address_16 = ++m->cpu.pc;
    if(!m->cpu.Z) {
        branch(m);
    }
}

static inline void bpl(APPLE2 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    CYCLE(m);
    m->cpu.address_16 = ++m->cpu.pc;
    if(!m->cpu.N) {
        branch(m);
    }
}

static inline void bvc(APPLE2 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    CYCLE(m);
    m->cpu.address_16 = ++m->cpu.pc;
    if(!m->cpu.V) {
        branch(m);
    }
}

static inline void bvs(APPLE2 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    CYCLE(m);
    m->cpu.address_16 = ++m->cpu.pc;
    if(m->cpu.V) {
        branch(m);
    }
}

static inline void brk_6502(APPLE2 *m) {
    m->cpu.pc = 0xFFFE;
    a(m);
    m->cpu.pc = m->cpu.address_16;
    // Interrupt flag on at break
    m->cpu.flags |= 0b00000100;
    CYCLE(m);
}

static inline void clc(APPLE2 *m) {
    read_from_memory(m, m->cpu.pc);
    m->cpu.C = 0;
    CYCLE(m);
}

static inline void cld(APPLE2 *m) {
    read_from_memory(m, m->cpu.pc);
    m->cpu.D = 0;
    CYCLE(m);
}

static inline void cli(APPLE2 *m) {
    read_from_memory(m, m->cpu.pc);
    m->cpu.I = 0;
    CYCLE(m);
}

static inline void clv(APPLE2 *m) {
    read_from_memory(m, m->cpu.pc);
    m->cpu.V = 0;
    CYCLE(m);
}

static inline void cmp_a16(APPLE2 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
    compare_bytes(m, m->cpu.A, m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void cmp_imm(APPLE2 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    compare_bytes(m, m->cpu.A, m->cpu.scratch_lo);
    m->cpu.pc++;
    CYCLE(m);
}

static inline void cpx_a16(APPLE2 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
    compare_bytes(m, m->cpu.X, m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void cpx_imm(APPLE2 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    compare_bytes(m, m->cpu.X, m->cpu.scratch_lo);
    m->cpu.pc++;
    CYCLE(m);
}

static inline void cpy_a16(APPLE2 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
    compare_bytes(m, m->cpu.Y, m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void cpy_imm(APPLE2 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    compare_bytes(m, m->cpu.Y, m->cpu.scratch_lo);
    m->cpu.pc++;
    CYCLE(m);
}

static inline void dec_a16(APPLE2 *m) {
    set_register_to_value(m, &m->cpu.scratch_hi, m->cpu.scratch_lo - 1);
    write_to_memory(m, m->cpu.address_16, m->cpu.scratch_hi);
    CYCLE(m);
}

static inline void dex(APPLE2 *m) {
    read_from_memory(m, m->cpu.pc);
    set_register_to_value(m, &m->cpu.X, m->cpu.X - 1);
    CYCLE(m);
}

static inline void dey(APPLE2 *m) {
    read_from_memory(m, m->cpu.pc);
    set_register_to_value(m, &m->cpu.Y, m->cpu.Y - 1);
    CYCLE(m);
}

static inline void eor_a16(APPLE2 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
    set_register_to_value(m, &m->cpu.A, m->cpu.A ^ m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void eor_imm(APPLE2 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    set_register_to_value(m, &m->cpu.A, m->cpu.A ^ m->cpu.scratch_lo);
    m->cpu.pc++;
    CYCLE(m);
}

static inline void inc_a16(APPLE2 *m) {
    set_register_to_value(m, &m->cpu.scratch_hi, m->cpu.scratch_lo + 1);
    write_to_memory(m, m->cpu.address_16, m->cpu.scratch_hi);
    CYCLE(m);
}

static inline void inx(APPLE2 *m) {
    read_from_memory(m, m->cpu.pc);
    set_register_to_value(m, &m->cpu.X, m->cpu.X + 1);
    CYCLE(m);
}

static inline void iny(APPLE2 *m) {
    read_from_memory(m, m->cpu.pc);
    set_register_to_value(m, &m->cpu.Y, m->cpu.Y + 1);
    CYCLE(m);
}

static inline void jmp_a16(APPLE2 *m) {
    m->cpu.pc = m->cpu.address_16;
    CYCLE(m);
}

static inline void jmp_ind(APPLE2 *m) {
    ah_read_a16_sl2al(m);
    m->cpu.pc = m->cpu.address_16;
    CYCLE(m);
}

static inline void jsr_a16(APPLE2 *m) {
    ah_read_pc(m);
    m->cpu.pc = m->cpu.address_16;
    CYCLE(m);
}

static inline void lda_a16(APPLE2 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
    set_register_to_value(m, &m->cpu.A, m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void lda_imm(APPLE2 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    set_register_to_value(m, &m->cpu.A, m->cpu.scratch_lo);
    m->cpu.pc++;
    CYCLE(m);
}

static inline void ldx_a16(APPLE2 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
    set_register_to_value(m, &m->cpu.X, m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void ldx_imm(APPLE2 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    set_register_to_value(m, &m->cpu.X, m->cpu.scratch_lo);
    m->cpu.pc++;
    CYCLE(m);
}

static inline void ldy_a16(APPLE2 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
    set_register_to_value(m, &m->cpu.Y, m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void ldy_imm(APPLE2 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    set_register_to_value(m, &m->cpu.Y, m->cpu.scratch_lo);
    m->cpu.pc++;
    CYCLE(m);
}

static inline void lsr_a(APPLE2 *m) {
    read_from_memory(m, m->cpu.pc);
    m->cpu.C = m->cpu.A & 0x01 ? 1 : 0;
    set_register_to_value(m, &m->cpu.A, m->cpu.A >>= 1);
    CYCLE(m);
}

static inline void lsr_a16(APPLE2 *m) {
    m->cpu.C = m->cpu.scratch_lo & 0x01 ? 1 : 0;
    set_register_to_value(m, &m->cpu.scratch_hi, m->cpu.scratch_lo >> 1);
    write_to_memory(m, m->cpu.address_16, m->cpu.scratch_hi);
    CYCLE(m);
}

static inline void ora_a16(APPLE2 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
    set_register_to_value(m, &m->cpu.A, m->cpu.A | m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void ora_imm(APPLE2 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    set_register_to_value(m, &m->cpu.A, m->cpu.A | m->cpu.scratch_lo);
    m->cpu.pc++;
    CYCLE(m);
}

static inline void pla(APPLE2 *m) {
    set_register_to_value(m, &m->cpu.A, pull(m));
    CYCLE(m);
}

static inline void plp(APPLE2 *m) {
    m->cpu.flags = (pull(m) & ~0b00010000) | 0b00100000;            // Break flag off, but - flag on
    CYCLE(m);
}

static inline void pha(APPLE2 *m) {
    push(m, m->cpu.A);
    CYCLE(m);
}

static inline void php(APPLE2 *m) {
    // Break flag on flags push
    push(m, m->cpu.flags | 0b00010000);
    CYCLE(m);
}

static inline void read_pc(APPLE2 *m) {
    read_from_memory(m, m->cpu.pc);
    CYCLE(m);
}

static inline void rol_a(APPLE2 *m) {
    uint8_t c = m->cpu.A & 0x80;
    read_pc(m);
    set_register_to_value(m, &m->cpu.A, (m->cpu.A << 1) | m->cpu.C);
    m->cpu.C = c ? 1 : 0;
    CYCLE(m);
}

static inline void rol_a16(APPLE2 *m) {
    uint8_t c = m->cpu.scratch_lo & 0x80;
    set_register_to_value(m, &m->cpu.scratch_lo, (m->cpu.scratch_lo << 1) | m->cpu.C);
    write_to_memory(m, m->cpu.address_16, m->cpu.scratch_lo);
    m->cpu.C = c ? 1 : 0;
    CYCLE(m);
}

static inline void ror_a(APPLE2 *m) {
    uint8_t c = m->cpu.A & 0x01;
    read_pc(m);
    set_register_to_value(m, &m->cpu.A, (m->cpu.A >> 1) | (m->cpu.C << 7));
    m->cpu.C = c;
    CYCLE(m);
}

static inline void ror_a16(APPLE2 *m) {
    uint8_t c = m->cpu.scratch_lo & 0x01;
    set_register_to_value(m, &m->cpu.scratch_lo, (m->cpu.scratch_lo >> 1) | (m->cpu.C << 7));
    write_to_memory(m, m->cpu.address_16, m->cpu.scratch_lo);
    m->cpu.C = c;
    CYCLE(m);
}

static inline void rti(APPLE2 *m) {
    ah_from_stack(m);
    m->cpu.pc = m->cpu.address_16;
    CYCLE(m);
}

static inline void rts(APPLE2 *m) {
    m->cpu.pc = m->cpu.address_16;
    al_read_pc(m);
    CYCLE(m);
}

static inline void sbc_a16(APPLE2 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
    subtract_value_from_accumulator(m, m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void sbc_imm(APPLE2 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    subtract_value_from_accumulator(m, m->cpu.scratch_lo);
    m->cpu.pc++;
    CYCLE(m);
}

static inline void sec(APPLE2 *m) {
    read_pc(m);
    m->cpu.C = 1;
    CYCLE(m);
}

static inline void sed(APPLE2 *m) {
    read_pc(m);
    m->cpu.D = 1;
    CYCLE(m);
}

static inline void sei(APPLE2 *m) {
    read_pc(m);
    m->cpu.I = 1;
    CYCLE(m);
}

static inline void sta_a16(APPLE2 *m) {
    write_to_memory(m, m->cpu.address_16, m->cpu.A);
    CYCLE(m);
}

static inline void stx_a16(APPLE2 *m) {
    write_to_memory(m, m->cpu.address_16, m->cpu.X);
    CYCLE(m);
}

static inline void sty_a16(APPLE2 *m) {
    write_to_memory(m, m->cpu.address_16, m->cpu.Y);
    CYCLE(m);
}

static inline void tax(APPLE2 *m) {
    read_pc(m);
    set_register_to_value(m, &m->cpu.X, m->cpu.A);
    CYCLE(m);
}

static inline void tay(APPLE2 *m) {
    read_pc(m);
    set_register_to_value(m, &m->cpu.Y, m->cpu.A);
    CYCLE(m);
}

static inline void tsx(APPLE2 *m) {
    read_pc(m);
    set_register_to_value(m, &m->cpu.X, m->cpu.sp - 0x100);
    CYCLE(m);
}

static inline void txa(APPLE2 *m) {
    read_pc(m);
    set_register_to_value(m, &m->cpu.A, m->cpu.X);
    CYCLE(m);
}

static inline void txs(APPLE2 *m) {
    read_pc(m);
    m->cpu.sp = 0x100 + m->cpu.X;
    CYCLE(m);
}

static inline void tya(APPLE2 *m) {
    read_pc(m);
    set_register_to_value(m, &m->cpu.A, m->cpu.Y);
    CYCLE(m);
}
