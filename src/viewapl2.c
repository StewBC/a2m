// Apple ][+ and //e Emhanced emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#include "header.h"

// The start of a line of pixels (192 rows) in HGR bytes.
// (Add to active Hires page ie $2000 or $4000)
int hgr_row_start[] = {
    0x0000, 0x0400, 0x0800, 0x0C00, 0x1000, 0x1400, 0x1800, 0x1C00,
    0x0080, 0x0480, 0x0880, 0x0C80, 0x1080, 0x1480, 0x1880, 0x1C80,
    0x0100, 0x0500, 0x0900, 0x0D00, 0x1100, 0x1500, 0x1900, 0x1D00,
    0x0180, 0x0580, 0x0980, 0x0D80, 0x1180, 0x1580, 0x1980, 0x1D80,
    0x0200, 0x0600, 0x0A00, 0x0E00, 0x1200, 0x1600, 0x1A00, 0x1E00,
    0x0280, 0x0680, 0x0A80, 0x0E80, 0x1280, 0x1680, 0x1A80, 0x1E80,
    0x0300, 0x0700, 0x0B00, 0x0F00, 0x1300, 0x1700, 0x1B00, 0x1F00,
    0x0380, 0x0780, 0x0B80, 0x0F80, 0x1380, 0x1780, 0x1B80, 0x1F80,
    0x0028, 0x0428, 0x0828, 0x0C28, 0x1028, 0x1428, 0x1828, 0x1C28,
    0x00A8, 0x04A8, 0x08A8, 0x0CA8, 0x10A8, 0x14A8, 0x18A8, 0x1CA8,
    0x0128, 0x0528, 0x0928, 0x0D28, 0x1128, 0x1528, 0x1928, 0x1D28,
    0x01A8, 0x05A8, 0x09A8, 0x0DA8, 0x11A8, 0x15A8, 0x19A8, 0x1DA8,
    0x0228, 0x0628, 0x0A28, 0x0E28, 0x1228, 0x1628, 0x1A28, 0x1E28,
    0x02A8, 0x06A8, 0x0AA8, 0x0EA8, 0x12A8, 0x16A8, 0x1AA8, 0x1EA8,
    0x0328, 0x0728, 0x0B28, 0x0F28, 0x1328, 0x1728, 0x1B28, 0x1F28,
    0x03A8, 0x07A8, 0x0BA8, 0x0FA8, 0x13A8, 0x17A8, 0x1BA8, 0x1FA8,
    0x0050, 0x0450, 0x0850, 0x0C50, 0x1050, 0x1450, 0x1850, 0x1C50,
    0x00D0, 0x04D0, 0x08D0, 0x0CD0, 0x10D0, 0x14D0, 0x18D0, 0x1CD0,
    0x0150, 0x0550, 0x0950, 0x0D50, 0x1150, 0x1550, 0x1950, 0x1D50,
    0x01D0, 0x05D0, 0x09D0, 0x0DD0, 0x11D0, 0x15D0, 0x19D0, 0x1DD0,
    0x0250, 0x0650, 0x0A50, 0x0E50, 0x1250, 0x1650, 0x1A50, 0x1E50,
    0x02D0, 0x06D0, 0x0AD0, 0x0ED0, 0x12D0, 0x16D0, 0x1AD0, 0x1ED0,
    0x0350, 0x0750, 0x0B50, 0x0F50, 0x1350, 0x1750, 0x1B50, 0x1F50,
    0x03D0, 0x07D0, 0x0BD0, 0x0FD0, 0x13D0, 0x17D0, 0x1BD0, 0x1FD0
};

// Start of each text/lores screen row, as an offset from the active page
int txt_row_start[] = {
    0x0000, 0x0080, 0x0100, 0x0180, 0x0200, 0x0280, 0x0300, 0x0380,
    0x0028, 0x00A8, 0x0128, 0x01A8, 0x0228, 0x02A8, 0x0328, 0x03A8,
    0x0050, 0x00D0, 0x0150, 0x01D0, 0x0250, 0x02D0, 0x0350, 0x03D0
};

// Use the Holger Picker sliding window method
// https://groups.google.com/g/comp.sys.apple2.programmer/c/iSmIAVA95WA/m/KKsYkTgfWSEJ
//
// 000 = black
// 001 = black
// 010 = purple/blue
// 011 = white
// 100 = black
// 101 = green/orange
// 110 = white
// 111 = white
// 2 x with phase selecting alternate color

mRGB palette[16] = {
    { 0X00, 0X00, 0X00 },                                   // HGR_BLACK
    { 0X00, 0X00, 0X00 },                                   // HGR_BLACK
    { 0xC7, 0x34, 0xFF },                                   // HGR_VIOLET
    { 0XFF, 0XFF, 0XFF },                                   // HGR_WHITE
    { 0X00, 0X00, 0X00 },                                   // HGR_BLACK
    { 0x38, 0xCB, 0x00 },                                   // HGR_GREEN
    { 0XFF, 0XFF, 0XFF },                                   // HGR_WHITE
    { 0XFF, 0XFF, 0XFF },                                   // HGR_WHITE
    { 0X00, 0X00, 0X00 },                                   // HGR_BLACK
    { 0X00, 0X00, 0X00 },                                   // HGR_BLACK
    { 0x0D, 0xA1, 0xFF },                                   // HGR_BLUE
    { 0XFF, 0XFF, 0XFF },                                   // HGR_WHITE
    { 0X00, 0X00, 0X00 },                                   // HGR_BLACK
    { 0xF2, 0x5E, 0x00 },                                   // HGR_ORANGE
    { 0XFF, 0XFF, 0XFF },                                   // HGR_WHITE
    { 0XFF, 0XFF, 0XFF },                                   // HGR_WHITE
};

