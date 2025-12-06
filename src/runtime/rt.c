// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "rt_lib.h"

#define TARGET_FPS              60
#define OPCODE_JSR              0x20
#define OPCODE_RTS              0x60

int rt_update(RUNTIME *rt);

void rt_apply_ini(RUNTIME *rt, INI_STORE *ini_store) {
    int slot_number = -1;

    const char *val;
    if((val = ini_get(ini_store, "machine", "turbo"))) {
        const char *s = val;
        // Count commas
        rt->turbo_count = 1;
        while(*s) {
            if(*s++ == ',') {
                rt->turbo_count++;
            }
        }
        // Make an array to hold turbo values
        rt->turbo = malloc(rt->turbo_count * sizeof(double));
        if(!rt->turbo) {
            rt->turbo_count = 0;
            return;
        }
        s = val;
        for(int i = 0; i < rt->turbo_count; i++) {
            // Skip leading spaces in the field
            while(*s == ' ' || *s == '\t') {
                s++;
            }
            if(*s == ',' || *s == '\0') {
                // Empty value: turbo = 1.0
                rt->turbo[i] = 1.0;
            } else {
                char *end;
                errno = 0;
                double v = strtod(s, &end);

                if(end == s) {
                    // No digits before the comma is a bad conversion: max speed
                    rt->turbo[i] = -1.0;
                } else {
                    // Got a number
                    v = fabs(v);

                    if(errno == ERANGE || isinf(v)) {
                        // Error or overflow, set to max speed
                        rt->turbo[i] = -1.0;
                    } else {
                        rt->turbo[i] = v;
                    }

                    s = end;
                }
            }
            // Move to next field: skip until comma or end
            while(*s && *s != ',') {
                s++;
            }
            if(*s == ',') {
                s++; // skip comma
            }
        }
    }

    INI_SECTION *s = ini_find_section(ini_store, "debug");
    if(s) {
        for(int i = 0; i < s->kv.items; i++) {
            INI_KV *kv = ARRAY_GET(&s->kv, INI_KV, i);
            const char *key = kv->key;
            const char *val = kv->val;
            if(0 == stricmp(key, "break")) {
                PARSEDBP parsed_line;
                memset(&parsed_line, 0, sizeof(PARSEDBP));
                if(A2_OK == parse_breakpoint_line(val, &parsed_line)) {
                    BREAKPOINT *bp = rt_bp_get_at_address(rt, (uint16_t)parsed_line.start, 0);
                    if(!bp) {
                        BREAKPOINT bp;
                        memset(&bp, 0, sizeof(bp));
                        bp.address = (uint16_t)parsed_line.start;
                        bp.address_range_end = (uint16_t)parsed_line.end;
                        bp.use_range = bp.address != bp.address_range_end;
                        bp.use_pc = MODE_PC == parsed_line.mode;
                        bp.break_on_read = MODE_READ & parsed_line.mode ? 1 : 0;
                        bp.break_on_write = MODE_WRITE & parsed_line.mode ? 1 : 0;
                        // SQW Look at why you set this up the way it is
                        bp.access = (bp.break_on_write << 2) | (bp.break_on_read << 1);
                        bp.counter_stop_value = parsed_line.count;
                        bp.counter_reset = parsed_line.reset;
                        bp.use_counter = bp.counter_stop_value || bp.counter_reset ? 1 : 0;
                        bp.action = parsed_line.action;
                        bp.type_text = parsed_line.type_text;
                        bp.slot = parsed_line.slot;
                        bp.device = parsed_line.device;
                        ARRAY_ADD(&rt->breakpoints, bp);
                        // SQW temp solution
                        if(parsed_line.action == ACTION_TRON) {
                            if(!rt->trace_log.file.is_file_open) {
                                // Assume average 2 cycle opcodes and 1 seconds of capture
                                rt_trace_init(rt, "./trace.txt", 1 * (int)CPU_FREQUENCY / 2);
                            }
                        }
                    } else {
                        free(parsed_line.type_text);
                    }
                } else {
                    free(parsed_line.type_text);
                }
            }
        }
    }
}

