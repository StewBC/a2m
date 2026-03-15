// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "hardware_lib.h"

static uint8_t via6522_irq_active(const VIA6522 *via);

static uint8_t via6522_read_port_a_pins(VIA6522 *via) {
    if(!via->owner) {
        return 0;
    }
    return mockingboard_read_via_port_a(via->owner, via->slot, via->pair_index);
}

static uint8_t via6522_read_port_a_data(VIA6522 *via) {
    uint8_t input = via6522_read_port_a_pins(via);
    return (uint8_t)((via->ora & via->ddra) | (input & (uint8_t)~via->ddra));
}

static uint8_t via6522_irq_active(const VIA6522 *via) {
    uint8_t active = (uint8_t)((via->ifr & via->ier) & 0x7Fu);

    if((active & VIA6522_IFR_T1) && via->owner &&
       via->owner->cpu.cycles < via->t1_irq_visible_cycle) {
        active &= (uint8_t)~VIA6522_IFR_T1;
    }

    return (uint8_t)(active != 0);
}

static uint8_t via6522_read_ifr(const VIA6522 *via) {
    uint8_t value = via->ifr;

    if(via6522_irq_active(via)) {
        value |= VIA6522_IFR_IRQ;
    }

    return value;
}

static void via6522_set_ifr(VIA6522 *via, uint8_t bits) {
    uint8_t before = via->ifr;
    via->ifr |= (bits & 0x7Fu);
}

static void via6522_clear_ifr(VIA6522 *via, uint8_t bits) {
    via->ifr &= (uint8_t)~(bits & 0x7Fu);
}

static uint8_t via6522_read_ier_spec(const VIA6522 *via) {
    return via->ier | 0x80;
}

static uint8_t via6522_t2_uses_board_startup_reload(const VIA6522 *via) {
    return via->board_t2_startup_free_run;
}

static void via6522_handle_t2_board_startup_underflow(VIA6522 *via) {
    // Mockingboard reset-time T2 countdown is a detection aid only.
    // Keep the counter moving without surfacing a synthetic IRQ source.
    via->t2_counter = via->t2_latch;
    via->t2_fired = 0;
}

static void via6522_set_t2_irq_armed(VIA6522 *via, uint8_t value) {
    via->t2_irq_armed = value;
}

static void via6522_set_t1_running(VIA6522 *via, uint8_t value) {
    via->t1_running = value;
}

static void via6522_set_t1_irq_armed(VIA6522 *via, uint8_t value) {
    via->t1_irq_armed = value;
}

static void via6522_set_t1_fired(VIA6522 *via, uint8_t value) {
    via->t1_fired = value;
}

static uint8_t via6522_t2_uses_pb6_pulse_count(const VIA6522 *via) {
    return (uint8_t)((via->acr & 0x20) != 0);
}

static uint8_t via6522_is_qualifying_edge(uint8_t old_level, uint8_t new_level, uint8_t positive_edge) {
    if(positive_edge) {
        return (uint8_t)(!old_level && new_level);
    }
    return (uint8_t)(old_level && !new_level);
}

static void via6522_handle_t2_timeout(VIA6522 *via, uint64_t cycle, const char *event) {
    via->t2_fired = 1;
    if(via->t2_irq_armed && !(via->ifr & VIA6522_IFR_T2)) {
        via6522_set_ifr(via, VIA6522_IFR_T2);
    }
    via->t2_counter = 0xFFFF;
    via6522_set_t2_irq_armed(via, 0);
}

static void via6522_t2_count_pulse(VIA6522 *via, uint64_t cycle) {
    uint16_t old_counter;

    if(via6522_t2_uses_board_startup_reload(via) || !via6522_t2_uses_pb6_pulse_count(via) || !via->t2_running) {
        return;
    }

    old_counter = via->t2_counter;
    via->t2_counter--;

    if(old_counter == 0) {
        via6522_handle_t2_timeout(via, cycle, "T2_UNDERFLOW_PB6");
    }
}