mRGB lores_palette[16] = {
    { 0x00, 0x00, 0x00 },                                   // Black
    { 0x9D, 0x09, 0x66 },                                   // Violet
    { 0x2A, 0x2A, 0xE5 },                                   // Dark blue
    { 0xC7, 0x34, 0xFF },                                   // Purple/Magenta
    { 0x00, 0x80, 0x00 },                                   // Dark green
    { 0x80, 0x80, 0x80 },                                   // Gray1
    { 0x0D, 0xA1, 0xFF },                                   // Light Blue
    { 0xAA, 0xAA, 0xFF },                                   // Meduim blue
    { 0x55, 0x55, 0x00 },                                   // Brown
    { 0xF2, 0x5E, 0x00 },                                   // Orange
    { 0xC0, 0xC0, 0xC0 },                                   // Gray2
    { 0xFF, 0x89, 0xE5 },                                   // Pink
    { 0x38, 0xCB, 0x00 },                                   // Green
    { 0xD5, 0xD5, 0x1A },                                   // Yellow
    { 0x62, 0xF6, 0x99 },                                   // Aqua (Cyan)
    { 0xFF, 0xFF, 0xFF },                                   // White
};

mRGB dhgr_palette[16] = {
    { 0x00, 0x00, 0x00 },                                   // Black
    { 0xC7, 0x34, 0xFF },                                   // Magenta
    { 0x55, 0x55, 0x00 },                                   // Brown
    { 0xF2, 0x5E, 0x00 },                                   // Orange
    { 0x00, 0x80, 0x00 },                                   // Dark Green
    { 0x80, 0x80, 0x80 },                                   // Grey1
    { 0x38, 0xCB, 0x00 },                                   // Green
    { 0xD5, 0xD5, 0x1A },                                   // Yellow
    { 0x2A, 0x2A, 0xE5 },                                   // Dark Blue
    { 0x9D, 0x09, 0x66 },                                   // Violet
    { 0xC0, 0xC0, 0xC0 },                                   // Grey2
    { 0xFF, 0x89, 0xE5 },                                   // Pink
    { 0xAA, 0xAA, 0xFF },                                   // Medium Blue
    { 0x0D, 0xA1, 0xFF },                                   // Light Blue
    { 0x62, 0xF6, 0x99 },                                   // Aqua (Cyan)
    { 0xFF, 0xFF, 0xFF }                                    // White
};

mRGB lores_palette_mono[16] = {
    { 0x00, 0x00, 0x00 },
    { 0x5F, 0x5F, 0x5F },
    { 0x1C, 0x1C, 0x1C },
    { 0x7A, 0x7A, 0x7A },
    { 0x4C, 0x4C, 0x4C },
    { 0x55, 0x55, 0x55 },
    { 0x3C, 0x3C, 0x3C },
    { 0x8A, 0x8A, 0x8A },
    { 0x56, 0x56, 0x56 },
    { 0x85, 0x85, 0x85 },
    { 0xAA, 0xAA, 0xAA },
    { 0xB4, 0xB4, 0xB4 },
    { 0x87, 0x87, 0x87 },
    { 0xB8, 0xB8, 0xB8 },
    { 0xAF, 0xAF, 0xAF },
    { 0xFF, 0xFF, 0xFF },
};

// color_table
uint32_t color_table[8][2][2]; // [bit_stream, 3 bits][column, even/odd][phase]

// Look Up Table with all cobinations of pixel values for color
typedef struct {
    uint32_t pixel[7];   // final RGBA pixels for p[px..px+6]
} HGRLUTENTRY;
// byte      : 0..127   (128) - byte & 0x7F (the 7 pixel bits)
// next_lsb  : 0..1     (2)   - next_byte & 1
// prev_bit  : 0..1     (2)   - previous bit from the prior column
// phase     : 0..1     (2)   - (byte >> 7) & 1
// start_bit : 0..1     (2)   - bit_column & 1
// --------------------------------
// total entries = 128 * 2 * 2 * 2 * 2 = 2048
static HGRLUTENTRY hgr_lut[128][2][2][2][2];
// Look Up Table with monochrome
typedef struct {
    uint32_t pixel[7];
} HGRMONOLUTENTRY;
static HGRMONOLUTENTRY hgr_mono_lut[128];
// Lookup tables for lores
static uint32_t gr_lut[16];
static uint32_t gr_mono_lut[16];
// A character width line of pixels for lores
static uint32_t gr_line[16][7];
static uint32_t gr_mono_line[16][7];

static const uint8_t rev4_lut[16] = {
    0x0, 0x8, 0x4, 0xC,
    0x2, 0xA, 0x6, 0xE,
    0x1, 0x9, 0x5, 0xD,
    0x3, 0xB, 0x7, 0xF
};

// I just recenly learnt that (for modern compilers) small memcpy's turn into better,
// safer, code than a cast assign
static inline uint32_t load_u32_unaligned(const void *p) {
    uint32_t v; memcpy(&v, p, sizeof v); return v;
}

static inline uint16_t load_u16_unaligned(const void *p) {
    uint16_t v; memcpy(&v, p, sizeof v); return v;
}