void rt_bind(RUNTIME *rt, APPLE2 *m, UI *ui) {
    // Bind to the Apple 2 - This needs to be here because some
    // bindings are UI specific so what UI needs to be resolved first
    A2OUT_CB a2rt_cb = {
        .cb_breakpoint_ctx =    {(void *)rt, rt_bp_callback},
        .cb_brk_ctx =           {(void *)rt, (cb_breakpoint)rt_brk_callback},
        .cb_diskactivity_ctx =  {(void *)ui, (cb_diskread)ui->ops->disk_read_led, (cb_diskwrite)ui->ops->disk_write_led},
        .cb_speaker_ctx =       {(void *)ui, (cb_speaker)ui->ops->speaker_toggle},
        .cb_inputdevice_ctx =   {(void *)ui, (cb_ptrig)ui->ops->ptrig, (cb_read_button)ui->ops->read_button, (cb_read_axis)ui->ops->read_axis}, // user, ptrig, button, axis
        .cb_clipboard_ctx =     {(void *)rt, (cb_clipboard)rt_feed_clipboard_key},
        .cb_trace_ctx =         {(void *)rt, (cb_trace)rt_trace},
    };

    // Set up the access helpers
    rt->m = m;
    ui->ops->set_runtime(ui, rt);

    // The hardware is configured, so start binding
    apple2_set_callbacks(m, &a2rt_cb);
    rt_bp_apply_masks(rt);
};

int rt_init(RUNTIME *rt, INI_STORE *ini_store) {

    // Clear everything
    memset(rt, 0, sizeof(RUNTIME));

    // Set the dynamic array up to hold breakpoints
    ARRAY_INIT(&rt->breakpoints, BREAKPOINT);

    // Init the symbol table
    rt_sym_init(rt, ini_store);

    // See if turbo settings or breakpoints came in at startup
    rt_apply_ini(rt, ini_store);

    // Create Turbo states if ini didn't
    if(!rt->turbo_count) {
        rt->turbo_count = 2;
        rt->turbo = (double *)malloc(rt->turbo_count * sizeof(double));
        if(rt->turbo) {
            rt->turbo[0] = 1.0;
            rt->turbo[1] = -1.0;
        } else {
            return A2_ERR;
        }
    }
    // Set the turbo speed
    rt->turbo_active = rt->turbo[rt->turbo_index];

    // Set to run
    rt->run = 1;

    return A2_OK;
}

