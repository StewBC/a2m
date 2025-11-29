// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

typedef struct RUNTIME RUNTIME;

// SQW flowmanager is gone - fix names etc.
void flowmanager_init(RUNTIME *rt, INI_STORE *ini_store);
BREAKPOINT *get_breakpoint_at_address(RUNTIME *rt, uint16_t pc, int running);
void breakpoint_callback(void *user, uint16_t address, uint8_t mask);
void breakpoint_reapply_address_masks(RUNTIME *rt);
