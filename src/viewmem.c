// Apple ][+ and //e Emhanced emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#include "header.h"

#define SCROLLBAR_W             20
#define ADDRESS_LABEL_H         20
#define ROW_H                   v->font_height
#define MAX_FIND_STRING_LENGTH  256

typedef struct RANGE_COLORS {
    struct nk_color text_color;
    struct nk_color background_color;
} RANGE_COLORS;

RANGE_COLORS viewmem_range_colors[16] = {
    { { 0XFF, 0XFF, 0XFF, 0XFF}, { 0X00, 0X00, 0X00, 0XFF} },
    { { 0XFF, 0XFF, 0XFF, 0XFF}, { 0X2F, 0X4F, 0X4F, 0XFF} },
    { { 0XFF, 0XFF, 0XFF, 0XFF}, { 0X00, 0X1F, 0X3F, 0XFF} },
    { { 0X00, 0X00, 0X00, 0XFF}, { 0XA9, 0XCC, 0XE3, 0XFF} },
    { { 0XFF, 0XFF, 0XFF, 0XFF}, { 0X2E, 0X8B, 0X57, 0XFF} },
    { { 0X00, 0X00, 0X00, 0XFF}, { 0XFF, 0XFF, 0XE0, 0XFF} },
    { { 0X00, 0X00, 0X00, 0XFF}, { 0XFF, 0XD1, 0XDC, 0XFF} },
    { { 0XFF, 0XFF, 0XFF, 0XFF}, { 0XFF, 0X8C, 0X00, 0XFF} },
    { { 0X00, 0X00, 0X00, 0XFF}, { 0XE0, 0XFF, 0XFF, 0XFF} },
    { { 0XFF, 0XFF, 0XFF, 0XFF}, { 0X5D, 0X3F, 0XD3, 0XFF} },
    { { 0X00, 0X00, 0X00, 0XFF}, { 0X90, 0XEE, 0X90, 0XFF} },
    { { 0XFF, 0XFF, 0XFF, 0XFF}, { 0X80, 0X00, 0X00, 0XFF} },
    { { 0X00, 0X00, 0X00, 0XFF}, { 0XAD, 0XD8, 0XE6, 0XFF} },
    { { 0X00, 0X00, 0X00, 0XFF}, { 0XF0, 0X80, 0X80, 0XFF} },
    { { 0XFF, 0XFF, 0XFF, 0XFF}, { 0X8B, 0X00, 0X00, 0XFF} },
    { { 0X00, 0X00, 0X00, 0XFF}, { 0XFF, 0XFF, 0XFF, 0XFF} },
};

static inline int viewmem_circular_delta(uint16_t cursor, uint16_t view) {
    uint16_t u = (uint16_t)(cursor - view);   // always wraps correctly
    if(u <= 0x7FFF) {
        return (int)u;    // forward short distance
    } else {
        return (int)u - 0x10000;    // backward short distance
    }
}

static inline uint16_t viewmem_wrap16_add(uint16_t a, int b) {
    return (uint16_t)(a + b);
}

static inline void viewmem_recenter_view(MEMSHOW *ms, MEMVIEW *mv) {
    int delta = viewmem_circular_delta(mv->cursor_address, mv->view_address);

    int row = delta >= 0 ? delta / ms->cols : -1 + ((delta + 1) / ms->cols);
    int col = delta - row * ms->cols;

    if(row < 0) {
        mv->view_address = viewmem_wrap16_add(mv->cursor_address, -col);
    } else if(row >= mv->rows) {
        mv->view_address = viewmem_wrap16_add(mv->cursor_address, -col - (mv->rows - 1) * ms->cols);
    }
}

void viewmem_find_string(APPLE2 *m, MEMSHOW *ms, MEMVIEW *mv) {
    uint8_t flags = mv->flags;
    uint16_t index = 0;
    for(size_t i = ms->last_found_address + 1; i < ms->last_found_address + 65537; i++) {
        uint16_t pc = i;
        while(read_from_memory_selected(m, pc + index, flags) == ms->find_string[index]) {
            if(++index == ms->find_string_len) {
                mv->cursor_address = mv->view_address = ms->last_found_address = pc;
                return;
            }
        }
        index = 0;
    }
}