static inline uint32_t clamp8i(int v) {
    if(v < 0) {
        return 0;
    }

    if(v > 255){
        return 255;
    }

    return v; 
}

// Put the next clipboard character into the KBD
void viewapl2_feed_clipboard_key(APPLE2 *m) {
    while(1) {
        uint8_t byte = !m->clipboard_text ? 0 : (uint8_t)m->clipboard_text[m->clipboard_index++];

        // End of buffer: cleanup + clear bit7 of $C000
        if (byte == 0) {
            SDL_free(m->clipboard_text);
            m->clipboard_text = NULL;
            m->clipboard_index = 0;
            m->write_pages.pages[KBD / PAGE_SIZE].bytes[KBD % PAGE_SIZE] &= 0x7F;
            return;
        }

        // ASCII path
        if (byte < 0x80) {
            if (byte == '\n') {
                // Skip LF
                continue;
            }
            if (byte == '\r') {
                byte = 0x0D;           // keep CR
            } else if (byte == '\t') {
                byte = ' ';            // tabs to space
            } else if (!m->model && byte >= 0x20 && byte <= 0x7E) {
                byte = (uint8_t)toupper(byte); // BASIC-friendly
            }
            // Emit whatever we have (including other control chars unchanged)
            m->RAM_MAIN[KBD] = byte | 0x80;
            return;
        }

        // Non-ASCII start byte: skip the UTF-8 multibyte sequence (drop it)
        while ((m->clipboard_text[m->clipboard_index] & 0xC0) == 0x80) {
            m->clipboard_index++;
        }
    }
}

// Initialize the color_table once, outside the rendering function
void viewapl2_init_color_table(APPLE2 *m) {
    SDL_PixelFormat *format = m->viewport->surface->format;
    for(int bit_stream = 0; bit_stream < 8; bit_stream++) {
        for(int column = 0; column < 2; column++) {
            for(int phase = 0; phase < 2; phase++) {
                int color = bit_stream;
                if(column && (color == 2 || color == 5)) {
                    color ^= 7;
                }
                color += (phase << 3);
                color_table[bit_stream][column][phase] = SDL_MapRGB(format, palette[color].r, palette[color].g, palette[color].b);
            }
        }
    }

    // Init the hgr_lut table
    for(int byte = 0; byte < 128; ++byte) {
        for(int next_lsb = 0; next_lsb < 2; ++next_lsb) {
            for(int prev_bit = 0; prev_bit < 2; ++prev_bit) {
                for(int phase = 0; phase < 2; ++phase) {
                    for(int start_bit = 0; start_bit < 2; ++start_bit) {
                        int stream = (next_lsb << 8) | (byte << 1) | prev_bit;
                        int parity = start_bit;

                        for(int b = 0; b < 7; ++b) {
                            int bit_stream = stream & 0b111;                 // same 3-bit window
                            hgr_lut[byte][next_lsb][prev_bit][phase][start_bit].pixel[b] =
                                color_table[bit_stream][parity][phase];
                            stream >>= 1;                                    // slide window
                            parity ^= 1;                                     // flip each pixel
                        }
                    }
                }
            }
        }
    }

    // Init the hgr_mono_lut table
    for(int byte = 0; byte < 128; ++byte) {
        uint8_t x = byte;
        for(int i = 0; i < 7; ++i) {
            hgr_mono_lut[byte].pixel[i] = (x & 1) ? color_table[7][0][0] : color_table[0][0][0];
            x >>= 1;
        }
    }

    // Init lores
    for(int byte = 0; byte < 16; ++byte) {
        gr_lut[byte] = SDL_MapRGB(format, lores_palette[byte].r, lores_palette[byte].g, lores_palette[byte].b);
        gr_mono_lut[byte] = SDL_MapRGB(format, lores_palette_mono[byte].r, lores_palette_mono[byte].g, lores_palette_mono[byte].b);
        for(int i = 0; i < 7; ++i) {
            gr_line[byte][i] = gr_lut[byte];
            gr_mono_line[byte][i] = gr_mono_lut[byte];
        }
    }

}

