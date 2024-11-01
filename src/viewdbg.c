// Apple ][+ emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#include "header.h"
#include "dbgopcds.h"

#define SYMBOL_COL_LEN      17 // 10 chars + \0
// pc: sym xx xx xx opc sym (+2 because SYMBOL_COL_LEN includes room for a \0)
#define CODE_LINE_LENGTH    (6+SYMBOL_COL_LEN+9+4+SYMBOL_COL_LEN)

// Colors for disassembly code
#define color_bg_cursor         nk_rgb(  0,255,255)
#define color_fg_cursor         nk_rgb(255, 70, 50)
#define color_bg_pc             nk_rgb(255,255,  0)
#define color_fg_pc             nk_rgb(128,  0,128)
#define color_bg_breakpoint     nk_rgb(255,0  ,  0)
#define color_fg_breakpoint     nk_rgb(  0,255,  0)

enum {
    SYMBOL_VIEW_ALL,
    SYMBOL_VIEW_MARGIN_FUNC,
    SYMBOL_VIEW_MARGIN,
    SYMBOL_VIEW_NONE,
};

void viewdbg_build_code_lines(APPLE2 *m, uint16_t pc, int lines_needed) {
    DEBUGGER *d = &m->viewport->debugger;
    int line = lines_needed / 2;
    uint16_t search_pc = pc;
    // Assume the pc is "locked in", so decode forward which is easy
    viewdbg_disassemble_line(m, search_pc, ARRAY_GET(d->code_lines, CODE_LINE, line));
    while(++line < lines_needed) {
        search_pc = viewdbg_next_pc(m, search_pc);
        viewdbg_disassemble_line(m, search_pc, ARRAY_GET(d->code_lines, CODE_LINE, line));
    }
    // Now decode backwards which is harder, making sure to hit PC, no matter what
    d->num_lines = lines_needed;
    line = lines_needed / 2;
    search_pc = pc;
    while(--line >= 0) {
        search_pc = viewdbg_prev_pc(m, search_pc);
        viewdbg_disassemble_line(m, search_pc, ARRAY_GET(d->code_lines, CODE_LINE, line));
    }
}

int viewdbg_disassemble_line(APPLE2 *m, uint16_t pc, CODE_LINE *line) {
    uint16_t address;
    char *text = line->text;
    DEBUGGER *d = &m->viewport->debugger;
    uint8_t instruction = read_from_memory_debug(m, pc);
    int length = opcode_lengths[instruction];
    char *symbol = d->symbol_view == SYMBOL_VIEW_NONE ? 0 : viewdbg_find_symbols(d, pc);
    line->pc = pc;
    line->length = length;
    if(get_breakpoint_at(&d->flowmanager, pc)) {
        sprintf(text,"%04X> ", pc);
        line->is_breakpoint = 1;
    } else {
        sprintf(text,"%04X: ", pc);
        line->is_breakpoint = 0;
    }
    text += 6;
    if(!symbol) {
        symbol = "                                ";
    }
    // Output is SYMBOL_COL_LEN-1 long, and I want the space so -2.
    snprintf(text, SYMBOL_COL_LEN, "%-*.*s ", SYMBOL_COL_LEN-2, SYMBOL_COL_LEN-2, symbol);
    text += SYMBOL_COL_LEN - 1;
    switch(length) {
        case 1:
            sprintf(text, "%02X           ", instruction);
            text += 9;
            sprintf(text, opcode_text[instruction]);
            break;
        case 2:
            address = read_from_memory_debug(m, pc+1);
            sprintf(text, "%02X %02X      ", instruction, address);
            text += 9;
            sprintf(text, opcode_text[instruction]);
            text += 4;
            // Decode the class to decide if a symbol lookup is needed
            switch(instruction & 0x0f) {
                case 0x00:  // Branches, adjusted (destination) lookup
                    symbol = d->symbol_view ? 0 : viewdbg_find_symbols(d, pc + 2 + (int8_t)address);
                    break;
                case 0x09:  // Immediate - no lookup
                    symbol = 0;
                    break;
                default:    // just look for the byte address
                    symbol = d->symbol_view ? 0 : viewdbg_find_symbols(d, address);
                    break;
            }
            if(!symbol) {
                sprintf(text, opcode_hex_params[instruction],address);
            } else {
                int tl = CODE_LINE_LENGTH-(text-line->text)-1;
                snprintf(text, tl, opcode_symbol_params[instruction],symbol);
            }
            break;
        case 3: {
            uint8_t al = read_from_memory_debug(m, pc+1);
            uint8_t ah = read_from_memory_debug(m, pc+2);
            address = ((ah << 8) | al);
            sprintf(text, "%02X %02X %02X ", instruction, al, ah);
            text += 9;
            sprintf(text, opcode_text[instruction]);
            text += 4;
            symbol = d->symbol_view & SYMBOL_VIEW_MARGIN ? 0 : viewdbg_find_symbols(d, address);
            if(!symbol) {
                sprintf(text, opcode_hex_params[instruction], address);
            } else {
                snprintf(text, CODE_LINE_LENGTH-(text-line->text)-1, opcode_symbol_params[instruction], symbol);
            }
            break;
        }
    }
    return length;
}