void viewmem_find_string_reverse(APPLE2 *m, MEMSHOW *ms, MEMVIEW *mv) {
    uint8_t flags = mv->flags;
    uint16_t index = ms->find_string_len - 1;
    for(int i = ms->last_found_address - 1; i > ms->last_found_address - 65537; i--) {
        uint16_t pc = i;
        while(read_from_memory_selected(m, pc + index, flags) == ms->find_string[index]) {
            if(index == 0) {
                pc += index;
                mv->cursor_address = mv->view_address = ms->last_found_address = pc;
                return;
            }
            --index;
        }
        index = ms->find_string_len - 1;
    }
}

int viewmem_init(MEMSHOW *ms) {
    MEMVIEW memview;
    memset(&memview, 0, sizeof(MEMVIEW));
    memset(ms, 0, sizeof(MEMSHOW));
    memview.flags = MEM_MAPPED_6502;

    ms->mem_views = (DYNARRAY *) malloc(sizeof(DYNARRAY));
    if(!ms->mem_views) {
        return A2_ERR;
    }
    // Init the array
    ARRAY_INIT(ms->mem_views, MEMVIEW);
    ARRAY_ADD(ms->mem_views, &memview);
    ms->find_string = (char *)malloc(MAX_FIND_STRING_LENGTH);
    if(ms->find_string) {
        ms->find_string_len = MAX_FIND_STRING_LENGTH;
    }
    return A2_OK;
}

void viewmem_cursor_home(MEMSHOW *ms, MEMVIEW *mv, int mod) {
    if(mv->cursor_field != CURSOR_ADDRESS) {
        if(!mod) {
            // move to column 0 of current row
            int delta = viewmem_circular_delta(mv->cursor_address, mv->view_address);
            int row = delta >= 0 ? delta / ms->cols : -1 + ((delta + 1) / ms->cols);
            mv->cursor_address = viewmem_wrap16_add(mv->cursor_address, -(delta - row * ms->cols));
        } else {
            // move to top-left of view
            mv->cursor_address = mv->view_address;
        }
        viewmem_recenter_view(ms, mv);
    }
    mv->cursor_digit = CURSOR_DIGIT0;
}

void viewmem_cursor_end(MEMSHOW *ms, MEMVIEW *mv, int mod) {
    if(mv->cursor_field == CURSOR_ADDRESS) {
        mv->cursor_digit = CURSOR_DIGIT3;
        return;
    }

    if(mod) {
        // Move to the last row
        mv->cursor_address = mv->view_address + ms->cols * (mv->rows - 1);
    }

    int delta = viewmem_circular_delta(mv->cursor_address, mv->view_address);
    int row = delta >= 0 ? delta / ms->cols : -1 + ((delta + 1) / ms->cols);
    int col = delta - row * ms->cols;

    // move to last column of row
    mv->cursor_address = viewmem_wrap16_add(mv->cursor_address, (ms->cols - 1 - col));

    viewmem_recenter_view(ms, mv);
    mv->cursor_digit = CURSOR_DIGIT0;
}


void viewmem_cursor_down(MEMSHOW *ms, MEMVIEW *mv) {
    if(mv->cursor_field != CURSOR_ADDRESS) {
        mv->cursor_address = viewmem_wrap16_add(mv->cursor_address, ms->cols);
        viewmem_recenter_view(ms, mv);
    }
}


void viewmem_cursor_left(MEMSHOW *ms, MEMVIEW *mv) {
    switch(mv->cursor_field) {
        case CURSOR_HEX:
            if(mv->cursor_digit == CURSOR_DIGIT0) {
                mv->cursor_address = viewmem_wrap16_add(mv->cursor_address, -1);
                mv->cursor_digit = CURSOR_DIGIT1;
            } else {
                mv->cursor_digit--;
            }
            break;

        case CURSOR_ASCII:
            mv->cursor_address = viewmem_wrap16_add(mv->cursor_address, -1);
            break;

        case CURSOR_ADDRESS:
            if(mv->cursor_digit != CURSOR_DIGIT0) {
                mv->cursor_digit--;
            }
            return; // no recenter needed
    }

    viewmem_recenter_view(ms, mv);
}