void viewapl2_process_event(APPLE2 *m, SDL_Event *e) {
    VIEWPORT *v = m->viewport;
    DEBUGGER *d = &v->debugger;

    // Get joystick states
    for(int i = 0; i < v->num_controllers; i++) {
        if(v->game_controller[i]) {
            v->axis_left_x[i] = (32768 + SDL_GameControllerGetAxis(v->game_controller[i], SDL_CONTROLLER_AXIS_LEFTX)) >> 8;
            v->axis_left_y[i] = (32768 + SDL_GameControllerGetAxis(v->game_controller[i], SDL_CONTROLLER_AXIS_LEFTY)) >> 8;
            v->button_a[i] = SDL_GameControllerGetButton(v->game_controller[i], SDL_CONTROLLER_BUTTON_A);
            v->button_b[i] = SDL_GameControllerGetButton(v->game_controller[i], SDL_CONTROLLER_BUTTON_B);
            v->button_x[i] = SDL_GameControllerGetButton(v->game_controller[i], SDL_CONTROLLER_BUTTON_X);
        }
    }

    // Keyboard keys directly to emulator
    if(e->type == SDL_TEXTINPUT) {
        // Handle regular text input (letters, symbols, etc.)
        m->RAM_MAIN[KBD] = 0x80 | e->text.text[0];
    } else if(e->type == SDL_KEYDOWN) {
        SDL_Keymod mod = SDL_GetModState();

        if(e->key.keysym.sym == SDLK_LALT) {
            m->open_apple = 0x80;
        }
        if(e->key.keysym.sym == SDLK_RALT) {
            m->closed_apple = 0x80;
        }

        if(mod & KMOD_CTRL) {
            // CTRL is held down, now check which key is pressed
            if(e->key.keysym.scancode != SDL_SCANCODE_LCTRL) {
                switch(e->key.keysym.sym) {
                    case SDLK_SCROLLLOCK:                       // CTRL+PAUSE generates SDLK_SCROLLLOCK on my PC
                    case SDLK_PAUSE:
                        // Reset the machine
                        apple2_machine_reset(m);
                        v->prev_cycles = 0;                     // So MHz doesn't jump massively
                        diskii_reset(m);
                        break;
                    default:
                        // CTRL+A = 1, etc.
                        m->RAM_MAIN[KBD] = 0x80 | (e->key.keysym.sym - 0x60);
                        break;
                }
            }
        } else {
            // Handle special keys like ENTER, BACKSPACE, etc.
            switch(e->key.keysym.sym) {
                case SDLK_BACKSPACE:
                    if(m->original_del) {                       // Apple ][ key for del
                        m->RAM_MAIN[KBD] = 0x80 + 127;
                    } else {                                    // CRSR left on del
                        m->RAM_MAIN[KBD] = 0x80 | e->key.keysym.sym;
                    }
                    break;

                case SDLK_RETURN:
                case SDLK_ESCAPE:
                case SDLK_TAB:
                    if(m->clipboard_text) {
                        // Kill any paste that's busy happening
                        SDL_free(m->clipboard_text);
                        m->clipboard_text = 0;
                        m->clipboard_index = 0;
                    }
                    m->RAM_MAIN[KBD] = 0x80 | e->key.keysym.sym;
                    break;

                case SDLK_UP:
                    m->RAM_MAIN[KBD] = 0x8B;                    // UP arrow
                    break;

                case SDLK_DOWN:
                    m->RAM_MAIN[KBD] = 0x8A;                    // DOWN arrow
                    break;

                case SDLK_LEFT:
                    m->RAM_MAIN[KBD] = 0x88;                    // LEFT arrow
                    break;

                case SDLK_RIGHT:
                    m->RAM_MAIN[KBD] = 0x95;                    // RIGHT arrow
                    break;

                case SDLK_INSERT:
                    if(mod & KMOD_SHIFT) {
                        if(SDL_HasClipboardText()) {
                            if(m->clipboard_text) {
                                SDL_free(m->clipboard_text);
                            }
                            m->clipboard_text = SDL_GetClipboardText();
                            m->clipboard_index = 0;
                            viewapl2_feed_clipboard_key(m);
                        }
                    }
                    break;

                default:
                    break;
            }
        }
    } else if(e->type == SDL_KEYUP) {
        switch(e->key.keysym.sym) {
            case SDLK_LALT:
                m->open_apple = 0;
                break;

            case SDLK_RALT:
                m->closed_apple = 0;
                break;
        }
    }
}

// Select which screen to display based on what mode is active
void viewapl2_screen_apple2(APPLE2 *m) {
    // SQW - Path diverts here between ][+ an //e - if 80 cols active
    switch(m->viewport->shadow_screen_mode & 0b111) {
        case 0b001:                                             // lores
            if(m->viewport->shadow_screen_mode & SCREEN_MODE_DOUBLE) {
                viewapl2_screen_dlores(m, 0, 24);
            } else {
                viewapl2_screen_lores(m, 0, 24);
            }
            break;

        case 0b011:                                             // mixed lores
            if(m->viewport->shadow_screen_mode & SCREEN_MODE_DOUBLE) {
                viewapl2_screen_dlores(m, 0, 20);
                viewapl2_screen_txt80(m, 20, 24);
            } else {
                viewapl2_screen_lores(m, 0, 20);
                viewapl2_screen_txt40(m, 20, 24);
            }
            break;

        case 0b101:                                             // hgr graphics
            if(m->viewport->shadow_screen_mode & SCREEN_MODE_DOUBLE) {
                viewapl2_screen_dhgr(m, 0, 192);
            } else {
                viewapl2_screen_hgr(m, 0, 192);
            }
            break;

        case 0b111:                                             // hgr, mixed graphics
            viewapl2_screen_hgr(m, 0, 160);
            viewapl2_screen_txt40(m, 20, 24);
            break;

        default:
            // case 0b000: // text
            // case 0b010: // mixed text (also just text)
            // case 0b100: // hgr but not graphics, so text
            // case 0b110: // hgr, mixed but not graphics, so text
            if(m->franklin80active) {
                viewapl2_screen_franklin80col(m, 0, 24);
            } else if(m->col80set) {
                viewapl2_screen_txt80(m, 0, 24);
            } else {
                viewapl2_screen_txt40(m, 0, 24);
            }
            break;
    }
}

