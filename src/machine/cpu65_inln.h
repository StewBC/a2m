// C64 6510 CPU helpers, adapted from the a2m cycle-accurate NMOS 6502 core.

#pragma once


#include <assert.h>

#define CYCLE(m)     do { (m)->cpu.cycles++; } while(0)

static inline uint8_t read_from_memory(cpu65_t *m, uint16_t address) {
    m->bus_access_kind = CPU65_BUS_ACCESS_DATA_READ;
    return m->read(m->user, address);
}

static inline void write_to_memory(cpu65_t *m, uint16_t address, uint8_t value) {
    m->bus_access_kind = CPU65_BUS_ACCESS_DATA_WRITE;
    m->write(m->user, address, value);
}

static inline uint8_t read_opcode(cpu65_t *m, uint16_t address) {
    m->bus_access_kind = CPU65_BUS_ACCESS_OPCODE_FETCH;
    return m->read(m->user, address);
}

static inline uint8_t read_operand(cpu65_t *m, uint16_t address) {
    m->bus_access_kind = CPU65_BUS_ACCESS_OPERAND_READ;
    return m->read(m->user, address);
}

static inline uint8_t read_dummy(cpu65_t *m, uint16_t address) {
    m->bus_access_kind = CPU65_BUS_ACCESS_DUMMY_READ;
    return m->read(m->user, address);
}

static inline uint8_t read_stack(cpu65_t *m, uint16_t address) {
    m->bus_access_kind = CPU65_BUS_ACCESS_STACK_READ;
    return m->read(m->user, address);
}

static inline void write_stack(cpu65_t *m, uint16_t address, uint8_t value) {
    m->bus_access_kind = CPU65_BUS_ACCESS_STACK_WRITE;
    m->write(m->user, address, value);
}

static inline uint8_t read_vector(cpu65_t *m, uint16_t address) {
    m->bus_access_kind = CPU65_BUS_ACCESS_VECTOR_READ;
    return m->read(m->user, address);
}

static inline uint8_t read_from_memory_debug(cpu65_t *m, uint16_t address) {
    return read_from_memory(m, address);
}

static inline uint16_t read_from_memory_debug_16(cpu65_t *m, uint16_t address) {
    uint8_t lo = read_from_memory_debug(m, address);
    uint8_t hi = read_from_memory_debug(m, (uint16_t)(address + 1));
    return (uint16_t)(lo | ((uint16_t)hi << 8));
}
// Setters
static inline void set_register_to_value(cpu65_t *m, uint8_t *reg, uint8_t value) {
    *reg = value;
    m->cpu.N = *reg & 0x80 ? 1 : 0;
    m->cpu.Z = *reg ? 0 : 1;
}

// Helper Functions
static inline void add_value_to_accumulator(cpu65_t *m, uint8_t value) {
    uint8_t a = m->cpu.A;
    uint8_t c = m->cpu.C;
    m->cpu.scratch_16 = m->cpu.A + value + c;
    set_register_to_value(m, &m->cpu.A, m->cpu.scratch_lo);
    m->cpu.scratch_lo = (a & 0x0F) + (value & 0x0F) + c;
    m->cpu.V = ((a ^ m->cpu.A) & ~(a ^ value) & 0x80) != 0 ? 1 : 0;
    m->cpu.C = m->cpu.scratch_hi;
    if(m->cpu.D) {
        m->cpu.scratch_hi = (a >> 4) + (value >> 4);
        if(m->cpu.scratch_lo > 9) {
            m->cpu.scratch_lo += 6;
            m->cpu.scratch_hi++;
        }
        {
            uint8_t intermediate = (m->cpu.scratch_hi << 4) | (m->cpu.scratch_lo & 0x0F);
            m->cpu.N = intermediate & 0x80 ? 1 : 0;
            m->cpu.V = ((a ^ intermediate) & ~(a ^ value) & 0x80) != 0 ? 1 : 0;
        }
        if(m->cpu.scratch_hi > 9) {
            m->cpu.scratch_hi += 6;
            m->cpu.C = 1;
        }
        m->cpu.A = (m->cpu.scratch_hi << 4) | (m->cpu.scratch_lo & 0x0F);
        if(m->cpu.class == CPU_65c02) {
            read_dummy(m, m->cpu.address_16);
            CYCLE(m);
            set_register_to_value(m, &m->cpu.A, m->cpu.A);
        }
    }
}

static inline void compare_bytes(cpu65_t *m, uint8_t lhs, uint8_t rhs) {
    m->cpu.Z = (lhs == rhs) ? 1 : 0;
    m->cpu.C = (lhs >= rhs) ? 1 : 0;
    m->cpu.N = ((lhs - rhs) & 0x80) ? 1 : 0;
}

static inline uint8_t pull(cpu65_t *m) {
    if(++m->cpu.sp >= 0x200) {
        m->cpu.sp = 0x100;
    }
    return read_stack(m, m->cpu.sp);
}

static inline void push(cpu65_t *m, uint8_t value) {
    write_stack(m, m->cpu.sp, value);
    if(--m->cpu.sp < 0x100) {
        m->cpu.sp += 0x100;
    }
}