void viewmem_cursor_right(MEMSHOW *ms, MEMVIEW *mv) {
    switch(mv->cursor_field) {
        case CURSOR_HEX:
            if(mv->cursor_digit == CURSOR_DIGIT1) {
                mv->cursor_address = viewmem_wrap16_add(mv->cursor_address, 1);
                mv->cursor_digit = CURSOR_DIGIT0;
            } else {
                mv->cursor_digit++;
            }
            break;

        case CURSOR_ASCII:
            mv->cursor_address = viewmem_wrap16_add(mv->cursor_address, 1);
            break;

        case CURSOR_ADDRESS:
            if(mv->cursor_digit != CURSOR_DIGIT3) {
                mv->cursor_digit++;
            } else {
                mv->cursor_field = mv->prev_field;
                mv->cursor_digit = mv->cursor_prev_digit;
            }
            return; // no view math
    }

    viewmem_recenter_view(ms, mv);
}


void viewmem_cursor_up(MEMSHOW *ms, MEMVIEW *mv) {
    if(mv->cursor_field != CURSOR_ADDRESS) {
        mv->cursor_address = viewmem_wrap16_add(mv->cursor_address, -ms->cols);
        viewmem_recenter_view(ms, mv);
    }
}


void viewmem_cursor_page_up(MEMSHOW *ms, MEMVIEW *mv) {
    if(mv->cursor_field != CURSOR_ADDRESS) {
        mv->view_address = viewmem_wrap16_add(mv->view_address, -(ms->cols * mv->rows));
    }
}

void viewmem_cursor_page_down(MEMSHOW *ms, MEMVIEW *mv) {
    if(mv->cursor_field != CURSOR_ADDRESS) {
        mv->view_address = viewmem_wrap16_add(mv->view_address, (ms->cols * mv->rows));
    }
}


