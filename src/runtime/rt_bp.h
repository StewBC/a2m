// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

typedef struct RUNTIME RUNTIME;

BREAKPOINT *rt_bp_get_at_address(RUNTIME *rt, uint16_t pc, int running);
void rt_bp_callback(void *user, uint16_t address, uint8_t mask);
void rt_bp_apply_masks(RUNTIME *rt);
