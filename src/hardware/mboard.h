// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

typedef struct {
    VIA6522 via[2];
    AY38910 ay[2];
    uint32_t ay_pending_cycles[2];
    uint8_t ay_bus_state[2];
    uint8_t board_startup_timer_seed_disabled;
} MOCKINGBOARD;

uint8_t mockingboard_read_via_port_a(const APPLE2 *m, uint8_t slot, uint8_t pair_index);
float mockingboard_get_sample(MOCKINGBOARD *mb);
uint8_t mockingboard_irq_pending(APPLE2 *m);
void mockingboard_queue_ay_cycles(MOCKINGBOARD *mb, uint32_t cycles);
void mockingboard_reset(MOCKINGBOARD *mb, int full);
void mockingboard_set_board_startup_timer_seed_enabled(MOCKINGBOARD *mb, uint8_t enabled);
uint8_t mockingboard_read_cn(APPLE2 *m, MOCKINGBOARD *mb, int slot, uint16_t address, uint8_t slot_offset);
void mockingboard_write_cn(APPLE2 *m, MOCKINGBOARD *mb, int slot, uint16_t address, uint8_t slot_offset, uint8_t value);
uint8_t mockingboard_read(APPLE2 *m, MOCKINGBOARD *mb, int slot, uint16_t address, uint8_t reg);
void mockingboard_write(APPLE2 *m, MOCKINGBOARD *mb, int slot, uint16_t address, uint8_t reg, uint8_t value);
