// Apple ][+ emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#include "header.h"

#define MEMSHOW_INITIAL_LINE_LENGTH 73
#define MEMSHOW_BYTES_PER_ROW       16
#define MAX_FIND_STRING_LENGTH      128

typedef struct RANGE_COLORS {
    struct nk_color text_color;
    struct nk_color background_color;
} RANGE_COLORS ;

RANGE_COLORS viewmem_range_colors[16] = {
    {{0XFF, 0XFF, 0XFF, 0XFF}, {0X00, 0X00, 0X00, 0XFF}},
    {{0XFF, 0XFF, 0XFF, 0XFF}, {0X2F, 0X4F, 0X4F, 0XFF}},
    {{0XFF, 0XFF, 0XFF, 0XFF}, {0X00, 0X1F, 0X3F, 0XFF}},
    {{0X00, 0X00, 0X00, 0XFF}, {0XA9, 0XCC, 0XE3, 0XFF}},
    {{0XFF, 0XFF, 0XFF, 0XFF}, {0X2E, 0X8B, 0X57, 0XFF}},
    {{0X00, 0X00, 0X00, 0XFF}, {0XFF, 0XFF, 0XE0, 0XFF}},
    {{0X00, 0X00, 0X00, 0XFF}, {0XFF, 0XD1, 0XDC, 0XFF}},
    {{0XFF, 0XFF, 0XFF, 0XFF}, {0XFF, 0X8C, 0X00, 0XFF}},
    {{0X00, 0X00, 0X00, 0XFF}, {0XE0, 0XFF, 0XFF, 0XFF}},
    {{0XFF, 0XFF, 0XFF, 0XFF}, {0X5D, 0X3F, 0XD3, 0XFF}},
    {{0X00, 0X00, 0X00, 0XFF}, {0X90, 0XEE, 0X90, 0XFF}},
    {{0XFF, 0XFF, 0XFF, 0XFF}, {0X80, 0X00, 0X00, 0XFF}},
    {{0X00, 0X00, 0X00, 0XFF}, {0XAD, 0XD8, 0XE6, 0XFF}},
    {{0X00, 0X00, 0X00, 0XFF}, {0XF0, 0X80, 0X80, 0XFF}},
    {{0XFF, 0XFF, 0XFF, 0XFF}, {0X8B, 0X00, 0X00, 0XFF}},
    {{0X00, 0X00, 0X00, 0XFF}, {0XFF, 0XFF, 0XFF, 0XFF}},
};

void viewmem_active_range(APPLE2 *m, int id) {
    MEMSHOW *ms = &m->viewport->memshow;
    MEMLINE *memline = ARRAY_GET(ms->lines, MEMLINE, 0);
    while(memline->id < id && memline->last_line < ms->num_lines - 1) {
        memline = ARRAY_GET(ms->lines, MEMLINE, memline->last_line + 1);
    }
    if(memline->id == id) {
        ms->cursor_y = memline->first_line;
    }
}

void viewmem_cursor_down(APPLE2 *m) {
    MEMSHOW *ms = &m->viewport->memshow;
    if(ARRAY_GET(ms->lines, MEMLINE, ms->cursor_y)->last_line == ms->cursor_y) {
        int top = ARRAY_GET(ms->lines, MEMLINE, ms->cursor_y)->first_line;
        viewmem_set_range_pc(m, ARRAY_GET(ms->lines, MEMLINE, top)->address + MEMSHOW_BYTES_PER_ROW);
    } else {
        ms->cursor_y++;
    }
}

void viewmem_cursor_left(APPLE2 *m) {
    MEMSHOW *ms = &m->viewport->memshow;
    if(ms->cursor_x > (ms->edit_mode_ascii ? 32 : 0)) {
        ms->cursor_x--;
    } else {
        ms->cursor_x = (ms->edit_mode_ascii ? 47 : 31);
        viewmem_cursor_up(m);
    }
}

void viewmem_cursor_right(APPLE2 *m) {
    MEMSHOW *ms = &m->viewport->memshow;
    if(ms->cursor_x < (ms->edit_mode_ascii ? 47 : 31)) {
        ms->cursor_x++;
    } else {
        ms->cursor_x = (ms->edit_mode_ascii ? 32 : 0);
        viewmem_cursor_down(m);
    }
}

