// Apple ][+ emulator
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
    { 0x9D, 0x09, 0x66 },                                   // Red
    { 0x2A, 0x2A, 0xE5 },                                   // Dark blue
    { 0xC7, 0x34, 0xFF },                                   // Purple
    { 0x00, 0x80, 0x00 },                                   // Dark green
    { 0x80, 0x80, 0x80 },                                   // Gray
    { 0x0D, 0xA1, 0xFF },                                   // Blue-cyan
    { 0xAA, 0xAA, 0xFF },                                   // Light blue
    { 0x55, 0x55, 0x00 },                                   // Brown
    { 0xF2, 0x5E, 0x00 },                                   // Orange
    { 0xC0, 0xC0, 0xC0 },                                   // Gray
    { 0xFF, 0x89, 0xE5 },                                   // Pink
    { 0x38, 0xCB, 0x00 },                                   // Bright green
    { 0xD5, 0xD5, 0x1A },                                   // Yellow
    { 0x62, 0xF6, 0x99 },                                   // Cyan
    { 0xFF, 0xFF, 0xFF },                                   // White
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

uint32_t color_table[8][2][2];                              // [bit_stream][column][phase]

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
}

void viewapl2_process_event(APPLE2 *m, SDL_Event *e) {
    VIEWPORT *v = m->viewport;
    DEBUGGER *d = &v->debugger;

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
                switch (e->key.keysym.sym) {
                case SDLK_SCROLLLOCK:                       // CTRL+PAUSE generates SDLK_SCROLLLOCK on my PC
                case SDLK_PAUSE:
                    cpu_init(m);
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
            switch (e->key.keysym.sym) {
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

            default:
                break;
            }
        }
    } else if(e->type == SDL_KEYUP) {
        switch (e->key.keysym.sym) {
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
    switch (m->viewport->shadow_screen_mode) {
    case 0b001:                                             // lores
        viewapl2_screen_lores(m, 0, 24);
        break;

    case 0b011:                                             // mixed lores
        viewapl2_screen_lores(m, 0, 20);
        viewapl2_screen_txt(m, 20, 24);
        break;

    case 0b101:                                             // hgr graphics
        viewapl2_screen_hgr(m, 0, 192);
        break;

    case 0b111:                                             // hgr, mixed graphics
        viewapl2_screen_hgr(m, 0, 160);
        viewapl2_screen_txt(m, 20, 24);
        break;

    default:
        // case 0b000: // text
        // case 0b010: // mixed text (also just text)
        // case 0b100: // hgr but not graphics, so text
        // case 0b110: // hgr, mixed but not graphics, so text
        if(m->cols80active) {
            viewapl2_screen_80col(m, 0, 24);
        } else {
            viewapl2_screen_txt(m, 0, 24);
        }
        break;
    }

    // Mark screen as updated now
    m->screen_updated = 0;
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
    int page = m->viewport->shadow_active_page ? 0x0800 : 0x0400;

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
            uint32_t pu = SDL_MapRGB(surface->format, lores_palette[upper].r, lores_palette[upper].g, lores_palette[upper].b);
            uint32_t pl = SDL_MapRGB(surface->format, lores_palette[lower].r, lores_palette[lower].g, lores_palette[lower].b);

            for(r = 0; r < 4; r++) {
                pr[0] = pu;
                pr[1] = pu;
                pr[2] = pu;
                pr[3] = pu;
                pr[4] = pu;
                pr[5] = pu;
                pr[6] = pu;
                pr += surface->w;
            }

            for(r = 0; r < 4; r++) {
                pr[0] = pl;
                pr[1] = pl;
                pr[2] = pl;
                pr[3] = pl;
                pr[4] = pl;
                pr[5] = pl;
                pr[6] = pl;
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
    int page = m->viewport->shadow_active_page ? 0x0800 : 0x0400;

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
            uint32_t pu =
                SDL_MapRGB(surface->format, lores_palette_mono[upper].r, lores_palette_mono[upper].g, lores_palette_mono[upper].b);
            uint32_t pl =
                SDL_MapRGB(surface->format, lores_palette_mono[lower].r, lores_palette_mono[lower].g, lores_palette_mono[lower].b);

            for(r = 0; r < 4; r++) {
                pr[0] = pu;
                pr[1] = pu;
                pr[2] = pu;
                pr[3] = pu;
                pr[4] = pu;
                pr[5] = pu;
                pr[6] = pu;
                pr += surface->w;
            }

            for(r = 0; r < 4; r++) {
                pr[0] = pl;
                pr[1] = pl;
                pr[2] = pl;
                pr[3] = pl;
                pr[4] = pl;
                pr[5] = pl;
                pr[6] = pl;
                pr += surface->w;
            }
            p += 7;
        }
    }
}