void viewmem_show(APPLE2 *m) {
    VIEWPORT *v = m->viewport;
    if(!v) {
        return;
    }
    MEMSHOW *ms = &v->memshow;
    struct nk_context *ctx = m->viewport->ctx;
    struct nk_vec2 pad = ctx->style.window.padding;
    struct nk_vec2 spc = ctx->style.window.spacing;
    struct nk_vec2 gpd = ctx->style.window.group_padding;
    float border = ctx->style.window.border;
    if(nk_begin(ctx, "Memory", v->layout.mem, NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_TITLE | NK_WINDOW_BORDER)) {
        MEMVIEW *active_view;

        float w_offset = ctx->current->layout->bounds.x;
        ctx->current->layout->bounds.w += w_offset;
        ctx->current->layout->bounds.x = 0;
        ctx->style.window.padding       = nk_vec2(0, 0);
        ctx->style.window.spacing       = nk_vec2(0, 0);
        ctx->style.window.group_padding = nk_vec2(0, 0);
        ctx->style.window.border        = 0.0f;

        int num_views = v->memshow.mem_views->items;
        struct nk_color active_background = ctx->style.window.background;

        nk_layout_row_begin(ctx, NK_STATIC, v->layout.mem.h, 2);
        nk_layout_row_push(ctx, v->layout.mem.w - SCROLLBAR_W);
        int active_top_row, view_top_row = 0;
        if(nk_group_begin(ctx, "mem-views", NK_WINDOW_NO_SCROLLBAR)) {
            for(int view = 0; view < num_views; view++) {
                MEMVIEW *mv = ARRAY_GET(ms->mem_views, MEMVIEW, view);
                if(ms->active_view_index == view) {
                    active_view = mv;
                    active_top_row = view_top_row;
                }
                uint16_t view_address = mv->view_address;
                nk_layout_row_dynamic(ctx, mv->rows * ROW_H, 1);
                view_top_row += mv->rows;
                if(nk_group_begin(ctx, "mem-rows", NK_WINDOW_NO_SCROLLBAR)) {
                    nk_layout_row_dynamic(ctx, ROW_H, 1);
                    struct nk_color last_c = ctx->style.window.background = viewmem_range_colors[view].background_color;
                    for(int row = 0; row < mv->rows; row++) {
                        memset(ms->str_buf, 0x20, ms->str_buf_len);
                        snprintf(ms->str_buf, ms->str_buf_len, "%04X:", view_address);
                        for(int col = 0; col < ms->cols; col++) {
                            uint8_t c = ms->u8_buf[col] = read_from_memory_selected(m, view_address + col, mv->flags);
                            snprintf(&ms->str_buf[5 + col * 3], ms->str_buf_len - 5 - (ms->cols * 3), "%02X ", c);
                        }
                        for(int col = 0; col < ms->cols; col++) {
                            uint8_t c = ms->u8_buf[col];
                            snprintf(&ms->str_buf[5 + ms->cols * 3 + col], ms->str_buf_len - 5 - (ms->cols * 3) - col, "%c", isprint(c) ? c : '.');
                        }
                        view_address += ms->cols;
                        nk_label_colored(ctx, ms->str_buf, NK_TEXT_LEFT, viewmem_range_colors[view].text_color);
                    }
                    nk_group_end(ctx);
                }
            }

            // Cursor
            if(m->stopped) {
                MEMVIEW *mv = active_view;
                int delta = viewmem_circular_delta(mv->cursor_address, mv->view_address);
                if(delta >= 0 && delta < (mv->rows * ms->cols)) {
                    int row = delta / ms->cols;
                    int col = delta % ms->cols;
                    struct nk_rect r = ctx->current->layout->bounds;
                    r.h = ROW_H;
                    r.w = v->font_width;
                    // -2 is a hack to get the cursor (size 13 due to font height) "over" the text looking good
                    r.y += (active_top_row + row) * ROW_H;
                    uint8_t dc, c = read_from_memory_selected(m, mv->cursor_address, mv->flags);
                    switch(mv->cursor_field) {
                        case CURSOR_ADDRESS: {
                                r.x = ctx->current->layout->at_x + v->font_width * mv->cursor_digit;
                                uint16_t row_address = mv->cursor_address - col;
                                uint8_t shift = (4 * (3 - mv->cursor_digit));
                                dc = (row_address >> shift) & 0x0f;
                                dc += (dc > 9) ? 'A' - 10 : '0';
                            }
                            break;
                        case CURSOR_HEX: {
                                r.x = v->font_width * ((col * 3 + 6) + mv->cursor_digit);
                                uint8_t shift = (4 * (1 - mv->cursor_digit));
                                dc = (c >> shift) & 0x0f;
                                dc += (dc > 9) ? 'A' - 10 : '0';
                            }
                            break;
                        case CURSOR_ASCII:
                            r.x = v->font_width * ((ms->cols * 3 + 6) + col);
                            dc = isprint(c) ? c : '.';
                            break;
                    }
                    nk_draw_text(&ctx->active->buffer, r, &dc, 1, ctx->style.font, viewmem_range_colors[ms->active_view_index].text_color, viewmem_range_colors[ms->active_view_index].background_color);
                }
            }

            ctx->style.window.background = active_background;
            nk_layout_row_begin(ctx, NK_DYNAMIC, 22, 4);
            nk_layout_row_push(ctx, 0.4);
            nk_labelf(ctx, NK_TEXT_ALIGN_LEFT, "Address: %04X", active_view->cursor_address);
            nk_layout_row_push(ctx, 0.14);
            if(nk_option_label(ctx, "6502", tst_mem_flag(active_view->flags, MEM_MAPPED_6502)) && !tst_mem_flag(active_view->flags, MEM_MAPPED_6502)) {
                clr_mem_flag(active_view->flags, MEM_MAIN);
                clr_mem_flag(active_view->flags, MEM_AUX);
                clr_mem_flag(active_view->flags, MEM_LC_BANK2);
                set_mem_flag(active_view->flags, MEM_MAPPED_6502);
            }
            if(nk_option_label(ctx, "64K", tst_mem_flag(active_view->flags, MEM_MAIN)) && !tst_mem_flag(active_view->flags, MEM_MAIN)) {
                clr_mem_flag(active_view->flags, MEM_MAPPED_6502);
                clr_mem_flag(active_view->flags, MEM_AUX);
                set_mem_flag(active_view->flags, MEM_MAIN);
                if(m->lc_bank2_enable) {
                    set_mem_flag(active_view->flags, MEM_LC_BANK2);
                } else {
                    clr_mem_flag(active_view->flags, MEM_LC_BANK2);
                }
            }
            if(nk_option_label_disabled(ctx, "128K", tst_mem_flag(active_view->flags, MEM_AUX), !m->model) && !tst_mem_flag(active_view->flags, MEM_AUX)) {
                clr_mem_flag(active_view->flags, MEM_MAPPED_6502);
                clr_mem_flag(active_view->flags, MEM_MAIN);
                set_mem_flag(active_view->flags, MEM_AUX);
                if(m->lc_bank2_enable) {
                    set_mem_flag(active_view->flags, MEM_LC_BANK2);
                } else {
                    clr_mem_flag(active_view->flags, MEM_LC_BANK2);
                }
            }
            int before = tst_mem_flag(active_view->flags, MEM_LC_BANK2);
            int after = nk_option_label_disabled(ctx, "LC Bank2", before, tst_mem_flag(active_view->flags, MEM_MAPPED_6502));
            if(after != before) {
                if(after) {
                    set_mem_flag(active_view->flags, MEM_LC_BANK2);
                } else {
                    clr_mem_flag(active_view->flags, MEM_LC_BANK2);
                }
            }
            nk_group_end(ctx);
        }

        // Lower down uses this
        MEMVIEW *mv = ARRAY_GET(ms->mem_views, MEMVIEW, ms->active_view_index);

        // Scrollbar
        nk_layout_row_push(ctx, SCROLLBAR_W);
        struct nk_rect sbar_bounds = v->layout.mem;
        sbar_bounds.x += sbar_bounds.w - SCROLLBAR_W;
        sbar_bounds.w = SCROLLBAR_W;
        sbar_bounds.h -= ms->header_height;
        sbar_bounds.y += ms->header_height;
        int address = mv->view_address;
        if(nk_input_is_mouse_hovering_rect(&ctx->input, v->layout.mem)) {
            int wheel = (int)ctx->input.mouse.scroll_delta.y;
            if(wheel) {
                address = (address - wheel * ms->cols) % 0x10000;
                if(mv->view_address < 0) {
                    address += 0x10000;
                }
            }
        }
        nk_custom_scrollbarv(ctx, sbar_bounds, 0x10000, mv->rows * ms->cols, &address, &ms->dragging, &ms->grab_offset);
        mv->view_address = (uint16_t)address;

        // Restore style padding
        ctx->style.window.padding       = pad;
        ctx->style.window.spacing       = spc;
        ctx->style.window.group_padding = gpd;
        ctx->style.window.border        = border;

        if(v->dlg_memory_find) {
            int ret;
            if((ret = viewdlg_find(ctx, nk_rect(10, 10, 400, 120), ms->find_string, &ms->find_string_len, MAX_FIND_STRING_LENGTH))) {
                if(ret && ret != 3) {
                    int value;
                    ms->find_string[ms->find_string_len] = 0;
                    if(ret == 2) {
                        for(int i = 0; i < ms->find_string_len; i += 2) {
                            int value;
                            if(1 == sscanf(ms->find_string + i, "%02x", &value)) {
                                ms->find_string[i / 2] = value;
                            }
                        }
                        ms->find_string_len /= 2;
                    }
                    ms->last_found_address = mv->cursor_address;
                    viewmem_find_string(m, ms, mv);
                }
                v->viewdlg_modal = 0;
                v->dlg_memory_find = 0;
            }
        }

        if(v->dlg_symbol_lookup_mem) {
            static uint16_t pc = 0;
            int ret;
            DEBUGGER *d = &v->debugger;
            if((ret = viewdlg_symbol_lookup(ctx, nk_rect(0, 0, 500, 240), &d->symbols_search, global_entry_buffer, &global_entry_length, &pc))) {
                if(ret == 1) {
                    mv->view_address = pc;  // Consider delta calc and -col
                    mv->cursor_address = pc;
                }
                v->dlg_symbol_lookup_mem = 0;
                v->viewdlg_modal = 0;
            }
        }
    }
    nk_end(ctx);
}