void viewapl2_screen_dlores(APPLE2 *m, int start, int end) {
    if(m->monitor_type) {
        // SQW
        viewapl2_screen_hgr_mono(m, start, end);
        return;
    }

    int y;
    SDL_Surface *surface = m->viewport->surface_wide;
    uint32_t *pixels = (uint32_t *) surface->pixels;
    // uint16_t page = m->viewport->shadow_page2set ? 0x0800 : 0x0400;
    int surface_width = surface->w;

    for(y = start; y < end; y++) {
        uint32_t *p = &pixels[y * 8 * surface_width];
        const uint8_t *man = m->RAM_MAIN + 0x0400 + txt_row_start[y];
        const uint8_t *aux = man + 0x10000;

        for(int col = 0; col < 80; col++) {
            int r;
            uint8_t index = col >> 1;
            uint8_t character = (col & 1) ? man[index] : aux[index];
            uint8_t upper = character & 0x0f;
            uint8_t lower = (character >> 4) & 0X0F;

            // SQW The colors are not right - I am getting brown for purple, etc.
            uint32_t *pr = p;
            for(r = 0; r < 4; r++) {
                memcpy(pr, gr_line[upper], sizeof(gr_line[upper]));
                pr += surface->w;
            }
            for(r = 0; r < 4; r++) {
                memcpy(pr, gr_line[lower], sizeof(gr_line[lower]));
                pr += surface->w;
            }
            p += 7;
        }
    }
}

// Display the lores screen in color
void viewapl2_screen_lores(APPLE2 *m, int start, int end) {
    if(m->monitor_type) {
        viewapl2_screen_lores_mono(m, start, end);
        return;
    }

    int x, y;
    SDL_Surface *surface = m->viewport->surface;
    uint32_t *pixels = (uint32_t *) surface->pixels;
    int page = m->viewport->shadow_page2set ? 0x0800 : 0x0400;

    // Loop through each row
    for(y = start; y < end; y++) {
        // Get the pointer to the start of the row in the SDL surface
        uint32_t *p = &pixels[y * 8 * surface->w];
        int address = page + txt_row_start[y];

        // Loop through every col (byte)
        for(int x = 0; x < 40; x++) {
            int r;
            // Get the byte on screen
            uint8_t character = m->RAM_MAIN[address + x];
            uint8_t upper = character & 0x0f;
            uint8_t lower = (character >> 4) & 0X0F;
            uint32_t *pr = p;
            for(r = 0; r < 4; r++) {
                memcpy(pr, gr_line[upper], sizeof(gr_line[upper]));
                pr += surface->w;
            }
            for(r = 0; r < 4; r++) {
                memcpy(pr, gr_line[lower], sizeof(gr_line[lower]));
                pr += surface->w;
            }
            p += 7;
        }
    }
}

// Display the lores screen in b&w
void viewapl2_screen_lores_mono(APPLE2 *m, int start, int end) {
    int x, y;
    SDL_Surface *surface = m->viewport->surface;
    uint32_t *pixels = (uint32_t *) surface->pixels;
    int page = m->viewport->shadow_page2set ? 0x0800 : 0x0400;

    // Loop through each row
    for(y = start; y < end; y++) {
        // Get the pointer to the start of the row in the SDL surface
        uint32_t *p = &pixels[y * 8 * surface->w];
        int address = page + txt_row_start[y];

        // Loop through every col (byte)
        for(int x = 0; x < 40; x++) {
            int r;
            // Get the byte on screen
            uint8_t character = m->RAM_MAIN[address + x];
            uint8_t upper = character & 0x0f;
            uint8_t lower = (character >> 4) & 0X0F;
            uint32_t *pr = p;
            for(r = 0; r < 4; r++) {
                memcpy(pr, gr_mono_line[upper], sizeof(gr_mono_line[upper]));
                pr += surface->w;
            }
            for(r = 0; r < 4; r++) {
                memcpy(pr, gr_mono_line[lower], sizeof(gr_mono_line[lower]));
                pr += surface->w;
            }
            p += 7;
        }
    }
}