char *viewdbg_find_symbols(DEBUGGER *d, uint32_t address) {
    DYNARRAY *s = &d->symbols[address & 0xff];
    int items = s->items;
    for(int i = 0; i < items; i++) {
        SYMBOL *sym = ARRAY_GET(s, SYMBOL, i);
        if(sym->pc < address) {
            continue;
        }
        else if(sym->pc == address) {
            return sym->symbol_name;
        } else {
            break;
        }
    }
    return 0;
}

int viewdbg_init(DEBUGGER *d, int num_lines) {
    // Allocate the array itself
    d->code_lines = (DYNARRAY*)malloc(sizeof(DYNARRAY));
    if(!d->code_lines) {
        return A2_ERR;
    }
    // Then size it for the number of entries
    ARRAY_INIT(d->code_lines, CODE_LINE);
    if(A2_OK != array_resize(d->code_lines, num_lines)) {
        array_free(d->code_lines);
        free(d->code_lines);
        return A2_ERR;
    }

    // Set all entries to 0
    memset(d->code_lines->data, 0, sizeof(CODE_LINE)*num_lines);
    // Allocate the buffer for the text of each line
    for(int i=0; i < num_lines; i++) {
        CODE_LINE *code_line = ARRAY_GET(d->code_lines, CODE_LINE, i);
        code_line->text = (char*)malloc(CODE_LINE_LENGTH);
        if(!code_line->text) {
            goto error;
        }
        *code_line->text = 0;
    }
    // Mark all the entries as being items (so 0..31 means 32 items)
    d->code_lines->items = num_lines;

    // Finally, set that this is the height that is being displayed
    d->num_lines = num_lines;

    // Make the 256 symbols "buckets"
    d->symbols = (DYNARRAY*)malloc(sizeof(DYNARRAY) * 256);
    if(!d->symbols) {
        goto error;
    }
    for(int i=0; i < 256; i++) {
        DYNARRAY *s = &d->symbols[i];
        ARRAY_INIT(s, SYMBOL);
    }

    // Load the symbols (no error if not loaded)
    UTIL_FILE fl;
    memset(&fl, 0, sizeof(fl));

    // Load the symbol files if they are found
    if(A2_OK == util_file_load(&fl, "symbols/A2_BASIC.SYM", "r")) {
        util_add_debug_symbols(d, fl.file_data, fl.file_size, 0);
    }
    if(A2_OK == util_file_load(&fl, "symbols/APPLE2E.SYM", "r")) {
        util_add_debug_symbols(d, fl.file_data, fl.file_size, 0);
    }
    if(A2_OK == util_file_load(&fl, "symbols/USER.SYM", "r")) {
        util_add_debug_symbols(d, fl.file_data, fl.file_size, 1);
    }

    // Init the breakpoint structures
    breakpoints_init(&d->flowmanager);
    return A2_OK;

error:
    for(int i=0; i < num_lines; i++) {
        free(ARRAY_GET(d->code_lines, CODE_LINE, i)->text);
    }
    array_free(d->code_lines);
    free(d->code_lines);
    d->code_lines = 0;
    return A2_ERR;
}

