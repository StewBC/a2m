// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "rt_lib.h"

static inline int rt_bp_addr_is_aux(APPLE2 *m, uint16_t address, uint8_t mask) {
    A2_STATE s = m->state_flags;
    int use_aux;

    if(address < 0x0200) {
        return (s & A2S_ALTZP) != 0;
    }
    if(address < 0xC000) {
        use_aux = (mask & WATCH_WRITE_BREAKPOINT) ? (s & A2S_RAMWRT) : (s & A2S_RAMRD);
        if(s & A2S_80STORE) {
            if(address >= 0x0400 && address < 0x0800) {
                use_aux = (s & A2S_PAGE2);
            } else if((s & A2S_HIRES) && address >= 0x2000 && address < 0x4000) {
                use_aux = (s & A2S_PAGE2);
            }
        }
        return use_aux != 0;
    }

    // For LC mapping, AUX is controlled by ALTZP
    return (s & A2S_ALTZP) != 0;
}

static inline int rt_bp_match_view(APPLE2 *m, VIEW_FLAGS vf, uint16_t address, uint8_t mask) {
    A2_STATE s = m->state_flags;
    A2SEL_48K sel_ram = vf_get_ram(vf);

    // I/O page: bank selection doesn't apply
    if(address >= 0xC000 && address <= 0xC0FF) {
        return 1;
    }

    // 0000-BFFF
    if(address < 0xC000) {
        if(sel_ram == A2SEL48K_MAPPED) {
            return 1;
        }
        return (sel_ram == A2SEL48K_AUX) == (rt_bp_addr_is_aux(m, address, mask) != 0);
    }

    // C100-CFFF
    if(address < 0xD000) {
        if(vf_get_c100(vf) == A2SELC100_MAPPED) {
            return 1;
        }
        // ROM selected
        return (s & A2S_CXSLOTROM_MB_ENABLE) != 0;
    }

    // D000-FFFF
    if(vf_get_d000(vf) == A2SELD000_MAPPED) {
        return 1;
    }

    // LC active depends on access type
    int lc_active = (mask & WATCH_WRITE_BREAKPOINT) ? (s & A2S_LC_WRITE) : (s & A2S_LC_READ);
    if(!lc_active) {
        return vf_get_d000(vf) == A2SELD000_ROM;
    }

    // LC is active
    if(vf_get_d000(vf) == A2SELD000_ROM) {
        return 0;
    }

    // Enforce MAIN/AUX selection for LC if it was chosen
    if(sel_ram != A2SEL48K_MAPPED) {
        int lc_is_aux = (s & A2S_ALTZP) != 0;
        if((sel_ram == A2SEL48K_AUX) != lc_is_aux) {
            return 0;
        }
    }

    if(address < 0xE000) {
        int bank2 = (s & A2S_LC_BANK2) != 0;
        if(vf_get_d000(vf) == A2SELD000_LC_B2) {
            return bank2;
        }
        if(vf_get_d000(vf) == A2SELD000_LC_B1) {
            return !bank2;
        }
        return 0;
    }

    // E000-FFFF: LC bank does not matter
    return vf_get_d000(vf) == A2SELD000_LC_B1 || vf_get_d000(vf) == A2SELD000_LC_B2;
}

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

static inline void rt_breakpoint_hit(RUNTIME *rt, BREAKPOINT *bp) {
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

        if(!rt_bp_match_view(rt->m, bp->selected_bank, address, WATCH_EXEC_BREAKPOINT)) {
            continue;
        }
        rt_breakpoint_hit(rt, bp);
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
        if(!bp->disabled && bp->access_mask & mask) {
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

            if(!rt_bp_match_view(m, bp->selected_bank, address, mask)) {
                continue;
            }
            rt_breakpoint_hit(rt, bp);
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
            m->ram.RAM_WATCH[address] &= ~(WATCH_EXEC_BREAKPOINT | WATCH_READ_BREAKPOINT | WATCH_WRITE_BREAKPOINT);
        }
    } else {
        for(;address <= end; address++) {
            m->ram.RAM_WATCH[address] |= bp->access_mask;
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