int rt_run(RUNTIME *rt, APPLE2 *m, UI *ui) {
    int ret_val = A2_OK;
    int quit = 0;
    const uint64_t pfreq = SDL_GetPerformanceFrequency();
    const uint64_t ticks_per_ms = pfreq / 1000;
    const double clock_cycles_per_tick = CPU_FREQUENCY / (double)pfreq;
    const uint64_t ticks_per_frame = pfreq / TARGET_FPS;
    uint64_t overhead_ticks = 0; // Assume emulation and rendering fit in 1 frame's time
    while(!quit) {
        uint64_t frame_start_ticks = SDL_GetPerformanceCounter();
        uint64_t desired_frame_end_ticks = frame_start_ticks + ticks_per_frame;
        int dirty_view = 0;

        if(rt->run) {
            dirty_view = rt->run;
            // If not going at max speed, or stepping the debugger
            if(rt->turbo_active > 0.0) {
                double cycles_per_frame = max(1, (CPU_FREQUENCY * rt->turbo_active) / TARGET_FPS - (overhead_ticks * clock_cycles_per_tick));
                uint64_t cycles = 0;
                while(cycles < cycles_per_frame) {
                    // See if a breakpoint was hit (will clear rt->run)
                    if(!rt_update(rt)) {
                        break;
                    }
                    size_t opcode_cycles = machine_run_opcode(m);
                    ui->ops->speaker_on_cycles(ui, opcode_cycles);
                    cycles += opcode_cycles;
                }
            } else {
                uint64_t emulation_cycles = frame_start_ticks + ticks_per_frame - overhead_ticks;
                while(SDL_GetPerformanceCounter() < emulation_cycles) {
                    // See if a breakpoint was hit (will clear rt->run)
                    if(!rt_update(rt)) {
                        break;
                    }
                    size_t opcode_cycles = machine_run_opcode(m);
                    ui->ops->speaker_on_cycles(ui, opcode_cycles);
                }
            }
        }

        // after a run_to_pc, the pc the debugger will want to show should be the cpu pc
        // ui.debugger.cursor_pc = m->cpu.pc;

        quit = ui->ops->process_events(ui, m);
        ui->ops->set_shadow_flags(ui, m->state_flags);
        ui->ops->render(ui, m, dirty_view);

        uint64_t frame_end_ticks = SDL_GetPerformanceCounter();
        // Delay to get the MHz emulation right for the desired FPS - no vsync
        if(frame_end_ticks < desired_frame_end_ticks) {
            uint32_t sleep_ms = (desired_frame_end_ticks - frame_end_ticks) / ticks_per_ms;
            // SQW Debug fix
            if(sleep_ms > (1000 / TARGET_FPS)) {
                sleep_ms = (1000 / TARGET_FPS) - 1;
                desired_frame_end_ticks = SDL_GetPerformanceCounter();
            }
            // Sleep for coarse delay
            if(sleep_ms > 1) {
                SDL_Delay(sleep_ms - 1);
            }
            // The emulation + rendering fit in a frame, no compensation
            overhead_ticks = 0;
        } else {
            // Over-ran the frame
            // In MHz mode, do more emulation per frame to hit the MHz desired
            // In max turbo, do less emulation to try and keep the FPS target
            overhead_ticks = frame_end_ticks - desired_frame_end_ticks;
        }
        // Tail spin for accuracy
        while(SDL_GetPerformanceCounter() < desired_frame_end_ticks) {
        }
    }

    return ret_val;
}

void rt_shutdown(RUNTIME *rt) {
    rt->turbo_count = 0;
    free(rt->turbo);
    rt->turbo = NULL;
    // trace before symbols as trace uses the symbols
    rt_trace_shutdown(rt);
    rt_sym_shutdown(rt);
}


void rt_machine_reset(RUNTIME *rt) {
    APPLE2 *m = rt->m;
    rt->run = 1;
    rt->run_to_pc = 0;
    apple2_machine_reset(m);
    diskii_reset(m);
}

void rt_brk_callback(RUNTIME *rt, uint16_t address, uint8_t mask) {
    rt->run = 0;
}


