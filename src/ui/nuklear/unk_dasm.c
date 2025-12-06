// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "unk_lib.h"
#include "dbgopcds.h"

#define SCROLLBAR_W             20
#define ADDRESS_LABEL_H         26
#define ROW_H                   v->font_height

// Colors for disassembly code
#define color_bg_cursor         nk_rgb(  0,255,255)
#define color_fg_cursor         nk_rgb(255, 70, 50)
#define color_bg_pc             nk_rgb(255,255,  0)
#define color_fg_pc             nk_rgb(128,  0,128)
#define color_bg_breakpoint     nk_rgb(255,0  ,  0)
#define color_fg_breakpoint     nk_rgb(  0,255,  0)

static inline struct nk_color unk_dasm_blend(struct nk_color a, struct nk_color b, float t) {
    return nk_rgb(a.r * (1.0f - t) + b.r * t, a.g * (1.0f - t) + b.g * t, a.b * (1.0f - t) + b.b * t);
}

static inline int unk_dasm_circular_delta(uint16_t cursor, uint16_t view) {
    uint16_t u = (uint16_t)(cursor - view);     // always wraps correctly
    if(u <= 0x7FFF) {
        return (int)u;                          // forward short distance
    } else {
        return (int)u - 0x10000;                // backward short distance
    }
}

static inline uint16_t unk_dasm_wrap16_add(uint16_t a, int b) {
    return (uint16_t)(a + b);
}

static inline uint16_t unk_dasm_next_opcode(APPLE2 *m, uint16_t address) {
    uint8_t instruction = read_from_memory_debug(m, address);
    int length = opcode_lengths[instruction];
    return address + length;
}

static inline uint16_t unk_dasm_prev_opcode(APPLE2 *m, uint16_t address) {
    // Assume 3 bytes per instruction, so step "some" lines back
    uint16_t step_back = 4 * 3;
    static uint64_t mx = 0;
    uint64_t iter = 0;
    while(step_back > 1) {
        uint16_t search_pc = address - step_back;
        // Walk towards the desired ADDRESS
        while(search_pc != address) {
            iter++;
            uint16_t next_pc = unk_dasm_next_opcode(m, search_pc);
            // If this matches up exactly, assume it's correct
            if(next_pc == address) {
                if(iter > mx) {
                    mx = iter;
                }
                return search_pc;
            }
            if(unk_dasm_circular_delta(next_pc, address) > 0) {
                break;
            }
            search_pc = next_pc;
        }
        step_back--;
    }
    // Give up and step back one byte
    if(iter > mx) {
        mx = iter;
    }
    return address - 1;
}

void unk_dasm_top_line_address(VIEWDASM *dv, APPLE2 *m, uint16_t address, int lines_from_top, int set_bottom) {
    while(lines_from_top--) {
        address = unk_dasm_prev_opcode(m, address);
    }
    dv->top_address = address;
    if(set_bottom) {
        lines_from_top = 0;
        while(++lines_from_top < dv->rows) {
            address = unk_dasm_next_opcode(m, address);
        }
        dv->bottom_address = address;
    }
}

// SQW this code (very much) needs improving...
void unk_dasm_recenter_view(UNK *v, int dirty) {
    VIEWDASM *dv = &v->viewdasm;
    APPLE2 *m = v->m;
    uint16_t address;
    if(dirty) {
        address = dv->cursor_address = m->cpu.pc;
        // dirty & 2 == run; dirty & 1 == step (run always set if step set)
        // if(dirty) {
        // I do this to lock the cursor to the middle for run, otherwise
        // it really doesn't look good to see the cursor flying around
        unk_dasm_top_line_address(dv, m, address, dv->rows / 2, 0);
        return;
        // }
    } else {
        address = dv->cursor_address;
    }

    int top_delta = unk_dasm_circular_delta(address, dv->top_address);
    int bottom_delta = unk_dasm_circular_delta(address, dv->bottom_address);
    // if(dirty && top_delta > 0 && bottom_delta < 0) {
    //     return;
    // }

    // if(dirty && (top_delta < dv->rows || bottom_delta > dv->rows)) {
    //     unk_dasm_top_line_address(dv, m, address, min(dv->rows, 2), 0);
    //     return;
    // }

    if(top_delta <= 0) {
        do {
            address = unk_dasm_prev_opcode(v->m, address);
            top_delta = unk_dasm_circular_delta(address, dv->top_address);
        } while(top_delta > 0);
        dv->top_address = address;
    } else {
        do {
            address = unk_dasm_next_opcode(v->m, address);
            bottom_delta = unk_dasm_circular_delta(address, dv->bottom_address);
        } while(bottom_delta < 0);
        unk_dasm_top_line_address(dv, m, address, dv->rows - 1, 0);
    }
}

