// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "rt_lib.h"

BREAKPOINT *rt_find_breakpoint(RUNTIME *rt, uint16_t address) {
    int items = rt->breakpoints.items;
    for(int i = 0; i < items; i++) {
        // Find a breakpoint at address
        BREAKPOINT *bp = ARRAY_GET(&rt->breakpoints, BREAKPOINT, i);
        if(address == bp->address) {
            return bp;
        }
    }
    return NULL;
}

static inline void rt_breakpoint_exec(RUNTIME *rt, BREAKPOINT *bp) {
    APPLE2 *m = rt->m;

    if(bp->use_counter) {
        // If this is using a counter, test trigger count
        if(++bp->counter_count >= bp->counter_stop_value) {
            // Trigger count met, reset counter and install reset count
            bp->counter_stop_value = bp->counter_reset;
            bp->counter_count = 0;
        } else {
            // Trigger count not met, don't exec the breakpoint yet
            return;
        }
    }

    // Execute the breakpoint action
    switch(bp->action) {
        case ACTION_BREAK:
            rt_machine_stop(rt);
            break;
        case ACTION_FAST:
            rt->turbo_active = -1.0;
            break;
        case ACTION_RESTORE:
            rt->turbo_active = rt->turbo[rt->turbo_index];
            break;
        case ACTION_SLOW:
            rt->turbo_active =  1.0;
            break;
        case ACTION_SWAP: {
                int index = m->diskii_controller[bp->slot].diskii_drive[bp->device].image_index + 1;
                int items = m->diskii_controller[bp->slot].diskii_drive[bp->device].images.items;
                diskii_mount_image(m, bp->slot, bp->device, index >= items ? 0 : index);
            }
            break;
        case ACTION_TROFF:
            rt_trace_off(rt);
            break;
        case ACTION_TRON:
            rt_trace_on(rt);
            break;
        case ACTION_TYPE:
            if(bp->type_text) {
                rt_paste_clipboard(rt, bp->type_text);
            }
            break;
        default:
            break;
    }
}

void rt_exec_matching_breakpoint(RUNTIME *rt, uint16_t address) {
    int items = rt->breakpoints.items;
    for(int i = 0; i < items; i++) {
        // Find a breakpoint at address
        BREAKPOINT *bp = ARRAY_GET(&rt->breakpoints, BREAKPOINT, i);
        if(bp->disabled || !bp->break_on_exec) {
            // Not eligable
            continue;
        }
        if(bp->use_range) {
            if(address < bp->address || address > bp->address_range_end) {
                // Not in range
                continue;
            }
        } else {
            if(address != bp->address) {
                // Not a non-range exat match
                continue;
            }
        }

        rt_breakpoint_exec(rt, bp);
        return;
    }
}

// The callback is only called for read or write, but exec does a read and will trigger also
void rt_bp_callback(void *user, uint16_t address, uint8_t mask) {
    RUNTIME *rt = (RUNTIME *)user;
    APPLE2 *m = rt->m;
    int items = rt->breakpoints.items;
    // Access triggered breakpoint - find the BP that set this
    for(size_t i = 0; i < items; i++) {
        BREAKPOINT *bp = ARRAY_GET(&rt->breakpoints, BREAKPOINT, i);
        if(!bp->disabled && bp->access_mask & (WATCH_READ_BREAKPOINT | WATCH_WRITE_BREAKPOINT)) {
            if(bp->use_range) {
                if(address < bp->address || address > bp->address_range_end) {
                    // Not in range
                    continue;
                }
            } else {
                if(address != bp->address) {
                    // Not a non-range exat match
                    continue;
                }
            }

            rt_breakpoint_exec(rt, bp);
            if(!rt->run) {
                // If the access caused a break, remember where exactly
                rt->access_bp = bp;
                rt->access_address = m->cpu.opcode_pc;
            }
            return;
        }
    }
}

void rt_apply_bp_mask(RUNTIME *rt, BREAKPOINT *bp, int clear) {
    int address, end;
    APPLE2 *m = rt->m;

    address = bp->address;
    if(bp->use_range) {
        end = bp->address_range_end;
    } else {
        end = address;
    }

    if(clear) {
        for(;address <= end; address++) {
            m->RAM_WATCH[address] &= ~(WATCH_EXEC_BREAKPOINT | WATCH_READ_BREAKPOINT | WATCH_WRITE_BREAKPOINT);
        }
    } else {
        for(;address <= end; address++) {
            m->RAM_WATCH[address] |= bp->access_mask;
        }
    }
}

void rt_apply_bp_masks(RUNTIME *rt) {
    int items = rt->breakpoints.items;

    // installthe masks for the active breakpoints
    for(size_t i = 0; i < items; i++) {
        BREAKPOINT *bp = ARRAY_GET(&rt->breakpoints, BREAKPOINT, i);
        if(!bp->disabled) {
            rt_apply_bp_mask(rt, bp, 0);
        }
    }
}