int rt_disassemble_line(RUNTIME *rt, uint16_t *address, int selected, char symbol_view, char *str_buf, int str_buf_len) {
    APPLE2 *m = rt->m;
    char address_symbol[11];
    uint16_t pc = *address;
    char *text = str_buf;
    int remain = str_buf_len;
    uint8_t instruction = read_from_memory_selected(m, pc, selected);
    int length = opcode_lengths[instruction];
    *address += length;
    int prt_len;
    uint16_t operands;
    char *symbol = symbol_view == SYMBOL_VIEW_NONE ? 0 : rt_sym_find_symbols(rt, pc);
    if(!symbol) {
        symbol = "                                ";
    }
    prt_len = snprintf(text, remain, "%04X: ", pc);
    adjust(text, remain, prt_len);
    prt_len = snprintf(text, remain, "%-*.*s ", SYMBOL_COL_LEN - 1, SYMBOL_COL_LEN - 1, symbol);
    adjust(text, remain, prt_len);
    switch(length) {
        case 1:
            prt_len = snprintf(text, remain, "%02X        %s", instruction, opcode_text[instruction]);
            adjust(text, remain, prt_len);
            break;
        case 2:
            operands = read_from_memory_selected(m, pc + 1, selected);
            prt_len = snprintf(text, remain, "%02X %02X     %s", instruction, operands, opcode_text[instruction]);
            adjust(text, remain, prt_len);
            // Decode the class to decide if a symbol lookup is needed
            // SQW 65c02 probably needs something here...
            switch(instruction & 0x0f) {
                case 0x00:                                          // Branches, adjusted (destination) lookup
                    if(symbol_view || instruction == 0xa0 || instruction == 0xc0 || instruction == 0xe0) {
                        symbol = 0;
                    } else {
                        symbol = rt_sym_find_symbols(rt, pc + 2 + (int8_t) operands);
                        // If no symbol but symbol view is on - resolve to an address and pretend that's the symbol
                        if(!symbol) {
                            snprintf(address_symbol, 11, "$%02X [%04X]", (uint8_t) operands, (uint16_t)(pc + 2 + (int8_t) operands));
                            symbol = address_symbol;
                        }
                    }
                    break;
                case 0x09:                                          // Immediate - no lookup
                    symbol = 0;
                    break;
                default:                                            // just look for the byte address
                    symbol = symbol_view ? 0 : rt_sym_find_symbols(rt, operands);
                    break;
            }
            if(!symbol) {
                prt_len = snprintf(text, remain, opcode_hex_params[instruction], operands);
            } else {
                prt_len = snprintf(text, remain, opcode_symbol_params[instruction], symbol);
            }
            adjust(text, remain, prt_len);
            break;
        case 3: {
                uint8_t al = read_from_memory_selected(m, pc + 1, selected);
                uint8_t ah = read_from_memory_selected(m, pc + 2, selected);
                operands = ((ah << 8) | al);
                prt_len = snprintf(text, remain, "%02X %02X %02X  %s", instruction, al, ah, opcode_text[instruction]);
                adjust(text, remain, prt_len);
                symbol = symbol_view & SYMBOL_VIEW_MARGIN ? 0 : rt_sym_find_symbols(rt, operands);
                if(!symbol) {
                    snprintf(text, remain, opcode_hex_params[instruction], operands);
                } else {
                    snprintf(text, remain, opcode_symbol_params[instruction], symbol);
                }
                break;
            }
    }
    return str_buf_len - remain;
}

// Debugger control
void rt_machine_pause(RUNTIME *rt) {
    rt->run = 0;
}

void rt_machine_run(RUNTIME *rt) {
    rt->run = 1;
    // rt->run_to_pc = 0;
}

void rt_machine_run_to_pc(RUNTIME *rt, uint16_t pc) {
    rt->run = 1;
    rt->run_to_pc = 1;
    rt->pc_to_run_to = pc;
}

void rt_machine_set_pc(RUNTIME *rt, uint16_t pc) {
    rt->m->cpu.pc = pc;
}

void rt_machine_set_sp(RUNTIME *rt, uint16_t sp) {
    rt->m->cpu.sp = sp;
}

void rt_machine_set_A(RUNTIME *rt, uint8_t A) {
    rt->m->cpu.A = A;
}

void rt_machine_set_X(RUNTIME *rt, uint8_t X) {
    rt->m->cpu.X = X;
}

void rt_machine_set_Y(RUNTIME *rt, uint8_t Y) {
    rt->m->cpu.Y = Y;
}

void rt_machine_set_flags(RUNTIME *rt, uint8_t flags) {
    rt->m->cpu.flags = flags;
}

void rt_machine_step(RUNTIME *rt) {
    rt->run = 1;
    rt->run_step = 1;
    // rt->run_to_pc = 0;
    // rt->run_step_out = 0;
}

void rt_machine_step_out(RUNTIME *rt) {
    APPLE2 *m = rt->m;
    rt->run = 1;
    rt->jsr_counter = 0;
    rt->run_step_out = 1;
}

