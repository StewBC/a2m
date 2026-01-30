// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "unk_lib.h"

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

mRGB palette_16[16] = {
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

mRGB palette_16_mono[16] = {
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

// double lores interleaves the palette entries of lowres
// so this table maps the colors properly based on the bits and bank
// effectivly de-interleaves the aux bits to non-aux colors
uint8_t double_aux_map[] = {
    0x00,
    0x02,
    0x04,
    0x06,
    0x08,
    0x0A,
    0x0C,
    0x0E,
    0x01,
    0x03,
    0x05,
    0x07,
    0x09,
    0x0B,
    0x0D,
    0x0F
};

// color_table
uint32_t color_table[8][2][2]; // [bit_stream, 3 bits][column, even/odd][phase]
// Look Up Table with all combinations of pixel values for color
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
static HGRLUTENTRY hgr_mono_lut[128];
// Lookup tables for lores
static uint32_t gr_lut[16];
static uint32_t gr_mono_lut[16];
// A character width line of pixels for lores. 16 colors and
// 7 pixels wide (all same color) but used with memcpy
static uint32_t gr_line[16][7];
static uint32_t gr_mono_line[16][7];

// Looupup table that revesrses the bits of the imdex
static const uint8_t rev4_lut[16] = {
    0x0, 0x8, 0x4, 0xC,
    0x2, 0xA, 0x6, 0xE,
    0x1, 0x9, 0x5, 0xD,
    0x3, 0xB, 0x7, 0xF
};

// I just recenly learnt that (for modern compilers) small memcpy's turn into better,
// safer, code than a cast assign
static inline uint32_t load_u32_unaligned(const void *p) {
    uint32_t v;
    memcpy(&v, p, sizeof v);
    return v;
}

static inline uint16_t load_u16_unaligned(const void *p) {
    uint16_t v;
    memcpy(&v, p, sizeof v);
    return v;
}

static inline uint32_t clamp8i(int v) {
    if(v < 0) {
        return 0;
    }

    if(v > 255) {
        return 255;
    }

    return v;
}


// Initialize the color_table once, outside the rendering function
void unk_apl2_init_color_table(UNK *v) {
    SDL_PixelFormat *format = v->surface->format;
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
            // Assign white or black only based on the bit being 1 ot 0
            hgr_mono_lut[byte].pixel[i] = (x & 1) ? color_table[7][0][0] : color_table[0][0][0];
            x >>= 1;
        }
    }

    // Init lores
    for(int byte = 0; byte < 16; ++byte) {
        gr_lut[byte] = SDL_MapRGB(format, palette_16[byte].r, palette_16[byte].g, palette_16[byte].b);
        gr_mono_lut[byte] = SDL_MapRGB(format, palette_16_mono[byte].r, palette_16_mono[byte].g, palette_16_mono[byte].b);
        for(int i = 0; i < 7; ++i) {
            gr_line[byte][i] = gr_lut[byte];
            gr_mono_line[byte][i] = gr_mono_lut[byte];
        }
    }

}

