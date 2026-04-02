// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

typedef struct {
    float left;
    float right;
} MOCKINGBOARD_SAMPLE;

typedef struct {
    AY38910_RENDER_ACCUM left;
    AY38910_RENDER_ACCUM right;
} MOCKINGBOARD_RENDER_ACCUM;

typedef struct {
    VIA6522 via[2];
    AY38910 ay[2];
    uint32_t ay_pending_cycles[2];
    uint8_t ay_bus_state[2];
    uint8_t board_startup_timer_seed_disabled;
    MOCKINGBOARD_RENDER_ACCUM render_accum;
    MOCKINGBOARD_SAMPLE rendered_sample;
} MOCKINGBOARD;

uint8_t mockingboard_read_via_port_a(const APPLE2 *m, uint8_t slot, uint8_t pair_index);
uint8_t mockingboard_is_audibly_idle(const MOCKINGBOARD *mb);
void mockingboard_reconcile_audio_state(MOCKINGBOARD *mb);
MOCKINGBOARD_SAMPLE mockingboard_peek_stereo_sample(const MOCKINGBOARD *mb);
MOCKINGBOARD_SAMPLE mockingboard_render_audio_sample(MOCKINGBOARD *mb, uint32_t cpu_cycles);
void mockingboard_render_accum_reset(MOCKINGBOARD_RENDER_ACCUM *accum);
void mockingboard_render_accum_add_current(const MOCKINGBOARD *mb, MOCKINGBOARD_RENDER_ACCUM *accum, uint32_t chip_cycles);
MOCKINGBOARD_SAMPLE mockingboard_render_accum_output(const MOCKINGBOARD_RENDER_ACCUM *accum);
MOCKINGBOARD_SAMPLE mockingboard_get_stereo_sample(MOCKINGBOARD *mb);
uint8_t mockingboard_irq_pending(APPLE2 *m);
void mockingboard_queue_ay_cycles(MOCKINGBOARD *mb, uint32_t cycles);
void mockingboard_reset(MOCKINGBOARD *mb, int full);
void mockingboard_set_board_startup_timer_seed_enabled(MOCKINGBOARD *mb, uint8_t enabled);
uint8_t mockingboard_read_cn(APPLE2 *m, MOCKINGBOARD *mb, int slot, uint16_t address, uint8_t slot_offset);
void mockingboard_write_cn(APPLE2 *m, MOCKINGBOARD *mb, int slot, uint16_t address, uint8_t slot_offset, uint8_t value);
uint8_t mockingboard_read(APPLE2 *m, MOCKINGBOARD *mb, int slot, uint16_t address, uint8_t reg);
void mockingboard_write(APPLE2 *m, MOCKINGBOARD *mb, int slot, uint16_t address, uint8_t reg, uint8_t value);