void viewmem_cursor_up(APPLE2 *m) {
    MEMSHOW *ms = &m->viewport->memshow;
    if(ARRAY_GET(ms->lines, MEMLINE, ms->cursor_y)->first_line == ms->cursor_y) {
        int top = ARRAY_GET(ms->lines, MEMLINE, ms->cursor_y)->first_line;
        viewmem_set_range_pc(m, ARRAY_GET(ms->lines, MEMLINE, top)->address - MEMSHOW_BYTES_PER_ROW);
    } else {
        ms->cursor_y--;
    }
}

void viewmem_find_string(APPLE2 *m) {
    MEMSHOW *ms = &m->viewport->memshow;
    uint16_t index = 0;
    for(size_t i = ms->last_found_address + 1; i < ms->last_found_address + 65537 ; i++) {
        uint16_t pc = i;
        while(read_from_memory_debug(m, pc + index) == ms->find_string[index]) {
            if(++index == ms->find_string_len) {
                viewmem_set_range_pc(m, pc);
                ms->last_found_address = pc;
                ms->cursor_x = 0;
                ms->cursor_y = ARRAY_GET(ms->lines, MEMLINE, ms->cursor_y)->first_line;
                return;
            }
        }
        index = 0;
    }
}

void viewmem_find_string_reverse(APPLE2 *m) {
    MEMSHOW *ms = &m->viewport->memshow;
    uint16_t index = ms->find_string_len - 1;
    for(int i = ms->last_found_address - 1; i > ms->last_found_address - 65537 ; i--) {
        uint16_t pc = i;
        while(read_from_memory_debug(m, pc + index) == ms->find_string[index]) {
            if(index == 0) {
                pc += index;
                viewmem_set_range_pc(m, pc);
                ms->last_found_address = pc;
                ms->cursor_x = 0;
                ms->cursor_y = ARRAY_GET(ms->lines, MEMLINE, ms->cursor_y)->first_line;
                return;
            }
            --index;
        }
        index = ms->find_string_len - 1;
    }
}

int viewmem_init(MEMSHOW *ms, int num_lines) {
    // Clear the memsho structure
    memset(ms, 0, sizeof(MEMSHOW));
    // Allocate the dynamic array structure
    ms->lines = (DYNARRAY*)malloc(sizeof(DYNARRAY));
    if(!ms->lines) {
        return A2_ERR;
    }
    // Init the array and size it to the number of lines needed
    ARRAY_INIT(ms->lines, MEMLINE);
    if(A2_OK != array_resize(ms->lines, num_lines)) {
        return A2_ERR;
    }
    // Set all of the MEMLINE structures to 0
    memset(ms->lines->data, 0, sizeof(MEMLINE) * num_lines);
    ms->num_lines = num_lines;
    ms->line_length = MEMSHOW_INITIAL_LINE_LENGTH;
    // Init each memline structure and allocate the space for the text
    for(int i=0; i < num_lines; i++) {
        MEMLINE *memline = ARRAY_GET(ms->lines, MEMLINE, i);
        memline->line_text = (char*)malloc(MEMSHOW_INITIAL_LINE_LENGTH);
        if(!memline->line_text) {
            return A2_ERR;
        }
        // null terminate the text and set up the address this line will show
        *memline->line_text = 0;
        memline->address = i * MEMSHOW_BYTES_PER_ROW;
        memline->last_line = num_lines - 1; 
    }
    ms->find_string = (char *)malloc(MAX_FIND_STRING_LENGTH + 1);
    if(!ms->find_string) {
        return A2_ERR;
    }
    return A2_OK;
}

