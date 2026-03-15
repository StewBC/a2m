// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "hardware_lib.h"

enum {
    MOCKINGBOARD_AY_BUS_INACTIVE = 0,
    MOCKINGBOARD_AY_BUS_LATCH_ADDRESS = 1,
    MOCKINGBOARD_AY_BUS_WRITE_DATA = 2,
    MOCKINGBOARD_AY_BUS_READ_DATA = 3,
};

static uint8_t mockingboard_decode_ay_bus_state(uint8_t orb) {
    switch(orb & 0x07) {
        case 0x07:
            return MOCKINGBOARD_AY_BUS_LATCH_ADDRESS;
        case 0x06:
            return MOCKINGBOARD_AY_BUS_WRITE_DATA;
        case 0x05:
            return MOCKINGBOARD_AY_BUS_READ_DATA;
        case 0x04:
        default:
            return MOCKINGBOARD_AY_BUS_INACTIVE;
    }
}

static void mockingboard_apply_via_to_ay(APPLE2 *m, MOCKINGBOARD *mb, int slot,
                                         int pair_index, uint8_t previous_bus_state) {
    VIA6522 *via = &mb->via[pair_index];
    AY38910 *ay = &mb->ay[pair_index];
    uint8_t data = via->ora;
    uint8_t bus_state = mockingboard_decode_ay_bus_state(via->orb);
    // Mockingboard wires ORB.b2 directly to the paired AY /RESET pin.
    if((via->orb & 0x04) == 0) {
        ay38910_reset(ay, CPU_FREQUENCY, CPU_FREQUENCY);
        return;
    }

    // Current validation-backed board translation:
    // - VIA ORA is the AY data bus
    // - VIA ORB command strobes currently observed in software are:
    //   0x04 = inactive, 0x07 = latch address, 0x06 = write data
    switch(bus_state) {
        case MOCKINGBOARD_AY_BUS_LATCH_ADDRESS:
            // AY register-select semantics are immediate bus effects; elapsed
            // time is advanced separately by the explicit batched AY policy.
            ay38910_select_register(ay, data & 0x0F);
            break;

        case MOCKINGBOARD_AY_BUS_WRITE_DATA:
            // AY data writes apply to the currently selected register without
            // stepping time here; the chip itself is advanced in batches at
            // opcode boundaries.
            ay38910_write_selected(ay, data);
            break;

        case MOCKINGBOARD_AY_BUS_READ_DATA:
            break;

        case MOCKINGBOARD_AY_BUS_INACTIVE:
        default:
            break;
    }
}

static void mockingboard_step_ay_devices(MOCKINGBOARD *mb, uint32_t cycles) {
    // Explicit AY batching policy:
    // advance both PSGs once per completed CPU opcode using the elapsed opcode
    // cycle count. AY-visible bus actions remain immediate register effects at
    // access time; they do not reintroduce per-cycle stepping or separate
    // access-time freshness work.
    ay38910_step_cycles(&mb->ay[0], cycles);
    ay38910_step_cycles(&mb->ay[1], cycles);
}

static int mockingboard_cn_decode(uint8_t slot_offset, uint8_t *via_index, uint8_t *via_reg) {
    if((slot_offset & 0x70) == 0x00 && (slot_offset & 0x80) == 0x00) {
        *via_index = 0;
        *via_reg = slot_offset & 0x0F;
        return 1;
    }
    if((slot_offset & 0x70) == 0x10 && (slot_offset & 0x80) == 0x80) {
        *via_index = 1;
        *via_reg = slot_offset & 0x0F;
        return 1;
    }
    if((slot_offset & 0x70) == 0x10 && (slot_offset & 0x80) == 0x00) {
        *via_index = 0;
        *via_reg = slot_offset & 0x0F;
        return 1;
    }
    if((slot_offset & 0x70) == 0x00 && (slot_offset & 0x80) == 0x80) {
        *via_index = 1;
        *via_reg = slot_offset & 0x0F;
        return 1;
    }
    return 0;
}

static uint8_t mockingboard_board_startup_timer_seed_enabled(const MOCKINGBOARD *mb) {
    return (uint8_t)(mb->board_startup_timer_seed_disabled ? 0 : 1);
}

static void mockingboard_apply_board_startup_timer_seed(MOCKINGBOARD *mb) {
    if(!mockingboard_board_startup_timer_seed_enabled(mb)) {
        return;
    }

    // Observed Apple II + Mockingboard behavior shows the VIA timers as
    // effectively running after board power-on/reset. This is modeled here as
    // board startup state, not as generic 6522 reset behavior.
    via6522_mockingboard_power_on_timer1(&mb->via[0]);
    via6522_mockingboard_power_on_timer1(&mb->via[1]);
    via6522_mockingboard_power_on_timer2(&mb->via[0]);
    via6522_mockingboard_power_on_timer2(&mb->via[1]);
}

static void mockingboard_bind_via_context(APPLE2 *m, MOCKINGBOARD *mb, int slot, uint8_t via_index) {
    mb->via[via_index].owner = m;
    mb->via[via_index].slot = (uint8_t)slot;
    mb->via[via_index].pair_index = via_index;
    // mb->via[via_index].compat_ier_readback_force_bit7 = mb->megaaudio_ier_bit7_clear ? 0 : 1;
}