uint16_t viewdbg_next_pc(APPLE2 *m, uint16_t pc) {
    uint8_t instruction = read_from_memory_debug(m, pc);
    int length = opcode_lengths[instruction];
    return pc + length;
}

uint16_t viewdbg_prev_pc(APPLE2 *m, uint16_t pc) {
    // Go back at least 7 "lines" (assume 3 byte instructions)
    // 7 is arbitrary but less screwes up more - balance speed with success
    uint16_t step_back = 7 * 3;
    while (step_back > 1) {
        uint16_t search_pc = pc - step_back;
        // Walk towards the desired PC
        while (search_pc != pc) {
            uint16_t next_pc = viewdbg_next_pc(m, search_pc);
            // If this matches up exactly, assume it's correct
            if (next_pc == pc) {
                return search_pc;
            }
            // Handle overshooting by comparing distances in a wraparound-safe way
            if ((next_pc > pc && (next_pc - pc) < 0x8000) ||
                (next_pc < pc && (pc - next_pc) > 0x8000)) {
                break;  // We've overshot the target
            }
            search_pc = next_pc;
        }
        step_back--;
    }
    // Give up and step back one byte
    return pc - 1;
}

int viewdbg_process_event(APPLE2 *m, SDL_Event *e) {
    if(e->type != SDL_KEYDOWN) {
        return 0;
    }

    SDL_Keymod mod = SDL_GetModState();
    VIEWPORT *v = m->viewport;
    DEBUGGER *d = &v->debugger;

    switch (e->key.keysym.sym) {
        case SDLK_g:
            if(mod & KMOD_CTRL && !v->viewdlg_modal) {
                v->viewdlg_modal = 1;
                v->dlg_dissassembler_go = 1;
            }
            break;

        case SDLK_p:
            if(mod & KMOD_CTRL) {
                m->cpu.pc = v->debugger.cursor_pc;
            }
            break;

        case SDLK_TAB:
            d->symbol_view++;
            break;

        case SDLK_UP:
            d->cursor_pc = viewdbg_prev_pc(m, d->cursor_pc);
            return 1;

        case SDLK_DOWN:
            d->cursor_pc = viewdbg_next_pc(m, d->cursor_pc);
            return 1;

        case SDLK_LEFT:
            break;

        case SDLK_RIGHT:
            break;

        case SDLK_PAGEUP:
            d->cursor_pc -= ARRAY_GET(d->code_lines, CODE_LINE, d->num_lines-1)->pc - ARRAY_GET(d->code_lines, CODE_LINE, 0)->pc;
            return 1;

        case SDLK_PAGEDOWN:
            d->cursor_pc += ARRAY_GET(d->code_lines, CODE_LINE, d->num_lines-1)->pc - ARRAY_GET(d->code_lines, CODE_LINE, 0)->pc;
            return 1;

        case SDLK_F1:
            if(v->show_help) {
                m->stopped = v->shadow_stopped;
            } else {
                v->shadow_stopped = m->stopped;
                m->stopped = 1;
            }
            v->show_help ^= 1;
            return 1;

        case SDLK_F2:
            viewport_toggle_debug(m);        // Open or close the debug window
            return 1;

        case SDLK_F3:
            m->free_run ^= 1;               // Toggle free-run / 1 MHz mode
            return 1;

        case SDLK_F5:
            m->stopped = 0;                // Toggle run mode
            break;

        case SDLK_F6:
            if(m->cpu.pc != d->cursor_pc) {
                // This can only happenn if emulator is stopped
                viewdbg_set_run_to_pc(m, d->cursor_pc);
                m->stopped = 0; // Put the emulator back in run mode
            }
            break;

        case SDLK_F9:
            if(m->stopped) {
                BREAKPOINT *b = get_breakpoint_at(&d->flowmanager, d->cursor_pc);
                if(!b) {
                    // Toggle breakpoint
                    BREAKPOINT bp;
                    memset(&bp, 0, sizeof(bp));
                    bp.pc = d->cursor_pc;
                    bp.condition = CONDITION_PC;
                    ARRAY_ADD(&d->flowmanager.breakpoints, bp);
                } else {
                    array_remove(&d->flowmanager.breakpoints, b);
                }
            }
            break;

        case SDLK_F10: {
                // F10 will step, but if JSR will step through (over)
                // if step over is active, F10 will do nothing.  This is so
                // F10 can be held down to step-run the program
                if(!d->flowmanager.run_to_pc_set) {
                    // Only JSR needs a run_to_pc breakpoint set for step-over
                    if(read_from_memory(m, m->cpu.pc) == OPCODE_JSR) {
                        viewdbg_step_over_pc(m, m->cpu.pc);
                        m->stopped = 0; // Put the emulator back in run mode
                    } else {
                        // Not a step over, just a step
                        m->stopped = 1; // Stop the emulator
                        m->step = 1;    // Step one opcode
                    }
                }
            }
            break;

        case SDLK_F11:
            // F11, with or without shift, will stop, even if run to rts is active
            // F11 with shift, otherwise will step out and without shift will just step
            d->flowmanager.run_to_pc_set = 0;
            if(mod & KMOD_SHIFT && !d->flowmanager.run_to_rts_set) {
                d->flowmanager.run_to_rts_set = 1;
                // Reset the counter for this nesting-level
                d->flowmanager.jsr_counter = 0;
                // If the cuurennt instruction is a JSR the counter needs to be corrected
                m->stopped = 0; // Clear stopped
                viewdbg_update(m);
                // if on a breakpoint, stopped will now be set, so clear it again
                m->stopped = 0; // Run the emulator to the RTS for this nesting-level
            } else {
                // If run to rts was active, stop that and reset the counter
                // This is needed so the user can initiate a new run to rts
                d->flowmanager.run_to_rts_set = 0;
                m->stopped = 1; // Stop the emulator
                m->step = 1;    // Step one opcode
            }
            break;

        case SDLK_F12:
            m->monitor_type ^= 1;            // now just b&w/color toggle
            return 1;

        default:
            break;
    }
    return 0;
}