static inline void subtract_value_from_accumulator(cpu65_t *m, uint8_t value) {
    uint8_t a = m->cpu.A;
    m->cpu.C ^= 1;
    m->cpu.scratch_16 = a - value - m->cpu.C;

    if(m->cpu.class == CPU_6502) {
        set_register_to_value(m, &m->cpu.A, m->cpu.scratch_lo);
        m->cpu.V = ((a ^ value) & (a ^ m->cpu.A) & 0x80) != 0 ? 1 : 0;
        if(m->cpu.D) {
            uint8_t lo = (a & 0x0F) - (value & 0x0F) - m->cpu.C;
            uint8_t hi = (a >> 4) - (value >> 4);
            if(lo & 0x10) {
                lo -= 6;
                hi--;
            }
            if(hi & 0xF0) {
                hi -= 6;
            }
            m->cpu.A = (hi << 4) | (lo & 0x0F);
        }
        m->cpu.C = m->cpu.scratch_16 < 0x100 ? 1 : 0;
    } else {
        m->cpu.A = m->cpu.scratch_lo;
        m->cpu.V = ((a ^ m->cpu.A) & (a ^ value) & 0x80) ? 1 : 0;
        if(m->cpu.D) {
            if((a & 0x0F) < ((value & 0x0F) + m->cpu.C)) {
                m->cpu.scratch_lo -= 0x06;
            }
            if(a < value + m->cpu.C) {
                m->cpu.scratch_lo -= 0x60;
            }
            read_from_memory(m, m->cpu.address_16);
            CYCLE(m);
        }
        m->cpu.C = m->cpu.scratch_hi ? 0 : 1;
        set_register_to_value(m, &m->cpu.A, m->cpu.scratch_lo);
    }
}

// Stage Helpers
static inline void ah_from_stack(cpu65_t *m) {
    m->cpu.address_hi = pull(m);
    CYCLE(m);
}

static inline void ah_read_a16_sl2al(cpu65_t *m) {
    m->cpu.address_lo++;
    m->cpu.address_hi = read_from_memory(m, m->cpu.address_16);
    m->cpu.address_lo = m->cpu.scratch_lo;
    CYCLE(m);
}

static inline void ah_read_pc(cpu65_t *m) {
    m->cpu.address_hi = read_operand(m, m->cpu.pc);
    m->cpu.pc++;
    CYCLE(m);
}

static inline void al_from_stack(cpu65_t *m) {
    m->cpu.address_lo = pull(m);
    CYCLE(m);
}

static inline void al_read_pc(cpu65_t *m) {
    m->cpu.address_lo = read_operand(m, m->cpu.pc);
    m->cpu.address_hi = 0;
    m->cpu.pc++;
    CYCLE(m);
}

static inline void branch(cpu65_t *m) {
    read_dummy(m, m->cpu.address_16);
    CYCLE(m);
    uint8_t lo = m->cpu.address_lo;
    m->cpu.address_lo += m->cpu.scratch_lo;
    if((lo + (int8_t)m->cpu.scratch_lo) & 0x100) {
        read_dummy(m, m->cpu.address_16);
        CYCLE(m);
    }
    m->cpu.pc += (int8_t)m->cpu.scratch_lo;
}

// static inline void brk_pc(cpu65_t *m) {
//     m->cpu.pc = 0xFFFE;
//     al_read_pc(m);
// }

static inline void p_from_stack(cpu65_t *m) {
    m->cpu.flags = (pull(m) & ~0b00010000) | 0b00100000;
    CYCLE(m);
}

static inline void pc_hi_to_stack(cpu65_t *m) {
    push(m, (m->cpu.pc >> 8) & 0xFF);
    CYCLE(m);
}

static inline void pc_lo_to_stack(cpu65_t *m) {
    push(m, m->cpu.pc & 0xFF);
    CYCLE(m);
}

static inline void read_a16_ind_x(cpu65_t *m) {
    read_dummy(m, m->cpu.address_16);
    m->cpu.address_lo += m->cpu.X;
    CYCLE(m);
}

static inline void read_a16_ind_y(cpu65_t *m) {
    read_dummy(m, m->cpu.address_16);
    m->cpu.address_lo += m->cpu.Y;
    CYCLE(m);
}

static inline void read_sp(cpu65_t *m) {
    read_dummy(m, m->cpu.sp);
    CYCLE(m);
}

static inline void sl_read_a16(cpu65_t *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
    CYCLE(m);
}

static inline void sl_write_a16(cpu65_t *m) {
    m->bus_access_kind = CPU65_BUS_ACCESS_RMW_DUMMY_WRITE;
    m->write(m->user, m->cpu.address_16, m->cpu.scratch_lo);
    CYCLE(m);
}

// pipelines
static inline void a(cpu65_t *m) {
    al_read_pc(m);
    ah_read_pc(m);
}

static inline void ar(cpu65_t *m) {
    a(m);
    sl_read_a16(m);
}

static inline void arw(cpu65_t *m) {
    a(m);
    sl_read_a16(m);
    if(m->cpu.class == CPU_6502) {
        sl_write_a16(m);
    } else {
        sl_read_a16(m);
    }
}

static inline void aix(cpu65_t *m) {
    a(m);
    uint8_t lo = m->cpu.address_lo;
    m->cpu.address_lo += m->cpu.X;
    if(m->cpu.address_lo < lo) {
        if(m->cpu.class == CPU_6502) {
            read_dummy(m, m->cpu.address_16);
        } else {
            read_dummy(m, m->cpu.pc - 1);
        }
        m->cpu.address_hi++;
        CYCLE(m);
    }
}

static inline void aipxr(cpu65_t *m) {
    a(m);
    uint8_t lo = m->cpu.address_lo;
    m->cpu.address_lo += m->cpu.X;
    if(m->cpu.class == CPU_6502) {
        read_dummy(m, m->cpu.address_16);
    } else {
        read_dummy(m, m->cpu.pc - 1);
    }
    CYCLE(m);
    if(m->cpu.address_lo < lo) {
        m->cpu.address_hi++;
    }
}

