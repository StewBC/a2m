// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

typedef struct APPLE2 APPLE2;

enum {
    VIA6522_REG_ORB   = 0x0,
    VIA6522_REG_ORA   = 0x1,
    VIA6522_REG_DDRB  = 0x2,
    VIA6522_REG_DDRA  = 0x3,
    VIA6522_REG_T1CL  = 0x4,
    VIA6522_REG_T1CH  = 0x5,
    VIA6522_REG_T1LL  = 0x6,
    VIA6522_REG_T1LH  = 0x7,
    VIA6522_REG_T2CL  = 0x8,
    VIA6522_REG_T2CH  = 0x9,
    VIA6522_REG_SR    = 0xA,
    VIA6522_REG_ACR   = 0xB,
    VIA6522_REG_PCR   = 0xC,
    VIA6522_REG_IFR   = 0xD,
    VIA6522_REG_IER   = 0xE,
    VIA6522_REG_ORA_NH = 0xF,
};

enum {
    VIA6522_IFR_CA2 = 1u << 0,
    VIA6522_IFR_CA1 = 1u << 1,
    VIA6522_IFR_SR  = 1u << 2,
    VIA6522_IFR_CB2 = 1u << 3,
    VIA6522_IFR_CB1 = 1u << 4,
    VIA6522_IFR_T2  = 1u << 5,
    VIA6522_IFR_T1  = 1u << 6,
    VIA6522_IFR_IRQ = 1u << 7,
};

typedef struct {
    // Mockingboard binding/context. This is not core 6522 chip state.
    APPLE2 *owner;
    uint8_t slot;
    uint8_t pair_index;

    // Core 6522-visible register state.
    uint8_t orb;
    uint8_t ora;
    uint8_t ddrb;
    uint8_t ddra;
    uint8_t acr;
    uint8_t pcr;
    uint8_t ifr;
    uint8_t ier;
    uint8_t sr;

    uint8_t t1_latch_lo;
    uint8_t t1_latch_hi;
    uint8_t t2_latch_lo;
    uint8_t t2_latch_hi;

    uint16_t t1_counter;
    uint16_t t1_latch;
    uint16_t t2_counter;
    uint16_t t2_latch;
    uint64_t timer_last_cycle;
    uint64_t t1_load_cycle;
    uint64_t t1_irq_visible_cycle;
    uint64_t t2_load_cycle;

    uint8_t t1_read_hi;
    uint8_t t1_read_latched;
    uint8_t t1_running;
    uint8_t t1_irq_armed;
    uint8_t t2_irq_armed;
    uint8_t t2_running;
    uint8_t t1_fired;
    uint8_t t2_fired;
    uint8_t t1_reload_pending;
    uint8_t t1_just_loaded;
    uint8_t t2_just_loaded;
    uint8_t port_b_input;
    uint8_t pb6_level;
    uint8_t ca1_level;
    uint8_t ca2_level;
    uint8_t cb1_level;
    uint8_t cb2_level;
    uint8_t ca2_pulse_pending;
    uint8_t cb2_pulse_pending;
    uint8_t sr_active;
    uint8_t sr_shift_count;
    // uint8_t sr_cb1_pulse_pending;
    uint16_t sr_t2_ticks_remaining;

    // Board integration state. This is explicit Mockingboard startup behavior.
    uint8_t board_t2_startup_free_run;

    // Compatibility quirks. These are not pure-spec 6522 behavior.
    // uint8_t compat_ier_readback_force_bit7;

} VIA6522;

void via6522_reset(VIA6522 *via);
void via6522_mockingboard_power_on_timer1(VIA6522 *via);
void via6522_mockingboard_power_on_timer2(VIA6522 *via);
uint8_t via6522_irq_pending(VIA6522 *via);
// void via6522_set_port_b_input(VIA6522 *via, uint8_t value);
// void via6522_set_pb6_level(VIA6522 *via, uint8_t level);
// void via6522_set_ca1_level(VIA6522 *via, uint8_t level);
// void via6522_set_ca2_level(VIA6522 *via, uint8_t level);
// void via6522_set_cb1_level(VIA6522 *via, uint8_t level);
// void via6522_set_cb2_level(VIA6522 *via, uint8_t level);
uint8_t via6522_read(VIA6522 *via, uint8_t reg);
void via6522_write(VIA6522 *via, uint8_t reg, uint8_t value);
void via6522_step_cycles(VIA6522 *via, uint32_t cycles);