void viewdbg_set_run_to_pc(APPLE2 *m, uint16_t pc) {
    DEBUGGER *d = &m->viewport->debugger;
    d->flowmanager.run_to_pc = pc;
    d->flowmanager.run_to_pc_set = 1;
}

void viewdbg_show(APPLE2* m) {
    VIEWPORT *v = m->viewport;
    struct nk_context *ctx = v->ctx;
    DEBUGGER *d = &v->debugger;

    // Populate the disassembly lines
    viewdbg_build_code_lines(m, d->cursor_pc, d->num_lines);
    int w = m->viewport->full_window_rect.w - m->viewport->target_rect.w;
    // Now draw the windows showing the lines
    if(nk_begin(ctx, "Disassembly", nk_rect(m->viewport->target_rect.w, 90, w, m->viewport->target_rect.h-90),
        NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_TITLE | NK_WINDOW_BORDER))
    {
        DEBUGGER *d = &v->debugger;
        for(int i=0; i < d->num_lines; i++) {
            CODE_LINE *code_line = ARRAY_GET(d->code_lines, CODE_LINE, i);
            uint16_t pc = code_line->pc;
            nk_layout_row_static(ctx, 10, w, 1);
            struct nk_color ob = ctx->style.window.background;
            if(pc == m->cpu.pc) {
                ctx->style.window.background = color_bg_pc;
                nk_label_colored(ctx, ARRAY_GET(d->code_lines, CODE_LINE, i)->text, NK_TEXT_ALIGN_LEFT, color_fg_pc);
            } else if(pc == d->cursor_pc) {
                ctx->style.window.background = color_bg_cursor;
                nk_label_colored(ctx, ARRAY_GET(d->code_lines, CODE_LINE, i)->text, NK_TEXT_ALIGN_LEFT, color_fg_cursor);
            } else if(code_line->is_breakpoint) {
                ctx->style.window.background = color_bg_breakpoint;
                nk_label_colored(ctx, ARRAY_GET(d->code_lines, CODE_LINE, i)->text, NK_TEXT_ALIGN_LEFT, color_fg_breakpoint);
            } else {
                nk_label(ctx, ARRAY_GET(d->code_lines, CODE_LINE, i)->text, NK_TEXT_ALIGN_LEFT);
            }
            ctx->style.window.background = ob;
        }
        if(v->dlg_dissassembler_go) {
            int ret;
            static char address[5] = {0,0,0,0,0};
            static int address_length = 0;
            if((ret = viewdlg_hex_address(ctx, nk_rect(60, 10, 280, 80), address, &address_length))) {
                if(ret == 1) {
                    int value;
                    address[address_length] = 0;
                    if(1 == sscanf(address, "%x", &value)) {
                        d->cursor_pc = value;
                    }
                }
                v->dlg_dissassembler_go = 0;
                v->viewdlg_modal = 0;
            }
        }
    }
    nk_end(ctx);
}

