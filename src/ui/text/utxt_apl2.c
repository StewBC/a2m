// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "utxt_lib.h"

void utxt_apl2_screen_apple2(UTXT *v) {
    APPLE2 *m = v->m;
    uint32_t mode = (m->col80set << 4) |
                    (m->dhires << 3)   | (m->hires << 2) |
                    (m->mixed << 1)    | (m->text);
    // The bits are col80, dhgr, hgr, mixed, text, (followed by bin, hex)
    switch(mode) {
        case 0x00: // 0 ,0 ,0 ,0 ,0 ,00000,00
            utxt_apl2_screen_box(v, 0, 24, 40, "LOWRES");
            break;
        case 0x01: // 0 ,0 ,0 ,0 ,1 ,00001,01
            // Franklin 80 not supported
            utxt_apl2_screen_txt(v, 0, 24, 40);
            break;
        case 0x02: // 0 ,0 ,0 ,1 ,0 ,00010,02
            utxt_apl2_screen_box(v, 0, 20, 40, "LOWRES");
            utxt_apl2_screen_txt(v, 20, 24, 40);
            break;
        case 0x03: // 0 ,0 ,0 ,1 ,1 ,00011,03
            utxt_apl2_screen_box(v, 0, 20, 40, "LOWRES");
            utxt_apl2_screen_txt(v, 0, 20, 40);
            break;
        case 0x04: // 0 ,0 ,1 ,0 ,0 ,00100,04
            utxt_apl2_screen_box(v, 0, 24, 40, "HGR");
            break;
        case 0x05: // 0 ,0 ,1 ,0 ,1 ,00101,05
            utxt_apl2_screen_txt(v, 0, 24, 40);
            break;
        case 0x06: // 0 ,0 ,1 ,1 ,0 ,00110,06
            utxt_apl2_screen_box(v, 0, 20, 40, "HGR");
            utxt_apl2_screen_txt(v, 20, 24, 40);
            break;
        case 0x07: // 0 ,0 ,1 ,1 ,1 ,00111,07
            utxt_apl2_screen_txt(v, 0, 24, 40);
            break;
        case 0x08: // 0 ,1 ,0 ,0 ,0 ,01000,08
            utxt_apl2_screen_box(v, 0, 24, 40, "LOWRES");
            break;
        case 0x09: // 0 ,1 ,0 ,0 ,1 ,01001,09
            utxt_apl2_screen_txt(v, 0, 24, 40);
            break;
        case 0x0A: // 0 ,1 ,0 ,1 ,0 ,01010,0A
            utxt_apl2_screen_box(v, 0, 20, 40, "LOWRES");
            utxt_apl2_screen_txt(v, 20, 24, 40);
            break;
        case 0x0B: // 0 ,1 ,0 ,1 ,1 ,01011,0B
            utxt_apl2_screen_txt(v, 20, 24, 40);
            break;
        case 0x0C: // 0 ,1 ,1 ,0 ,0 ,01100,0C
            utxt_apl2_screen_box(v, 0, 24, 40, "HGR");
            break;
        case 0x0D: // 0 ,1 ,1 ,0 ,1 ,01101,0D
            utxt_apl2_screen_txt(v, 0, 24, 40);
            break;
        case 0x0E: // 0 ,1 ,1 ,1 ,0 ,01110,0E
            utxt_apl2_screen_box(v, 0, 20, 40, "HGR");
            utxt_apl2_screen_txt(v, 20, 24, 40);
            break;
        case 0x0F: // 0 ,1 ,1 ,1 ,1 ,01111,0F
            utxt_apl2_screen_txt(v, 0, 24, 40);
            break;
        case 0x10: // 1 ,0 ,0 ,0 ,0 ,10000,10
            utxt_apl2_screen_box(v, 0, 24, 80, "DOUBLE LOWRES");
            break;
        case 0x11: // 1 ,0 ,0 ,0 ,1 ,10001,11
            utxt_apl2_screen_txt(v, 0, 24, 80);
            break;
        case 0x12: // 1 ,0 ,0 ,1 ,0 ,10010,12
            utxt_apl2_screen_box(v, 0, 20, 80, "DOUBLE LOWRES");
            utxt_apl2_screen_txt(v, 20, 24, 80);
            break;
        case 0x13: // 1 ,0 ,0 ,1 ,1 ,10011,13
            utxt_apl2_screen_txt(v, 0, 24, 80);
            break;
        case 0x14: // 1 ,0 ,1 ,0 ,0 ,10100,14
            utxt_apl2_screen_box(v, 0, 24, 40, "HGR");
            break;
        case 0x15: // 1 ,0 ,1 ,0 ,1 ,10101,15
            utxt_apl2_screen_txt(v, 0, 24, 80);
            break;
        case 0x16: // 1 ,0 ,1 ,1 ,0 ,10110,16
            utxt_apl2_screen_box(v, 0, 20, 40, "HGR");
            utxt_apl2_screen_txt(v, 20, 24, 80);
            break;
        case 0x17: // 1 ,0 ,1 ,1 ,1 ,10111,17
            utxt_apl2_screen_txt(v, 0, 24, 80);
            break;
        case 0x18: // 1 ,1 ,0 ,0 ,0 ,11000,18
            utxt_apl2_screen_box(v, 0, 24, 80, "DOUBLE LOWRES");
            break;
        case 0x19: // 1 ,1 ,0 ,0 ,1 ,11001,19
            utxt_apl2_screen_txt(v, 0, 24, 80);
            break;
        case 0x1A: // 1 ,1 ,0 ,1 ,0 ,11010,1A
            utxt_apl2_screen_box(v, 0, 20, 80, "DOUBLE LOWRES");
            utxt_apl2_screen_txt(v, 20, 24, 80);
            break;
        case 0x1B: // 1 ,1 ,0 ,1 ,1 ,11011,1B
            utxt_apl2_screen_box(v, 0, 20, 80, "DOUBLE LOWRES");
            utxt_apl2_screen_txt(v, 20, 24, 80);
            break;
        case 0x1C: // 1 ,1 ,1 ,0 ,0 ,11100,1C
            utxt_apl2_screen_box(v, 0, 24, 80, "DOUBLE HIRES");
            break;
        case 0x1D: // 1 ,1 ,1 ,0 ,1 ,11101,1D
            utxt_apl2_screen_txt(v, 0, 24, 80);
            break;
        case 0x1E: // 1 ,1 ,1 ,1 ,0 ,11110,1E
            utxt_apl2_screen_box(v, 0, 20, 80, "DOUBLE HIRES");
            utxt_apl2_screen_txt(v, 20, 24, 80);
            break;
        case 0x1F: // 1 ,1 ,1 ,1 ,1 ,11111,1F
            utxt_apl2_screen_txt(v, 0, 24, 80);
            break;
    }
    utxt_apl2_screen_show_modifiers(v);
}