void unk_dasm_cursor_down(UNK *v, VIEWDASM *dv) {
    if(dv->cursor_field != CURSOR_ADDRESS) {
        dv->cursor_address = unk_dasm_next_opcode(v->m, dv->cursor_address);
        unk_dasm_recenter_view(v, 0);
    }
}

void unk_dasm_cursor_end(UNK *v, VIEWDASM *dv, int mod) {
    if(dv->cursor_field == CURSOR_ADDRESS) {
        dv->cursor_digit = CURSOR_DIGIT3;
        return;
    }

    if(mod & KMOD_CTRL) {
        unk_dasm_top_line_address(dv, v->m, 0xFFFF, dv->rows - 1, 1);
    }
    dv->cursor_address = dv->bottom_address;
}

void unk_dasm_cursor_home(UNK *v, VIEWDASM *dv, int mod) {
    if(dv->cursor_field == CURSOR_ADDRESS) {
        dv->cursor_digit = CURSOR_DIGIT0;
        return;
    }

    if(mod & KMOD_CTRL) {
        dv->top_address = 0x0000;
    }
    dv->cursor_address = dv->top_address;
}

void unk_dasm_cursor_left(UNK *v, VIEWDASM *dv) {
    if(dv->cursor_field == CURSOR_ADDRESS) {
        if(dv->cursor_digit != CURSOR_DIGIT0) {
            dv->cursor_digit--;
        }
        return; // no recenter needed
    }

    unk_dasm_recenter_view(v, 0);
}

void unk_dasm_cursor_page_up(UNK *v, VIEWDASM *dv) {
    if(dv->cursor_field != CURSOR_ADDRESS) {
        uint16_t address = dv->top_address;
        unk_dasm_top_line_address(dv, v->m, address, dv->rows - 1, 0);
    }
}

void unk_dasm_cursor_page_down(UNK *v, VIEWDASM *dv) {
    if(dv->cursor_field != CURSOR_ADDRESS) {
        uint16_t address = dv->bottom_address;
        unk_dasm_top_line_address(dv, v->m, address, 0, 0);
    }
}

void unk_dasm_cursor_right(UNK *v, VIEWDASM *dv) {
    if(dv->cursor_field == CURSOR_ADDRESS) {
        if(dv->cursor_digit != CURSOR_DIGIT3) {
            dv->cursor_digit++;
        } else {
            dv->cursor_field = CURSOR_ASCII;
        }
        return; // no view math
    }

    unk_dasm_recenter_view(v, 0);
}

void unk_dasm_cursor_up(UNK *v, VIEWDASM *dv) {
    if(dv->cursor_field != CURSOR_ADDRESS) {
        dv->cursor_address = unk_dasm_prev_opcode(v->m, dv->cursor_address);
        unk_dasm_recenter_view(v, 0);
    }
}

int unk_dasm_init(VIEWDASM *dv) {
    dv->cursor_field = CURSOR_ASCII;

    errlog_init(&dv->errorlog);
    dv->assembler_config.auto_run_after_assemble = nk_true;
    dv->assembler_config.start_address = 0x2000;
    dv->assembler_config.start_address_text_len =
        sprintf(dv->assembler_config.start_address_text, "%04X", dv->assembler_config.start_address);
    dv->assembler_config.dlg_asm_filebrowser = 0;
    array_init(&dv->assembler_config.file_browser.dir_contents, sizeof(FILE_INFO));
    return A2_OK;
}