void viewdbg_shutdown(DEBUGGER *d) {
    int i;
    for(i=0; i < d->code_lines->size; i++) {
        free(ARRAY_GET(d->code_lines, CODE_LINE, i)->text);
    }
    array_free(d->code_lines);
    free(d->code_lines);
    d->code_lines = 0;

    for(i=0; i<256; i++) {
        array_free(&d->symbols[i]);
    }
    free(d->symbols);
    d->symbols = 0;
}

void viewdbg_step_over_pc(APPLE2 *m, uint16_t pc) {
    DEBUGGER *d = &m->viewport->debugger;
    uint8_t instruction = read_from_memory_debug(m, pc);
    int length = opcode_lengths[instruction];
    d->flowmanager.run_to_pc = pc + length;
    d->flowmanager.run_to_pc_set = 1;
}

void viewdbg_update(APPLE2 *m) {
    VIEWPORT *v = m->viewport;
    if(!v) {
        return;
    }

    DEBUGGER *d = &v->debugger;
    // after a step, the pc the debugger will want to show should be the cpu pc
    d->cursor_pc = m->cpu.pc;
    // See if a breakpoint was hit
    if(!m->stopped) {
        if(d->flowmanager.run_to_pc_set && d->flowmanager.run_to_pc == m->cpu.pc) {
            m->stopped = 1;
            d->flowmanager.run_to_pc_set = 0;
        } else if(d->flowmanager.run_to_rts_set) {
            uint8_t instruction = read_from_memory_debug(m, m->cpu.pc);
            switch(instruction) {
                case OPCODE_JSR:
                    d->flowmanager.jsr_counter++;
                    break;
                case OPCODE_RTS:
                    if(--d->flowmanager.jsr_counter < 0) {
                        m->stopped = 1;
                        m->step = 1;
                        d->flowmanager.run_to_rts_set = 0;
                    }
                    break;
            }
        }
        if(get_breakpoint_at(&d->flowmanager, m->cpu.pc)) {
            m->stopped = 1;
        }
    }
    if(m->stopped && !m->step) {
        v->debugger.prev_stop_cycles = v->debugger.stop_cycles;
        v->debugger.stop_cycles = m->cpu.cycles;
    }
}