void utxt_apl2_screen_show_modifiers(UTXT *v) {
    char turbo[8];

    move(24,0);
    if(v->ctrl) {
        attron(A_REVERSE);
        addstr(" CTRL ");
        attroff(A_REVERSE);
    } else {
        addstr(" CTRL ");
    }
    addstr("  ");
    if(v->m->open_apple) {
        attron(A_REVERSE);
        addstr(" OPEN-A ");
        attroff(A_REVERSE);
    } else {
        addstr(" OPEN-A ");
    }
    addstr("  ");
    if(v->m->closed_apple) {
        attron(A_REVERSE);
        addstr(" CLOSE-A ");
        attroff(A_REVERSE);
    } else {
        addstr(" CLOSE-A ");
    }
    addstr("  Turbo: ");
    if(v->rt->turbo_active >= 0) {
        snprintf(turbo, 8, "%1.0f", v->rt->turbo_active);
        addstr(turbo);
    } else {
        addstr("Max");
    }
}

void utxt_apl2_screen_box(UTXT *v, int start, int end, int width, char *text) {
    int height = end - start;
    int len = strlen(text);

    WINDOW *rect = derwin(stdscr, height, width, start, 0);
    box(rect, 0, 0);
    wmove(rect, height/2, (width - len) / 2);
    waddnstr(rect, text, len);
}

void utxt_apl2_screen_txt(UTXT *v, int start, int end, int width) {
    APPLE2 *m = v->m;
    Uint64 now = SDL_GetPerformanceCounter();
    double freq = (double)SDL_GetPerformanceFrequency();
    // I got 3.7 from recording a flash on my Platinum //e - 0.17 to 0.44 for a change so 0.27
    uint8_t time_inv = (((uint64_t)(now * 3.7 / freq)) & 1) ? 0xFF : 0x00;
    int alt_charset = m->model ? m->altcharset : 0;
    int x, y;

    uint16_t page = width == 80 || !m->page2set ? 0x0400 : 0x0800;

    // Loop through each row
    for(y = start; y < end; y++) {
        int address = page + apl2_txt_row_start[y];
        move(y, 0);
        // Loop through every col (byte)
        for(int x = 0; x < width; x++) {
            int r;
            uint8_t inv = 0x00;
            int char_in_bank = width == 80 ? ((x & 1) ? (x >> 1) : (x >> 1) + 0x10000) : x;
            uint8_t character = m->RAM_MAIN[address + char_in_bank];   // Get the character on screen
            if(character < 0x80) {
                if(character >= 0x40) {
                    if(!alt_charset) {
                        character -= 0x40;
                        inv = time_inv;
                    }
                } else {
                    inv = 0xFF;
                }
            } else {
                character -= 0x80;
                if(character == 0x7f) {
                    character = 0x20;
                    inv = 0xff;
                }
            }
            if(character < 32) {
                character += 0x40;
            }
            if(inv) {
                attron(A_REVERSE);
            }
            addch(character & 0x7F);
            if(inv) {
                attroff(A_REVERSE);
            }
        }
    }
}