void unk_apl2_process_event(UNK *v, SDL_Event *e) {
    VIEWDASM *dv = &v->viewdasm;
    APPLE2 *m = v->m;
    RUNTIME *rt = v->rt;
    uint8_t active_key = 0;
    
    if(e->type == SDL_KEYDOWN) {
        SDL_Keymod mod = SDL_GetModState();

        if(e->key.keysym.sym == SDLK_LALT) {
            set_flags(m->state_flags, A2S_OPEN_APPLE);
            return;
        }
        if(e->key.keysym.sym == SDLK_RALT) {
            set_flags(m->state_flags, A2S_CLOSED_APPLE);
            return;
        }

        if(mod & KMOD_CTRL) {
            // CTRL is held down, now check which key is pressed
            if(e->key.keysym.scancode != SDL_SCANCODE_LCTRL) {
                switch(e->key.keysym.sym) {
                    case SDLK_SCROLLLOCK:                       // CTRL+PAUSE generates SDLK_SCROLLLOCK on my PC
                    case SDLK_PAUSE:
                        // Reset the machine
                        rt_machine_reset(rt);
                        v->prev_cycles = 0;                     // So MHz doesn't jump massively
                        return;

                    default:
                        // CTRL+A = 1, etc.
                        active_key = 0x80 | (e->key.keysym.sym - 0x60);
                        break;
                }
            }
        } else {
            // Handle special keys like ENTER, BACKSPACE, etc.
            switch(e->key.keysym.sym) {
                case SDLK_DELETE:
                    // This is the original_del on DEL key
                    active_key = 0x80 + 127;
                    break;

                case SDLK_BACKSPACE:
                    // SQW - Add UI/INI toggle
                    if(v->original_del) {                       // Apple ][ key for del
                        active_key = 0x80 + 127;
                    } else {                                    // CRSR left on del
                        // This just works so much nicer normally
                        active_key = 0x80 | e->key.keysym.sym;
                    }
                    break;

                case SDLK_RETURN:
                case SDLK_ESCAPE:
                case SDLK_TAB:
                    // SQW - Maybe reactivate?  Kill the paste from the clipboard
                    active_key = 0x80 | e->key.keysym.sym;
                    break;

                case SDLK_UP:
                    active_key = 0x8B;                    // UP arrow
                    break;

                case SDLK_DOWN:
                    active_key = 0x8A;                    // DOWN arrow
                    break;

                case SDLK_LEFT:
                    active_key = 0x88;                    // LEFT arrow
                    break;

                case SDLK_RIGHT:
                    active_key = 0x95;                    // RIGHT arrow
                    break;

                case SDLK_INSERT:
                    if(mod & KMOD_SHIFT) {
                        if(SDL_HasClipboardText()) {
                            active_key = 0;
                            char *clipboard_text = SDL_GetClipboardText();
                            rt_paste_clipboard(rt, clipboard_text);
                            SDL_free(clipboard_text);
                        }
                    }
                    break;

                default: {
                        // When ALT is used SDL_TEXTINPUT doesn't work and
                        // e->key.keysym is the key (like SHIFT+/ would just be /)
                        // Figure out what to put through to the Apple II based on
                        // mod and key - OA and CA are handled already
                        uint8_t a2_key;
                        if(unk_ascii_from_sdl_keydown(e->key.keysym.sym, mod, &a2_key)) {
                            active_key = 0x80 | a2_key;
                        }
                    }
                    break;
            }
        }
    } else if(e->type == SDL_KEYUP) {
        switch(e->key.keysym.sym) {
            case SDLK_LALT:
                clr_flags(m->state_flags, A2S_OPEN_APPLE);
                break;

            case SDLK_RALT:
                clr_flags(m->state_flags, A2S_CLOSED_APPLE);
                break;
            
            default:
                apple2_clear_key_held(m);
                break;
        }
    }
    if(active_key) {
        apple2_set_key_held(m, active_key);
    }
}

