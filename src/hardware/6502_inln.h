// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

#define CYCLE(m)     m->cpu.cycles++

// Memory access
static inline uint8_t read_from_memory(APPLE2 *m, uint16_t address) {
    size_t page = address / PAGE_SIZE;
    size_t offset = address % PAGE_SIZE;
    assert(page < m->pages.num_pages);
    uint8_t cb_mask = m->pages.watch_read_pages[page][offset];
    uint8_t byte = m->pages.read_pages[page][offset];
    if(cb_mask) {
        // Want to do read before breakpoint is checked
        if(cb_mask & WATCH_IO_PORT) {
            byte = apple2_softswitch_read_callback(m, address);
        }
        if(cb_mask & WATCH_READ_BREAKPOINT) {
            m->a2out_cb.cb_breakpoint_ctx.cb_breakpoint(m->a2out_cb.cb_breakpoint_ctx.user, address, WATCH_READ_BREAKPOINT);
        }
        // // If the read was done, return that value
        // if(cb_mask & WATCH_WRITE_BREAKPOINT) {
        //     return byte;
        // }
    }
    return byte;
}

static inline uint8_t read_from_memory_debug(APPLE2 *m, uint16_t address) {
    assert(address / PAGE_SIZE < m->pages.num_pages);
    return m->pages.read_pages[address / PAGE_SIZE][address % PAGE_SIZE];
}

static inline uint16_t read_from_memory_debug_16(APPLE2 *m, uint16_t address) {
    uint8_t lo = read_from_memory_debug(m, address);
    uint8_t hi = read_from_memory_debug(m, (uint16_t)(address + 1));
    return (uint16_t)(lo | ((uint16_t)hi << 8));
}

static inline uint8_t read_from_memory_selected(APPLE2 *m, uint16_t address, int selected) {
    switch(tst_flags(selected, (MEM_MAIN | MEM_AUX))) {
        case MEM_MAPPED_6502:
            return m->pages.read_pages[address / PAGE_SIZE][address % PAGE_SIZE];
        case MEM_MAIN:
            if(address < 0xD000) {
                return m->ram.RAM_MAIN[address];
            }
            if(address < 0xE000) {
                if(!(selected & MEM_LC_BANK2)) {
                    // D123 + 1000 = e123 & 1fff =  123 (bank1)
                    // d123               & 1fff = 1123 (bank2)
                    // So set bank 1 address to exxx.
                    address += 0x1000;
                }
                return m->ram.RAM_LC[(address & 0x1fff)];
            }
            // LC 0x2000+
            return m->ram.RAM_LC[(address & 0x3fff)];
        case MEM_AUX:
            if(address < 0xD000) {
                return m->ram.RAM_MAIN[address + 0x10000];
            }
            if(address < 0xE000) {
                if(!(selected & MEM_LC_BANK2)) {
                    // D123 + 1000 = e123 & 5fff = 4123 (aux bank1)
                    // d123               & 5fff = 5123 (aux bank2)
                    // So set bank 1 address to exxx.
                    address += 0x1000;
                }
                return m->ram.RAM_LC[(address & 0x5fff)];
            }
            // exxx-ffff & 7fggg = aux lc ram
            return m->ram.RAM_LC[(address & 0x7fff)];
        default:
            assert(0);
            return 0;
    }
}

static inline uint64_t read_last_write_from_selected(APPLE2 *m, uint16_t address, int selected) {
    switch(tst_flags(selected, (MEM_MAIN | MEM_AUX))) {
        case MEM_MAPPED_6502:
            return m->pages.last_write_pages[address / PAGE_SIZE][address % PAGE_SIZE];
        case MEM_MAIN:
            if(address < 0xD000) {
                return m->ram.RAM_LAST_WRITE[address];
            }
            if(address < 0xE000) {
                if(!(selected & MEM_LC_BANK2)) {
                    // D123 + 1000 = e123 & 1fff =  123 (bank1)
                    // d123               & 1fff = 1123 (bank2)
                    // So set bank 1 address to exxx.
                    address += 0x1000;
                }
                return m->ram.RAM_LC_LAST_WRITE[(address & 0x1fff)];
            }
            // LC 0x2000+
            return m->ram.RAM_LC_LAST_WRITE[(address & 0x3fff)];
        case MEM_AUX:
            if(address < 0xD000) {
                return m->ram.RAM_LAST_WRITE[address + 0x10000];
            }
            if(address < 0xE000) {
                if(!(selected & MEM_LC_BANK2)) {
                    // D123 + 1000 = e123 & 5fff = 4123 (aux bank1)
                    // d123               & 5fff = 5123 (aux bank2)
                    // So set bank 1 address to exxx.
                    address += 0x1000;
                }
                return m->ram.RAM_LC_LAST_WRITE[(address & 0x5fff)];
            }
            // exxx-ffff & 7fggg = aux lc ram
            return m->ram.RAM_LC_LAST_WRITE[(address & 0x7fff)];
        default:
            assert(0);
            return 0;
    }
}