static inline uint16_t via6522_compose_latch(uint8_t hi, uint8_t lo) {
    return (uint16_t)(((uint16_t)hi << 8) | lo);
}

static uint8_t via6522_read_requires_timer_freshness(uint8_t reg) {
    switch(reg & 0x0F) {
        case VIA6522_REG_T1CL:
        case VIA6522_REG_T1CH:
        case VIA6522_REG_T2CL:
        case VIA6522_REG_T2CH:
        case VIA6522_REG_IFR:
            return 1;

        default:
            return 0;
    }
}

static void via6522_write_t1_latch_lo(VIA6522 *via, uint8_t value) {
    via->t1_latch_lo = value;
    via->t1_latch = via6522_compose_latch(via->t1_latch_hi, via->t1_latch_lo);
}

static void via6522_write_t1_latch_hi(VIA6522 *via, uint8_t value) {
    via->t1_latch_hi = value;
    via->t1_latch = via6522_compose_latch(via->t1_latch_hi, via->t1_latch_lo);
}

static void via6522_load_timer1_counter(VIA6522 *via) {
    uint64_t cycle = via->owner ? via->owner->cpu.cycles : 0;
    uint8_t had_t1 = via->ifr & VIA6522_IFR_T1;
    via->t1_counter = via->t1_latch;
    via6522_set_t1_running(via, 1);
    via6522_set_t1_irq_armed(via, 1);
    via6522_set_t1_fired(via, 0);
    via->t1_reload_pending = 0;
    via->t1_irq_visible_cycle = 0;
    via->t1_read_latched = 0;
    via->t1_just_loaded = 1;
    // T1 becomes active on the cycle after the high-byte write completes.
    via->t1_load_cycle = cycle + 1;
    via6522_clear_ifr(via, VIA6522_IFR_T1);
    if(had_t1) {
    }
}

static void via6522_step_timer1(VIA6522 *via, uint32_t cycles) {
    uint64_t cycle_cursor = 0;
    uint64_t cycle_now = via->owner ? via->owner->cpu.cycles : (via->timer_last_cycle + cycles);

    if(!via->t1_running) {
        return;
    }

    cycle_cursor = via->timer_last_cycle;

    if(via->t1_just_loaded && via->owner) {
        uint64_t active_cycle = via->t1_load_cycle;
        uint64_t batch_start = cycle_cursor;
        uint64_t batch_end = via->owner->cpu.cycles;

        if(active_cycle >= batch_end) {
            via->t1_just_loaded = 0;
            return;
        }

        if(active_cycle > batch_start) {
            uint32_t skipped = (uint32_t)(active_cycle - batch_start);
            if(skipped >= cycles) {
                via->t1_just_loaded = 0;
                return;
            }
            cycles -= skipped;
            cycle_cursor = active_cycle;
        }

        via->t1_just_loaded = 0;
    }

    while(cycles > 0) {
        if(via->t1_reload_pending) {
            // For one-shot T1, the first post-underflow boundary still exposes
            // $FFFF. The latch becomes visible on the following boundary,
            // without decrementing again in that same cycle.
            cycles--;
            cycle_cursor++;
            via->t1_counter = via->t1_latch;
            via->t1_reload_pending = 0;
            continue;
        }

        uint16_t old_counter = via->t1_counter;
        cycles--;
        cycle_cursor++;
        via->t1_counter--;

        if(old_counter != 0) {
            continue;
        }

        if(via->t1_running) {
            if(via->acr & 0x40) {
                if(via->t1_irq_armed && !(via->ifr & VIA6522_IFR_T1)) {
                    via6522_set_ifr(via, VIA6522_IFR_T1);
                }
                // PB7 output behavior remains deferred; current T1 timing work
                // models reload/IRQ semantics only.
                via->t1_counter = via->t1_latch;
                via6522_set_t1_irq_armed(via, 1);
                via6522_set_t1_fired(via, 0);
            } else {
                via->t1_reload_pending = 1;
                if(via->t1_irq_armed && !(via->ifr & VIA6522_IFR_T1)) {
                    via6522_set_ifr(via, VIA6522_IFR_T1);
                    via->t1_irq_visible_cycle = cycle_cursor + 1;
                    via6522_set_t1_irq_armed(via, 0);
                }
                via6522_set_t1_fired(via, 1);
            }
        } else {
            return;
        }
    }
}