// Select which screen to display based on what mode is active
void unk_apl2_screen_apple2(UNK *v) {
    APPLE2 *m = v->m;
    uint32_t s = v->shadow_state;
    uint32_t mode =
        (((s & A2S_COL80)  != 0) << 4) |
        (((s & A2S_DHIRES) != 0) << 3) |
        (((s & A2S_HIRES)  != 0) << 2) |
        (((s & A2S_MIXED)  != 0) << 1) |
        (((s & A2S_TEXT)   != 0) << 0);
    // The bits are col80, dhgr, hgr, mixed, text, (followed by bin, hex)
    switch(mode) {
        case 0x00: // 0 ,0 ,0 ,0 ,0 ,00000,00
            unk_apl2_screen_lores(v, 0, 24);
            break;
        case 0x01: // 0 ,0 ,0 ,0 ,1 ,00001,01
            if(tst_flags(m->state_flags, A2S_FRANKLIN80ACTIVE)) {
                unk_apl2_screen_franklin80col(v, 0, 24);
            } else {
                unk_apl2_screen_txt40(v, 0, 24);
            }
            break;
        case 0x02: // 0 ,0 ,0 ,1 ,0 ,00010,02
            unk_apl2_screen_lores(v, 0, 20);
            unk_apl2_screen_txt40(v, 20, 24);
            break;
        case 0x03: // 0 ,0 ,0 ,1 ,1 ,00011,03
            unk_apl2_screen_txt40(v, 0, 20);
            unk_apl2_screen_lores(v, 20, 24);
            break;
        case 0x04: // 0 ,0 ,1 ,0 ,0 ,00100,04
            unk_apl2_screen_hgr(v, 0, 192);
            break;
        case 0x05: // 0 ,0 ,1 ,0 ,1 ,00101,05
            unk_apl2_screen_txt40(v, 0, 24);
            break;
        case 0x06: // 0 ,0 ,1 ,1 ,0 ,00110,06
            unk_apl2_screen_hgr(v, 0, 160);
            unk_apl2_screen_txt40(v, 20, 24);
            break;
        case 0x07: // 0 ,0 ,1 ,1 ,1 ,00111,07
            unk_apl2_screen_txt40(v, 0, 24);
            break;
        case 0x08: // 0 ,1 ,0 ,0 ,0 ,01000,08
            unk_apl2_screen_lores(v, 0, 24);
            break;
        case 0x09: // 0 ,1 ,0 ,0 ,1 ,01001,09
            unk_apl2_screen_txt40(v, 0, 24);
            break;
        case 0x0A: // 0 ,1 ,0 ,1 ,0 ,01010,0A
            unk_apl2_screen_lores(v, 0, 20);
            unk_apl2_screen_txt40(v, 20, 24);
            break;
        case 0x0B: // 0 ,1 ,0 ,1 ,1 ,01011,0B
            unk_apl2_screen_txt40(v, 0, 24);
            break;
        case 0x0C: // 0 ,1 ,1 ,0 ,0 ,01100,0C
            unk_apl2_screen_hgr(v, 0, 192);
            break;
        case 0x0D: // 0 ,1 ,1 ,0 ,1 ,01101,0D
            unk_apl2_screen_txt40(v, 0, 24);
            break;
        case 0x0E: // 0 ,1 ,1 ,1 ,0 ,01110,0E
            unk_apl2_screen_hgr(v, 0, 160);
            unk_apl2_screen_txt40(v, 20, 24);
            break;
        case 0x0F: // 0 ,1 ,1 ,1 ,1 ,01111,0F
            unk_apl2_screen_txt40(v, 0, 24);
            break;
        case 0x10: // 1 ,0 ,0 ,0 ,0 ,10000,10
            unk_apl2_screen_dlores(v, 0, 24);
            break;
        case 0x11: // 1 ,0 ,0 ,0 ,1 ,10001,11
            unk_apl2_screen_txt80(v, 0, 24);
            break;
        case 0x12: // 1 ,0 ,0 ,1 ,0 ,10010,12
            unk_apl2_screen_dlores(v, 0, 20);
            unk_apl2_screen_txt80(v, 20, 24);
            break;
        case 0x13: // 1 ,0 ,0 ,1 ,1 ,10011,13
            unk_apl2_screen_txt80(v, 0, 24);
            break;
        case 0x14: // 1 ,0 ,1 ,0 ,0 ,10100,14
            unk_apl2_screen_dhgr(v, 0, 192);
            break;
        case 0x15: // 1 ,0 ,1 ,0 ,1 ,10101,15
            unk_apl2_screen_txt80(v, 0, 24);
            break;
        case 0x16: // 1 ,0 ,1 ,1 ,0 ,10110,16
            unk_apl2_screen_hgr(v, 0, 160);
            unk_apl2_screen_txt80(v, 20, 24);
            break;
        case 0x17: // 1 ,0 ,1 ,1 ,1 ,10111,17
            unk_apl2_screen_txt80(v, 0, 24);
            break;
        case 0x18: // 1 ,1 ,0 ,0 ,0 ,11000,18
            unk_apl2_screen_dlores(v, 0, 24);
            break;
        case 0x19: // 1 ,1 ,0 ,0 ,1 ,11001,19
            unk_apl2_screen_txt80(v, 0, 24);
            break;
        case 0x1A: // 1 ,1 ,0 ,1 ,0 ,11010,1A
            unk_apl2_screen_dlores(v, 0, 20);
            unk_apl2_screen_txt80(v, 20, 24);
            break;
        case 0x1B: // 1 ,1 ,0 ,1 ,1 ,11011,1B
            unk_apl2_screen_dlores(v, 0, 20);
            unk_apl2_screen_txt80(v, 20, 24);
            break;
        case 0x1C: // 1 ,1 ,1 ,0 ,0 ,11100,1C
            unk_apl2_screen_dhgr(v, 0, 192);
            break;
        case 0x1D: // 1 ,1 ,1 ,0 ,1 ,11101,1D
            unk_apl2_screen_txt80(v, 0, 24);
            break;
        case 0x1E: // 1 ,1 ,1 ,1 ,0 ,11110,1E
            unk_apl2_screen_dhgr(v, 0, 160);
            unk_apl2_screen_txt80(v, 20, 24);
            break;
        case 0x1F: // 1 ,1 ,1 ,1 ,1 ,11111,1F
            unk_apl2_screen_txt80(v, 0, 24);
            break;
    }
}