void viewmem_join_range(APPLE2 *m, int direction) {
    MEMSHOW *ms = &m->viewport->memshow;
    int line = ms->cursor_y;
    MEMLINE *memline = ARRAY_GET(ms->lines, MEMLINE, line);
    if(direction < 0) {
        // Join with the block before 
        if(memline->first_line != 0) {
            // Only if this block isn't already the 1st block (block 0)
            MEMLINE *block = ARRAY_GET(ms->lines, MEMLINE, memline->first_line - 1);
            uint16_t id = block->id; 
            uint16_t address = ARRAY_GET(ms->lines, MEMLINE, memline->first_line)->address;
            uint16_t last_line = memline->last_line;
            line = block->first_line;
            while(line <= last_line) {
                // Set the new combined block id and other parameters
                memline = ARRAY_GET(ms->lines, MEMLINE, line);
                memline->first_line = block->first_line;
                memline->last_line = last_line;
                memline->id = id;
                line++;
            }
            // Set the start of the block to the start of the block that was
            // actve when the join happened
            viewmem_set_range_pc(m, address);
            // Renumber all following blocks down by 1
            while(line < ms->num_lines) {
                memline = ARRAY_GET(ms->lines, MEMLINE, line);
                memline->id--;
                line++;
            }
        }
    } else {
        if(memline->last_line < ms->num_lines - 1) {
            MEMLINE *block = ARRAY_GET(ms->lines, MEMLINE, memline->last_line + 1);
            uint16_t id = memline->id; 
            uint16_t address = ARRAY_GET(ms->lines, MEMLINE, memline->first_line)->address;
            uint16_t first_line = memline->first_line;
            uint16_t last_line = block->last_line;
            line = memline->first_line;
            while(line <= last_line) {
                memline = ARRAY_GET(ms->lines, MEMLINE, line);
                memline->first_line = first_line;
                memline->last_line = last_line;
                memline->id = id;
                line++;
            }
            viewmem_set_range_pc(m, address);
            while(line < ms->num_lines) {
                memline = ARRAY_GET(ms->lines, MEMLINE, line);
                memline->id--;
                line++;
            }
        }
    }
}

void viewmem_new_range(APPLE2 *m) {
    MEMSHOW *ms = &m->viewport->memshow;
    int line = ms->cursor_y;
    int id = ARRAY_GET(ms->lines, MEMLINE, line)->id;
    if(!line) {
        // The first line can't be a new block
        return;
    } else if(line == ms->num_lines - 1) {
        // The last line can only be a new block if it is part of a block with the next to last line
        if(id != ARRAY_GET(ms->lines, MEMLINE, ms->num_lines - 2)->id) {
            return;
        }
    } else {
        // The line can only be a new block if it is part of a block on the line before
        if(id != ARRAY_GET(ms->lines, MEMLINE, line - 1)->id) {
            return;
        }
    }
    // This line passes tests and can become a new block
    // Set the last line in the block that is before the new block to the line before the new block
    int block_line = line - 1;
    MEMLINE *block;
    do {
        block = ARRAY_GET(ms->lines, MEMLINE, block_line);
        if(block->id == id) {
            block->last_line = line - 1;
            block_line--;
        }
    } while(block->id == id && block_line >= 0);
    // Set the first line in the new block and update the id in new block and all subsequent blocks
    block_line = line;
    while(block_line < ms->num_lines) {
        block = ARRAY_GET(ms->lines, MEMLINE, block_line);
        if(block->id == id) {
            block->first_line = line;
        }
        block->id++;
        block_line++;
    }
}

void viewmem_page_down(APPLE2 *m) {
    MEMSHOW *ms = &m->viewport->memshow;
    MEMLINE *line = ARRAY_GET(ms->lines, MEMLINE, ms->cursor_y);
    // MEMLINE *first_line = ARRAY_GET(ms->lines, MEMLINE, line->first_line);
    MEMLINE *last_line = ARRAY_GET(ms->lines, MEMLINE, line->last_line);
    viewmem_set_range_pc(m, last_line->address + MEMSHOW_BYTES_PER_ROW);
}

void viewmem_page_up(APPLE2 *m) {
    MEMSHOW *ms = &m->viewport->memshow;
    MEMLINE *line = ARRAY_GET(ms->lines, MEMLINE, ms->cursor_y);
    MEMLINE *first_line = ARRAY_GET(ms->lines, MEMLINE, line->first_line);
    MEMLINE *last_line = ARRAY_GET(ms->lines, MEMLINE, line->last_line);
    viewmem_set_range_pc(m, first_line->address - (last_line->address - first_line->address) - MEMSHOW_BYTES_PER_ROW);
}