void unk_dasm_resize_view(UNK *v) {
    VIEWDASM *dv = &v->viewdasm;

    struct nk_context *ctx = v->ctx;
    struct nk_style *style = &ctx->style;
    // calc is from nk_begin_titled
    dv->header_height = 2.0f * style->window.header.padding.y + 2.0f * style->window.header.label_padding.y + v->font_height;
    struct nk_rect parent = v->layout.dasm;

    float view_width = parent.w - 3.0f * ctx->style.window.border - SCROLLBAR_W;
    int cols = view_width / v->font_width;
    dv->cols = cols;
    int rows = (parent.h - (ADDRESS_LABEL_H + dv->header_height)) / ROW_H;
    dv->rows = rows;
    // 52 is more or less what rt_disassemble_line uses to place a line
    int cvt_size = cols > 60 ? cols : 60;
    if(dv->str_buf_len < cvt_size) {
        char *new_cvt_buf = (char *)realloc(dv->str_buf, cvt_size + 1);
        if(new_cvt_buf) {
            dv->str_buf = new_cvt_buf;
            dv->str_buf_len = cvt_size + 1;
        } else {
            free(new_cvt_buf);
        }
    }
    unk_dasm_recenter_view(v, 0);
}

void unk_dasm_process_event(UNK *v, SDL_Event *e) {
    RUNTIME *rt = v->rt;
    APPLE2 *m = v->m;
    SDL_Keymod mod = SDL_GetModState();
    VIEWDASM *dv = &v->viewdasm;

    if(v->show_help) {
        if(e->key.keysym.sym == SDLK_F1) {
            v->show_help = 0;
            if(v->shadow_run) {
                rt_machine_run(rt);
            }
        }
        return;
    }

    if(dv->cursor_field == CURSOR_ADDRESS && !(mod & KMOD_CTRL)) {
        uint8_t key = e->key.keysym.sym;
        if((key >= SDLK_0 && key <= SDLK_9) || (key >= SDLK_a && key <= SDLK_f)) {
            // This is address mode
            key -= key >= SDLK_a ? SDLK_a - 10 : SDLK_0;
            uint8_t shift = (4 * (3 - dv->cursor_digit));
            uint16_t address = dv->cursor_address;
            address &= ~(0x0f << shift);
            address |= (key << shift);
            dv->cursor_address = address;
            unk_dasm_recenter_view(v, 0);
            unk_dasm_cursor_right(v, dv);
            return;
        }
    }

    switch(e->key.keysym.sym) {
        case SDLK_a:
            if(mod & KMOD_CTRL) {
                if(dv->cursor_field == CURSOR_ASCII) {
                    dv->cursor_field = CURSOR_ADDRESS;
                    dv->cursor_digit = CURSOR_DIGIT0;
                    unk_dasm_recenter_view(v, 0);
                } else {
                    dv->cursor_field = CURSOR_ASCII;
                }
            }
            break;

        case SDLK_b:
            if(!v->unk_dlg_modal) {
                if((mod & KMOD_CTRL) && !(mod & KMOD_SHIFT)) {
                    if(dv->assembler_config.file_browser.dir_selected.name_length) {
                        ASSEMBLER_CONFIG *ac = &dv->assembler_config;
                        errlog_clean(&dv->errorlog);

                        // Creat the assembler and init clears it to all 0's
                        ASSEMBLER as;
                        assembler_init(&as, &dv->errorlog, v->m, (output_byte)write_to_memory_selected);
                        // so set any state after init
                        as.selected = ac->flags;
                        // The assembler valid opcodes are 0 = 65c02, so opposite to this
                        as.valid_opcodes = MODEL_APPLE_IIEE - m->model;
                        if(A2_OK != assembler_assemble(&as, ac->file_browser.dir_selected.name, 0)) {
                            as.pass = 2;
                            asm_err(&as, "Could not open file for assembly.");
                        }
                        rt_sym_remove_symbols(rt, "assembler");
                        size_t bucket_index;
                        for(bucket_index = 0; bucket_index < 256; bucket_index++) {
                            size_t symbol_index;
                            DYNARRAY *bucket = &as.symbol_table[bucket_index];
                            for(symbol_index = 0; symbol_index < bucket->items; symbol_index++) {
                                SYMBOL_LABEL *sl = ARRAY_GET(bucket, SYMBOL_LABEL, symbol_index);
                                rt_sym_add_symbol(rt, "assembler", sl->symbol_name, sl->symbol_length, sl->symbol_value, 1);
                            }
                        }
                        assembler_shutdown(&as);
                        rt_sym_search_update(rt);

                        if(dv->errorlog.log_array.items) {
                            v->unk_dlg_modal = 1;
                            v->dlg_assembler_errors = 1;
                        } else {
                            if(ac->reset_stack) {
                                rt_machine_set_sp(rt, 0x1ff);
                            }
                            if(ac->auto_run_after_assemble) {
                                rt_machine_set_pc(rt, ac->start_address);
                                rt_machine_run(rt);
                            }
                        }
                    }
                } else if((mod & KMOD_CTRL) && (mod & KMOD_SHIFT)) {
                    dv->temp_assembler_config = dv->assembler_config;
                    v->unk_dlg_modal = 1;
                    v->dlg_assembler_config = 1;
                }
            }
            break;

        case SDLK_e:
            if(mod & KMOD_CTRL && !v->unk_dlg_modal) {
                v->unk_dlg_modal = 1;
                v->dlg_assembler_errors = 1;
            }
            break;

        case SDLK_p:
            if(mod & KMOD_CTRL) {
                rt_machine_set_pc(rt, v->viewdasm.cursor_address);
            }
            break;

        case SDLK_s:
            if(mod & KMOD_CTRL) {
                global_entry_length = 0;
                v->unk_dlg_modal = 1;
                v->dlg_symbol_lookup_dbg = 1;
            }
            break;

        case SDLK_RETURN:
            if(dv->cursor_field == CURSOR_ADDRESS) {
                dv->cursor_field = CURSOR_ASCII;
            }
            break;

        case SDLK_TAB:
            if(mod & KMOD_SHIFT) {
                if(--dv->symbol_view < SYMBOL_VIEW_ALL) {
                    dv->symbol_view = SYMBOL_VIEW_NONE;
                }
            } else {
                if(++dv->symbol_view > SYMBOL_VIEW_NONE) {
                    dv->symbol_view = SYMBOL_VIEW_ALL;
                }
            }
            break;

        case SDLK_HOME:
            unk_dasm_cursor_home(v, dv, mod);
            break;

        case SDLK_END:
            unk_dasm_cursor_end(v, dv, mod);
            break;

        case SDLK_UP:
            unk_dasm_cursor_up(v, dv);
            break;

        case SDLK_DOWN:
            unk_dasm_cursor_down(v, dv);
            break;

        case SDLK_LEFT:
            unk_dasm_cursor_left(v, dv);
            break;

        case SDLK_RIGHT:
            unk_dasm_cursor_right(v, dv);
            break;

        case SDLK_PAGEUP:
            unk_dasm_cursor_page_up(v, dv);
            break;

        case SDLK_PAGEDOWN:
            unk_dasm_cursor_page_down(v, dv);
            break;

        case SDLK_F1:
            v->show_help = 1;
            v->shadow_run = rt->run;
            rt_machine_pause(rt);
            break;

        case SDLK_F2:
            unk_toggle_debug(v);                           // Open or close the debug window
            break;

        case SDLK_F3:
            if(++rt->turbo_index >= rt->turbo_count) {
                rt->turbo_index = 0;
            }
            rt->turbo_active = rt->turbo[rt->turbo_index];
            break;

        case SDLK_F5:
            rt_machine_run(rt);                                     // Toggle run mode
            break;

        case SDLK_F6:
            if(m->cpu.pc != dv->cursor_address) {
                // This can only happen if emulator is not in run
                rt_machine_run_to_pc(rt, dv->cursor_address);
                // rt_machine_run(rt);                                 // Put the emulator back in run mode
            }
            break;

        case SDLK_F9:
            if(!rt->run) {
                BREAKPOINT *b = rt_bp_get_at_address(rt, dv->cursor_address, 0);
                if(!b) {
                    // Toggle breakpoint
                    BREAKPOINT bp;
                    memset(&bp, 0, sizeof(bp));
                    bp.address = dv->cursor_address;
                    bp.use_pc = bp.break_on_read = bp.break_on_write = 1;
                    bp.counter_stop_value = bp.counter_reset = 1;
                    ARRAY_ADD(&rt->breakpoints, bp);
                } else {
                    array_remove(&rt->breakpoints, b);
                }
            }
            break;

        case SDLK_F10: {
                // F10 will step, but if JSR will step through (over)
                // if step over is active, F10 will do nothing.  This is so
                // F10 can be held down to step-run the program
                if(!rt->run_to_pc) {
                    rt_machine_step_over(rt);
                }
            }
            break;

        case SDLK_F11:
            // F11, with or without shift, will stop, even if run to rts is active
            // F11 with shift, otherwise will step out and without shift will just step
            if(!rt->run && mod & KMOD_SHIFT) {
                rt_machine_step_out(rt);
            } else {
                rt_machine_step(rt);                                    // Step one opcode
            }
            break;

        case SDLK_F12:
            if(mod & KMOD_SHIFT) {
                if(m->franklin80installed) {
                    rt_machine_toggle_franklin80_active(rt);
                }
            } else {
                if(m->dhires && m->col80set && m->hires) {
                    if(++v->monitor_type > MONITOR_RGB) {
                        v->monitor_type = MONITOR_COLOR;
                    }
                } else {
                    v->monitor_type ^= 1;
                }
            }
            break;

        default:
            break;
    }
}

