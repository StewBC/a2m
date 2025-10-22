// Apple ][+ and //e Emhanced emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#include "header.h"
#include "dbgopcds.h"

#define SYMBOL_COL_LEN      17                              // 10 chars + \0
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

static ASSEMBLER_CONFIG local_assembler_config;

#ifdef _WIN32
char *strndup(const char *string, size_t length) {
    char *string_copy = (char *)malloc(length + 1);
    if(string_copy) {
        strncpy(string_copy, string, length);
        string_copy[length] = '\0';
    }
    return string_copy;
}
#endif


int viewdbg_add_symbol(DEBUGGER *d, const char *symbol_source, const char *symbol_name, size_t symbol_name_length, uint16_t address, int overwrite) {
    // Find where to insert the symbol
    enum {ACTION_NONE, ACTION_ADD, ACTION_UPDATE};
    DYNARRAY *bucket = &d->symbols[address & 0xff];
    int symbol_index, action = ACTION_ADD, items = bucket->items;
    for(symbol_index = 0; symbol_index < items; symbol_index++) {
        // Sort the address, low to high, into the array ("bucket)"
        SYMBOL *symbol = ARRAY_GET(bucket, SYMBOL, symbol_index);
        if(address <= symbol->pc) {
            // If the address already has a name, maybe skip it
            action = 2;
            if(address == symbol->pc) {
                if(overwrite) {
                    free(symbol->symbol_name);
                } else {
                    action = ACTION_NONE;
                }
            } else {
                array_copy_items(bucket, symbol_index, items, symbol_index + 1);
            }
            break;
        }
    }
    // Insert the symbol into the array if the address isn't already named
    if(action) {
        if(ACTION_ADD == action) {
            // Append this symbol to the end
            if(A2_OK != array_add(bucket, NULL)) {
                return A2_ERR;
            }
        }
        SYMBOL *symbol = ARRAY_GET(bucket, SYMBOL, symbol_index);
        symbol->pc = address;
        symbol->symbol_source = symbol_source;
        symbol->symbol_name = strndup(symbol_name, symbol_name_length);
        if(!symbol->symbol_name) {
            return A2_ERR;
        }
    }
    return A2_OK;
}