// Display the double hires screen
void viewapl2_screen_dhgr(APPLE2 *m, int start, int end) {
    if(m->monitor_type) {
        // SQW
        viewapl2_screen_dhgr_rgb(m, start, end);
        return;
    }

    int y;
    SDL_Surface *surface = m->viewport->surface_wide;
    uint32_t *pixels = (uint32_t *) surface->pixels;
    uint16_t page = m->viewport->shadow_page2set ? 0x4000 : 0x2000;
    int surface_width = surface->w;
    SDL_PixelFormat *format = m->viewport->surface->format; // (unused here but keeping your locals)

    for(y = start; y < end; y++) {
        uint32_t *p = &pixels[y * surface_width];
        uint16_t address = page + hgr_row_start[y];
        const uint8_t *aux = m->RAM_MAIN + page + hgr_row_start[y] + 0x10000;  // b0,b2,b4 live here
        const uint8_t *man = m->RAM_MAIN + page + hgr_row_start[y];            // b1,b3 live here
        uint8_t R = 0, G = 0, B = 0;                                           // exponential moving average

        for(int col = 0; col < 40; col += 2) {
            // Pull 3 aux bytes (b0,b2,b4) and 2 main bytes (b1,b3) at once, clear bit7 in bulk.
            uint32_t AUX = load_u32_unaligned(aux + col) & 0x7F7F7F7Fu; // b0=AUX0, b2=AUX1, b4=AUX2, (AUX3 unused)
            uint16_t MAN = load_u16_unaligned(man + col) & 0x7F7Fu;     // b1=MAN0, b3=MAN1

            // Extract the bytes, interleaving aux / main
            uint8_t b0 = (uint8_t)(AUX     );
            uint8_t b1 = (uint8_t)(MAN     );
            uint8_t b2 = (uint8_t)(AUX >> 8);
            uint8_t b3 = (uint8_t)(MAN >> 8);
            uint8_t b4 = (col < 38) ? (uint8_t)(AUX >> 16) : 0;

            // Make nibbles 
            uint8_t t0 =  (b0     ) & 0x0F;
            uint8_t t1 =  (b0 >> 4) | ((b1 & 0x1) << 3);
            uint8_t t2 =  (b1 >> 1) & 0x0F;
            uint8_t t3 = ((b1 >> 5) & 0x03) | ((b2 & 0x3) << 2);
            uint8_t t4 =  (b2 >> 2) & 0x0F;
            uint8_t t5 =  (b2 >> 6) | ((b3 & 0x7) << 1);
            uint8_t t6 =  (b3 >> 3)       ;
            uint8_t t7 =  (b4     ) & 0x0F;

            // Put the bits in a stream (reverse display order)
            uint32_t stream = t7 << 28 | t6 << 24 | t5 << 20 | t4 << 16 | t3 << 12 | t2 << 8 | t1 << 4 | t0;

            // Tunables:
            const int phase0  = 1;  // flip 0/1 if whole image hue is inverted
            const int phase90 = 1;  // shift 0..3 to rotate hue
            const int kY = 20;      // brightness gain
            const int kI = 32;      // saturation (I axis)
            const int kQ = 24;      // saturation (Q axis)
            const int dead = 3;     // chroma deadzone to keep near-grays neutral

            for(int x = 0; x < 28; x++) {
                uint8_t px = stream & 0x0f;
                stream >>= 1;

                int fx = x;

                int b0 = (px & 1);
                int b1 = (px & 2) >> 1;
                int b2 = (px & 4) >> 2;
                int b3 = (px & 8) >> 3;

                int s0 = b0 ? +1 : -1;
                int s1 = b1 ? +1 : -1;
                int s2 = b2 ? +1 : -1;
                int s3 = b3 ? +1 : -1;

                int r0 = (((fx + 0 + phase0 ) & 1) ? -1 : +1);
                int r1 = (((fx + 0 + phase0 ) & 1) ? -1 : +1);
                int r2 = (((fx + 1 + phase0 ) & 1) ? -1 : +1);
                int r3 = (((fx + 1 + phase0 ) & 1) ? -1 : +1);

                int t0 =   (fx + 0 + phase90) & 3;
                int t1 =   (fx + 0 + phase90) & 3;
                int t2 =   (fx + 1 + phase90) & 3;
                int t3 =   (fx + 1 + phase90) & 3;

                int q0 = (t0 == 0) ? +1 : (t0 == 2) ? -1 : 0;
                int q1 = (t1 == 0) ? +1 : (t1 == 2) ? -1 : 0;
                int q2 = (t2 == 0) ? +1 : (t2 == 2) ? -1 : 0;
                int q3 = (t3 == 0) ? +1 : (t3 == 2) ? -1 : 0;

                int Iraw = s0*r0 + s1*r1 + s2*r2 + s3*r3;
                int Qraw = s0*q0 + s1*q1 + s2*q2 + s3*q3;

                // Deadzone: keep near-gray neutral
                int amag = (Iraw < 0 ? -Iraw : Iraw) + (Qraw < 0 ? -Qraw : Qraw);
                if (amag <= dead) {
                    Iraw = 0;
                    Qraw = 0; 
                }

                int Y = kY * px;
                int I = kI * Iraw;
                int Q = kQ * Qraw;

                uint8_t R0 = clamp8i(Y + 245*I + 159*Q);
                uint8_t G0 = clamp8i(Y -  70*I - 166*Q);
                uint8_t B0 = clamp8i(Y - 283*I + 436*Q);

                R = (R0 + R) / 2; // R = (1-0.5)R + 0.5R
                G = (G0 + G) / 2;
                B = (B0 + B) / 2;
                
                *p++ = SDL_MapRGB(format, R, G, B);
            }
        }
    }
}

// Display the double hires screen
void viewapl2_screen_dhgr_rgb(APPLE2 *m, int start, int end) {
    int y;
    SDL_Surface *surface = m->viewport->surface_wide;
    uint32_t *pixels = (uint32_t *) surface->pixels;
    uint16_t page = m->viewport->shadow_page2set ? 0x4000 : 0x2000;
    int surface_width = surface->w;
    SDL_PixelFormat *format = m->viewport->surface->format; // (unused here but keeping your locals)

    for(y = start; y < end; y++) {
        uint32_t *p = &pixels[y * surface_width];
        uint16_t address = page + hgr_row_start[y];
        const uint8_t *aux = m->RAM_MAIN + page + hgr_row_start[y] + 0x10000;  // b0,b2,b4 live here
        const uint8_t *man = m->RAM_MAIN + page + hgr_row_start[y];            // b1,b3 live here

        for(int col = 0; col < 40; col += 2) {
            // Pull 2 aux bytes (b0,b2) and 2 main bytes (b1,b3) at once, clear bit7 in bulk.
            uint32_t A = load_u16_unaligned(aux + col) & 0x7F7Fu;   // b0=A0, b2=A1
            uint16_t B = load_u16_unaligned(man + col) & 0x7F7Fu;   // b1=B0, b3=B1

            // Extract the bytes, interleaving aux / main
            uint8_t b0 = (uint8_t)(A      );
            uint8_t b1 = (uint8_t)(B      );
            uint8_t b2 = (uint8_t)(A >>  8);
            uint8_t b3 = (uint8_t)(B >>  8);

            // Make nibbles (SQW and the reverse bits seem to work better but I don't know...)
            uint8_t t0 = rev4_lut[  (b0     ) & 0x0F];
            uint8_t t1 = rev4_lut[  (b0 >> 4) | ((b1 & 0x1) << 3)];
            uint8_t t2 = rev4_lut[  (b1 >> 1) & 0x0F];
            uint8_t t3 = rev4_lut[ ((b1 >> 5) & 0x03) | ((b2 & 0x3) << 2)];
            uint8_t t4 = rev4_lut[  (b2 >> 2) & 0x0F];
            uint8_t t5 = rev4_lut[  (b2 >> 6) | ((b3 & 0x7) << 1)];
            uint8_t t6 = rev4_lut[  (b3 >> 3)       ];

            // Put the bits in a stream
            uint32_t stream = t6 << 24 | t5 << 20 | t4 << 16 | t3 << 12 | t2 << 8 | t1 << 4 | t0;

            for (int x = 0; x < 28/4; ++x) {
                int bits = stream & 0x0f;
                uint32_t rgb_pixel = SDL_MapRGB(format, dhgr_palette[bits].r, dhgr_palette[bits].g, dhgr_palette[bits].b);
                *p++ = rgb_pixel;
                *p++ = rgb_pixel;
                *p++ = rgb_pixel;
                *p++ = rgb_pixel;
                stream >>= 4;
            }
        }
    }
}