// Display the hires screen
void viewapl2_screen_hgr(APPLE2 *m, int start, int end) {
    if(m->monitor_type) {
        viewapl2_screen_hgr_mono(m, start, end);
        return;
    }

    int y;
    SDL_Surface *surface = m->viewport->surface;
    uint32_t *pixels = (uint32_t *) surface->pixels;
    uint16_t page = m->viewport->shadow_active_page ? 0x4000 : 0x2000;
    int surface_width = surface->w;

    for(y = start; y < end; y++) {
        uint32_t *p = &pixels[y * surface_width];
        int px = 0;
        uint16_t address = page + hgr_row_start[y];
        uint8_t byte = m->RAM_MAIN[address];

        int current_stream = 0;
        for(int column = 0, bit_column = 0; column < 40; column++) {
            // Extract the phase from the byte
            int phase = (byte & 0x80) >> 7;

            // Read the next byte
            uint8_t next_byte = 0;
            if(column + 1 < 40) {
                next_byte = m->RAM_MAIN[address + column + 1];
            }
            // Prepare the current stream as the next byte (for lsb), the current byte and the last
            // current bit (now prev bit)
            current_stream = ((next_byte & 0x01) << 8) | ((byte & 0x7F) << 1) | (current_stream & 1);

            // Process the 7 bits
            for(int bit_position = 0; bit_position < 7; bit_position++) {
                // Get the 3 bits that matter
                int bit_stream = current_stream & 0b111;
                // and shift right so that the current bit becomes the prev bit
                current_stream >>= 1;

                // Find the pixel color, and plot it
                uint32_t pixel_rgb = color_table[bit_stream][bit_column][phase];
                p[px++] = pixel_rgb;
                bit_column ^= 1;
            }

            // Prepare for the next iteration
            byte = next_byte;
        }
    }
}

// Display the hires screen in B&W
void viewapl2_screen_hgr_mono(APPLE2 *m, int start, int end) {
    int x, y;
    SDL_Surface *surface = m->viewport->surface;
    uint32_t *pixels = (uint32_t *) surface->pixels;
    int page = m->viewport->shadow_active_page ? 0x4000 : 0x2000;
    uint32_t c[2] = { color_table[0][0][0], color_table[7][0][0] };

    // Loop through each row
    for(y = start; y < end; y++) {
        // Get the pointer to the start of the row in the SDL surface
        uint32_t *p = &pixels[y * surface->w];
        int address = page + hgr_row_start[y];

        // Loop through every bytes (40 iterations for each 280-pixel row)
        uint8_t *bytes = &m->RAM_MAIN[address];
        for(int x = 0; x < 40; x++) {
            uint8_t pixels = *bytes++;
            p[0] = c[pixels & 1];
            pixels >>= 1;
            p[1] = c[pixels & 1];
            pixels >>= 1;
            p[2] = c[pixels & 1];
            pixels >>= 1;
            p[3] = c[pixels & 1];
            pixels >>= 1;
            p[4] = c[pixels & 1];
            pixels >>= 1;
            p[5] = c[pixels & 1];
            pixels >>= 1;
            p[6] = c[pixels & 1];
            p += 7;
        }
    }
}

// Display the text screen
void viewapl2_screen_txt(APPLE2 *m, int start, int end) {
    int x, y;
    SDL_Surface *surface = m->viewport->surface;
    uint32_t *pixels = (uint32_t *) surface->pixels;
    int page = m->viewport->shadow_active_page ? 0x0800 : 0x0400;
    uint32_t c[2] = { color_table[0][0][0], color_table[7][0][0] };

    // Loop through each row
    for(y = start; y < end; y++) {
        // Get the pointer to the start of the row in the SDL surface
        uint32_t *p = &pixels[y * 8 * surface->w];
        int address = page + txt_row_start[y];

        // Loop through every col (byte)
        for(int x = 0; x < 40; x++) {
            int r;
            // Get the character on screen
            int character = m->RAM_MAIN[address + x];
            // See if inverse
            uint8_t inv = (~character >> 7) & 1;
            // Get the font offset in the font blocks
            uint8_t *character_font = &m->roms.blocks[ROM_CHARACTER].bytes[character * 8];
            // "Plot" the character to the SDL graphics screen
            uint32_t *pr = p;
            for(r = 0; r < 8; r++) {
                uint8_t pixels = *character_font++;
                for(int i = 6; i >= 0; i--) {
                    pr[i] = c[inv ^ (pixels & 1)];
                    pixels >>= 1;
                }
                // This is for //e font
                // for(int i=0; i < 7 ; i++) {
                //     pr[i] = c[inv ^ (pixels & 1)];
                //     pixels >>= 1;
                // }
                pr += surface->w;
            }
            p += 7;
        }
    }
}

// Display the 80 col text screen
void viewapl2_screen_80col(APPLE2 *m, int start, int end) {
    int x, y;
    FRANKLIN_DISPLAY *fd80 = &m->franklin_display;
    SDL_Surface *surface = m->viewport->surface640;
    uint32_t *pixels = (uint32_t *) surface->pixels;
    // int page = m->viewport->shadow_active_page ? 0x0800 : 0x0400;
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
            uint8_t inv = (character >> 7) & 1;
            // Get the font offset in the font blocks
            uint8_t *character_font = &m->roms.blocks[ROM_FRANKLIN_ACE_CHARACTER].bytes[character * 16];
            // "Plot" the character to the SDL graphics screen
            uint32_t *pr = p;
            for(r = 0; r < 8; r++) {
                uint8_t pixels = *character_font++;
                for(int i = 7; i >= 0; i--) {
                    pr[i] = c[inv ^ (pixels & 1)];
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