uint8_t mockingboard_read_via_port_a(const APPLE2 *m, uint8_t slot, uint8_t pair_index) {
    const MOCKINGBOARD *mb;
    const AY38910 *ay;
    uint8_t value = 0xFF;

    if(!m || slot >= 8 || pair_index >= 2) {
        return 0;
    }

    if(m->slot_cards[slot].slot_type != SLOT_TYPE_MOCKINGBOARD) {
        return 0;
    }

    mb = &m->mockingboard[slot];
    ay = &mb->ay[pair_index];
    if(mb->ay_bus_state[pair_index] == MOCKINGBOARD_AY_BUS_READ_DATA) {
        // AY register readback uses the current selected-register contents.
        // Time advancement for the PSG remains opcode-batched elsewhere.
        value = ay38910_read_selected(ay);
    }

    return value;
}

float mockingboard_get_sample(const MOCKINGBOARD *mb) {
    // Board-level aggregation keeps the AY core's unipolar chip output intact
    // and averages the two PSG instances without centering them here.
    return (ay38910_get_sample(&mb->ay[0]) + ay38910_get_sample(&mb->ay[1])) * 0.5f;
}

uint8_t mockingboard_irq_pending(const APPLE2 *m) {
    for(int slot = 1; slot < 8; slot++) {
        if(m->slot_cards[slot].slot_type != SLOT_TYPE_MOCKINGBOARD) {
            continue;
        }

        if(via6522_irq_pending(&m->mockingboard[slot].via[0]) ||
           via6522_irq_pending(&m->mockingboard[slot].via[1])) {
            return 1;
        }
    }
    return 0;
}

void mockingboard_on_cycles(APPLE2 *m, uint32_t cycles) {
    if(!cycles) {
        return;
    }

    uint8_t slot = m->mb_slot;
    mockingboard_bind_via_context(m, &m->mockingboard[slot], slot, 0);
    mockingboard_bind_via_context(m, &m->mockingboard[slot], slot, 1);
    via6522_step_cycles(&m->mockingboard[slot].via[0], cycles);
    via6522_step_cycles(&m->mockingboard[slot].via[1], cycles);
    mockingboard_step_ay_devices(&m->mockingboard[slot], cycles);
}

void mockingboard_reset(MOCKINGBOARD *mb, int full) {
    mb->ay_bus_state[0] = MOCKINGBOARD_AY_BUS_INACTIVE;
    mb->ay_bus_state[1] = MOCKINGBOARD_AY_BUS_INACTIVE;
    via6522_reset(&mb->via[0]);
    via6522_reset(&mb->via[1]);
    ay38910_reset(&mb->ay[0], CPU_FREQUENCY, CPU_FREQUENCY);
    ay38910_reset(&mb->ay[1], CPU_FREQUENCY, CPU_FREQUENCY);
    if(full) {
        mockingboard_apply_board_startup_timer_seed(mb);
    }
}

void mockingboard_set_board_startup_timer_seed_enabled(MOCKINGBOARD *mb, uint8_t enabled) {
    mb->board_startup_timer_seed_disabled = (uint8_t)(enabled ? 0 : 1);
}

uint8_t mockingboard_read_cn(APPLE2 *m, MOCKINGBOARD *mb, int slot, uint16_t address, uint8_t slot_offset) {
    uint8_t via_index;
    uint8_t via_reg;

    if(mockingboard_cn_decode(slot_offset, &via_index, &via_reg)) {
        mockingboard_bind_via_context(m, mb, slot, via_index);
        return via6522_read(&mb->via[via_index], via_reg);
    }
    // Floating bus value
    return 0xA0;
}

void mockingboard_write_cn(APPLE2 *m, MOCKINGBOARD *mb, int slot, uint16_t address, uint8_t slot_offset, uint8_t value) {
    uint8_t via_index;
    uint8_t via_reg;

    if(!mockingboard_cn_decode(slot_offset, &via_index, &via_reg)) {
        return;
    }

    mockingboard_bind_via_context(m, mb, slot, via_index);
    via6522_write(&mb->via[via_index], via_reg, value);
    if(via_reg == VIA6522_REG_ORB) {
        uint8_t previous_bus_state = mb->ay_bus_state[via_index];
        mb->ay_bus_state[via_index] = mockingboard_decode_ay_bus_state(mb->via[via_index].orb);
        mockingboard_apply_via_to_ay(m, mb, slot, via_index, previous_bus_state);
    }
}

uint8_t mockingboard_read(APPLE2 *m, MOCKINGBOARD *mb, int slot, uint16_t address, uint8_t reg) {
    // Compatibility alias:
    // C0n0-C0n7 -> VIA #0 register window, C0n8-C0nF -> VIA #1 register window.
    uint8_t via_index = (reg >> 3) & 0x01;
    uint8_t via_reg = reg & 0x07;
    mockingboard_bind_via_context(m, mb, slot, via_index);
    return via6522_read(&mb->via[via_index], via_reg);
}

void mockingboard_write(APPLE2 *m, MOCKINGBOARD *mb, int slot, uint16_t address, uint8_t reg, uint8_t value) {
    // Compatibility alias:
    // C0n0-C0n7 -> VIA #0 register window, C0n8-C0nF -> VIA #1 register window.
    uint8_t via_index = (reg >> 3) & 0x01;
    uint8_t via_reg = reg & 0x07;
    mockingboard_bind_via_context(m, mb, slot, via_index);
    via6522_write(&mb->via[via_index], via_reg, value);

    if(via_reg == VIA6522_REG_ORB) {
        uint8_t previous_bus_state = mb->ay_bus_state[via_index];
        mb->ay_bus_state[via_index] = mockingboard_decode_ay_bus_state(mb->via[via_index].orb);
        mockingboard_apply_via_to_ay(m, mb, slot, via_index, previous_bus_state);
    }
}