void viewmem_resize_view(APPLE2 *m) {
    VIEWPORT *v = m->viewport;
    MEMSHOW *ms = &v->memshow;

    struct nk_context *ctx = m->viewport->ctx;
    struct nk_style *style = &ctx->style;
    // calc is from nk_begin_titled
    ms->header_height = 2.0f * style->window.header.padding.y + 2.0f * style->window.header.label_padding.y + v->font_height;
    struct nk_rect parent = v->layout.mem;

    float view_width = parent.w - 3.0f * ctx->style.window.border - SCROLLBAR_W;
    int visible_cols = view_width / v->font_width;

    int view_total_rows = (parent.h - (ADDRESS_LABEL_H + ms->header_height)) / ROW_H;
    int view_rows = view_total_rows / ms->mem_views->items;
    int view_height_overflow = view_total_rows - (view_rows * ms->mem_views->items);

    for(int view = 0; view < ms->mem_views->items; view++) {
        MEMVIEW *mv = ARRAY_GET(ms->mem_views, MEMVIEW, view);
        mv->rows = view_rows + (view_height_overflow ? (--view_height_overflow, 1) : 0);
    }

    // 16 cols at 4 chars/col (hex 'XX ' and char ' ') + xxxx:
    int cvt_size = visible_cols > (5 + 16 * 4) ? visible_cols : (5 + 16 * 4);
    if(ms->str_buf_len < cvt_size) {
        char *new_cvt_buf = (char *)realloc(ms->str_buf, cvt_size + 1);
        char *new_u8_buf = (uint8_t *)realloc(ms->u8_buf, cvt_size);
        if(new_u8_buf && new_cvt_buf) {
            ms->str_buf = new_cvt_buf;
            ms->u8_buf = new_u8_buf;
            ms->str_buf_len = cvt_size + 1;
        } else {
            free(new_cvt_buf);
            free(new_u8_buf);
        }
    }
    ms->cols = visible_cols <= ms->str_buf_len ? (visible_cols - 5) / 4 : (ms->str_buf_len - 5) / 4;
}