void viewapl2_screen_hgr(APPLE2 *m, int start, int end) {
    if(m->monitor_type) {
        viewapl2_screen_hgr_mono(m, start, end);
        return;
    }

    int y;
    SDL_Surface *surface = m->viewport->surface;
    uint32_t *pixels = (uint32_t *) surface->pixels;
    uint16_t page = m->viewport->shadow_page2set ? 0x4000 : 0x2000;
    int surface_width = surface->w;

    for(y = start; y < end; y++) {
        uint32_t *p = &pixels[y * surface_width];
        int px = 0;
        uint16_t address = page + hgr_row_start[y];
        uint8_t byte = m->RAM_MAIN[address];

        int bit_column = 0;                 // start of scanline
        int prev_bit = 0;                   // first column "left neighbor" is 0

        for(int col = 0; col < 40; ++col) {
            uint8_t next_byte = (col + 1 < 40) ? m->RAM_MAIN[address + col + 1] : 0;
            int next_lsb = next_byte & 1;
            int phase   = (byte >> 7) & 1;

            const HGRLUTENTRY *e = &hgr_lut[byte & 0x7F][next_lsb][prev_bit][phase][bit_column];

            // emit the 7 pixels already expanded in the LUT
            p[px + 0] = e->pixel[0];
            p[px + 1] = e->pixel[1];
            p[px + 2] = e->pixel[2];
            p[px + 3] = e->pixel[3];
            p[px + 4] = e->pixel[4];
            p[px + 5] = e->pixel[5];
            p[px + 6] = e->pixel[6];
            px += 7;

            // prepare for next column
            bit_column ^= 1;                // 7 flips net to one flip per column
            prev_bit = (byte >> 6) & 1;     // current byte's bit 6 (msb ignoring phase bit)
            byte = next_byte;
        }
    }
}

// Display the hires screen in B&W
void viewapl2_screen_hgr_mono(APPLE2 *m, int start, int end) {
    int x, y;
    SDL_Surface *surface = m->viewport->surface;
    uint32_t *pixels = (uint32_t *) surface->pixels;
    int page = m->viewport->shadow_page2set ? 0x4000 : 0x2000;
    uint32_t c[2] = { color_table[0][0][0], color_table[7][0][0] };

    // Loop through each row
    for(y = start; y < end; y++) {
        // Get the pointer to the start of the row in the SDL surface
        uint32_t *p = &pixels[y * surface->w];
        int address = page + hgr_row_start[y];

        // Loop through every column (40 iterations for each 280-pixel row)
        uint8_t *bytes = &m->RAM_MAIN[address];
        for(int x = 0; x < 40; x++) {
            const HGRMONOLUTENTRY *e = &hgr_mono_lut[*bytes++ & 0x7F];
            p[0] = e->pixel[0];
            p[1] = e->pixel[1];
            p[2] = e->pixel[2];
            p[3] = e->pixel[3];
            p[4] = e->pixel[4];
            p[5] = e->pixel[5];
            p[6] = e->pixel[6];
            p += 7;
        }
    }
}

// Display the text screen
void viewapl2_screen_txt40(APPLE2 *m, int start, int end) {
    int x, y;
    SDL_Surface *surface = m->viewport->surface;
    uint32_t *pixels = (uint32_t *) surface->pixels;
    int page = m->viewport->shadow_page2set ? 0x0800 : 0x0400;
    Uint64 now = SDL_GetPerformanceCounter();
    double freq = (double)SDL_GetPerformanceFrequency();
    // I got 3.7 from recording a flash on my Platinum //e - 0.17 to 0.44 for a change so 0.27
    uint8_t time_inv = (((uint64_t)(now * 3.7 / freq)) & 1) ? 0xFF : 0x00;
    int alt_charset = m->model ? m->model && m->altcharset : 0;

    // Loop through each row
    for(y = start; y < end; y++) {
        // Get the pointer to the start of the row in the SDL surface
        uint32_t *p = &pixels[y * 8 * surface->w];
        int address = page + txt_row_start[y];

        // Loop through every col (byte)
        for(int x = 0; x < 40; x++) {
            int r;
            uint8_t inv = 0x00;
            uint8_t character = m->RAM_MAIN[address + x];   // Get the character on screen
            if(character < 0x80) {
                if(character >= 0x40) {
                    if(!alt_charset) {
                        character &= 0x3F;
                        inv = time_inv;
                    }
                } else if(!m->model) {
                    inv = 0xFF;
                }
            }
            // Get the font offset in the font blocks
            uint8_t *character_font = &m->roms.blocks[ROM_APPLE2_CHARACTER].bytes[character * 8];
            // "Plot" the character to the SDL graphics screen
            uint32_t *pr = p;
            for(r = 0; r < 8; r++) {
                uint8_t pixels = inv ^ *character_font++;
                for(int i = 6; i >= 0; i--) {
                    pr[i] = gr_lut[(pixels & 1) ? 15 : 0];
                    pixels >>= 1;
                }
                pr += surface->w;
            }
            p += 7;
        }
    }
}