void unk_apl2_screen_dlores(UNK *v, int start, int end) {
    APPLE2 *m = v->m;
    if(v->monitor_type & MONITOR_MONO) {
        // SQW
        unk_apl2_screen_dlores_mono(v, start, end);
        return;
    }

    SDL_Surface *surface = v->surface_wide;
    uint32_t *pixels = (uint32_t *) surface->pixels;
    int surface_width = surface->w;
    int y;

    for(y = start; y < end; y++) {
        uint32_t *p = &pixels[y * 8 * surface_width];
        const uint8_t *man = m->ram.RAM_MAIN + 0x00400 + apl2_txt_row_start[y];
        const uint8_t *aux = m->ram.RAM_MAIN + 0x10400 + apl2_txt_row_start[y];

        for(int col = 0; col < 80; col++) {
            int r;
            uint8_t index = col >> 1;
            uint8_t character, upper, lower;
            if(col & 0x1) {
                character = man[index];
                upper = character & 0x0F;
                lower = (character >> 4) & 0X0F;
            } else {
                character = aux[index];
                upper = double_aux_map[character & 0x0F];
                lower = double_aux_map[(character >> 4) & 0X0F];
            }

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

void unk_apl2_screen_dlores_mono(UNK *v, int start, int end) {
    APPLE2 *m = v->m;
    SDL_Surface *surface = v->surface_wide;
    uint32_t *pixels = (uint32_t *) surface->pixels;
    int surface_width = surface->w;
    int y;

    for(y = start; y < end; y++) {
        uint32_t *p = &pixels[y * 8 * surface_width];
        const uint8_t *man = m->ram.RAM_MAIN + 0x00400 + apl2_txt_row_start[y];
        const uint8_t *aux = m->ram.RAM_MAIN + 0x10400 + apl2_txt_row_start[y];

        for(int col = 0; col < 80; col++) {
            int r;
            uint8_t index = col >> 1;
            uint8_t character, upper, lower;
            if(col & 0x1) {
                character = man[index];
                upper = character & 0x0F;
                lower = (character >> 4) & 0X0F;
            } else {
                character = aux[index];
                upper = double_aux_map[character & 0x0F];
                lower = double_aux_map[(character >> 4) & 0X0F];
            }

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

// Display the lores screen in color
void unk_apl2_screen_lores(UNK *v, int start, int end) {
    APPLE2 *m = v->m;
    if(v->monitor_type & MONITOR_MONO) {
        unk_apl2_screen_lores_mono(v, start, end);
        return;
    }

    SDL_Surface *surface = v->surface;
    uint32_t *pixels = (uint32_t *) surface->pixels;
    uint16_t page = tst_flags(v->shadow_state, A2S_PAGE2) ? 0x0800 : 0x0400;
    int x, y;

    // Loop through each row
    for(y = start; y < end; y++) {
        // Get the pointer to the start of the row in the SDL surface
        uint32_t *p = &pixels[y * 8 * surface->w];
        int address = page + apl2_txt_row_start[y];

        // Loop through every col (byte)
        for(int x = 0; x < 40; x++) {
            int r;
            // Get the byte on screen
            uint8_t character = m->ram.RAM_MAIN[address + x];
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
void unk_apl2_screen_lores_mono(UNK *v, int start, int end) {
    APPLE2 *m = v->m;
    SDL_Surface *surface = v->surface;
    uint32_t *pixels = (uint32_t *) surface->pixels;
    uint16_t page = tst_flags(v->shadow_state, A2S_PAGE2) ? 0x0800 : 0x0400;
    int x, y;

    // Loop through each row
    for(y = start; y < end; y++) {
        // Get the pointer to the start of the row in the SDL surface
        uint32_t *p = &pixels[y * 8 * surface->w];
        int address = page + apl2_txt_row_start[y];

        // Loop through every col (byte)
        for(int x = 0; x < 40; x++) {
            int r;
            // Get the byte on screen
            uint8_t character = m->ram.RAM_MAIN[address + x];
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
#define FIR_N 17
#define FIR_R (FIR_N/2)

// Auto-generated by aw-tapper.py
// best phase_bias = 0, mse = 7.51990031e-03

static const float firR[8][17] = {
    {-0.016904f, -0.027155f, 0.010452f, 0.024283f, -0.027454f, -0.067371f, -0.004237f, 0.117520f, 0.233910f, 0.139867f, 0.057731f, 0.128694f, 0.005809f, -0.045540f, -0.007857f, -0.029063f, -0.009251f},
    {-0.023828f, 0.014303f, 0.018103f, -0.027534f, -0.048666f, -0.004226f, 0.040589f, 0.030412f, 0.168196f, 0.244264f, 0.097504f, 0.000583f, -0.021966f, -0.016426f, -0.004493f, -0.002304f, -0.000747f},
    {-0.007018f, -0.000031f, -0.009251f, -0.028732f, -0.000067f, 0.036549f, -0.023962f, -0.004369f, 0.213003f, 0.241609f, 0.034096f, -0.016234f, -0.004702f, 0.006691f, 0.001450f, -0.004601f, 0.003853f},
    {-0.008834f, -0.017873f, -0.026898f, 0.015132f, 0.045651f, -0.036160f, -0.051209f, 0.119742f, 0.210822f, 0.125556f, 0.012997f, -0.030614f, 0.089933f, -0.004963f, -0.005775f, 0.010696f, -0.008198f},
    {-0.010350f, -0.013309f, 0.013164f, 0.039394f, -0.014079f, -0.073123f, 0.015591f, 0.054826f, 0.236090f, 0.049621f, 0.055868f, 0.161017f, -0.022885f, 0.037385f, -0.030139f, -0.022468f, -0.004096f},
    {-0.010238f, 0.011783f, 0.027652f, -0.026975f, -0.049212f, 0.013430f, 0.028665f, 0.033121f, 0.103426f, 0.235847f, 0.101844f, 0.001450f, 0.020393f, -0.023308f, -0.011663f, -0.006950f, 0.009034f},
    {-0.003150f, 0.013604f, -0.009991f, -0.032105f, -0.005062f, 0.006401f, -0.019288f, -0.038102f, 0.229202f, 0.275538f, 0.027088f, 0.017462f, -0.008142f, -0.011332f, 0.001486f, -0.005118f, -0.007845f},
    {0.013090f, -0.009605f, -0.028752f, 0.010346f, 0.008455f, -0.044011f, -0.088105f, 0.128621f, 0.319829f, 0.118677f, 0.052542f, -0.017540f, 0.008913f, -0.000416f, -0.007309f, -0.009458f, -0.013471f},
};

static const float firG[8][17] = {
    {-0.002957f, 0.012931f, -0.003167f, -0.017862f, -0.018069f, 0.017232f, 0.002793f, 0.008266f, 0.200303f, 0.241653f, 0.048549f, -0.026947f, -0.000097f, -0.000625f, -0.001476f, 0.005715f, 0.006152f},
    {0.009142f, -0.001173f, -0.009055f, -0.000520f, 0.019410f, -0.001677f, -0.029367f, 0.039276f, 0.260702f, 0.176359f, 0.007255f, -0.001254f, 0.009478f, 0.001333f, 0.002229f, 0.005855f, -0.001599f},
    {0.002042f, -0.013080f, 0.002254f, 0.035833f, 0.009860f, -0.039267f, -0.025494f, 0.139800f, 0.241337f, 0.097871f, 0.017792f, 0.025965f, -0.003691f, 0.005293f, 0.005153f, -0.008714f, 0.006260f},
    {0.001137f, -0.002928f, 0.024380f, 0.010795f, -0.041606f, -0.023666f, 0.028401f, 0.095231f, 0.114815f, 0.165643f, 0.063179f, -0.006346f, 0.053415f, -0.007036f, -0.016720f, 0.003637f, 0.006010f},
    {-0.004257f, 0.017297f, -0.002061f, -0.032380f, -0.013955f, 0.007176f, 0.003643f, 0.010197f, 0.208898f, 0.269136f, 0.036350f, -0.013918f, -0.004014f, -0.021352f, 0.008789f, 0.002928f, -0.003493f},
    {0.006667f, -0.001345f, -0.010999f, -0.006135f, 0.023150f, -0.006933f, -0.013857f, 0.045868f, 0.283881f, 0.174498f, -0.004796f, -0.001536f, -0.009753f, 0.008537f, 0.002299f, 0.005323f, -0.006159f},
    {0.002566f, -0.014265f, 0.000333f, 0.038087f, -0.002850f, -0.027702f, -0.029687f, 0.178969f, 0.235256f, 0.100441f, 0.027635f, -0.018912f, 0.019250f, -0.004139f, 0.000887f, -0.001368f, -0.001710f},
    {-0.013655f, -0.006745f, 0.020259f, 0.002588f, -0.011904f, -0.040753f, 0.071812f, 0.089791f, 0.210658f, 0.175758f, 0.007811f, 0.010983f, -0.058817f, -0.000126f, -0.002761f, -0.001948f, 0.005567f},
};

static const float firB[8][17] = {
    {-0.002208f, -0.008520f, -0.022228f, 0.006407f, 0.041946f, -0.046218f, -0.052336f, 0.058202f, 0.361470f, 0.095953f, -0.006712f, 0.039185f, -0.021161f, -0.004662f, -0.004584f, 0.001151f, -0.005416f},
    {-0.000585f, -0.018974f, 0.012940f, 0.086303f, -0.043786f, -0.089630f, 0.002140f, 0.291120f, 0.133813f, 0.025518f, 0.033910f, -0.027350f, -0.002268f, 0.001724f, 0.009283f, -0.006960f, 0.025540f},
    {-0.001122f, 0.015122f, 0.045202f, -0.045937f, -0.091372f, -0.006878f, 0.117705f, -0.020485f, 0.107366f, 0.185751f, 0.058364f, 0.000094f, -0.040013f, 0.061137f, -0.005406f, 0.019724f, 0.005970f},
    {0.010223f, 0.031703f, -0.010151f, -0.047065f, -0.022731f, 0.092886f, -0.045500f, -0.143334f, 0.152539f, 0.211105f, 0.036049f, 0.070327f, 0.018944f, -0.001498f, -0.001812f, -0.012403f, -0.003048f},
    {0.010163f, -0.011968f, -0.030984f, 0.005989f, 0.033797f, -0.043755f, -0.018816f, 0.027576f, 0.335362f, 0.050692f, 0.007486f, 0.075768f, 0.015646f, 0.040038f, -0.049707f, -0.008185f, -0.014722f},
    {-0.034220f, -0.015093f, 0.014109f, 0.045034f, -0.007617f, -0.070161f, 0.019312f, 0.266642f, 0.145319f, 0.025931f, 0.027195f, 0.069122f, -0.019318f, -0.025838f, -0.004884f, -0.037766f, 0.030913f},
    {-0.007942f, 0.010336f, 0.017687f, -0.021823f, -0.063582f, 0.031938f, 0.117895f, -0.039815f, 0.102444f, 0.237035f, 0.096938f, 0.016267f, -0.051791f, -0.020050f, -0.013510f, 0.006042f, 0.009366f},
    {0.000453f, -0.010793f, -0.003847f, -0.039646f, -0.012345f, 0.096180f, -0.010242f, -0.092368f, 0.294606f, 0.252674f, -0.008559f, 0.010806f, -0.136029f, -0.015865f, 0.005750f, -0.001120f, 0.006409f},
};

static const float biasR[8] = {0.466044f, 0.494020f, 0.494295f, 0.484833f, 0.475716f, 0.500457f, 0.494969f, 0.480405f};
static const float biasG[8] = {0.502057f, 0.502767f, 0.501710f, 0.497701f, 0.495992f, 0.498192f, 0.498323f, 0.498383f};
static const float biasB[8] = {0.482707f, 0.502964f, 0.508645f, 0.504306f, 0.487179f, 0.506681f, 0.506438f, 0.507104f};

// Per-phase black level measured from .byte $00,$00,$00,$00 band
static const float blackR[8] = {-0.017390f, 0.030255f, 0.056012f, 0.044826f, 0.003207f, 0.042159f, 0.064323f, 0.038599f};
static const float blackG[8] = {0.029663f, 0.016374f, 0.002496f, 0.029358f, 0.027011f, 0.009484f, -0.004469f, 0.039866f};
static const float blackB[8] = {0.052436f, 0.070227f, 0.103425f, 0.168072f, 0.062799f, 0.078000f, 0.079002f, 0.171039f};

// Per-phase white level measured from .byte $7f,$7f,$7f,$7f band
static const float whiteR[8] = {0.949478f, 0.957784f, 0.932578f, 0.924839f, 0.948224f, 0.958756f, 0.925614f, 0.922211f};
static const float whiteG[8] = {0.974450f, 0.989160f, 1.000924f, 0.966043f, 0.964974f, 0.986901f, 1.001115f, 0.956900f};
static const float whiteB[8] = {0.912978f, 0.935702f, 0.913865f, 0.840539f, 0.911559f, 0.935361f, 0.933874f, 0.843169f};

// Per-phase gain so (rgb - black) * gain maps white->1.0
static const float gainR[8] = {1.034268f, 1.078133f, 1.140815f, 1.136347f, 1.058182f, 1.090992f, 1.161048f, 1.131719f};
static const float gainG[8] = {1.058439f, 1.027975f, 1.001574f, 1.067595f, 1.066141f, 1.023106f, 0.994448f, 1.090473f};
static const float gainB[8] = {1.162058f, 1.155435f, 1.233897f, 1.487062f, 1.178189f, 1.166370f, 1.169766f, 1.487808f};

static const int phase_bias = 0;

static const float chroma_knee = 0.85f;   // start suppressing chroma
static const float chroma_max  = 1.00f;   // fully suppress by here

static inline float fir_eval(const float *k, const float *x, int n, int idx) {
    // clamp edges
    float acc = 0.0f;
    for(int t = 0; t < FIR_N; t++) {
        int xi = idx + (t - FIR_R);
        if(xi < 0) {
            xi = 0;
        } else if(xi >= n) {
            xi = n - 1;
        }
        acc += k[t] * x[xi];
    }
    return acc;
}

void unk_apl2_screen_dhgr(UNK *v, int start, int end) {
    APPLE2 *m = v->m;
    SDL_Surface *surface = v->surface_wide; // 560 wide
    uint32_t *pixels = (uint32_t *)surface->pixels;
    SDL_PixelFormat *format = surface->format;

    uint16_t page = tst_flags(v->shadow_state, A2S_PAGE2) ? 0x4000 : 0x2000;

    for(int y = start; y < end; y++) {
        uint32_t *p = &pixels[y * surface->w];

        const uint8_t *aux = m->ram.RAM_MAIN + page + hgr_row_start[y] + 0x10000;
        const uint8_t *man = m->ram.RAM_MAIN + page + hgr_row_start[y];
        float row_560[560];
        int index = 0;
        for(int col = 0; col < 40; col += 2) {

            uint32_t AUX = load_u16_unaligned(aux + col) & 0x7F7F;
            uint16_t MAN = load_u16_unaligned(man + col) & 0x7F7F;

            uint8_t b0 = (uint8_t)(AUX);
            uint8_t b1 = (uint8_t)(MAN);
            uint8_t b2 = (uint8_t)(AUX >> 8);
            uint8_t b3 = (uint8_t)(MAN >> 8);

            uint32_t stream =
                (((uint32_t)b3 & 0x7F) << 21) |
                (((uint32_t)b2 & 0x7F) << 14) |
                (((uint32_t)b1 & 0x7F) <<  7) |
                (((uint32_t)b0 & 0x7F));

            if(v->monitor_type & MONITOR_MONO) {
                for(int bit = 0; bit < 28; bit++) {
                    if(stream & 1) {
                        *p++ = SDL_MapRGB(format, 0xFF, 0xFF, 0xFF);
                    } else {
                        *p++ = SDL_MapRGB(format, 0x00, 0x00, 0x00);
                    }
                    stream >>= 1;
                }
            } else {
                for(int bit = 0; bit < 28; bit++) {
                    row_560[index++] = stream & 1 ? 1.0f : -1.0f;
                    stream >>= 1;
                }
            }
        }

        if(!(v->monitor_type & MONITOR_MONO)) {
            for(int x = 0; x < 560; x++) {
                int ph = (x + phase_bias) & 7;

                float r = biasR[ph] + fir_eval(firR[ph], row_560, 560, x);
                float g = biasG[ph] + fir_eval(firG[ph], row_560, 560, x);
                float b = biasB[ph] + fir_eval(firB[ph], row_560, 560, x);

                // Normalize using per-phase black + gain (learned from $00 and $7f reference bands)
                r = (r - blackR[ph]) * gainR[ph];
                g = (g - blackG[ph]) * gainG[ph];
                b = (b - blackB[ph]) * gainB[ph];


                // Luma-based chroma suppression (fixes white yellow banding)
                // Approximate luma
                float y = 0.299f * r + 0.587f * g + 0.114f * b;

                // Compute suppression factor in [0..1]
                float s = 1.0f;
                if(y > chroma_knee) {
                    s = 1.0f - (y - chroma_knee) / (chroma_max - chroma_knee);
                    if(s < 0.0f) {
                        s = 0.0f;
                    }
                }

                // Separate luma + chroma
                float cr = r - y;
                float cg = g - y;
                float cb = b - y;

                // Suppress chroma near white
                r = y + cr * s;
                g = y + cg * s;
                b = y + cb * s;

                // Clamp
                float R = r < 0.0 ? 0.0 : r > 1.0 ? 1.0 : r;
                float G = g < 0.0 ? 0.0 : g > 1.0 ? 1.0 : g;
                float B = b < 0.0 ? 0.0 : b > 1.0 ? 1.0 : b;

                *p++ = SDL_MapRGB(format, (R * 255.0f), (G * 255.0f), (B * 255.0f));
            }
        }
    }
}

void unk_apl2_screen_hgr(UNK *v, int start, int end) {
    APPLE2 *m = v->m;
    if(v->monitor_type & MONITOR_MONO) {
        unk_apl2_screen_hgr_mono(v, start, end);
        return;
    }

    SDL_Surface *surface = v->surface;
    uint32_t *pixels = (uint32_t *) surface->pixels;
    uint16_t page = tst_flags(v->shadow_state, A2S_PAGE2) ? 0x4000 : 0x2000;
    int surface_width = surface->w;
    int y;

    for(y = start; y < end; y++) {
        uint32_t *p = &pixels[y * surface_width];
        int px = 0;
        uint16_t address = page + hgr_row_start[y];
        uint8_t byte = m->ram.RAM_MAIN[address];

        int bit_column = 0;                 // start of scanline
        int prev_bit = 0;                   // first column "left neighbor" is 0

        for(int col = 0; col < 40; ++col) {
            uint8_t next_byte = (col + 1 < 40) ? m->ram.RAM_MAIN[address + col + 1] : 0;
            int next_lsb = next_byte & 1;
            int phase   = (byte >> 7) & 1;

            const HGRLUTENTRY *e = &hgr_lut[byte & 0x7F][next_lsb][prev_bit][phase][bit_column];

            // emit_byte the 7 pixels already expanded in the LUT
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
void unk_apl2_screen_hgr_mono(UNK *v, int start, int end) {
    APPLE2 *m = v->m;
    SDL_Surface *surface = v->surface;
    uint32_t *pixels = (uint32_t *) surface->pixels;
    uint16_t page = tst_flags(v->shadow_state, A2S_PAGE2) ? 0x4000 : 0x2000;
    uint32_t c[2] = { color_table[0][0][0], color_table[7][0][0] };
    int x, y;

    // Loop through each row
    for(y = start; y < end; y++) {
        // Get the pointer to the start of the row in the SDL surface
        uint32_t *p = &pixels[y * surface->w];
        int address = page + hgr_row_start[y];

        // Loop through every column (40 iterations for each 280-pixel row)
        uint8_t *bytes = &m->ram.RAM_MAIN[address];
        for(int x = 0; x < 40; x++) {
            const HGRLUTENTRY *e = &hgr_mono_lut[*bytes++ & 0x7F];
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
void unk_apl2_screen_txt40(UNK *v, int start, int end) {
    APPLE2 *m = v->m;
    SDL_Surface *surface = v->surface;
    uint32_t *pixels = (uint32_t *) surface->pixels;
    uint16_t page = tst_flags(v->shadow_state, A2S_PAGE2) ? 0x0800 : 0x0400;
    uint64_t now = perf_counter();
    double freq = (double)perf_frequency();
    // I got 3.7 from recording a flash on my Platinum //e - 0.17 to 0.44 for a change so 0.27
    uint8_t time_inv = (((uint64_t)(now * 3.7 / freq)) & 1) ? 0xFF : 0x00;
    int alt_charset = tst_flags(m->state_flags, A2S_ALTCHARSET);
    int x, y;

    // Loop through each row
    for(y = start; y < end; y++) {
        // Get the pointer to the start of the row in the SDL surface
        uint32_t *p = &pixels[y * 8 * surface->w];
        int address = page + apl2_txt_row_start[y];

        // Loop through every col (byte)
        for(int x = 0; x < 40; x++) {
            int r;
            uint8_t inv = 0x00;
            uint8_t character = m->ram.RAM_MAIN[address + x];   // Get the character on screen
            if(character < 0x80) {
                if(character >= 0x40) {
                    if(!alt_charset) {
                        character &= 0x3F;
                        inv = time_inv;
                    }
                } else if(m->model == MODEL_APPLE_II_PLUS) {
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

void unk_apl2_screen_txt80(UNK *v, int start, int end) {
    APPLE2 *m = v->m;
    SDL_Surface *surface = v->surface_wide;
    uint32_t *pixels = (uint32_t *) surface->pixels;
    uint64_t now = perf_counter();
    double freq = (double)perf_frequency();
    // I got 3.7 from recording a flash on my Platinum //e - 0.17 to 0.44 for a change so 0.27
    uint8_t time_inv = (((uint64_t)(now * 3.7 / freq)) & 1) ? 0xFF : 0x00;
    int alt_charset = tst_flags(m->state_flags, A2S_ALTCHARSET);
    int x, y;

    // Loop through each row
    for(y = start; y < end; y++) {
        // Get the pointer to the start of the row in the SDL surface
        uint32_t *p = &pixels[y * 8 * surface->w];
        int address = 0x0400 + apl2_txt_row_start[y];

        // Loop through every col (byte)
        for(int x = 0; x < 80; x++) {
            int r;
            uint8_t inv = 0x00;
            int char_in_bank = (x & 1) ? (x >> 1) : (x >> 1) + 0x10000;
            uint8_t character = m->ram.RAM_MAIN[address + char_in_bank];   // Get the character on screen
            if(character < 0x80) {
                if(character >= 0x40) {
                    if(!alt_charset) {
                        character &= 0x3F;
                        inv = time_inv;
                    }
                } else if(m->model == MODEL_APPLE_II_PLUS) {
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
void unk_apl2_screen_franklin80col(UNK *v, int start, int end) {
    APPLE2 *m = v->m;
    const FRANKLIN_DISPLAY *fd80 = &m->franklin_display;
    SDL_Surface *surface = v->surface_wide;
    uint32_t *pixels = (uint32_t *) surface->pixels;
    uint32_t c[2] = { color_table[0][0][0], color_table[7][0][0] };
    uint16_t display_offset = 256 * fd80->registers[0x0c] + fd80->registers[0x0d];
    int x, y;

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