int viewdbg_add_symbols(DEBUGGER *d, const char *symbol_source, char *input, size_t data_length, int overwrite) {
    int state = 0;
    char *end = input + data_length;
    while(input < end) {
        unsigned int value;
        int parsed;
        // Search for a hex string at the start of a line
        if(1 == sscanf(input, "%x%n", &value, &parsed)) {
            uint16_t address = value;
            input += parsed;
            char *symbol_name = input + strspn(input, " \t");
            char *symbol_end = strpbrk(symbol_name, " \t\n\r");
            if(!symbol_end) {
                symbol_end = end;
            }
            int symbol_length = symbol_end - symbol_name;
            input = symbol_end;
            if(A2_OK != viewdbg_add_symbol(d, symbol_source, symbol_name, symbol_length, address, overwrite)) {
                return A2_ERR;
            }
        }
        // Find the end of the line
        while(input < end && *input && !is_newline(*input)) {
            input++;
        }
        // Skip past the end of the line
        while(input < end && *input && is_newline(*input)) {
            input++;
        }
    }
    return A2_OK;
}

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
    char address_symbol[11];
    uint16_t address;
    char *text = line->text;
    DEBUGGER *d = &m->viewport->debugger;
    uint8_t instruction = read_from_memory_debug(m, pc);
    int length = opcode_lengths[instruction];
    char *symbol = d->symbol_view == SYMBOL_VIEW_NONE ? 0 : viewdbg_find_symbols(d, pc);
    line->pc = pc;
    line->length = length;
    if(breakpoint_at(&d->flowmanager, pc, 0)) {
        sprintf(text, "%04X> ", pc);
        line->is_breakpoint = 1;
    } else {
        sprintf(text, "%04X: ", pc);
        line->is_breakpoint = 0;
    }
    text += 6;
    if(!symbol) {
        symbol = "                                ";
    }
    // Output is SYMBOL_COL_LEN-1 long, and I want the space so -2.
    snprintf(text, SYMBOL_COL_LEN, "%-*.*s ", SYMBOL_COL_LEN - 2, SYMBOL_COL_LEN - 2, symbol);
    text += SYMBOL_COL_LEN - 1;
    switch(length) {
        case 1:
            sprintf(text, "%02X           ", instruction);
            text += 9;
            strcpy(text, opcode_text[instruction]);
            break;
        case 2:
            address = read_from_memory_debug(m, pc + 1);
            sprintf(text, "%02X %02X      ", instruction, address);
            text += 9;
            strcpy(text, opcode_text[instruction]);
            text += 4;
            // Decode the class to decide if a symbol lookup is needed
            switch(instruction & 0x0f) {
                case 0x00:                                          // Branches, adjusted (destination) lookup
                    if(d->symbol_view || instruction == 0xa0 || instruction == 0xc0 || instruction == 0xe0) {
                        symbol = 0;
                    } else {
                        symbol = viewdbg_find_symbols(d, pc + 2 + (int8_t) address);
                        // If no symbol but symbol view is on - resolve to an address and pretend that's the symbol
                        if(!symbol) {
                            sprintf(address_symbol, "$%02X [%04X]", (uint8_t) address, (uint16_t)(pc + 2 + (int8_t) address));
                            symbol = address_symbol;
                        }
                    }
                    break;
                case 0x09:                                          // Immediate - no lookup
                    symbol = 0;
                    break;
                default:                                            // just look for the byte address
                    symbol = d->symbol_view ? 0 : viewdbg_find_symbols(d, address);
                    break;
            }
            if(!symbol) {
                sprintf(text, opcode_hex_params[instruction], address);
            } else {
                int tl = CODE_LINE_LENGTH - (text - line->text) - 1;
                snprintf(text, tl, opcode_symbol_params[instruction], symbol);
            }
            break;
        case 3: {
                uint8_t al = read_from_memory_debug(m, pc + 1);
                uint8_t ah = read_from_memory_debug(m, pc + 2);
                address = ((ah << 8) | al);
                sprintf(text, "%02X %02X %02X ", instruction, al, ah);
                text += 9;
                strcpy(text, opcode_text[instruction]);
                text += 4;
                symbol = d->symbol_view & SYMBOL_VIEW_MARGIN ? 0 : viewdbg_find_symbols(d, address);
                if(!symbol) {
                    sprintf(text, opcode_hex_params[instruction], address);
                } else {
                    snprintf(text, CODE_LINE_LENGTH - (text - line->text) - 1, opcode_symbol_params[instruction], symbol);
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
        } else if(sym->pc == address) {
            return sym->symbol_name;
        } else {
            break;
        }
    }
    return 0;
}