void viewapl2_screen_txt80(APPLE2 *m, int start, int end) {
    int x, y;
    SDL_Surface *surface = m->viewport->surface_wide;
    uint32_t *pixels = (uint32_t *) surface->pixels;
    Uint64 now = SDL_GetPerformanceCounter();
    double freq = (double)SDL_GetPerformanceFrequency();
    // I got 3.7 from recording a flash on my Platinum //e - 0.17 to 0.44 for a change so 0.27
    uint8_t time_inv = (((uint64_t)(now * 3.7 / freq)) & 1) ? 0xFF : 0x00;
    int alt_charset = m->model ? m->model && m->altcharset : 0;

    // Loop through each row
    for(y = start; y < end; y++) {
        // Get the pointer to the start of the row in the SDL surface
        uint32_t *p = &pixels[y * 8 * surface->w];
        int address = 0x0400 + txt_row_start[y];

        // Loop through every col (byte)
        for(int x = 0; x < 80; x++) {
            int r;
            uint8_t inv = 0x00;
            int char_in_bank = (x & 1) ? (x >> 1) : (x >> 1) + 0x10000;
            uint8_t character = m->RAM_MAIN[address + char_in_bank];   // Get the character on screen
            if(character < 0x80) {
                if(character >= 0x40) {
                    if(!alt_charset) {
                        character &= 0x3F;
                        inv = time_inv;
                    }
                } else if(!m->model) {
                    inv = 0xFF;
                }
            }
            // Get the font offset in the font blocks
            uint8_t *character_font = &m->roms.blocks[ROM_APPLE2_CHARACTER].bytes[character * 8];
            // "Plot" the character to the SDL graphics screen
            uint32_t *pr = p;
            for(r = 0; r < 8; r++) {
                uint8_t pixels = inv ^ *character_font++;
                for(int i = 6; i >= 0; i--) {
                    pr[i] = gr_lut[(pixels & 1) ? 15 : 0];
                    pixels >>= 1;
                }
                pr += surface->w;
            }
            p += 7;
        }
    }
}

// Display the 80 col text screen in franklin 80 mode
void viewapl2_screen_franklin80col(APPLE2 *m, int start, int end) {
    int x, y;
    FRANKLIN_DISPLAY *fd80 = &m->franklin_display;
    SDL_Surface *surface = m->viewport->surface_wide;
    uint32_t *pixels = (uint32_t *) surface->pixels;
    // int page = m->viewport->shadow_page2set ? 0x0800 : 0x0400;
    uint32_t c[2] = { color_table[0][0][0], color_table[7][0][0] };
    uint16_t display_offset = 256 * fd80->registers[0x0c] + fd80->registers[0x0d];

    // Loop through each row
    for(y = start; y < end; y++) {
        // Get the pointer to the start of the row in the SDL surface
        uint32_t *p = &pixels[y * 8 * surface->w];
        int address = y * 80 + display_offset;

        // Loop through every col (byte)
        for(int x = 0; x < 80; x++) {
            int r;
            // Get the character on screen
            int character = fd80->display_ram[(address + x) & 0x7ff];
            // See if inverse
            uint8_t inv = (character >> 7) & 1 ? 0xff : 0;
            // Get the font offset in the font blocks
            uint8_t *character_font = &m->roms.blocks[ROM_FRANKLIN_ACE_CHARACTER].bytes[character * 16];
            // "Plot" the character to the SDL graphics screen
            uint32_t *pr = p;
            for(r = 0; r < 8; r++) {
                uint8_t pixels = inv ^ *character_font++;
                for(int i = 7; i >= 0; i--) {
                    pr[i] = c[pixels & 1];
                    pixels >>= 1;
                }
                pr += surface->w;
            }
            p += 8;
        }
    }
    // Draw the cursor
    uint16_t cursor_address = 256 * fd80->registers[FD80_CURSOR_HI] + fd80->registers[FD80_CURSOR_LO];
    uint16_t delta = (cursor_address - display_offset) % 2048;
    uint8_t cursor_x = delta % 80;
    uint8_t cursor_y = delta / 80;
    uint32_t *p = &pixels[cursor_x * 8 + cursor_y * 8 * surface->w];
    int character = fd80->display_ram[cursor_address % 2048];
    uint8_t *character_font = &m->roms.blocks[ROM_FRANKLIN_ACE_CHARACTER].bytes[character * 16];
    for(int r = 0; r < 8; r++) {
        uint8_t pixels = 0xFF ^ *character_font++;
        for(int i = 7; i >= 0; i--) {
            p[i] = c[pixels & 1];
            pixels >>= 1;
        }
        p += surface->w;
    }
}