int viewmem_process_event(APPLE2 *m, SDL_Event *e, int window) {
    SDL_Keymod mod = SDL_GetModState();
    VIEWPORT *v = m->viewport;
    MEMSHOW *ms = &v->memshow;

    if(ms->edit_mode_ascii) {
        if(e->type == SDL_TEXTINPUT) {
            write_to_memory(m, ms->cursor_address, e->text.text[0]);
            viewmem_cursor_right(m);
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
        // ALT - 0 .. f - Switch zone
        uint8_t key = e->key.keysym.sym;
        if((key >= SDLK_0 && key <= SDLK_9) || (key >= SDLK_a && key <= SDLK_f)) {
            key -= key >= SDLK_a ? SDLK_a - 10 : SDLK_0;
            viewmem_active_range(m, key);
        }
    } else if(mod & KMOD_CTRL) {
        switch (e->key.keysym.sym) {
            case SDLK_f:    // CTRL F - Find
                if(!v->viewdlg_modal) {
                    ms->find_string_len = 0;
                    v->viewdlg_modal = 1;
                    v->dlg_memory_find = 1;
                }
                break;
                
            case SDLK_g:    // CTRL G - Goto address
                if(!v->viewdlg_modal) {
                    v->viewdlg_modal = 1;
                    v->dlg_memory_go = 1;
                }
                break;

            case SDLK_h:    // CTRL H - Find & Replace
                break;

            case SDLK_j:    // CTRL J - Join (down) CTRL SHIFT J - Join Up
                viewmem_join_range(m, (mod & KMOD_SHIFT) ? -1 : 1);
                break;

            case SDLK_n:    // CTRL (shift) N - Find (prev) Next
                if(mod & KMOD_SHIFT) {
                    viewmem_find_string_reverse(m);
                } else {
                    viewmem_find_string(m);
                }
                break;

            case SDLK_s:    // CTRL S - New Range (Split)
                viewmem_new_range(m);
                break;

            case SDLK_t:    // CTRL T - Toggle between ascii and hex edit
                ms->edit_mode_ascii ^= 1;
                ms->cursor_x = (ms->edit_mode_ascii ? (ms->cursor_x / 2 + 32) : ((ms->cursor_x - 32) * 2));
                break;
        }
    } else {
        // Regular, unmodified keys
        uint8_t key = e->key.keysym.sym;
        if((key >= SDLK_0 && key <= SDLK_9) || (key >= SDLK_a && key <= SDLK_f)) {
            // This is HEX mode only as ascii mode was handled at the start
            uint8_t byte = read_from_memory_debug(m, ms->cursor_address);
            key -= key >= SDLK_a ? SDLK_a - 10 : SDLK_0;
            if(ms->cursor_x & 1) {
                byte &= 0xf0;
                byte |= key;
            } else {
                byte &= 0x0F;
                byte |= (key << 4);
            }
            write_to_memory(m, ms->cursor_address, byte);
            viewmem_cursor_right(m);
            return 0;
        }

        // Unmodified special keys
        switch (e->key.keysym.sym) {
            case SDLK_UP:
                viewmem_cursor_up(m);
                break;

            case SDLK_DOWN:
                viewmem_cursor_down(m);
                break;

            case SDLK_LEFT:
                viewmem_cursor_left(m);
                break;

            case SDLK_RIGHT:
                viewmem_cursor_right(m);
                break;

            case SDLK_PAGEUP:
                viewmem_page_up(m);
                break;

            case SDLK_PAGEDOWN:
                viewmem_page_down(m);
                break;

            default:
                if(ms->edit_mode_ascii) {
                    write_to_memory(m, ms->cursor_address, e->key.keysym.sym);
                    viewmem_cursor_right(m);
                }
                break;
        }
    }

    return 0;
}

void viewmem_set_range_pc(APPLE2 *m, uint16_t address) {
    MEMSHOW *ms = &m->viewport->memshow;
    int line = ms->cursor_y;
    MEMLINE *memline = ARRAY_GET(ms->lines, MEMLINE, line);
    int end_line = memline->last_line;
    line = memline->first_line;
    while(line <= end_line) {
        ARRAY_GET(ms->lines, MEMLINE, line)->address = address;
        address += MEMSHOW_BYTES_PER_ROW;
        line++;
    }
}

void viewmem_show(APPLE2 *m) {
    struct nk_context *ctx = m->viewport->ctx;
    VIEWPORT *v = m->viewport;
    MEMSHOW *ms = &v->memshow;
    int w = 512;
    viewmem_update(m);
    if(nk_begin(ctx, "Memory", nk_rect(0, m->viewport->target_rect.h, w, m->viewport->full_window_rect.h - m->viewport->target_rect.h),
        NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_TITLE | NK_WINDOW_BORDER)) {
        nk_style_set_font(ctx, &v->font->handle);
        nk_layout_row_static(ctx, 10, w, 1);
        struct nk_color active_background = ctx->style.window.background;
        struct nk_color last_c;
        for(int i = 0; i < ms->num_lines; i++) {
            MEMLINE *memline = ARRAY_GET(ms->lines, MEMLINE, i);
            last_c = ctx->style.window.background = viewmem_range_colors[memline->id].background_color;
            nk_label_colored(ctx, memline->line_text, NK_TEXT_ALIGN_LEFT, viewmem_range_colors[memline->id].text_color);
            if(i == ms->cursor_y) {
                struct nk_rect r = ctx->current->layout->bounds;
                r.h = v->font_height;
                r.w = v->font_width;
                r.y += ctx->style.edit.cursor_size + ((r.h + 1) * i); // empirically determined
                int cx = (ms->cursor_x <= 31 ? (7 + (ms->cursor_x / 2) * 3 + (ms->cursor_x % 2)) : (ms->cursor_x + 23));
                r.x += cx * r.w;
                nk_draw_text(&ctx->active->buffer, r, &memline->line_text[cx], 1, ctx->style.font, viewmem_range_colors[memline->id].text_color, viewmem_range_colors[memline->id].background_color);
                ms->cursor_address = memline->address + (ms->cursor_x <= 31 ? (ms->cursor_x / 2) : (ms->cursor_x - 32));
            }
        }
        ctx->style.window.background = active_background;
        nk_style_set_font(ctx, &v->font->handle);
        nk_labelf(ctx, NK_TEXT_ALIGN_LEFT, "Address: %04X", ms->cursor_address);
    }
    if(v->dlg_memory_go) {
        int ret;
        static char address[5] = {0,0,0,0,0};
        static int address_length = 0;
        if((ret = viewdlg_hex_address(ctx, nk_rect(80, 10, 280, 80), address, &address_length))) {
            if(ret == 1) {
                int value;
                address[address_length] = 0;
                if(1 == sscanf(address, "%x", &value)) {
                    viewmem_set_range_pc(m, value);
                }
            }
            v->viewdlg_modal = 0;
            v->dlg_memory_go = 0;
        }
    }
    if(v->dlg_memory_find) {
        int ret;
        if((ret = viewdlg_find(ctx, nk_rect(10, 10, 400, 120), ms->find_string, &ms->find_string_len, MAX_FIND_STRING_LENGTH))) {
            if(ret && ret != 3) {
                int value;
                ms->find_string[ms->find_string_len] = 0;
                if(ret == 2) {
                    for(int i = 0; i < ms->find_string_len; i += 2) {
                        int value;
                        if(1 == sscanf(ms->find_string + i,"%02x", &value)) {
                            ms->find_string[i/2] = value;
                        }
                    }
                    ms->find_string_len /= 2;
                }
                ms->last_found_address = ms->cursor_address;
                viewmem_find_string(m);
            }
            v->viewdlg_modal = 0;
            v->dlg_memory_find = 0;
        }
    }
    nk_end(ctx);
}

void viewmem_update(APPLE2 *m) {
    uint8_t characters[MEMSHOW_BYTES_PER_ROW];
    MEMSHOW *ms = &m->viewport->memshow;
    // Populate the memshow rows
    for(int i = 0; i < ms->num_lines; i++) {
        int j;
        MEMLINE *memline = ARRAY_GET(ms->lines, MEMLINE, i);
        uint16_t address = memline->address;
        sprintf(memline->line_text, "%X:%04X:", memline->id, address);
        for(j = 0; j < MEMSHOW_BYTES_PER_ROW; j++) {
            characters[j] = read_from_memory_debug(m, address++);
            sprintf(memline->line_text+7+j*3,"%02X ", characters[j]);
        }
        for(j = 0; j < MEMSHOW_BYTES_PER_ROW; j++) {
            sprintf(memline->line_text+7+MEMSHOW_BYTES_PER_ROW*3+j,"%c", isprint(characters[j]) ? characters[j] : isprint(characters[j] & 0x7f) ? characters[j] & 0x7f : '.');
        }
    }
}