static void via6522_step_timer2(VIA6522 *via, uint32_t cycles) {
    uint64_t cycle_cursor = via->timer_last_cycle;
    uint64_t cycle_now = via->owner ? via->owner->cpu.cycles : (via->timer_last_cycle + cycles);

    if(via6522_t2_uses_pb6_pulse_count(via) && !via6522_t2_uses_board_startup_reload(via)) {
        return;
    }

    if(via->t2_just_loaded && via->owner) {
        uint64_t active_cycle = via->t2_load_cycle;
        uint64_t batch_start = cycle_cursor;
        uint64_t batch_end = cycle_now;

        if(active_cycle >= batch_end) {
            via->t2_just_loaded = 0;
            return;
        }

        if(active_cycle > batch_start) {
            uint32_t skipped = (uint32_t)(active_cycle - batch_start);
            if(skipped >= cycles) {
                via->t2_just_loaded = 0;
                return;
            }
            cycles -= skipped;
        }
        via->t2_just_loaded = 0;
    }

    while(cycles > 0) {
        if(!via->t2_running) {
            return;
        }

        uint32_t ticks = (uint32_t)via->t2_counter + 1u;
        if(cycles < ticks) {
            uint16_t old_counter = via->t2_counter;
            via->t2_counter = (uint16_t)(via->t2_counter - cycles);
            {
                uint32_t i;
                uint16_t counter = old_counter;
                for(i = 0; i < cycles; ++i) {
                    uint16_t next_counter = (uint16_t)(counter - 1);
                    counter = next_counter;
                }
            }
            return;
        }

        cycles -= ticks;
        if(via6522_t2_uses_board_startup_reload(via)) {
            via6522_handle_t2_board_startup_underflow(via);
        } else {
            via6522_handle_t2_timeout(via, via->owner ? via->owner->cpu.cycles : 0, "T2_UNDERFLOW_CONTINUE");
        }
    }
    // Exact timeout-boundary timing remains part of the later timing pass.
}

static void via6522_reconcile_timer_observable_state(VIA6522 *via) {
    uint64_t target_cycle;
    uint64_t delta;

    if(!via || !via->owner) {
        return;
    }

    target_cycle = via->owner->cpu.cycles;
    if(target_cycle <= via->timer_last_cycle) {
        return;
    }

    delta = target_cycle - via->timer_last_cycle;
    while(delta > 0) {
        uint32_t step_cycles = delta > UINT32_MAX ? UINT32_MAX : (uint32_t)delta;
        via6522_step_timer1(via, step_cycles);
        via6522_step_timer2(via, step_cycles);
        via->timer_last_cycle += step_cycles;
        delta -= step_cycles;
    }
}

void via6522_reset(VIA6522 *via) {
    APPLE2 *owner = via->owner;
    uint8_t slot = via->slot;
    uint8_t pair_index = via->pair_index;
    uint8_t t1_latch_lo = via->t1_latch_lo;
    uint8_t t1_latch_hi = via->t1_latch_hi;

    memset(via, 0, sizeof(*via));
    via->owner = owner;
    via->slot = slot;
    via->pair_index = pair_index;
    via->t1_latch_lo = t1_latch_lo;
    via->t1_latch_hi = t1_latch_hi;
    via->t1_latch = via6522_compose_latch(via->t1_latch_hi, via->t1_latch_lo);
    via->port_b_input = 0xFF;
    via->pb6_level = 1;
    via->timer_last_cycle = owner ? owner->cpu.cycles : 0;
    via->t1_counter = 0xFFFF;
    via->t2_counter = 0xFFFF;
    via->orb = 0xFF;
    via6522_set_t1_running(via, 0);
    via6522_set_t1_irq_armed(via, 0);
    via6522_set_t1_fired(via, 0);
    via6522_set_t2_irq_armed(via, 0);
    via6522_clear_ifr(via, VIA6522_IFR_SR);
}

