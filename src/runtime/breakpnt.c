// Apple ][+ and //e Emhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "runtime_lib.h"

// Running is 0 when drawing the disassembly, but <> 0 when testing for a break running the emulation
// SQW - This needs updating to show where read/write breakpoints are, and for finding those
// through ini creation
BREAKPOINT *get_breakpoint_at_address(RUNTIME *rt, uint16_t pc, int running) {
    int items = rt->breakpoints.items;
    for(int i = 0; i < items; i++) {
        // Find a breakpoint at address
        BREAKPOINT *bp = ARRAY_GET(&rt->breakpoints, BREAKPOINT, i);
        if(!bp->disabled && bp->use_pc) {
            if(pc == bp->address) {
                // The breakpoint matches the address
                if(running && bp->use_counter) {
                    // If this is a match when executing, and using a counter, test trigger count
                    if(++bp->counter_count >= bp->counter_stop_value) {
                        // Trigger count met, reset counter and install reset count
                        bp->counter_stop_value = bp->counter_reset;
                        bp->counter_count = 0;
                        return bp;
                    }
                } else {
                    // Not running, or not using a counter, this BP matches
                    return bp;
                }
            }
        }
    }
    return 0;
}

void breakpoint_callback(void *user, uint16_t address, uint8_t mask) {
    RUNTIME *rt = (RUNTIME*)user;
    APPLE2 *m = rt->m;
    int items = rt->breakpoints.items;
    // Access triggered breakpoint - find the BP that set this
    for(size_t i = 0; i < items; i++) {
        BREAKPOINT *bp = ARRAY_GET(&rt->breakpoints, BREAKPOINT, i);
        if(!bp->disabled && !bp->use_pc) {
            // Can't be disabled or pc break, must be access break
            if(bp->use_range) {
                // address must be in range to trigger
                if(bp->address > address || bp->address_range_end < address || !(bp->access & m->RAM_WATCH[address])) {
                    continue;
                }
            } else if(bp->address != address) {
                // If not a range PC, address must be exact match
                continue;
            }
            // This is a matching BP now
            if(bp->use_counter) {
                // If using counters, see if it is trigger time
                if(++bp->counter_count >= bp->counter_stop_value) {
                    // Trigger so install reset counter, reset the count and stop the emulation
                    bp->counter_count = 0;
                    bp->counter_stop_value = bp->counter_reset;
                    rt->run = 1;
                    rt->run_to_pc = 0;
                    rt->run_step_out = 0;
                }
            } else {
                // If not using a counter, trigger by stopping the emulation
                rt->run = 1;
                rt->run_to_pc = 0;
                rt->run_step_out = 0;
            }
        }
    }
}

void breakpoint_reapply_address_masks(RUNTIME *rt) {
    APPLE2 *m = rt->m;
    int items = rt->breakpoints.items;
    // Clear all the callback masks for access breakpoints
    for(size_t i = 0; i < BANK_SIZE; i++) {
        m->RAM_WATCH[i] &= ~6;                              // See BREAKPOINT access for reason
    }
    // re-install the masks for the active breakpoints
    for(size_t i = 0; i < items; i++) {
        BREAKPOINT *bp = ARRAY_GET(&rt->breakpoints, BREAKPOINT, i);
        if(!bp->disabled && !bp->use_pc) {
            for(uint16_t address = bp->address; address != (bp->address_range_end + 1); address++) {
                m->RAM_WATCH[address] |= bp->access;
            }
        }
    }
}
