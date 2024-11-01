// Apple ][+ emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#include "header.h"

void breakpoints_init(FLOWMANAGER *b) {
    // Clear the whole structure
    memset(b, 0, sizeof(FLOWMANAGER));
    // Set the dynamic array up to hold breakpoints
    ARRAY_INIT(&b->breakpoints, BREAKPOINT);
}

BREAKPOINT *get_breakpoint_at(FLOWMANAGER *b, uint16_t pc) {
    int items = b->breakpoints.items;
    for(int i = 0; i < items; i++) {
        BREAKPOINT *bp = ARRAY_GET(&b->breakpoints, BREAKPOINT, i);
        if(!bp->disabled && bp->condition & CONDITION_PC && pc == bp->pc) {
            return bp;
        }
    }
    return 0;
}