void via6522_mockingboard_power_on_timer1(VIA6522 *via) {
    via->t1_latch_lo = 0xFF;
    via->t1_latch_hi = 0xFF;
    via->t1_latch = 0xFFFF;
    via->t1_counter = 0xFFFF;
    via->timer_last_cycle = via->owner ? via->owner->cpu.cycles : 0;
    via->t1_read_hi = 0xFF;
    via->t1_read_latched = 0;
    via->t1_just_loaded = 0;
    via->t1_reload_pending = 0;
    via->t1_irq_visible_cycle = 0;
    via->t1_load_cycle = 0;
    via->t1_running = 1;
    via->t1_irq_armed = 0;
    via->t1_fired = 1;
    via->t1_reload_pending = 0;
    via6522_clear_ifr(via, VIA6522_IFR_T1);
}

void via6522_mockingboard_power_on_timer2(VIA6522 *via) {
    via->t2_latch_lo = 0xFF;
    via->t2_latch_hi = 0xFF;
    via->t2_latch = 0xFFFF;
    via->t2_counter = 0xFFFF;
    via->timer_last_cycle = via->owner ? via->owner->cpu.cycles : 0;
    via->t2_load_cycle = 0;
    via->t2_running = 1;
    via6522_set_t2_irq_armed(via, 0);
    via->t2_fired = 0;
    via->t2_just_loaded = 0;
    via->board_t2_startup_free_run = 1;
    via6522_clear_ifr(via, VIA6522_IFR_T2);
}

uint8_t via6522_irq_pending(const VIA6522 *via) {
    return via6522_irq_active(via);
}

void via6522_set_pb6_level(VIA6522 *via, uint8_t level) {
    uint8_t previous_level = via->pb6_level;
    uint8_t new_level = (uint8_t)(level ? 1 : 0);

    via->pb6_level = new_level;
    if(new_level) {
        via->port_b_input |= 0x40;
    } else {
        via->port_b_input &= (uint8_t)~0x40;
    }
    if(previous_level && !new_level) {
        via6522_t2_count_pulse(via, via->owner ? via->owner->cpu.cycles : 0);
    }
}

uint8_t via6522_read(VIA6522 *via, uint8_t reg) {
    if(via6522_read_requires_timer_freshness(reg)) {
        via6522_reconcile_timer_observable_state(via);
    }
    switch(reg & 0x0F) {
        case VIA6522_REG_ORB:
            return via->orb;

        case VIA6522_REG_ORA:
            return via6522_read_port_a_data(via);

        case VIA6522_REG_ORA_NH:
            return via6522_read_port_a_data(via);

        case VIA6522_REG_DDRB:
            return via->ddrb;

        case VIA6522_REG_DDRA:
            return via->ddra;

        case VIA6522_REG_T1CL:
            {
                via->t1_read_hi = (uint8_t)(via->t1_counter >> 8);
                via->t1_read_latched = 1;
                if(via->ifr & VIA6522_IFR_T1) {
                }
                via6522_clear_ifr(via, VIA6522_IFR_T1);
                uint8_t value = (uint8_t)(via->t1_counter & 0x00FF);
                return value;
            }

        case VIA6522_REG_T1CH:
            {
                uint8_t value;
                uint8_t latched = via->t1_read_latched;

                if(via->t1_read_latched) {
                    via->t1_read_latched = 0;
                    value = via->t1_read_hi;
                } else {
                    value = (uint8_t)(via->t1_counter >> 8);
                }
                return value;
            }

        case VIA6522_REG_T1LL:
            return via->t1_latch_lo;

        case VIA6522_REG_T1LH:
            return via->t1_latch_hi;

        case VIA6522_REG_T2CL:
                via6522_clear_ifr(via, VIA6522_IFR_T2);
                return (via->t2_counter & 0x00FF);

        case VIA6522_REG_T2CH:
            return (uint8_t)(via->t2_counter >> 8);

        case VIA6522_REG_ACR:
            return via->acr;

        case VIA6522_REG_PCR:
            return via->pcr;

        case VIA6522_REG_IFR:
            return via6522_read_ifr(via);

        case VIA6522_REG_IER:
                return via6522_read_ier_spec(via);

        case VIA6522_REG_SR:
            via6522_clear_ifr(via, VIA6522_IFR_SR);
            return via->sr;

        default:
            return 0;
    }
}