void unk_dasm_show(UNK *v, int dirty) {
    APPLE2 *m = v->m;
    RUNTIME *rt = v->rt;
    VIEWDASM *dv = &v->viewdasm;
    int ret;
    struct nk_context *ctx = v->ctx;
    struct nk_vec2 pad = ctx->style.window.padding;
    struct nk_vec2 spc = ctx->style.window.spacing;
    struct nk_vec2 gpd = ctx->style.window.group_padding;
    float border = ctx->style.window.border;

    // If running need to re-sync
    if(dirty) {
        // but of only stepping, don't move the cursor to the middle
        unk_dasm_recenter_view(v, dirty);
    }
    if(nk_begin(ctx, "Disassembly", v->layout.dasm, NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_TITLE | NK_WINDOW_BORDER)) {
        uint16_t pc = dv->top_address;

        float w_offset = ctx->current->layout->bounds.x;
        ctx->current->layout->bounds.w += w_offset;
        ctx->current->layout->bounds.x = 0;
        ctx->style.window.padding       = nk_vec2(0, 0);
        ctx->style.window.spacing       = nk_vec2(0, 0);
        ctx->style.window.group_padding = nk_vec2(0, 0);
        ctx->style.window.border        = 0.0f;

        nk_layout_row_begin(ctx, NK_STATIC, v->layout.dasm.h, 2);
        nk_layout_row_push(ctx, v->layout.dasm.w - SCROLLBAR_W);
        if(nk_group_begin(ctx, "dasm-view", NK_WINDOW_NO_SCROLLBAR)) {
            nk_layout_row_dynamic(ctx, dv->rows * ROW_H, 1);
            if(nk_group_begin(ctx, "dasm-rows", NK_WINDOW_NO_SCROLLBAR)) {
                nk_layout_row_dynamic(ctx, ROW_H, 1);
                struct nk_rect r = nk_widget_bounds(ctx);
                int cursor_y = 0; // SQW Because the PC is sometimes not on col 0 (asm didn't work out) this remains unset
                struct nk_color ob = ctx->style.window.background;
                uint16_t current_pc = dv->top_address;
                for(int i = 0; i < dv->rows; i++) {
                    current_pc = pc;
                    rt_disassemble_line(rt, &pc, dv->flags, dv->symbol_view, dv->str_buf, dv->str_buf_len);
                    struct nk_color bg = ob;
                    struct nk_color fg = ctx->style.text.color;
                    BREAKPOINT *bp = rt_bp_get_at_address(rt, current_pc, 0);
                    // See if the mouse has been clicked over this row to be drawn
                    if(nk_widget_is_mouse_clicked(ctx, NK_BUTTON_LEFT)) {
                        float rel_x = (ctx->input.mouse.pos.x - r.x) / v->font_width;
                        dv->cursor_address = current_pc;
                        if(rel_x < 4) {
                            dv->cursor_field = CURSOR_ADDRESS;
                            dv->cursor_digit = rel_x;
                        } else {
                            dv->cursor_field = CURSOR_ASCII;
                        }
                    }
                    if(current_pc == m->cpu.pc) {
                        if(dirty) {
                            dv->cursor_address = current_pc;
                        }
                        fg = color_fg_pc;
                        bg = color_bg_pc;
                        if(dv->cursor_address != current_pc) {
                            fg = unk_dasm_blend(color_fg_pc, color_fg_cursor, 0.5);
                            bg = unk_dasm_blend(color_bg_pc, color_bg_cursor, 0.5);
                        }
                        if(bp) {
                            fg = unk_dasm_blend(fg, color_fg_breakpoint, 0.5);
                            bg = unk_dasm_blend(fg, color_bg_breakpoint, 0.5);
                        }
                    } else if(!dirty && current_pc == dv->cursor_address) {
                        fg = color_fg_cursor;
                        bg = color_bg_cursor;
                        if(bp) {
                            fg = unk_dasm_blend(fg, color_fg_breakpoint, 0.5);
                            bg = unk_dasm_blend(fg, color_bg_breakpoint, 0.5);
                        }
                    } else if(bp) {
                        fg = color_fg_breakpoint;
                        bg = color_bg_breakpoint;
                    }
                    if(dv->cursor_field == CURSOR_ADDRESS && current_pc == dv->cursor_address) {
                        // Can't draw the cursor here yet - ctx->current->layout->bounds does
                        // not yet include this row
                        cursor_y = i * ROW_H;
                    }
                    ctx->style.window.background = bg;
                    nk_label_colored(ctx, dv->str_buf, NK_TEXT_LEFT, fg);
                    ctx->style.window.background = ob;
                }
                dv->bottom_address = current_pc;
                if(dv->cursor_field == CURSOR_ADDRESS) {
                    struct nk_rect r = ctx->current->layout->bounds;
                    r.h = ROW_H;
                    r.w = v->font_width;
                    r.y += cursor_y;
                    r.x = ctx->current->layout->at_x + v->font_width * dv->cursor_digit;
                    uint16_t row_address = dv->cursor_address;
                    uint8_t shift = (4 * (3 - dv->cursor_digit));
                    char dc = (row_address >> shift) & 0x0f;
                    dc += (dc > 9) ? 'A' - 10 : '0';
                    struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
                    nk_draw_text(canvas, r, &dc, 1, ctx->style.font, nk_rgb(0, 0, 0), nk_rgb(255, 255, 255));
                }
                nk_group_end(ctx);
            }

            nk_layout_row_begin(ctx, NK_DYNAMIC, 22, 4);
            nk_layout_row_push(ctx, 0.35);
            nk_spacer(ctx);
            nk_layout_row_push(ctx, 0.15);
            if(nk_option_label(ctx, "6502", !tst_flags(dv->flags, MEM_MAIN | MEM_AUX)) && tst_flags(dv->flags, MEM_MAIN | MEM_AUX)) {
                clr_flags(dv->flags, MEM_MAIN);
                clr_flags(dv->flags, MEM_AUX);
                clr_flags(dv->flags, MEM_LC_BANK2);
            }
            if(nk_option_label(ctx, "64K", tst_flags(dv->flags, MEM_MAIN)) && !tst_flags(dv->flags, MEM_MAIN)) {
                clr_flags(dv->flags, MEM_AUX);
                set_flags(dv->flags, MEM_MAIN);
                if(m->lc_bank2_enable) {
                    set_flags(dv->flags, MEM_LC_BANK2);
                } else {
                    clr_flags(dv->flags, MEM_LC_BANK2);
                }
            }
            if(nk_option_label_disabled(ctx, "128K", tst_flags(dv->flags, MEM_AUX), !m->model) && !tst_flags(dv->flags, MEM_AUX)) {
                clr_flags(dv->flags, MEM_MAIN);
                set_flags(dv->flags, MEM_AUX);
                if(m->lc_bank2_enable) {
                    set_flags(dv->flags, MEM_LC_BANK2);
                } else {
                    clr_flags(dv->flags, MEM_LC_BANK2);
                }
            }
            int before = tst_flags(dv->flags, MEM_LC_BANK2);
            int after = nk_option_label_disabled(ctx, "LC Bank2", before, !tst_flags(dv->flags, MEM_MAIN | MEM_AUX));
            if(after != before) {
                if(after) {
                    set_flags(dv->flags, MEM_LC_BANK2);
                } else {
                    clr_flags(dv->flags, MEM_LC_BANK2);
                }
            }
            nk_group_end(ctx);
        }


        // Scrollbar
        nk_layout_row_push(ctx, SCROLLBAR_W);
        struct nk_rect sbar_bounds = v->layout.dasm;
        sbar_bounds.x += sbar_bounds.w - SCROLLBAR_W;
        sbar_bounds.w = SCROLLBAR_W;
        sbar_bounds.h -= (dv->header_height + ADDRESS_LABEL_H);
        sbar_bounds.y += dv->header_height;
        int view_address = dv->top_address;
        int address = view_address;
        if(nk_input_is_mouse_hovering_rect(&ctx->input, v->layout.dasm)) {
            // SQW - the 3 is a "bytes per row to scroll" number
            int wheel = (int)ctx->input.mouse.scroll_delta.y * 3;
            if(wheel) {
                address = (address - wheel) % 0x10000;
                if(address < 0) {
                    address += 0x10000;
                }
            }
        }
        // The dv->rows * 2 is "2 = average bytes per row" - this can actually be calculated for better results
        nk_custom_scrollbarv(ctx, sbar_bounds, 0x10000, dv->rows * 2, &address, &dv->dragging, &dv->grab_offset);
        dv->top_address = address;

        // Restore style padding
        ctx->style.window.padding       = pad;
        ctx->style.window.spacing       = spc;
        ctx->style.window.group_padding = gpd;
        ctx->style.window.border        = border;

        if(v->dlg_assembler_config) {
            dv->temp_assembler_config.model = m->model;
            if((ret = unk_dlg_assembler_config(ctx, nk_rect(0, 0, 360, 140), &dv->temp_assembler_config))) {
                dv->temp_assembler_config.dlg_asm_filebrowser = 0;
                if(ret == 1) {
                    dv->assembler_config = dv->temp_assembler_config;
                }
                v->dlg_assembler_config = 0;
                v->unk_dlg_modal = 0;
            }
        }
        if(v->dlg_assembler_errors) {
            if((ret = unk_dlg_assembler_errors(v, ctx, nk_rect(0, 0, 360, 430)))) {
                errlog_clean(&dv->errorlog);
                v->dlg_assembler_errors = 0;
                v->unk_dlg_modal = 0;
            }
        }
        if(v->dlg_symbol_lookup_dbg) {
            static uint16_t pc = 0;
            if((ret = unk_dlg_symbol_lookup(ctx, nk_rect(0, 0, 360, 430), &rt->symbols_search, global_entry_buffer, &global_entry_length, &pc))) {
                if(ret == 1) {
                    dv->cursor_address = pc;
                    unk_dasm_recenter_view(v, 0);
                }
                v->dlg_symbol_lookup_dbg = 0;
                v->unk_dlg_modal = 0;
            }
        }
    }
    nk_end(ctx);
    if(dv->temp_assembler_config.dlg_asm_filebrowser) {
        FILE_BROWSER *fb = &dv->temp_assembler_config.file_browser;
        int ret = unk_dlg_file_browser(ctx, fb);
        if(ret >= 0) {
            array_free(&dv->temp_assembler_config.file_browser.dir_contents);
            dv->temp_assembler_config.dlg_asm_filebrowser = 0;
            if(1 == ret) {
                // A file was selected, so get a FQN
                strncat(fb->dir_selected.name, "/", PATH_MAX - 1);
                strncat(fb->dir_selected.name, fb->file_selected.name, PATH_MAX - 1);
            }
        }
    }
}

void unk_dasm_shutdown(VIEWDASM *dv) {
    int i;
    free(dv->str_buf);
    dv->str_buf = 0;
    dv->str_buf_len = 0;
}