int viewmem_process_event(APPLE2 *m, SDL_Event *e, int window) {
    SDL_Keymod mod = SDL_GetModState();
    VIEWPORT *v = m->viewport;
    MEMSHOW *ms = &v->memshow;
    MEMVIEW *mv = ARRAY_GET(ms->mem_views, MEMVIEW, ms->active_view_index);

    if(mv->cursor_field == CURSOR_ASCII) {
        if(e->type == SDL_TEXTINPUT) {
            write_to_memory_selected(m, mv->cursor_address, mv->flags, e->text.text[0]);
            viewmem_cursor_right(ms, mv);
        } else if(!(mod & (KMOD_CTRL | KMOD_ALT)) && e->key.keysym.sym >= 32 && e->key.keysym.sym < 127) {
            // SDL_TEXTINPUT also has SDL_KEYDOWN for same key, so filter out keys that were already
            // processed, but keep keys that want/need processing - like ENTER or cursor, or CTRL/ALT mod, etc.
            return 0;
        }
    }
    // Only key events, and not for a modifier key by itself
    if(e->type != SDL_KEYDOWN || e->key.keysym.scancode == SDL_SCANCODE_CAPSLOCK || e->key.keysym.scancode >= SDL_SCANCODE_LCTRL) {
        return 0;
    }
    // Seperate into MOD codes and regular keys for better clarity
    if(mod & KMOD_ALT) {
        switch(e->key.keysym.sym) {
            case SDLK_DOWN:
                ms->active_view_index = (ms->active_view_index + 1) % ms->mem_views->items;
                break;

            case SDLK_UP:
                ms->active_view_index = (ms->active_view_index - 1) % ms->mem_views->items;
                break;
        }
    } else if(mod & KMOD_CTRL) {
        switch(e->key.keysym.sym) {
            case SDLK_a:                                        // CTRL G - Goto view_address
                if(mv->cursor_field == CURSOR_ADDRESS) {
                    mv->cursor_field = mv->prev_field;
                    mv->cursor_digit = mv->cursor_prev_digit;
                } else {
                    mv->prev_field = mv->cursor_field;
                    mv->cursor_prev_digit = mv->cursor_digit;
                    mv->cursor_digit = CURSOR_DIGIT0;
                    mv->cursor_field = CURSOR_ADDRESS;
                }
                break;

            case SDLK_f:                                        // CTRL F - Find
                if(!v->viewdlg_modal) {
                    ms->find_string_len = 0;
                    v->viewdlg_modal = 1;
                    v->dlg_memory_find = 1;
                }
                break;

            case SDLK_h:                                        // CTRL H - Find & Replace
                break;

            case SDLK_j:                                        // CTRL J - Join (down) CTRL SHIFT J - Join Up
                // viewmem_join_range(m, (mod & KMOD_SHIFT) ? -1 : 1);
                if(ms->mem_views->items > 1) {
                    array_remove(ms->mem_views, mv);
                    if(ms->active_view_index >= ms->mem_views->items) {
                        ms->active_view_index--;
                    }
                    viewmem_resize_view(m);
                }
                break;

            case SDLK_n:                                        // CTRL (shift) N - Find (prev) Next
                if(mod & KMOD_SHIFT) {
                    viewmem_find_string_reverse(m, ms, mv);
                } else {
                    viewmem_find_string(m, ms, mv);
                }
                break;

            case SDLK_s:                                        // CTRL S - Search
                global_entry_length = 0;
                v->viewdlg_modal = 1;
                v->dlg_symbol_lookup_mem = 1;
                break;

            case SDLK_t:                                        // CTRL T - Toggle between ascii and hex edit
                mv->cursor_field = CURSOR_ASCII - mv->cursor_field;
                break;

            case SDLK_v:                                        // CTRL v - New Range (Split)
                if(ms->mem_views->items < 16) {
                    MEMVIEW v;
                    memset(&v, 0, sizeof(v));
                    v.view_address = v.cursor_address = mv->cursor_address;
                    v.flags = mv->flags;
                    ARRAY_ADD(ms->mem_views, &v);
                    viewmem_resize_view(m);
                }
                break;

            case SDLK_HOME:
                viewmem_cursor_home(ms, mv, 1);
                break;

            case SDLK_END:
                viewmem_cursor_end(ms, mv, 1);
                break;

            case SDLK_DOWN:
                mv->view_address -= ms->cols;
                break;

            case SDLK_UP:
                mv->view_address += ms->cols;
                break;
        }
    } else {
        // Regular, unmodified keys
        uint8_t key = e->key.keysym.sym;
        if((key >= SDLK_0 && key <= SDLK_9) || (key >= SDLK_a && key <= SDLK_f)) {
            key -= key >= SDLK_a ? SDLK_a - 10 : SDLK_0;
            if(mv->cursor_field == CURSOR_HEX) {
                // This is HEX mode only as ascii mode was handled at the start
                uint8_t byte = read_from_memory_selected(m, mv->cursor_address, mv->flags);
                if(mv->cursor_digit == CURSOR_DIGIT1) {
                    byte &= 0xf0;
                    byte |= key;
                } else {
                    byte &= 0x0F;
                    byte |= (key << 4);
                }
                write_to_memory_selected(m, mv->cursor_address, mv->flags, byte);
            } else {
                // This is address mode
                int delta = viewmem_circular_delta(mv->cursor_address, mv->view_address);
                int row = delta / ms->cols;
                int col = delta % ms->cols;
                uint8_t shift = (4 * (3 - mv->cursor_digit));
                uint16_t address = mv->cursor_address - col;
                address &= ~(0x0f << shift);
                address |= (key << shift);
                mv->cursor_address = address;
                mv->view_address = address - delta;
            }
            viewmem_cursor_right(ms, mv);
            return 0;
        }
        // Unmodified special keys
        switch(e->key.keysym.sym) {
            case SDLK_HOME:
                viewmem_cursor_home(ms, mv, 0);
                break;

            case SDLK_END:
                viewmem_cursor_end(ms, mv, 0);
                break;

            case SDLK_UP:
                viewmem_cursor_up(ms, mv);
                break;

            case SDLK_DOWN:
                viewmem_cursor_down(ms, mv);
                break;

            case SDLK_LEFT:
                viewmem_cursor_left(ms, mv);
                break;

            case SDLK_RIGHT:
                viewmem_cursor_right(ms, mv);
                break;

            case SDLK_PAGEUP:
                viewmem_cursor_page_up(ms, mv);
                break;

            case SDLK_PAGEDOWN:
                viewmem_cursor_page_down(ms, mv);
                break;

            default:
                // This is where ENTER will come, for example
                if(mv->cursor_field == CURSOR_ASCII) {
                    write_to_memory(m, mv->cursor_address, e->key.keysym.sym);
                    viewmem_cursor_right(ms, mv);
                }
                break;
        }
    }

    return 0;
}