int viewdbg_init(DEBUGGER *d, int num_lines) {
    // Allocate the array itself
    d->code_lines = (DYNARRAY *) malloc(sizeof(DYNARRAY));
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
    memset(d->code_lines->data, 0, sizeof(CODE_LINE) * num_lines);
    // Allocate the buffer for the text of each line
    for(int i = 0; i < num_lines; i++) {
        CODE_LINE *code_line = ARRAY_GET(d->code_lines, CODE_LINE, i);
        code_line->text = (char *) malloc(CODE_LINE_LENGTH);
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
    d->symbols = (DYNARRAY *) malloc(sizeof(DYNARRAY) * 256);
    if(!d->symbols) {
        goto error;
    }
    for(int i = 0; i < 256; i++) {
        DYNARRAY *s = &d->symbols[i];
        ARRAY_INIT(s, SYMBOL);
    }

    // Init the search array
    ARRAY_INIT(&d->symbols_search, SYMBOL *);

    // Load the symbols (no error if not loaded)
    UTIL_FILE symbol_file;
    memset(&symbol_file, 0, sizeof(symbol_file));

    // Load the symbol files if they are found
    if(A2_OK == util_file_load(&symbol_file, "symbols/A2_BASIC.SYM", "r")) {
        viewdbg_add_symbols(d, "A2_BASIC", symbol_file.file_data, symbol_file.file_size, 0);
    }
    if(A2_OK == util_file_load(&symbol_file, "symbols/APPLE2E.SYM", "r")) {
        viewdbg_add_symbols(d, "APPLE2E", symbol_file.file_data, symbol_file.file_size, 0);
    }
    if(A2_OK == util_file_load(&symbol_file, "symbols/USER.SYM", "r")) {
        viewdbg_add_symbols(d, "USER", symbol_file.file_data, symbol_file.file_size, 1);
    }
    if(A2_OK != viewdbg_symbol_search_update(d)) {
        goto error;
    }

    // Init the breakpoint structures
    breakpoints_init(&d->flowmanager);

    // Init the assembler structure
    d->assembler_config.auto_run_after_assemble = nk_true;
    d->assembler_config.start_address = 0x2000;
    d->assembler_config.start_address_text_len =
        sprintf(d->assembler_config.start_address_text, "%04X", d->assembler_config.start_address);
    d->assembler_config.dlg_asm_filebrowser = 0;
    array_init(&d->assembler_config.file_browser.dir_contents, sizeof(FILE_INFO));
    return A2_OK;

error:
    for(int i = 0; i < num_lines; i++) {
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
    // 7 is arbitrary but less screws up more - balance speed with success
    uint16_t step_back = 7 * 3;
    while(step_back > 1) {
        uint16_t search_pc = pc - step_back;
        // Walk towards the desired PC
        while(search_pc != pc) {
            uint16_t next_pc = viewdbg_next_pc(m, search_pc);
            // If this matches up exactly, assume it's correct
            if(next_pc == pc) {
                return search_pc;
            }
            // Handle overshooting by comparing distances in a wraparound-safe way
            if((next_pc > pc && (next_pc - pc) < 0x8000) || (next_pc < pc && (pc - next_pc) > 0x8000)) {
                break;                                      // We've overshot the target
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

    switch(e->key.keysym.sym) {
        case SDLK_a:
            if(!v->viewdlg_modal) {
                if((mod & KMOD_CTRL) && !(mod & KMOD_SHIFT)) {
                    if(d->assembler_config.file_browser.dir_selected.name_length) {
                        ASSEMBLER_CONFIG *ac = &d->assembler_config;
                        ASSEMBLER assembler;
                        as = &assembler;

                        errlog_clean();
                        assembler_init(m);
                        if(A2_OK != assembler_assemble(ac->file_browser.dir_selected.name, 0 /*ac->start_address */)) {
                            int loglevel = errorlog.log_level;
                            errorlog.log_level = 1;
                            as->pass = 2;
                            errlog("Could not open file %s to assemble", ac->file_browser.dir_selected.name);
                            errorlog.log_level = loglevel;
                        }
                        viewdbg_remove_symbols(d, "assembler");
                        size_t bucket_index;
                        for(bucket_index = 0; bucket_index < 256; bucket_index++) {
                            size_t symbol_index;
                            DYNARRAY *bucket = &as->symbol_table[bucket_index];
                            for(symbol_index = 0; symbol_index < bucket->items; symbol_index++) {
                                SYMBOL_LABEL *sl = ARRAY_GET(bucket, SYMBOL_LABEL, symbol_index);
                                viewdbg_add_symbol(d, "assembler", sl->symbol_name, sl->symbol_length, sl->symbol_value, 1);
                            }
                        }
                        assembler_shutdown();
                        viewdbg_symbol_search_update(d);

                        if(errorlog.log_array.items) {
                            v->viewdlg_modal = 1;
                            v->dlg_assassembler_errors = 1;
                        } else {
                            if(ac->reset_stack) {
                                m->cpu.sp = 0x1ff;
                            }
                            if(ac->auto_run_after_assemble) {
                                m->cpu.pc = ac->start_address;
                                m->stopped = 0;
                            }
                        }
                    }
                } else if((mod & KMOD_CTRL) && (mod & KMOD_SHIFT)) {
                    local_assembler_config = d->assembler_config;
                    v->viewdlg_modal = 1;
                    v->dlg_assassembler_config = 1;
                }
            }
            break;

        case SDLK_e:
            if(mod & KMOD_CTRL && !v->viewdlg_modal) {
                v->viewdlg_modal = 1;
                v->dlg_assassembler_errors = 1;
            }
            break;

        case SDLK_g:
            if(mod & KMOD_CTRL && !v->viewdlg_modal) {
                global_entry_length = 0;
                v->viewdlg_modal = 1;
                v->dlg_disassembler_go = 1;
            }
            break;

        case SDLK_p:
            if(mod & KMOD_CTRL) {
                m->cpu.pc = v->debugger.cursor_pc;
            }
            break;

        case SDLK_s:
            if(mod & KMOD_CTRL) {
                global_entry_length = 0;
                v->viewdlg_modal = 1;
                v->dlg_symbol_lookup_dbg = 1;
            }
            break;

        case SDLK_TAB:
            d->symbol_view++;
            break;

        case SDLK_HOME:
            d->cursor_pc = ARRAY_GET(d->code_lines, CODE_LINE, 0)->pc;
            break;

        case SDLK_END:
            d->cursor_pc = ARRAY_GET(d->code_lines, CODE_LINE, CODE_LINES_COUNT - 1)->pc;
            break;

        case SDLK_UP:
            d->cursor_pc = viewdbg_prev_pc(m, d->cursor_pc);
            return 1;

        case SDLK_DOWN:
            d->cursor_pc = viewdbg_next_pc(m, d->cursor_pc);
            return 1;

        case SDLK_LEFT:
        case SDLK_RIGHT:
            d->cursor_pc = m->cpu.pc;
            break;

        case SDLK_PAGEUP:
            d->cursor_pc -= ARRAY_GET(d->code_lines, CODE_LINE, d->num_lines - 1)->pc - ARRAY_GET(d->code_lines, CODE_LINE, 0)->pc;
            return 1;

        case SDLK_PAGEDOWN:
            d->cursor_pc += ARRAY_GET(d->code_lines, CODE_LINE, d->num_lines - 1)->pc - ARRAY_GET(d->code_lines, CODE_LINE, 0)->pc;
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
            viewport_toggle_debug(m);                           // Open or close the debug window
            return 1;

        case SDLK_F3:
            if(++m->turbo_index >= m->turbo_count) {
                m->turbo_index = 0;
            }
            m->turbo_active = m->turbo[m->turbo_index];
            return 1;

        case SDLK_F5:
            m->stopped = 0;                                     // Toggle run mode
            break;

        case SDLK_F6:
            if(m->cpu.pc != d->cursor_pc) {
                // This can only happen if emulator is stopped
                viewdbg_set_run_to_pc(m, d->cursor_pc);
                m->stopped = 0;                                 // Put the emulator back in run mode
            }
            break;

        case SDLK_F9:
            if(m->stopped) {
                BREAKPOINT *b = breakpoint_at(&d->flowmanager, d->cursor_pc, 0);
                if(!b) {
                    // Toggle breakpoint
                    BREAKPOINT bp;
                    memset(&bp, 0, sizeof(bp));
                    bp.address = d->cursor_pc;
                    bp.use_pc = bp.break_on_read = bp.break_on_write = 1;
                    bp.counter_stop_value = bp.counter_reset = 1;
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
                        m->stopped = 0;                         // Put the emulator back in run mode
                    } else {
                        // Not a step over, just a step
                        m->stopped = 1;                         // Stop the emulator
                        m->step = 1;                            // Step one opcode
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
                // If the current instruction is a JSR the counter needs to be corrected
                m->stopped = 0;                                 // Clear stopped
                viewdbg_update(m);
                // if on a breakpoint, stopped will now be set, so clear it again
                m->stopped = 0;                                 // Run the emulator to the RTS for this nesting-level
            } else {
                // If run to rts was active, stop that and reset the counter
                // This is needed so the user can initiate a new run to rts
                d->flowmanager.run_to_rts_set = 0;
                m->stopped = 1;                                 // Stop the emulator
                m->step = 1;                                    // Step one opcode
            }
            break;

        case SDLK_F12:
            if(mod & KMOD_SHIFT) {
                // Force switching screen view modes
                int mode = ((m->franklin80active & 1) << 1) + (m->monitor_type & 1);
                if(++mode == 3) {
                    mode = 0;
                }
                m->franklin80active = (mode & 2) >> 1;
                m->monitor_type = mode & 1;
            } else {
                switch(m->screen_mode) {
                    case 0b000:
                    case 0b010:
                    case 0b100:
                    case 0b110:
                        // If a text mode, toggle 80 col mode
                        m->franklin80active ^= 1;                       // 80 col toggle
                        break;
                    default:
                        // Otherwise toggle mono mode
                        m->monitor_type ^= 1;                       // b&w/color toggle
                        break;
                }
            }
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

void viewdbg_remove_symbols(DEBUGGER *d, const char *symbol_source) {
    size_t bucket_index;
    for(bucket_index = 0; bucket_index < 256; bucket_index++) {
        size_t symbol_index = 0;
        DYNARRAY *bucket = &d->symbols[bucket_index];
        while(symbol_index < bucket->items) {
            SYMBOL *symbol = ARRAY_GET(bucket, SYMBOL, symbol_index);
            if(0 == strcmp(symbol_source, symbol->symbol_source)) {
                free(symbol->symbol_name);
                array_remove(bucket, symbol);
                continue;
            }
            symbol_index++;
        }
    }
}

void viewdbg_show(APPLE2 *m) {
    VIEWPORT *v = m->viewport;
    struct nk_context *ctx = v->ctx;
    DEBUGGER *d = &v->debugger;
    int ret;

    // Populate the disassembly lines
    viewdbg_build_code_lines(m, d->cursor_pc, d->num_lines);
    int w = m->viewport->full_window_rect.w - m->viewport->target_rect.w;
    // Now draw the windows showing the lines
    if(nk_begin(ctx, "Disassembly", nk_rect(747, 90, 373, 470), NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_TITLE | NK_WINDOW_BORDER)) {
        DEBUGGER *d = &v->debugger;
        for(int i = 0; i < d->num_lines; i++) {
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
        if(v->dlg_assassembler_config) {
            if((ret = viewdlg_assembler_config(ctx, nk_rect(0, 0, 360, 115), &local_assembler_config))) {
                local_assembler_config.dlg_asm_filebrowser = 0;
                if(ret == 1) {
                    d->assembler_config = local_assembler_config;
                }
                v->dlg_assassembler_config = 0;
                v->viewdlg_modal = 0;
            }
        }
        if(v->dlg_assassembler_errors) {
            if((ret = viewdlg_assembler_errors(ctx, nk_rect(0, 0, 360, 430)))) {
                errlog_clean();
                v->dlg_assassembler_errors = 0;
                v->viewdlg_modal = 0;
            }
        }
        if(v->dlg_disassembler_go) {
            if((ret = viewdlg_hex_address(ctx, nk_rect(60, 10, 280, 80), global_entry_buffer, &global_entry_length))) {
                if(ret == 1) {
                    int value;
                    global_entry_buffer[global_entry_length] = 0;
                    if(1 == sscanf(global_entry_buffer, "%x", &value)) {
                        d->cursor_pc = value;
                    }
                }
                v->dlg_disassembler_go = 0;
                v->viewdlg_modal = 0;
            }
        }
        if(v->dlg_symbol_lookup_dbg) {
            static uint16_t pc = 0;
            if((ret = viewdlg_symbol_lookup(ctx, nk_rect(0, 0, 360, 430), &d->symbols_search, global_entry_buffer, &global_entry_length, &pc))) {
                if(ret == 1) {
                    d->cursor_pc = pc;
                }
                v->dlg_symbol_lookup_dbg = 0;
                v->viewdlg_modal = 0;
            }
        }
    }
    nk_end(ctx);
    if(local_assembler_config.dlg_asm_filebrowser) {
        FILE_BROWSER *fb = &local_assembler_config.file_browser;
        int ret = viewdlg_file_browser(ctx, fb);
        if(ret >= 0) {
            local_assembler_config.dlg_asm_filebrowser = 0;
            if(1 == ret) {
                // A file was selected, so get a FQN
                strncat(fb->dir_selected.name, "/", PATH_MAX - 1);
                strncat(fb->dir_selected.name, fb->file_selected.name, PATH_MAX - 1);
            }
        }
    }
}

void viewdbg_shutdown(DEBUGGER *d) {
    int i;
    for(i = 0; i < d->code_lines->size; i++) {
        free(ARRAY_GET(d->code_lines, CODE_LINE, i)->text);
    }
    array_free(d->code_lines);
    free(d->code_lines);
    d->code_lines = 0;

    for(i = 0; i < 256; i++) {
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

int viewdbg_symbol_sort(const void *lhs, const void *rhs) {
    SYMBOL *symb_lhs = *(SYMBOL **)lhs;
    SYMBOL *symb_rhs = *(SYMBOL **)rhs;
    int result = stricmp(symb_lhs->symbol_source, symb_rhs->symbol_source);
    if(!result) {
        return stricmp(symb_lhs->symbol_name, symb_rhs->symbol_name);
    }
    return result;
}

int viewdbg_symbol_search_update(DEBUGGER *d) {
    size_t index;
    int count = 0;
    DYNARRAY *search = &d->symbols_search;

    // Clear the search (also good if there's an error)
    search->items = 0;
    // See how many symbols there are
    for(index = 0; index < 256; index++) {
        count += d->symbols[index].items;
    }
    // Make room for a list that size
    if(A2_OK != array_resize(search, count)) {
        return A2_ERR;
    }
    // Populate the search list
    for(index = 0; index < 256; index++) {
        size_t bucket_index;
        DYNARRAY *bucket = &d->symbols[index];
        for(bucket_index = 0; bucket_index < bucket->items; bucket_index++) {
            SYMBOL *s = ARRAY_GET(bucket, SYMBOL, bucket_index);
            if(A2_OK != ARRAY_ADD(search, s)) {
                array_free(search);
                return A2_ERR;
            }
        }
    }
    // Sort by source, name
    qsort(search->data, search->items, search->element_size, viewdbg_symbol_sort);
    return A2_OK;
}

void viewdbg_update(APPLE2 *m) {
    VIEWPORT *v = m->viewport;
    if(!v) {
        return;
    }

    DEBUGGER *d = &v->debugger;
    // after a step, the pc the debugger will want to show should be the cpu pc
    d->cursor_pc = m->cpu.pc;
    // See if a breakpoint was hit (Also in step mode so counters can update if needed)
    if(!m->stopped || m->step) {
        m->step = 0;                                        // Set step off
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
                        m->step = 1;                            // Step the RTS
                        d->flowmanager.run_to_rts_set = 0;
                    }
                    break;
            }
        }
        if(breakpoint_at(&d->flowmanager, m->cpu.pc, 1)) {
            m->stopped = 1;
        }
    }
    if(m->stopped && !m->step) {
        v->debugger.prev_stop_cycles = v->debugger.stop_cycles;
        v->debugger.stop_cycles = m->cpu.cycles;
    }
}