void via6522_write(VIA6522 *via, uint8_t reg, uint8_t value) {
    switch(reg & 0x0F) {
        case VIA6522_REG_ORB:
            via->orb = value;
            return;

        case VIA6522_REG_ORA:
            via->ora = value;
            return;

        case VIA6522_REG_ORA_NH:
            via->ora = value;
            return;

        case VIA6522_REG_DDRB:
            via->ddrb = value;
            return;

        case VIA6522_REG_DDRA:
            via->ddra = value;
            return;

        case VIA6522_REG_T1CL:
            via6522_write_t1_latch_lo(via, value);
            return;

        case VIA6522_REG_T1CH:
            via6522_write_t1_latch_hi(via, value);
            via6522_load_timer1_counter(via);
            return;

        case VIA6522_REG_T1LL:
            via6522_write_t1_latch_lo(via, value);
            return;

        case VIA6522_REG_T1LH:
            via6522_write_t1_latch_hi(via, value);
            via6522_clear_ifr(via, VIA6522_IFR_T1);
            return;

        case VIA6522_REG_T2CL:
            via->t2_latch_lo = value;
            via->t2_latch = via6522_compose_latch(via->t2_latch_hi, via->t2_latch_lo);
            return;

        case VIA6522_REG_T2CH:
            via->t2_latch_hi = value;
            via->t2_latch = via6522_compose_latch(via->t2_latch_hi, via->t2_latch_lo);
            via->t2_counter = via->t2_latch;
            via->t2_load_cycle = via->owner ? via->owner->cpu.cycles + 1 : 1;
            via->t2_just_loaded = 1;
            via->t2_running = 1;
            via6522_set_t2_irq_armed(via, 1);
            via->t2_fired = 0;
            via->board_t2_startup_free_run = 0;
            via6522_clear_ifr(via, VIA6522_IFR_T2);
            return;

        case VIA6522_REG_ACR:
            via->acr = value;
            return;

        case VIA6522_REG_PCR:
            via->pcr = value;
            return;

        case VIA6522_REG_IFR:
                via6522_clear_ifr(via, value);
                return;

        case VIA6522_REG_IER:
            if(value & 0x80) {
                via->ier |= (value & 0x7F);
            } else {
                via->ier &= (uint8_t)~(value & 0x7F);
            }
            return;

        case VIA6522_REG_SR:
            via->sr = value;
            return;

        default:
            return;
    }
}

void via6522_step_cycles(VIA6522 *via, uint32_t cycles) {
    uint64_t target_cycle;
    uint32_t delta_cycles;

    // The VIA is advanced at opcode-completion boundaries. `cycles` is the
    // total cycle count consumed by the just-finished CPU opcode, not a
    // sub-cycle stream. Timing-sensitive behavior below is therefore a
    // deliberate opcode-batch approximation.
    // Current batch order is part of the observable discrete model:
    // timers first, then SR, then deferred pulse release.
    if(via->owner) {
        target_cycle = via->owner->cpu.cycles;
        if(target_cycle <= via->timer_last_cycle) {
            return;
        }
        delta_cycles = (uint32_t)(target_cycle - via->timer_last_cycle);
    } else {
        target_cycle = via->timer_last_cycle + cycles;
        delta_cycles = cycles;
    }

    via6522_step_timer1(via, delta_cycles);
    via6522_step_timer2(via, delta_cycles);
    via->timer_last_cycle = target_cycle;
}