static inline void write_to_memory(APPLE2 *m, uint16_t address, uint8_t value) {
    size_t page = address / PAGE_SIZE;
    size_t offset = address % PAGE_SIZE;
    assert(page < m->pages.num_pages);
    uint8_t cb_mask = m->pages.watch_write_pages[page][offset];
    uint64_t last_write = m->pages.last_write_pages[page][offset];
    last_write <<= 16;
    last_write |= m->cpu.opcode_pc;
    m->pages.last_write_pages[page][offset] = last_write;
    if(cb_mask) {
        if(cb_mask & WATCH_IO_PORT) {
            apple2_softswitch_write_callback(m, address, value);
        }
        if(cb_mask & WATCH_WRITE_BREAKPOINT) {
            m->a2out_cb.cb_breakpoint_ctx.cb_breakpoint(m->a2out_cb.cb_breakpoint_ctx.user, address, WATCH_WRITE_BREAKPOINT);
        }
    }
    if(!(cb_mask & WATCH_IO_PORT)) {
        m->pages.write_pages[page][offset] = value;
    }
}

static inline void write_to_memory_selected(APPLE2 *m, RAMVIEW_FLAGS selected, uint16_t address, uint8_t value) {
    switch(tst_flags(selected, (MEM_MAIN | MEM_AUX))) {
        case MEM_MAPPED_6502:
            m->pages.write_pages[address / PAGE_SIZE][address % PAGE_SIZE] = value;
            break;
        case MEM_MAIN:
            if(address < 0xD000) {
                m->ram.RAM_MAIN[address] = value;
                break;
            }
            if(address < 0xE000) {
                if(!(selected & MEM_LC_BANK2)) {
                    // D123 + 1000 = e123 & 1fff =  123 (bank1)
                    // d123               & 1fff = 1123 (bank2)
                    // So set bank 1 address to exxx.
                    address += 0x1000;
                }
                m->ram.RAM_LC[(address & 0x1fff)] = value;
                break;
            }
            // LC 0x2000+
            m->ram.RAM_LC[(address & 0x3fff)] = value;
            break;
        case MEM_AUX:
            if(address < 0xD000) {
                m->ram.RAM_MAIN[address + 0x10000] = value;
                break;
            }
            if(address < 0xE000) {
                if(!(selected & MEM_LC_BANK2)) {
                    // D123 + 1000 = e123 & 5fff = 4123 (aux bank1)
                    // d123               & 5fff = 5123 (aux bank2)
                    // So set bank 1 address to exxx.
                    address += 0x1000;
                }
                m->ram.RAM_LC[(address & 0x5fff)] = value;
                break;
            }
            // exxx-ffff & 7fggg = aux lc ram
            m->ram.RAM_LC[(address & 0x7fff)] = value;
            break;
        default:
            assert(0);
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
        if(m->cpu.scratch_lo > 9) {
            m->cpu.scratch_lo += 6;
            m->cpu.scratch_hi++;
        }
        if(m->cpu.scratch_hi > 9) {
            m->cpu.scratch_hi += 6;
            m->cpu.C = 1;
        }
        m->cpu.A = (m->cpu.scratch_hi << 4) | (m->cpu.scratch_lo & 0x0F);
        if(m->cpu.class == CPU_65c02) {
            read_from_memory(m, m->cpu.address_16);
            CYCLE(m);
            set_register_to_value(m, &m->cpu.A, m->cpu.A);
        }
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

// static inline void brk_pc(APPLE2 *m) {
//     m->cpu.pc = 0xFFFE;
//     al_read_pc(m);
// }

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
    a(m);
    sl_read_a16(m);
}

static inline void arw(APPLE2 *m) {
    a(m);
    sl_read_a16(m);
    if(m->cpu.class == CPU_6502) {
        sl_write_a16(m);
    } else {
        sl_read_a16(m);
    }
}

static inline void aix(APPLE2 *m) {
    a(m);
    uint8_t lo = m->cpu.address_lo;
    m->cpu.address_lo += m->cpu.X;
    if(m->cpu.address_lo < lo) {
        if(m->cpu.class == CPU_6502) {
            read_from_memory(m, m->cpu.address_16);
        } else {
            read_from_memory(m, m->cpu.pc - 1);
        }
        m->cpu.address_hi++;
        CYCLE(m);
    }
}

static inline void aipxr(APPLE2 *m) {
    a(m);
    uint8_t lo = m->cpu.address_lo;
    m->cpu.address_lo += m->cpu.X;
    if(m->cpu.class == CPU_6502) {
        read_from_memory(m, m->cpu.address_16);
    } else {
        read_from_memory(m, m->cpu.pc - 1);
    }
    CYCLE(m);
    if(m->cpu.address_lo < lo) {
        m->cpu.address_hi++;
    }
}

static inline void aixrr(APPLE2 *m) {
    aix(m);
    sl_read_a16(m);
    sl_read_a16(m);
}

static inline void aipxrw(APPLE2 *m) {
    aipxr(m);
    sl_read_a16(m);
    if(m->cpu.class == CPU_6502) {
        sl_write_a16(m);
    } else {
        sl_read_a16(m);
    }
}

static inline void aiy(APPLE2 *m) {
    a(m);
    uint8_t lo = m->cpu.address_lo;
    m->cpu.address_lo += m->cpu.Y;
    if(m->cpu.address_lo < lo) {
        if(m->cpu.class == CPU_6502) {
            read_from_memory(m, m->cpu.address_16);
        } else {
            read_from_memory(m, m->cpu.pc - 1);
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
        read_from_memory(m, m->cpu.pc - 1);
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

static inline void mixrw(APPLE2 *m) {
    mix(m);
    sl_read_a16(m);
    if(m->cpu.class == CPU_6502) {
        sl_write_a16(m);
    } else {
        sl_read_a16(m);
    }
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
            read_from_memory(m, m->cpu.pc - 1);
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
        read_from_memory(m, m->cpu.pc - 1);
    }
    CYCLE(m);
    if(m->cpu.address_lo < lo) {
        m->cpu.address_hi++;
    }
}

static inline void miz(APPLE2 *m) {
    al_read_pc(m);
    sl_read_a16(m);
    ah_read_a16_sl2al(m);
}

static inline void mizy(APPLE2 *m) {
    al_read_pc(m);
    read_a16_ind_y(m);
}

static inline void mrw(APPLE2 *m) {
    al_read_pc(m);
    sl_read_a16(m);
    if(m->cpu.class == CPU_6502) {
        sl_write_a16(m);
    } else {
        sl_read_a16(m);
    }
}

static inline void read_pc_1(APPLE2 *m) {
    read_from_memory(m, m->cpu.pc - 1);
    CYCLE(m);
}

static inline void read_pc(APPLE2 *m) {
    read_from_memory(m, m->cpu.pc);
    CYCLE(m);
}

static inline void unimplemented(APPLE2 *m) {
    m->cpu.cycles = -1;
}

// Pipeline selectors
static inline void aixr_sel(APPLE2 *m) {
    if(m->cpu.class == CPU_6502) {
        aipxrw(m);
    } else {
        aixrr(m);
    }
}

// Instructions
static inline void adc_a16(APPLE2 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
    add_value_to_accumulator(m, m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void adc_imm(APPLE2 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    m->cpu.pc++;
    CYCLE(m);
    add_value_to_accumulator(m, m->cpu.scratch_lo);
    if(m->cpu.class == CPU_65c02 && m->cpu.D) {
        m->cpu.address_16 = 0x56;
    }
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

static inline void bit_imm(APPLE2 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    CYCLE(m);
    m->cpu.Z = (m->cpu.A & m->cpu.scratch_lo) == 0 ? -1 : 0;
    m->cpu.pc++;
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

static inline void bra(APPLE2 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    CYCLE(m);
    m->cpu.address_16 = ++m->cpu.pc;
    branch(m);
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

static inline void a2_brk(APPLE2 *m) {
    // m->cpu.pc = 0xFFFE;
    // a(m);
    // m->cpu.pc = m->cpu.address_16;
    // if(m->cpu.class == CPU_6502) {
    //     // Interrupt flag on at break
    //     m->cpu.flags |= 0b00000100;
    // } else {
    //     m->cpu.flags &= ~0b00001000;
    //     if(m->cpu.flags & 0b00100000) {
    //         // Interrupt flag on at break, if '-' flag is set
    //         m->cpu.flags |= 0b00000100;
    //     }
    // }
    // m->trace = 0;
    // m->run = 0;
    m->a2out_cb.cb_brk_ctx.cb_breakpoint(m->a2out_cb.cb_brk_ctx.user, m->cpu.pc, 0);
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

static inline void dea(APPLE2 *m) {
    read_from_memory(m, m->cpu.pc);
    set_register_to_value(m, &m->cpu.A, m->cpu.A - 1);
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

static inline void ina(APPLE2 *m) {
    read_from_memory(m, m->cpu.pc);
    set_register_to_value(m, &m->cpu.A, m->cpu.A + 1);
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
}

static inline void jmp_ind(APPLE2 *m) {
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

static inline void jmp_ind_x(APPLE2 *m) {
    a(m);
    read_from_memory(m, m->cpu.pc - 2);
    m->cpu.address_16 += m->cpu.X;
    CYCLE(m);
    sl_read_a16(m);
    m->cpu.address_16++;
    m->cpu.scratch_hi = read_from_memory(m, m->cpu.address_16);
    CYCLE(m);
    m->cpu.pc = m->cpu.scratch_16;
}

static inline void jsr_a16(APPLE2 *m) {
    ah_read_pc(m);
    m->cpu.pc = m->cpu.address_16;
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

static inline void phx(APPLE2 *m) {
    push(m, m->cpu.X);
    CYCLE(m);
}

static inline void phy(APPLE2 *m) {
    push(m, m->cpu.Y);
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

static inline void plx(APPLE2 *m) {
    set_register_to_value(m, &m->cpu.X, pull(m));
    CYCLE(m);
}

static inline void ply(APPLE2 *m) {
    set_register_to_value(m, &m->cpu.Y, pull(m));
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

static inline void rol_a(APPLE2 *m) {
    uint8_t c = m->cpu.A & 0x80;
    read_pc(m);
    set_register_to_value(m, &m->cpu.A, (m->cpu.A << 1) | m->cpu.C);
    m->cpu.C = c ? 1 : 0;
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
}

static inline void rts(APPLE2 *m) {
    m->cpu.pc = m->cpu.address_16;
    al_read_pc(m);
}

static inline void sbc_a16(APPLE2 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
    CYCLE(m);
    subtract_value_from_accumulator(m, m->cpu.scratch_lo);
}

static inline void sbc_imm(APPLE2 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    m->cpu.pc++;
    CYCLE(m);
    subtract_value_from_accumulator(m, m->cpu.scratch_lo);

}

static inline void sec(APPLE2 *m) {
    read_pc(m);
    m->cpu.C = 1;
}

static inline void sed(APPLE2 *m) {
    read_pc(m);
    m->cpu.D = 1;
}

static inline void sei(APPLE2 *m) {
    read_pc(m);
    m->cpu.I = 1;
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

static inline void stz_a16(APPLE2 *m, uint8_t value) {
    write_to_memory(m, m->cpu.address_16, value);
    CYCLE(m);
}

static inline void tax(APPLE2 *m) {
    read_pc(m);
    set_register_to_value(m, &m->cpu.X, m->cpu.A);
}

static inline void tay(APPLE2 *m) {
    read_pc(m);
    set_register_to_value(m, &m->cpu.Y, m->cpu.A);
}

static inline void trb(APPLE2 *m) {
    m->cpu.Z = (m->cpu.A & m->cpu.scratch_lo) == 0;
    m->cpu.scratch_lo = (m->cpu.A ^ 0xff) & m->cpu.scratch_lo;
    write_to_memory(m, m->cpu.address_16, m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void tsb(APPLE2 *m) {
    m->cpu.Z = (m->cpu.A & m->cpu.scratch_lo) == 0;
    m->cpu.scratch_lo |= m->cpu.A;
    write_to_memory(m, m->cpu.address_16, m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void tsx(APPLE2 *m) {
    read_pc(m);
    set_register_to_value(m, &m->cpu.X, m->cpu.sp - 0x100);
}

static inline void txa(APPLE2 *m) {
    read_pc(m);
    set_register_to_value(m, &m->cpu.A, m->cpu.X);
}

static inline void txs(APPLE2 *m) {
    read_pc(m);
    m->cpu.sp = 0x100 + m->cpu.X;
}

static inline void tya(APPLE2 *m) {
    read_pc(m);
    set_register_to_value(m, &m->cpu.A, m->cpu.Y);
}