static inline void aixrr(cpu65_t *m) {
    aix(m);
    sl_read_a16(m);
    sl_read_a16(m);
}

static inline void aipxrw(cpu65_t *m) {
    aipxr(m);
    sl_read_a16(m);
    if(m->cpu.class == CPU_6502) {
        sl_write_a16(m);
    } else {
        sl_read_a16(m);
    }
}

static inline void aiy(cpu65_t *m) {
    a(m);
    uint8_t lo = m->cpu.address_lo;
    m->cpu.address_lo += m->cpu.Y;
    if(m->cpu.address_lo < lo) {
        if(m->cpu.class == CPU_6502) {
            read_dummy(m, m->cpu.address_16);
        } else {
            read_dummy(m, m->cpu.pc - 1);
        }
        m->cpu.address_hi++;
        CYCLE(m);
    }
}

static inline void aiyr(cpu65_t *m) {
    a(m);
    uint8_t lo = m->cpu.address_lo;
    m->cpu.address_lo += m->cpu.Y;
    if(m->cpu.class == CPU_6502) {
        read_dummy(m, m->cpu.address_16);
    } else {
        read_dummy(m, m->cpu.pc - 1);
    }
    CYCLE(m);
    if(m->cpu.address_lo < lo) {
        m->cpu.address_hi++;
    }
}

static inline void aiyr_und(cpu65_t *m) {
    a(m);
    m->cpu.scratch_hi = m->cpu.address_hi + 1;
    uint8_t lo = m->cpu.address_lo;
    m->cpu.address_lo += m->cpu.Y;
    read_dummy(m, m->cpu.address_16);
    CYCLE(m);
    m->cpu.page_fault = m->cpu.address_lo < lo;
    if(m->cpu.page_fault) {
        m->cpu.address_hi++;
    }
}

static inline void aipxr_und(cpu65_t *m) {
    a(m);
    m->cpu.scratch_hi = m->cpu.address_hi + 1;
    uint8_t lo = m->cpu.address_lo;
    m->cpu.address_lo += m->cpu.X;
    read_dummy(m, m->cpu.address_16);
    CYCLE(m);
    m->cpu.page_fault = m->cpu.address_lo < lo;
    if(m->cpu.page_fault) {
        m->cpu.address_hi++;
    }
}

static inline void mix(cpu65_t *m) {
    al_read_pc(m);
    read_a16_ind_x(m);
}

static inline void mixa(cpu65_t *m) {
    mix(m);
    sl_read_a16(m);
    ah_read_a16_sl2al(m);
}

static inline void mixrw(cpu65_t *m) {
    mix(m);
    sl_read_a16(m);
    if(m->cpu.class == CPU_6502) {
        sl_write_a16(m);
    } else {
        sl_read_a16(m);
    }
}

static inline void miy(cpu65_t *m) {
    al_read_pc(m);
    sl_read_a16(m);
    ah_read_a16_sl2al(m);
    uint8_t lo = m->cpu.address_lo;
    m->cpu.address_lo += m->cpu.Y;
    if(m->cpu.address_lo < lo) {
        if(m->cpu.class == CPU_6502) {
            read_dummy(m, m->cpu.address_16);
        } else {
            read_dummy(m, m->cpu.pc - 1);
        }
        m->cpu.address_hi++;
        CYCLE(m);
    }
}

static inline void miyr(cpu65_t *m) {
    al_read_pc(m);
    sl_read_a16(m);
    ah_read_a16_sl2al(m);
    uint8_t lo = m->cpu.address_lo;
    m->cpu.address_lo += m->cpu.Y;
    if(m->cpu.class == CPU_6502) {
        read_dummy(m, m->cpu.address_16);
    } else {
        read_dummy(m, m->cpu.pc - 1);
    }
    CYCLE(m);
    if(m->cpu.address_lo < lo) {
        m->cpu.address_hi++;
    }
}

static inline void miyr_und(cpu65_t *m) {
    al_read_pc(m);
    sl_read_a16(m);
    ah_read_a16_sl2al(m);
    m->cpu.scratch_hi = m->cpu.address_hi + 1;
    uint8_t lo = m->cpu.address_lo;
    m->cpu.address_lo += m->cpu.Y;
    read_dummy(m, m->cpu.address_16);
    CYCLE(m);
    m->cpu.page_fault = m->cpu.address_lo < lo;
    if(m->cpu.page_fault) {
        m->cpu.address_hi++;
    }
}

static inline void miz(cpu65_t *m) {
    al_read_pc(m);
    sl_read_a16(m);
    ah_read_a16_sl2al(m);
}

static inline void mizy(cpu65_t *m) {
    al_read_pc(m);
    read_a16_ind_y(m);
}

static inline void mrw(cpu65_t *m) {
    al_read_pc(m);
    sl_read_a16(m);
    if(m->cpu.class == CPU_6502) {
        sl_write_a16(m);
    } else {
        sl_read_a16(m);
    }
}

static inline void read_pc_1(cpu65_t *m) {
    read_dummy(m, m->cpu.pc - 1);
    CYCLE(m);
}

static inline void read_pc(cpu65_t *m) {
    read_dummy(m, m->cpu.pc);
    CYCLE(m);
}

static inline void unimplemented(cpu65_t *m) {
    m->cpu.cycles = -1;
}

static inline void unstable_store_a16(cpu65_t *m, uint8_t value) {
    if(m->cpu.page_fault) {
        m->cpu.address_hi &= value;
    }
    write_to_memory(m, m->cpu.address_16, value);
    CYCLE(m);
}