void rt_machine_step_over(RUNTIME *rt) {
    APPLE2 *m = rt->m;
    uint8_t instruction = read_from_memory_debug(m, m->cpu.pc);
    if(instruction == OPCODE_JSR) {
        int length = opcode_lengths[instruction];
        rt->pc_to_run_to = m->cpu.pc + length;
        rt->run_to_pc = 1;
    }
    rt->run_step = 1;
    rt->run = 1;
}

void rt_machine_stop(RUNTIME *rt) {
    rt->run = 0;
    rt->run_step = 0;
    rt->run_step_out = 0;
    rt->run_to_pc = 0;
    rt->pc_to_run_to = 0;
    rt->jsr_counter = 0;
}

void rt_machine_toggle_franklin80_active(RUNTIME *rt) {
    APPLE2 *m = rt->m;
    m->franklin80active ^= 1;
}

// Make a copy of the clipboard text
void rt_paste_clipboard(RUNTIME *rt, char *clipboard_text) {
    free(rt->clipboard_text);
    rt->clipboard_text = strdup(clipboard_text);
    rt->clipboard_index = 0;
    rt_feed_clipboard_key(rt);
}

// Put the next clipboard character into the KBD
int rt_feed_clipboard_key(RUNTIME *rt) {
    if(!rt->clipboard_text) {
        return 0;
    }
    APPLE2 *m = rt->m;
    while(1) {
        uint8_t byte = rt->clipboard_text[rt->clipboard_index++];

        // End of buffer: cleanup + clear bit7 of $C000
        if(byte == 0) {
            free(rt->clipboard_text);
            rt->clipboard_text = NULL;
            rt->clipboard_index = 0;
            return 0;
        }
        // ASCII path
        if(byte < 0x80) {
            if(byte == '\n') {
                // Skip LF
                continue;
            }
            if(byte == '\r') {
                byte = 0x0D;           // keep CR
            } else if(byte == '\t') {
                byte = ' ';            // tabs to space
            } else if(!m->model && byte >= 0x20 && byte <= 0x7E) {
                byte = (uint8_t)toupper(byte); // BASIC-friendly
            }
            // Emit whatever we have (including other control chars unchanged)
            m->RAM_MAIN[KBD] = byte | 0x80;
            return 1;
        }

        // Non-ASCII start byte: skip the UTF-8 multibyte sequence (drop it)
        while((rt->clipboard_text[rt->clipboard_index] & 0xC0) == 0x80) {
            rt->clipboard_index++;
        }
    }
}

// This runs before machine step so the logic seems a bit weird
int rt_update(RUNTIME *rt) {
    APPLE2 *m = rt->m;

    // Only look for breakpoints if not stepping
    if(!rt->run_step) {
        if(rt->run_to_pc && rt->pc_to_run_to == m->cpu.pc) {
            rt_machine_stop(rt);
        } else if(rt->run_step_out) {
            uint8_t instruction = read_from_memory_debug(m, m->cpu.pc);
            switch(instruction) {
                case OPCODE_JSR:
                    rt->jsr_counter++;
                    break;
                case OPCODE_RTS:
                    if(--rt->jsr_counter < 0) {
                        rt_machine_stop(rt);           // Stop
                        return 1;                           // But step the RTS
                    }
                    break;
            }
        }
        BREAKPOINT *bp = rt_bp_get_at_address(rt, m->cpu.pc, 1);
        if(bp) {
            if(!bp->action) {
                rt_machine_stop(rt);
            } else {
                switch(bp->action) {
                    // SQW - optimize access
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
        }
    } else if(!rt->run_to_pc) {
        // If stepping, and not stepping over, then stop
        rt_machine_stop(rt);
        return 1;
    } else {
        // Otherwise not stepping, but running, so future breakpoints
        // other than the one on the first step, if there was one, will break again
        rt->run_step = 0;
    }

    if(!rt->run) {
        // When no longer running record the cycles for delta purposes
        rt->prev_stop_cycles = rt->stop_cycles;
        rt->stop_cycles = m->cpu.cycles;
    }
    return rt->run;
}