// Pipeline selectors
static inline void aixr_sel(cpu65_t *m) {
    if(m->cpu.class == CPU_6502) {
        aipxrw(m);
    } else {
        aixrr(m);
    }
}

// IRQ
static inline void cpu65_read_interrupt_vector(cpu65_t *m, uint16_t vector) {
    m->cpu.address_lo = read_vector(m, vector);
    CYCLE(m);
    m->cpu.address_hi = read_vector(m, (uint16_t)(vector + 1u));
    CYCLE(m);
    m->cpu.pc = m->cpu.address_16;
}

static inline void cpu65_irq(cpu65_t *m) {
    cpu65_read_interrupt_vector(m, 0xFFFEu);
    m->cpu.I = 1;
    if(m->cpu.class == CPU_65c02) {
        m->cpu.D = 0;
    }
}

static inline uint8_t cpu65_irq_pending(cpu65_t *m) {
    return m->irq_pending ? m->irq_pending(m->user) : 0;
}

static inline uint8_t cpu65_take_irq_if_pending(cpu65_t *m) {
    uint8_t pending = cpu65_irq_pending(m);
    uint8_t irq_disable = m->cpu.I;
    if(m->cpu.irq_defer) {
        irq_disable = m->cpu.irq_defer_i;
    }
    if(!pending || irq_disable) {
        if(m->cpu.irq_defer) {
            m->cpu.irq_defer = 0;
        }
        return 0;
    }
    if(m->cpu.irq_defer) {
        m->cpu.irq_defer = 0;
    }

    m->cpu.opcode_pc = m->cpu.pc;
    read_dummy(m, m->cpu.pc);
    CYCLE(m);
    read_sp(m);
    pc_hi_to_stack(m);
    pc_lo_to_stack(m);
    push(m, (m->cpu.flags & (uint8_t)~0b00010000) | 0b00100000);
    CYCLE(m);
    m->cpu.irq_entries++;
    cpu65_irq(m);
    return 1;
}

static inline void cpu65_nmi(cpu65_t *m) {
    cpu65_read_interrupt_vector(m, 0xFFFAu);
    m->cpu.I = 1;
    if(m->cpu.class == CPU_65c02) {
        m->cpu.D = 0;
    }
}

static inline uint8_t cpu65_nmi_pending(cpu65_t *m) {
    return m->nmi_pending ? m->nmi_pending(m->user) : 0;
}

static inline uint8_t cpu65_take_nmi_if_pending(cpu65_t *m) {
    if(!cpu65_nmi_pending(m)) {
        return 0;
    }

    m->cpu.opcode_pc = m->cpu.pc;
    read_dummy(m, m->cpu.pc);
    CYCLE(m);
    read_sp(m);
    pc_hi_to_stack(m);
    pc_lo_to_stack(m);
    push(m, (m->cpu.flags & (uint8_t)~0b00010000) | 0b00100000);
    CYCLE(m);
    m->cpu.nmi_entries++;
    cpu65_nmi(m);
    return 1;
}

// Instructions
static inline void adc_a16(cpu65_t *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
    add_value_to_accumulator(m, m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void adc_imm(cpu65_t *m) {
    m->cpu.scratch_lo = read_operand(m, m->cpu.pc);
    m->cpu.pc++;
    CYCLE(m);
    if(m->cpu.class == CPU_65c02 && m->cpu.D) {
        m->cpu.address_16 = 0x56;
    }
    add_value_to_accumulator(m, m->cpu.scratch_lo);
}

static inline void ahx_a16(cpu65_t *m) {
    unstable_store_a16(m, m->cpu.A & m->cpu.X & m->cpu.scratch_hi);
}

static inline void anc_imm(cpu65_t *m) {
    m->cpu.scratch_lo = read_operand(m, m->cpu.pc);
    m->cpu.pc++;
    set_register_to_value(m, &m->cpu.A, m->cpu.A & m->cpu.scratch_lo);
    m->cpu.C = m->cpu.N;
    CYCLE(m);
}

static inline void alr_imm(cpu65_t *m) {
    m->cpu.scratch_lo = read_operand(m, m->cpu.pc);
    m->cpu.pc++;
    m->cpu.A &= m->cpu.scratch_lo;
    m->cpu.C = m->cpu.A & 0x01 ? 1 : 0;
    set_register_to_value(m, &m->cpu.A, m->cpu.A >> 1);
    CYCLE(m);
}

static inline void and_a16(cpu65_t *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
    set_register_to_value(m, &m->cpu.A, m->cpu.A & m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void and_imm(cpu65_t *m) {
    m->cpu.scratch_lo = read_operand(m, m->cpu.pc);
    set_register_to_value(m, &m->cpu.A, m->cpu.A & m->cpu.scratch_lo);
    m->cpu.pc++;
    CYCLE(m);
}

static inline void arr_imm(cpu65_t *m) {
    m->cpu.scratch_lo = read_operand(m, m->cpu.pc);
    m->cpu.pc++;
    uint8_t value = m->cpu.A & m->cpu.scratch_lo;
    uint8_t result = (value >> 1) | (m->cpu.C << 7);
    set_register_to_value(m, &m->cpu.A, result);
    m->cpu.C = result & 0x40 ? 1 : 0;
    m->cpu.V = ((result >> 6) ^ (result >> 5)) & 0x01;
    if(m->cpu.D) {
        if(((value & 0x0F) + (value & 0x01)) > 5) {
            m->cpu.A = (m->cpu.A & 0xF0) | ((m->cpu.A + 0x06) & 0x0F);
        }
        if((value + (value & 0x10)) > 0x50) {
            m->cpu.A += 0x60;
            m->cpu.C = 1;
        } else {
            m->cpu.C = 0;
        }
    }
    CYCLE(m);
}

static inline void asl_a(cpu65_t *m) {
    read_dummy(m, m->cpu.pc);
    m->cpu.C = m->cpu.A & 0x80 ? 1 : 0;
    set_register_to_value(m, &m->cpu.A, m->cpu.A <<= 1);
    CYCLE(m);
}

static inline void asl_a16(cpu65_t *m) {
    m->cpu.C = m->cpu.scratch_lo & 0x80 ? 1 : 0;
    set_register_to_value(m, &m->cpu.scratch_hi, m->cpu.scratch_lo << 1);
    write_to_memory(m, m->cpu.address_16, m->cpu.scratch_hi);
    CYCLE(m);
}

static inline void axs_imm(cpu65_t *m) {
    m->cpu.scratch_lo = read_operand(m, m->cpu.pc);
    m->cpu.pc++;
    uint8_t value = m->cpu.A & m->cpu.X;
    compare_bytes(m, value, m->cpu.scratch_lo);
    m->cpu.X = value - m->cpu.scratch_lo;
    CYCLE(m);
}

static inline void bcc(cpu65_t *m) {
    m->cpu.scratch_lo = read_operand(m, m->cpu.pc);
    CYCLE(m);
    m->cpu.address_16 = ++m->cpu.pc;
    if(!m->cpu.C) {
        branch(m);
    }
}

static inline void bcs(cpu65_t *m) {
    m->cpu.scratch_lo = read_operand(m, m->cpu.pc);
    CYCLE(m);
    m->cpu.address_16 = ++m->cpu.pc;
    if(m->cpu.C) {
        branch(m);
    }
}

static inline void beq(cpu65_t *m) {
    m->cpu.scratch_lo = read_operand(m, m->cpu.pc);
    CYCLE(m);
    m->cpu.address_16 = ++m->cpu.pc;
    if(m->cpu.Z) {
        branch(m);
    }
}

static inline void bit_a16(cpu65_t *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
    set_register_to_value(m, &m->cpu.scratch_hi, m->cpu.A & m->cpu.scratch_lo);
    m->cpu.flags &= 0b00111111;
    m->cpu.flags |= (m->cpu.scratch_lo & 0b11000000);
    CYCLE(m);
}

static inline void bit_imm(cpu65_t *m) {
    m->cpu.scratch_lo = read_operand(m, m->cpu.pc);
    CYCLE(m);
    m->cpu.Z = (m->cpu.A & m->cpu.scratch_lo) == 0 ? -1 : 0;
    m->cpu.pc++;
}

static inline void bmi(cpu65_t *m) {
    m->cpu.scratch_lo = read_operand(m, m->cpu.pc);
    CYCLE(m);
    m->cpu.address_16 = ++m->cpu.pc;
    if(m->cpu.N) {
        branch(m);
    }
}

static inline void bne(cpu65_t *m) {
    m->cpu.scratch_lo = read_operand(m, m->cpu.pc);
    CYCLE(m);
    m->cpu.address_16 = ++m->cpu.pc;
    if(!m->cpu.Z) {
        branch(m);
    }
}

static inline void bpl(cpu65_t *m) {
    m->cpu.scratch_lo = read_operand(m, m->cpu.pc);
    CYCLE(m);
    m->cpu.address_16 = ++m->cpu.pc;
    if(!m->cpu.N) {
        branch(m);
    }
}

static inline void bra(cpu65_t *m) {
    m->cpu.scratch_lo = read_operand(m, m->cpu.pc);
    CYCLE(m);
    m->cpu.address_16 = ++m->cpu.pc;
    branch(m);
}

static inline void bvc(cpu65_t *m) {
    m->cpu.scratch_lo = read_operand(m, m->cpu.pc);
    CYCLE(m);
    m->cpu.address_16 = ++m->cpu.pc;
    if(!m->cpu.V) {
        branch(m);
    }
}

static inline void bvs(cpu65_t *m) {
    m->cpu.scratch_lo = read_operand(m, m->cpu.pc);
    CYCLE(m);
    m->cpu.address_16 = ++m->cpu.pc;
    if(m->cpu.V) {
        branch(m);
    }
}

static inline void cpu65_brk(cpu65_t *m) {
    cpu65_read_interrupt_vector(m, 0xFFFEu);
    if(m->cpu.class == CPU_6502) {
        // Interrupt flag on at break
        m->cpu.flags |= 0b00000100;
    } else {
        m->cpu.flags &= ~0b00001000;
        if(m->cpu.flags & 0b00100000) {
            // Interrupt flag on at break, if '-' flag is set
            m->cpu.flags |= 0b00000100;
        }
    }
}

static inline void clc(cpu65_t *m) {
    read_dummy(m, m->cpu.pc);
    m->cpu.C = 0;
    CYCLE(m);
}

static inline void cld(cpu65_t *m) {
    read_dummy(m, m->cpu.pc);
    m->cpu.D = 0;
    CYCLE(m);
}

static inline void cli(cpu65_t *m) {
    read_dummy(m, m->cpu.pc);
    m->cpu.irq_defer = 1;
    m->cpu.irq_defer_i = 1;
    m->cpu.I = 0;
    CYCLE(m);
}

static inline void clv(cpu65_t *m) {
    read_dummy(m, m->cpu.pc);
    m->cpu.V = 0;
    CYCLE(m);
}

static inline void cmp_a16(cpu65_t *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
    compare_bytes(m, m->cpu.A, m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void cmp_imm(cpu65_t *m) {
    m->cpu.scratch_lo = read_operand(m, m->cpu.pc);
    compare_bytes(m, m->cpu.A, m->cpu.scratch_lo);
    m->cpu.pc++;
    CYCLE(m);
}

static inline void cpx_a16(cpu65_t *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
    compare_bytes(m, m->cpu.X, m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void cpx_imm(cpu65_t *m) {
    m->cpu.scratch_lo = read_operand(m, m->cpu.pc);
    compare_bytes(m, m->cpu.X, m->cpu.scratch_lo);
    m->cpu.pc++;
    CYCLE(m);
}

static inline void cpy_a16(cpu65_t *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
    compare_bytes(m, m->cpu.Y, m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void cpy_imm(cpu65_t *m) {
    m->cpu.scratch_lo = read_operand(m, m->cpu.pc);
    compare_bytes(m, m->cpu.Y, m->cpu.scratch_lo);
    m->cpu.pc++;
    CYCLE(m);
}

static inline void dcp_a16(cpu65_t *m) {
    m->cpu.scratch_lo--;
    write_to_memory(m, m->cpu.address_16, m->cpu.scratch_lo);
    compare_bytes(m, m->cpu.A, m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void dea(cpu65_t *m) {
    read_dummy(m, m->cpu.pc);
    set_register_to_value(m, &m->cpu.A, m->cpu.A - 1);
    CYCLE(m);
}

static inline void dec_a16(cpu65_t *m) {
    set_register_to_value(m, &m->cpu.scratch_hi, m->cpu.scratch_lo - 1);
    write_to_memory(m, m->cpu.address_16, m->cpu.scratch_hi);
    CYCLE(m);
}

static inline void dex(cpu65_t *m) {
    read_dummy(m, m->cpu.pc);
    set_register_to_value(m, &m->cpu.X, m->cpu.X - 1);
    CYCLE(m);
}

static inline void dey(cpu65_t *m) {
    read_dummy(m, m->cpu.pc);
    set_register_to_value(m, &m->cpu.Y, m->cpu.Y - 1);
    CYCLE(m);
}

static inline void eor_a16(cpu65_t *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
    set_register_to_value(m, &m->cpu.A, m->cpu.A ^ m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void eor_imm(cpu65_t *m) {
    m->cpu.scratch_lo = read_operand(m, m->cpu.pc);
    set_register_to_value(m, &m->cpu.A, m->cpu.A ^ m->cpu.scratch_lo);
    m->cpu.pc++;
    CYCLE(m);
}

static inline void inc_a16(cpu65_t *m) {
    set_register_to_value(m, &m->cpu.scratch_hi, m->cpu.scratch_lo + 1);
    write_to_memory(m, m->cpu.address_16, m->cpu.scratch_hi);
    CYCLE(m);
}

static inline void ina(cpu65_t *m) {
    read_dummy(m, m->cpu.pc);
    set_register_to_value(m, &m->cpu.A, m->cpu.A + 1);
    CYCLE(m);
}

static inline void inx(cpu65_t *m) {
    read_dummy(m, m->cpu.pc);
    set_register_to_value(m, &m->cpu.X, m->cpu.X + 1);
    CYCLE(m);
}

static inline void iny(cpu65_t *m) {
    read_dummy(m, m->cpu.pc);
    set_register_to_value(m, &m->cpu.Y, m->cpu.Y + 1);
    CYCLE(m);
}

static inline void isc_a16(cpu65_t *m) {
    m->cpu.scratch_lo++;
    write_to_memory(m, m->cpu.address_16, m->cpu.scratch_lo);
    subtract_value_from_accumulator(m, m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void jam(cpu65_t *m) {
    read_pc(m);
    read_from_memory(m, 0xFFFF);
    CYCLE(m);
    read_from_memory(m, 0xFFFE);
    CYCLE(m);
    read_from_memory(m, 0xFFFE);
    CYCLE(m);
    for(int i = 0; i < 6; i++) {
        read_from_memory(m, 0xFFFF);
        CYCLE(m);
    }
}

static inline void jmp_a16(cpu65_t *m) {
    m->cpu.pc = m->cpu.address_16;
}

static inline void jmp_ind(cpu65_t *m) {
    m->cpu.address_lo++;
    m->cpu.scratch_hi = read_from_memory(m, m->cpu.address_16);
    CYCLE(m);
    if(m->cpu.class == CPU_65c02) {
        if(!m->cpu.address_lo) {
            m->cpu.address_hi++;
        }
        m->cpu.scratch_hi = read_from_memory(m, m->cpu.address_16);
        CYCLE(m);
    }
    m->cpu.pc = m->cpu.scratch_16;
}

static inline void jmp_ind_x(cpu65_t *m) {
    a(m);
    read_dummy(m, m->cpu.pc - 2);
    m->cpu.address_16 += m->cpu.X;
    CYCLE(m);
    sl_read_a16(m);
    m->cpu.address_16++;
    m->cpu.scratch_hi = read_from_memory(m, m->cpu.address_16);
    CYCLE(m);
    m->cpu.pc = m->cpu.scratch_16;
}

static inline void jsr_a16(cpu65_t *m) {
    ah_read_pc(m);
    m->cpu.pc = m->cpu.address_16;
}

static inline void las_a16(cpu65_t *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16) & (m->cpu.sp & 0xFF);
    m->cpu.A = m->cpu.scratch_lo;
    m->cpu.sp = 0x100 + m->cpu.scratch_lo;
    set_register_to_value(m, &m->cpu.X, m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void lax_a16(cpu65_t *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
    m->cpu.A = m->cpu.scratch_lo;
    set_register_to_value(m, &m->cpu.X, m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void lax_imm_und(cpu65_t *m) {
    m->cpu.scratch_lo = read_operand(m, m->cpu.pc);
    m->cpu.pc++;
    m->cpu.A = (m->cpu.A | 0xEE) & m->cpu.scratch_lo;
    set_register_to_value(m, &m->cpu.X, m->cpu.A);
    CYCLE(m);
}

static inline void lda_a16(cpu65_t *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
    set_register_to_value(m, &m->cpu.A, m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void lda_imm(cpu65_t *m) {
    m->cpu.scratch_lo = read_operand(m, m->cpu.pc);
    set_register_to_value(m, &m->cpu.A, m->cpu.scratch_lo);
    m->cpu.pc++;
    CYCLE(m);
}

static inline void ldx_a16(cpu65_t *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
    set_register_to_value(m, &m->cpu.X, m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void ldx_imm(cpu65_t *m) {
    m->cpu.scratch_lo = read_operand(m, m->cpu.pc);
    set_register_to_value(m, &m->cpu.X, m->cpu.scratch_lo);
    m->cpu.pc++;
    CYCLE(m);
}

static inline void ldy_a16(cpu65_t *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
    set_register_to_value(m, &m->cpu.Y, m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void ldy_imm(cpu65_t *m) {
    m->cpu.scratch_lo = read_operand(m, m->cpu.pc);
    set_register_to_value(m, &m->cpu.Y, m->cpu.scratch_lo);
    m->cpu.pc++;
    CYCLE(m);
}

static inline void lsr_a(cpu65_t *m) {
    read_dummy(m, m->cpu.pc);
    m->cpu.C = m->cpu.A & 0x01 ? 1 : 0;
    set_register_to_value(m, &m->cpu.A, m->cpu.A >>= 1);
    CYCLE(m);
}

static inline void lsr_a16(cpu65_t *m) {
    m->cpu.C = m->cpu.scratch_lo & 0x01 ? 1 : 0;
    set_register_to_value(m, &m->cpu.scratch_hi, m->cpu.scratch_lo >> 1);
    write_to_memory(m, m->cpu.address_16, m->cpu.scratch_hi);
    CYCLE(m);
}

static inline void ora_a16(cpu65_t *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
    set_register_to_value(m, &m->cpu.A, m->cpu.A | m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void ora_imm(cpu65_t *m) {
    m->cpu.scratch_lo = read_operand(m, m->cpu.pc);
    set_register_to_value(m, &m->cpu.A, m->cpu.A | m->cpu.scratch_lo);
    m->cpu.pc++;
    CYCLE(m);
}

static inline void phx(cpu65_t *m) {
    push(m, m->cpu.X);
    CYCLE(m);
}

static inline void phy(cpu65_t *m) {
    push(m, m->cpu.Y);
    CYCLE(m);
}

static inline void pla(cpu65_t *m) {
    set_register_to_value(m, &m->cpu.A, pull(m));
    CYCLE(m);
}

static inline void plp(cpu65_t *m) {
    m->cpu.flags = (pull(m) & ~0b00010000) | 0b00100000;            // Break flag off, but - flag on
    CYCLE(m);
}

static inline void plx(cpu65_t *m) {
    set_register_to_value(m, &m->cpu.X, pull(m));
    CYCLE(m);
}

static inline void ply(cpu65_t *m) {
    set_register_to_value(m, &m->cpu.Y, pull(m));
    CYCLE(m);
}

static inline void pha(cpu65_t *m) {
    push(m, m->cpu.A);
    CYCLE(m);
}

static inline void php(cpu65_t *m) {
    // Break flag on flags push
    push(m, m->cpu.flags | 0b00010000);
    CYCLE(m);
}

static inline void rla_a16(cpu65_t *m) {
    uint8_t c = m->cpu.scratch_lo & 0x80;
    m->cpu.scratch_lo = (m->cpu.scratch_lo << 1) | m->cpu.C;
    write_to_memory(m, m->cpu.address_16, m->cpu.scratch_lo);
    set_register_to_value(m, &m->cpu.A, m->cpu.A & m->cpu.scratch_lo);
    m->cpu.C = c ? 1 : 0;
    CYCLE(m);
}

static inline void rol_a(cpu65_t *m) {
    uint8_t c = m->cpu.A & 0x80;
    read_pc(m);
    set_register_to_value(m, &m->cpu.A, (m->cpu.A << 1) | m->cpu.C);
    m->cpu.C = c ? 1 : 0;
}

static inline void rol_a16(cpu65_t *m) {
    uint8_t c = m->cpu.scratch_lo & 0x80;
    set_register_to_value(m, &m->cpu.scratch_lo, (m->cpu.scratch_lo << 1) | m->cpu.C);
    write_to_memory(m, m->cpu.address_16, m->cpu.scratch_lo);
    m->cpu.C = c ? 1 : 0;
    CYCLE(m);
}

static inline void ror_a(cpu65_t *m) {
    uint8_t c = m->cpu.A & 0x01;
    read_pc(m);
    set_register_to_value(m, &m->cpu.A, (m->cpu.A >> 1) | (m->cpu.C << 7));
    m->cpu.C = c;
}

static inline void ror_a16(cpu65_t *m) {
    uint8_t c = m->cpu.scratch_lo & 0x01;
    set_register_to_value(m, &m->cpu.scratch_lo, (m->cpu.scratch_lo >> 1) | (m->cpu.C << 7));
    write_to_memory(m, m->cpu.address_16, m->cpu.scratch_lo);
    m->cpu.C = c;
    CYCLE(m);
}

static inline void rti(cpu65_t *m) {
    ah_from_stack(m);
    m->cpu.pc = m->cpu.address_16;
}

static inline void rts(cpu65_t *m) {
    m->cpu.pc = m->cpu.address_16;
    al_read_pc(m);
}

static inline void sbc_a16(cpu65_t *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
    CYCLE(m);
    subtract_value_from_accumulator(m, m->cpu.scratch_lo);
}

static inline void sbc_imm(cpu65_t *m) {
    m->cpu.scratch_lo = read_operand(m, m->cpu.pc);
    m->cpu.pc++;
    CYCLE(m);
    if(m->cpu.class == CPU_65c02 && m->cpu.D) {
        m->cpu.address_16 = 0;
    }
    subtract_value_from_accumulator(m, m->cpu.scratch_lo);

}

static inline void sec(cpu65_t *m) {
    read_pc(m);
    m->cpu.C = 1;
}

static inline void sed(cpu65_t *m) {
    read_pc(m);
    m->cpu.D = 1;
}

static inline void sei(cpu65_t *m) {
    read_pc(m);
    m->cpu.irq_defer = 0;
    m->cpu.irq_defer_i = 0;
    m->cpu.I = 1;
}

static inline void sta_a16(cpu65_t *m) {
    write_to_memory(m, m->cpu.address_16, m->cpu.A);
    CYCLE(m);
}

static inline void sax_a16(cpu65_t *m) {
    write_to_memory(m, m->cpu.address_16, m->cpu.A & m->cpu.X);
    CYCLE(m);
}

static inline void slo_a16(cpu65_t *m) {
    m->cpu.C = m->cpu.scratch_lo & 0x80 ? 1 : 0;
    m->cpu.scratch_lo <<= 1;
    write_to_memory(m, m->cpu.address_16, m->cpu.scratch_lo);
    set_register_to_value(m, &m->cpu.A, m->cpu.A | m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void sre_a16(cpu65_t *m) {
    m->cpu.C = m->cpu.scratch_lo & 0x01 ? 1 : 0;
    m->cpu.scratch_lo >>= 1;
    write_to_memory(m, m->cpu.address_16, m->cpu.scratch_lo);
    set_register_to_value(m, &m->cpu.A, m->cpu.A ^ m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void rra_a16(cpu65_t *m) {
    uint8_t c = m->cpu.scratch_lo & 0x01;
    m->cpu.scratch_lo = (m->cpu.scratch_lo >> 1) | (m->cpu.C << 7);
    write_to_memory(m, m->cpu.address_16, m->cpu.scratch_lo);
    m->cpu.C = c;
    add_value_to_accumulator(m, m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void stx_a16(cpu65_t *m) {
    write_to_memory(m, m->cpu.address_16, m->cpu.X);
    CYCLE(m);
}

static inline void sty_a16(cpu65_t *m) {
    write_to_memory(m, m->cpu.address_16, m->cpu.Y);
    CYCLE(m);
}

static inline void stz_a16(cpu65_t *m, uint8_t value) {
    write_to_memory(m, m->cpu.address_16, value);
    CYCLE(m);
}

static inline void shs_a16(cpu65_t *m) {
    m->cpu.sp = 0x100 + (m->cpu.A & m->cpu.X);
    unstable_store_a16(m, m->cpu.sp & m->cpu.scratch_hi);
}

static inline void shy_a16(cpu65_t *m) {
    unstable_store_a16(m, m->cpu.Y & m->cpu.scratch_hi);
}

static inline void shx_a16(cpu65_t *m) {
    unstable_store_a16(m, m->cpu.X & m->cpu.scratch_hi);
}

static inline void tax(cpu65_t *m) {
    read_pc(m);
    set_register_to_value(m, &m->cpu.X, m->cpu.A);
}

static inline void tay(cpu65_t *m) {
    read_pc(m);
    set_register_to_value(m, &m->cpu.Y, m->cpu.A);
}

static inline void trb(cpu65_t *m) {
    m->cpu.Z = (m->cpu.A & m->cpu.scratch_lo) == 0;
    m->cpu.scratch_lo = (m->cpu.A ^ 0xff) & m->cpu.scratch_lo;
    write_to_memory(m, m->cpu.address_16, m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void tsb(cpu65_t *m) {
    m->cpu.Z = (m->cpu.A & m->cpu.scratch_lo) == 0;
    m->cpu.scratch_lo |= m->cpu.A;
    write_to_memory(m, m->cpu.address_16, m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void tsx(cpu65_t *m) {
    read_pc(m);
    set_register_to_value(m, &m->cpu.X, m->cpu.sp - 0x100);
}

static inline void txa(cpu65_t *m) {
    read_pc(m);
    set_register_to_value(m, &m->cpu.A, m->cpu.X);
}

static inline void txs(cpu65_t *m) {
    read_pc(m);
    m->cpu.sp = 0x100 + m->cpu.X;
}

static inline void tya(cpu65_t *m) {
    read_pc(m);
    set_register_to_value(m, &m->cpu.A, m->cpu.Y);
}

static inline void xaa_imm(cpu65_t *m) {
    m->cpu.scratch_lo = read_operand(m, m->cpu.pc);
    m->cpu.pc++;
    set_register_to_value(m, &m->cpu.A, (m->cpu.A | 0xEE) & m->cpu.X & m->cpu.scratch_lo);
    CYCLE(m);
}
