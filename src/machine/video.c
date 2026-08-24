#include "video.h"

#include "apple2.h"

#include <stdlib.h>
#include <string.h>

/* HGR row offsets within a 8K page (same table as a2m). */
static const uint16_t hgr_row_start[192] = {
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

uint16_t apple2_video_text_line_base(uint8_t text_row)
{
    /* Standard Apple II text line base within $400 page. */
    return (uint16_t)(((text_row & 7u) << 7) + ((text_row >> 3) * 40u));
}

uint16_t apple2_video_hgr_line_offset(uint8_t pixel_row)
{
    if (pixel_row >= 192u) {
        return 0;
    }
    return hgr_row_start[pixel_row];
}

/* a2m palette_16 — LORES (and text on/off). ARGB8888. */
static const uint32_t LORES_PALETTE[16] = {
    0xFF000000u, /* 0  Black */
    0xFF9D0966u, /* 1  Magenta / deep red */
    0xFF2A2AE5u, /* 2  Dark blue */
    0xFFC734FFu, /* 3  Purple */
    0xFF008000u, /* 4  Dark green */
    0xFF808080u, /* 5  Gray1 */
    0xFF0DA1FFu, /* 6  Medium blue */
    0xFFAAAAFFu, /* 7  Light blue */
    0xFF555500u, /* 8  Brown */
    0xFFF25E00u, /* 9  Orange */
    0xFFC0C0C0u, /* 10 Gray2 */
    0xFFFF89E5u, /* 11 Pink */
    0xFF38CB00u, /* 12 Green */
    0xFFD5D51Au, /* 13 Yellow */
    0xFF62F699u, /* 14 Aqua */
    0xFFFFFFFFu  /* 15 White */
};

/* P4 white, P1-ish green, P3-ish amber. On-pixel colour for discrete bits. */
static const uint32_t PHOSPHOR_ARGB[3] = {
    0xFFFFFFFFu,
    0xFF33FF66u,
    0xFFFFB000u
};

/* LORES/DLORES: phosphor x Rec.601 luma of LORES_PALETTE, one table per
   phosphor. Filled in paint_init_luts. */
static uint32_t mono_lores[3][16];

/*
 * a2m HGR Holger-Picker 3-bit window palette (phase selects green/violet vs
 * orange/blue set). Indices 0..7 phase0, 8..15 phase1.
 */
static const uint32_t HGR_PALETTE[16] = {
    0xFF000000u, 0xFF000000u, 0xFFC734FFu, 0xFFFFFFFFu, /* black, black, violet, white */
    0xFF000000u, 0xFF38CB00u, 0xFFFFFFFFu, 0xFFFFFFFFu, /* black, green, white, white */
    0xFF000000u, 0xFF000000u, 0xFF0DA1FFu, 0xFFFFFFFFu, /* black, black, blue, white */
    0xFF000000u, 0xFFF25E00u, 0xFFFFFFFFu, 0xFFFFFFFFu  /* black, orange, white, white */
};

/* Precomputed 7-pixel ARGB run for one HGR byte + neighbour context (a2m). */
typedef struct {
    uint32_t pixel[7];
} hgr_lut_entry;

/* [byte7][next_lsb][prev_bit][phase][start_bit] */
static hgr_lut_entry hgr_lut[128][2][2][2][2];
/* a2m DHGR 5-bit window → LORES palette index by NTSC phase. */
static uint32_t dhgr_lut[32][4];
static int paint_luts_ready;

/* a2m: map 4-bit pattern to LORES colour index for DHGR. */
static const int dhgr_pattern_to_color[16] = {
    0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15
};

static int rotate_right4(int nibble, int phase)
{
    return ((nibble >> phase) | (nibble << (4 - phase))) & 0xF;
}

static void paint_init_luts(void)
{
    uint32_t color_table[8][2][2];
    int bit_stream;
    int column;
    int phase;
    int byte;
    int next_lsb;
    int prev_bit;
    int start_bit;
    int b;
    int pattern;

    if (paint_luts_ready) {
        return;
    }

    for (bit_stream = 0; bit_stream < 8; bit_stream++) {
        for (column = 0; column < 2; column++) {
            for (phase = 0; phase < 2; phase++) {
                int color = bit_stream;
                if (column && (color == 2 || color == 5)) {
                    color ^= 7;
                }
                color += (phase << 3);
                color_table[bit_stream][column][phase] = HGR_PALETTE[color & 15];
            }
        }
    }

    for (byte = 0; byte < 128; byte++) {
        for (next_lsb = 0; next_lsb < 2; next_lsb++) {
            for (prev_bit = 0; prev_bit < 2; prev_bit++) {
                for (phase = 0; phase < 2; phase++) {
                    for (start_bit = 0; start_bit < 2; start_bit++) {
                        int stream = (next_lsb << 8) | (byte << 1) | prev_bit;
                        int parity = start_bit;
                        for (b = 0; b < 7; b++) {
                            int win = stream & 0x7;
                            hgr_lut[byte][next_lsb][prev_bit][phase][start_bit]
                                .pixel[b] = color_table[win][parity][phase];
                            stream >>= 1;
                            parity ^= 1;
                        }
                    }
                }
            }
        }
    }

    for (pattern = 0; pattern < 32; pattern++) {
        for (phase = 0; phase < 4; phase++) {
            int color_idx;
            int force_white =
                ((pattern & 0x1E) == 0x1E) || ((pattern & 0x0F) == 0x0F);
            int force_black =
                ((pattern & 0x1E) == 0x00) || ((pattern & 0x0F) == 0x00);
            if (force_white) {
                color_idx = 15;
            } else if (force_black) {
                color_idx = 0;
            } else {
                int nibble = (pattern >> 1) & 0xF;
                int rotated = rotate_right4(nibble, phase);
                color_idx = dhgr_pattern_to_color[rotated];
            }
            dhgr_lut[pattern][phase] = LORES_PALETTE[color_idx & 15];
        }
    }

    for (pattern = 0; pattern < 16; pattern++) {
        uint32_t src = LORES_PALETTE[pattern];
        unsigned int r = (src >> 16) & 0xffu;
        unsigned int g = (src >> 8) & 0xffu;
        unsigned int b = src & 0xffu;
        unsigned int y = (299u * r + 587u * g + 114u * b) / 1000u;
        int ph;
        for (ph = 0; ph < 3; ph++) {
            unsigned int pr = (PHOSPHOR_ARGB[ph] >> 16) & 0xffu;
            unsigned int pg = (PHOSPHOR_ARGB[ph] >> 8) & 0xffu;
            unsigned int pb = PHOSPHOR_ARGB[ph] & 0xffu;
            mono_lores[ph][pattern] =
                0xFF000000u |
                (((pr * y) / 255u) << 16) |
                (((pg * y) / 255u) << 8) |
                ((pb * y) / 255u);
        }
    }

    paint_luts_ready = 1;
}

static int video_phosphor_index(const apple2_video *v)
{
    int p = (v != NULL) ? (int)v->phosphor : 0;
    if (p < 0 || p > 2) {
        return 0;
    }
    return p;
}

static uint32_t paint_on_colour(const apple2_video *v)
{
    if (v == NULL || !v->mono) {
        return LORES_PALETTE[15];
    }
    return PHOSPHOR_ARGB[video_phosphor_index(v)];
}

static uint32_t paint_cell_colour(const apple2_video *v, unsigned int nibble)
{
    nibble &= 15u;
    if (v == NULL || !v->mono) {
        return LORES_PALETTE[nibble];
    }
    return mono_lores[video_phosphor_index(v)][nibble];
}

static uint8_t video_read_host(const apple2_t *m, uint32_t host_offset)
{
    if (host_offset >= APPLE2_RAM_MAIN_SIZE) {
        return 0;
    }
    return m->ram_main[host_offset];
}

static uint32_t video_display_flags(const apple2_t *m)
{
    const uint32_t mask = A2S_COL80 | A2S_ALTCHARSET | A2S_TEXT |
        A2S_MIXED | A2S_PAGE2 | A2S_HIRES | A2S_DHIRES;

    if (m->video.display_override_enabled) {
        return (m->state_flags & ~mask) |
            (m->video.display_override_flags & mask);
    }
    return m->state_flags;
}

/*
 * Display page selection:
 * - 80STORE off: PAGE2 selects $800/$4000 vs $400/$2000 in main.
 * - 80STORE on: PAGE2 selects aux bank for text/HGR display pages (//e).
 */
static uint32_t text_host_addr(
    const apple2_t *m,
    uint32_t flags,
    uint16_t offset_in_page)
{
    uint16_t base = (flags & A2S_PAGE2) ? 0x0800u : 0x0400u;
    uint32_t addr = (uint32_t)base + offset_in_page;
    if ((flags & A2S_80STORE) && (flags & A2S_PAGE2)) {
        addr += 0x10000u;
        /* With 80STORE+PAGE2, base stays $400 in aux for text page 1 hardware;
           simplified: use $400 aux when 80STORE. */
        addr = 0x10000u + 0x0400u + offset_in_page;
    }
    return addr;
}

static uint32_t hgr_host_addr(
    const apple2_t *m,
    uint32_t flags,
    uint16_t offset_in_page)
{
    uint16_t base = (flags & A2S_PAGE2) ? 0x4000u : 0x2000u;
    uint32_t addr = (uint32_t)base + offset_in_page;
    if ((flags & A2S_80STORE) && (flags & A2S_PAGE2)) {
        addr = 0x10000u + 0x2000u + offset_in_page;
    } else if (!(flags & A2S_80STORE) && (flags & A2S_PAGE2)) {
        addr = 0x4000u + offset_in_page;
    } else {
        addr = 0x2000u + offset_in_page;
    }
    return addr;
}

static bool line_is_text(const apple2_t *m, uint16_t line)
{
    uint32_t flags = video_display_flags(m);
    if (line >= APPLE2_VIDEO_VISIBLE_LINES) {
        return false;
    }
    if (flags & A2S_TEXT) {
        return true;
    }
    if ((flags & A2S_MIXED) && line >= 160u) {
        return true; /* bottom 4 text rows */
    }
    return false;
}

/* Single HGR (not double-res): HIRES without COL80. */
static bool line_is_hgr(const apple2_t *m, uint16_t line)
{
    uint32_t flags = video_display_flags(m);
    if (line >= APPLE2_VIDEO_VISIBLE_LINES) {
        return false;
    }
    if (flags & A2S_TEXT) {
        return false;
    }
    if (!(flags & A2S_HIRES)) {
        return false;
    }
    if (flags & A2S_COL80) {
        /* a2m mode matrix: mixed 80-column HGR stays single-res until
           DHIRES is enabled. Non-mixed 80-column HGR is DHGR either way. */
        return (flags & A2S_MIXED) && !(flags & A2S_DHIRES) && line < 160u;
    }
    if ((flags & A2S_MIXED) && line >= 160u) {
        return false;
    }
    return true;
}

/* DHGR: COL80 + HIRES (a2m mode table). */
static bool line_is_dhgr(const apple2_t *m, uint16_t line)
{
    uint32_t flags = video_display_flags(m);
    if (line >= APPLE2_VIDEO_VISIBLE_LINES) {
        return false;
    }
    if (flags & A2S_TEXT) {
        return false;
    }
    if (!(flags & A2S_HIRES) || !(flags & A2S_COL80)) {
        return false;
    }
    if ((flags & A2S_MIXED) && !(flags & A2S_DHIRES)) {
        return false;
    }
    if ((flags & A2S_MIXED) && line >= 160u) {
        return false;
    }
    return true;
}

/* Double LORES: COL80 + GR (not TEXT, not HIRES). a2m mode matrix. */
static bool line_is_dlores(const apple2_t *m, uint16_t line)
{
    uint32_t flags = video_display_flags(m);
    if (line >= APPLE2_VIDEO_VISIBLE_LINES) {
        return false;
    }
    if (flags & A2S_TEXT) {
        return false;
    }
    if (flags & A2S_HIRES) {
        return false;
    }
    if (!(flags & A2S_COL80)) {
        return false;
    }
    if ((flags & A2S_MIXED) && line >= 160u) {
        return false;
    }
    return true;
}

/* GR (not TEXT, not HIRES, not 80-col); MIXED uses text on lines 160..191. */
static bool line_is_lores(const apple2_t *m, uint16_t line)
{
    uint32_t flags = video_display_flags(m);
    if (line >= APPLE2_VIDEO_VISIBLE_LINES) {
        return false;
    }
    if (flags & A2S_TEXT) {
        return false;
    }
    if (flags & A2S_HIRES) {
        return false;
    }
    if (flags & A2S_COL80) {
        return false; /* dlores */
    }
    if ((flags & A2S_MIXED) && line >= 160u) {
        return false;
    }
    return true;
}

static bool display_is_80col(const apple2_t *m)
{
    return (video_display_flags(m) & A2S_COL80) != 0;
}

/* a2m: de-interleave aux dlores nibbles onto the standard LORES palette. */
static const uint8_t double_aux_map[16] = {
    0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E,
    0x01, 0x03, 0x05, 0x07, 0x09, 0x0B, 0x0D, 0x0F
};

/*
 * DLORES display pages: main + aux text pages interleaved.
 * 80STORE off + PAGE2 → $800 / $10800; otherwise $400 / $10400.
 * (a2m hard-coded page 1; PAGE2 matches the rest of the v2 painter.)
 */
static void dlores_page_bases(
    const apple2_t *m,
    uint32_t *main_base,
    uint32_t *aux_base)
{
    uint32_t flags = video_display_flags(m);
    uint32_t page = 0x0400u;
    if ((flags & A2S_PAGE2) && !(flags & A2S_80STORE)) {
        page = 0x0800u;
    }
    *main_base = page;
    *aux_base = 0x10000u + page;
}

static uint8_t scanner_fetch(apple2_t *m)
{
    apple2_video *v = &m->video;
    uint16_t line = v->line;
    uint16_t h = v->cycle_in_line;

    if (line >= APPLE2_VIDEO_VISIBLE_LINES || h >= APPLE2_VIDEO_H_VISIBLE_CYCLES) {
        return v->last_video_byte;
    }

    if (m->state_flags & A2S_TEXT ||
        ((m->state_flags & A2S_MIXED) && line >= 160u)) {
        uint8_t trow = (uint8_t)(line / 8u);
        uint32_t addr = text_host_addr(
            m, m->state_flags,
            (uint16_t)(apple2_video_text_line_base(trow) + h));
        v->last_video_byte = video_read_host(m, addr);
        return v->last_video_byte;
    }

    if (!(m->state_flags & A2S_TEXT) &&
        (m->state_flags & A2S_HIRES) &&
        !(m->state_flags & A2S_COL80) &&
        !((m->state_flags & A2S_MIXED) && line >= 160u)) {
        uint32_t addr = hgr_host_addr(
            m, m->state_flags,
            (uint16_t)(apple2_video_hgr_line_offset((uint8_t)line) + h));
        v->last_video_byte = video_read_host(m, addr);
        return v->last_video_byte;
    }

    /* Lores path: text-page layout, byte is color cell. */
    {
        uint8_t trow = (uint8_t)(line / 8u);
        uint32_t addr = text_host_addr(
            m, m->state_flags,
            (uint16_t)(apple2_video_text_line_base(trow) + h));
        v->last_video_byte = video_read_host(m, addr);
        return v->last_video_byte;
    }
}

/* Write one logical dot as two horizontal ARGB pixels (560-wide contract). */
static void paint_dot_x2(apple2_video *v, uint16_t line, uint16_t x, uint32_t color)
{
    size_t base = (size_t)line * (size_t)APPLE2_VIDEO_WIDTH + (size_t)x;
    v->fb[base] = color;
    v->fb[base + 1u] = color;
}

/*
 * Text glyph rules (a2m txt40/txt80):
 * - $80..$FF: normal
 * - $40..$7F without ALTCHARSET: flash (mask to $3F, invert on flash phase)
 * - $00..$3F on ][+: inverse
 * - //e char ROM supplies inverse glyphs for low codes; no extra XOR
 * Flash rate ≈ half period every ~15 frames (~4 Hz).
 * pixel_double: 40-col (7 logical → 14 host); false for 80-col (7 host px).
 */
static void paint_text_glyph(apple2_t *m, uint16_t line, uint16_t x0, uint8_t ch,
                             int pixel_double)
{
    apple2_video *v = &m->video;
    uint8_t character = ch;
    uint8_t inv = 0x00;
    uint8_t row_in_char = (uint8_t)(line & 7u);
    uint8_t bits;
    int b;
    const uint8_t *crom = m->rom_char;
    size_t csz = m->rom_char_size;
    int alt_charset = (video_display_flags(m) & A2S_ALTCHARSET) != 0;
    uint8_t flash_phase =
        ((m->video.frame_number / 15u) & 1u) ? 0xFFu : 0x00u;
    uint16_t width = pixel_double ? 14u : 7u;

    if (crom == NULL || csz < 64u * 8u || v->fb == NULL) {
        return;
    }
    if (x0 + width > APPLE2_VIDEO_WIDTH || line >= APPLE2_VIDEO_HEIGHT) {
        return;
    }

    if (character < 0x80u) {
        if (character >= 0x40u) {
            if (!alt_charset) {
                character = (uint8_t)(character & 0x3Fu);
                inv = flash_phase;
            }
        } else if (m->model == APPLE2_MODEL_II_PLUS) {
            inv = 0xFFu;
        }
    }

    if (csz >= 256u * 8u) {
        bits = crom[((size_t)character * 8u) + row_in_char];
    } else {
        bits = crom[((size_t)(character & 0x3Fu) * 8u) + row_in_char];
    }
    bits ^= inv;

    /* a2m: leftmost host pixel is bit6 of the glyph row. */
    for (b = 0; b < 7; b++) {
        int on = (bits >> (6 - b)) & 1;
        uint32_t color = on ? paint_on_colour(v) : LORES_PALETTE[0];
        if (pixel_double) {
            paint_dot_x2(v, line, (uint16_t)(x0 + (uint16_t)(b * 2)), color);
        } else {
            v->fb[(size_t)line * (size_t)APPLE2_VIDEO_WIDTH + (size_t)x0 +
                  (size_t)b] = color;
        }
    }
}

/* 40-col: one scanner column → one glyph pixel-doubled. */
static void paint_text40_column(apple2_t *m, uint16_t line, uint16_t col, uint8_t ch)
{
    uint16_t x0 = (uint16_t)(col * (uint16_t)APPLE2_VIDEO_PIXELS_PER_COLUMN);
    paint_text_glyph(m, line, x0, ch, 1);
}

/*
 * 80-col: one scanner column = aux glyph then main glyph (7+7 host pixels).
 * Display page is always $400 main / $10400 aux (a2m txt80).
 */
static void paint_text80_column(apple2_t *m, uint16_t line, uint16_t col)
{
    uint8_t trow = (uint8_t)(line / 8u);
    uint16_t base = apple2_video_text_line_base(trow);
    uint8_t aux_ch = video_read_host(m, 0x10000u + 0x0400u + base + col);
    uint8_t man_ch = video_read_host(m, 0x0400u + base + col);
    uint16_t x0 = (uint16_t)(col * (uint16_t)APPLE2_VIDEO_PIXELS_PER_COLUMN);
    paint_text_glyph(m, line, x0, aux_ch, 0);
    paint_text_glyph(m, line, (uint16_t)(x0 + 7u), man_ch, 0);
}

/* LORES cell: upper nibble = top 4 scanlines, lower = bottom 4 (a2m). */
static void paint_lores_column(apple2_t *m, uint16_t line, uint16_t col, uint8_t byte)
{
    apple2_video *v = &m->video;
    uint8_t nibble =
        ((line & 7u) < 4u) ? (uint8_t)(byte & 0x0Fu) : (uint8_t)((byte >> 4) & 0x0Fu);
    uint32_t color = paint_cell_colour(v, nibble);
    uint16_t x0 = (uint16_t)(col * (uint16_t)APPLE2_VIDEO_PIXELS_PER_COLUMN);
    int b;

    if (x0 + (uint16_t)APPLE2_VIDEO_PIXELS_PER_COLUMN > APPLE2_VIDEO_WIDTH ||
        line >= APPLE2_VIDEO_HEIGHT || v->fb == NULL) {
        return;
    }

    for (b = 0; b < (int)APPLE2_VIDEO_PIXELS_PER_COLUMN; b++) {
        v->fb[(size_t)line * (size_t)APPLE2_VIDEO_WIDTH + (size_t)x0 + (size_t)b] =
            color;
    }
}

/* One 7-host-pixel DLORES half-column (a2m gr_line cell). */
static void paint_dlores_half(
    apple2_t *m,
    uint16_t line,
    uint16_t x0,
    uint8_t character,
    int from_aux)
{
    apple2_video *v = &m->video;
    uint8_t nibble =
        ((line & 7u) < 4u) ? (uint8_t)(character & 0x0Fu)
                           : (uint8_t)((character >> 4) & 0x0Fu);
    uint32_t color;
    int b;

    if (from_aux) {
        nibble = double_aux_map[nibble & 0x0Fu];
    }
    if (v->fb == NULL || line >= APPLE2_VIDEO_HEIGHT ||
        (uint32_t)x0 + 7u > (uint32_t)APPLE2_VIDEO_WIDTH) {
        return;
    }

    color = paint_cell_colour(v, nibble);
    for (b = 0; b < 7; b++) {
        v->fb[(size_t)line * (size_t)APPLE2_VIDEO_WIDTH + (size_t)x0 + (size_t)b] =
            color;
    }
}

/*
 * Double LORES (a2m unk_apl2_screen_dlores colour): one scanner column =
 * aux half-column then main half-column (7+7 host pixels). Aux nibbles go
 * through double_aux_map. Display pages follow PAGE2 when 80STORE is off.
 */
static void paint_dlores_column(apple2_t *m, uint16_t line, uint16_t col)
{
    uint8_t trow = (uint8_t)(line / 8u);
    uint16_t row = apple2_video_text_line_base(trow);
    uint32_t main_base;
    uint32_t aux_base;
    uint16_t x0 = (uint16_t)(col * (uint16_t)APPLE2_VIDEO_PIXELS_PER_COLUMN);
    uint8_t aux_ch;
    uint8_t man_ch;

    dlores_page_bases(m, &main_base, &aux_base);
    aux_ch = video_read_host(m, aux_base + row + col);
    man_ch = video_read_host(m, main_base + row + col);
    paint_dlores_half(m, line, x0, aux_ch, 1);
    paint_dlores_half(m, line, (uint16_t)(x0 + 7u), man_ch, 0);
}

static void paint_dlores_line(apple2_t *m, uint16_t line)
{
    uint16_t col;
    for (col = 0; col < APPLE2_VIDEO_H_VISIBLE_CYCLES; col++) {
        paint_dlores_column(m, line, col);
    }
}

/*
 * a2m HGR colour: Holger Picker 3-bit window with prev/next neighbour bits.
 * Each logical HGR dot is written as two host pixels (560-wide contract).
 */
static void paint_hgr_column(apple2_t *m, uint16_t line, uint16_t col, uint8_t byte)
{
    apple2_video *v = &m->video;
    uint16_t x0 = (uint16_t)(col * (uint16_t)APPLE2_VIDEO_PIXELS_PER_COLUMN);
    uint16_t line_off;
    uint8_t prev_byte;
    uint8_t next_byte;
    int prev_bit;
    int next_lsb;
    int phase;
    int start_bit;
    int b;
    const hgr_lut_entry *e;
    uint32_t flags = video_display_flags(m);

    if (x0 + (uint16_t)APPLE2_VIDEO_PIXELS_PER_COLUMN > APPLE2_VIDEO_WIDTH ||
        line >= APPLE2_VIDEO_HEIGHT || v->fb == NULL) {
        return;
    }

    if (v->mono) {
        uint32_t on = paint_on_colour(v);
        int bit;
        for (bit = 0; bit < 7; bit++) {
            uint32_t color = ((byte >> bit) & 1) ? on : LORES_PALETTE[0];
            paint_dot_x2(
                v, line, (uint16_t)(x0 + (uint16_t)(bit * 2)), color);
        }
        return;
    }

    paint_init_luts();

    line_off = apple2_video_hgr_line_offset((uint8_t)line);
    prev_byte = (col > 0u)
                    ? video_read_host(
                        m, hgr_host_addr(m, flags, (uint16_t)(line_off + col - 1u)))
                    : 0u;
    next_byte = (col + 1u < 40u)
                    ? video_read_host(
                        m, hgr_host_addr(m, flags, (uint16_t)(line_off + col + 1u)))
                    : 0u;
    prev_bit = (col > 0u) ? ((prev_byte >> 6) & 1) : 0;
    next_lsb = next_byte & 1;
    phase = (byte >> 7) & 1;
    start_bit = (int)(col & 1u);

    e = &hgr_lut[byte & 0x7Fu][next_lsb][prev_bit][phase][start_bit];
    for (b = 0; b < 7; b++) {
        paint_dot_x2(v, line, (uint16_t)(x0 + (uint16_t)(b * 2)), e->pixel[b]);
    }
}

/*
 * DHGR full scanline (a2m unk_apl2_screen_dhgr colour path).
 * Built once at h=0: main/aux HGR bytes → 560 bits → 5-bit window + phase LUT.
 */
static void paint_dhgr_line(apple2_t *m, uint16_t line)
{
    apple2_video *v = &m->video;
    uint16_t page = (video_display_flags(m) & A2S_PAGE2) ? 0x4000u : 0x2000u;
    uint16_t row_off;
    uint8_t row_bits[565];
    int index = 2;
    int col;
    int x;

    if (v->fb == NULL || line >= APPLE2_VIDEO_HEIGHT) {
        return;
    }

    paint_init_luts();
    row_off = apple2_video_hgr_line_offset((uint8_t)line);
    memset(row_bits, 0, sizeof(row_bits));

    for (col = 0; col < 40; col += 2) {
        uint8_t b0 = (uint8_t)(video_read_host(m, 0x10000u + page + row_off + (uint16_t)col) &
                               0x7Fu);
        uint8_t b1 = (uint8_t)(video_read_host(m, page + row_off + (uint16_t)col) & 0x7Fu);
        uint8_t b2 =
            (uint8_t)(video_read_host(m, 0x10000u + page + row_off + (uint16_t)col + 1u) &
                      0x7Fu);
        uint8_t b3 =
            (uint8_t)(video_read_host(m, page + row_off + (uint16_t)col + 1u) & 0x7Fu);
        uint32_t stream = ((uint32_t)b3 << 21) | ((uint32_t)b2 << 14) |
                          ((uint32_t)b1 << 7) | (uint32_t)b0;
        int bit;
        for (bit = 0; bit < 28; bit++) {
            row_bits[index++] = (uint8_t)(stream & 1u);
            stream >>= 1;
        }
    }

    if (v->mono) {
        uint32_t on = paint_on_colour(v);
        for (x = 0; x < APPLE2_VIDEO_WIDTH; x++) {
            v->fb[(size_t)line * (size_t)APPLE2_VIDEO_WIDTH + (size_t)x] =
                row_bits[2 + x] ? on : LORES_PALETTE[0];
        }
        return;
    }

    for (x = 0; x < APPLE2_VIDEO_WIDTH; x++) {
        uint8_t bits = (uint8_t)((row_bits[x] << 4) | (row_bits[x + 1] << 3) |
                                 (row_bits[x + 2] << 2) | (row_bits[x + 3] << 1) |
                                 row_bits[x + 4]);
        v->fb[(size_t)line * (size_t)APPLE2_VIDEO_WIDTH + (size_t)x] =
            dhgr_lut[bits & 31u][(x + 3) & 3];
    }
}

static void paint_at_beam(apple2_t *m)
{
    apple2_video *v = &m->video;
    uint32_t flags;

    if (!v->paint_enabled || v->fb == NULL) {
        (void)scanner_fetch(m);
        return;
    }

    if (v->line >= APPLE2_VIDEO_VISIBLE_LINES ||
        v->cycle_in_line >= APPLE2_VIDEO_H_VISIBLE_CYCLES) {
        return;
    }

    (void)scanner_fetch(m);
    flags = video_display_flags(m);

    if (line_is_text(m, v->line)) {
        if (display_is_80col(m)) {
            paint_text80_column(m, v->line, v->cycle_in_line);
        } else {
            uint8_t trow = (uint8_t)(v->line / 8u);
            uint8_t data = video_read_host(
                m,
                text_host_addr(
                    m, flags,
                    (uint16_t)(apple2_video_text_line_base(trow) +
                               v->cycle_in_line)));
            paint_text40_column(m, v->line, v->cycle_in_line, data);
        }
    } else if (line_is_dhgr(m, v->line)) {
        /* Full-line paint once per scanline (a2m window needs neighbours). */
        if (v->cycle_in_line == 0u) {
            paint_dhgr_line(m, v->line);
        }
    } else if (line_is_hgr(m, v->line)) {
        uint8_t data = video_read_host(
            m,
            hgr_host_addr(
                m, flags,
                (uint16_t)(apple2_video_hgr_line_offset((uint8_t)v->line) +
                           v->cycle_in_line)));
        paint_hgr_column(m, v->line, v->cycle_in_line, data);
    } else if (line_is_dlores(m, v->line)) {
        paint_dlores_column(m, v->line, v->cycle_in_line);
    } else if (line_is_lores(m, v->line)) {
        uint8_t trow = (uint8_t)(v->line / 8u);
        uint8_t data = video_read_host(
            m,
            text_host_addr(
                m, flags,
                (uint16_t)(apple2_video_text_line_base(trow) +
                           v->cycle_in_line)));
        paint_lores_column(m, v->line, v->cycle_in_line, data);
    }
}

void apple2_video_paint_full_frame(apple2_t *m)
{
    apple2_video *v;
    uint16_t line;
    uint16_t col;
    uint32_t flags;

    if (m == NULL) {
        return;
    }
    v = &m->video;
    if (v->fb == NULL) {
        return;
    }

    paint_init_luts();
    flags = video_display_flags(m);

    for (line = 0; line < APPLE2_VIDEO_VISIBLE_LINES; line++) {
        if (line_is_text(m, line)) {
            if (display_is_80col(m)) {
                for (col = 0; col < APPLE2_VIDEO_H_VISIBLE_CYCLES; col++) {
                    paint_text80_column(m, line, col);
                }
            } else {
                for (col = 0; col < APPLE2_VIDEO_H_VISIBLE_CYCLES; col++) {
                    uint8_t trow = (uint8_t)(line / 8u);
                    uint32_t addr = text_host_addr(
                        m, flags,
                        (uint16_t)(apple2_video_text_line_base(trow) + col));
                    paint_text40_column(m, line, col, video_read_host(m, addr));
                }
            }
        } else if (line_is_dhgr(m, line)) {
            paint_dhgr_line(m, line);
        } else if (line_is_hgr(m, line)) {
            uint16_t line_off = apple2_video_hgr_line_offset((uint8_t)line);
            for (col = 0; col < APPLE2_VIDEO_H_VISIBLE_CYCLES; col++) {
                uint8_t byte = video_read_host(
                    m, hgr_host_addr(m, flags, (uint16_t)(line_off + col)));
                paint_hgr_column(m, line, col, byte);
            }
        } else if (line_is_dlores(m, line)) {
            paint_dlores_line(m, line);
        } else if (line_is_lores(m, line)) {
            uint8_t trow = (uint8_t)(line / 8u);
            uint16_t base = apple2_video_text_line_base(trow);
            for (col = 0; col < APPLE2_VIDEO_H_VISIBLE_CYCLES; col++) {
                uint8_t byte =
                    video_read_host(
                        m, text_host_addr(m, flags, (uint16_t)(base + col)));
                paint_lores_column(m, line, col, byte);
            }
        }
    }
}

void apple2_video_set_display_override(
    apple2_t *m,
    bool enabled,
    uint32_t flags)
{
    const uint32_t mask = A2S_COL80 | A2S_ALTCHARSET | A2S_TEXT |
        A2S_MIXED | A2S_PAGE2 | A2S_HIRES | A2S_DHIRES;

    if (m == NULL) {
        return;
    }
    m->video.display_override_enabled = enabled;
    m->video.display_override_flags = flags & mask;
}

void apple2_video_set_monitor(
    apple2_t *m,
    bool colour,
    apple2_video_phosphor phosphor)
{
    if (m == NULL) {
        return;
    }
    paint_init_luts();
    m->video.mono = !colour;
    if (phosphor > APPLE2_VIDEO_PHOSPHOR_AMBER) {
        phosphor = APPLE2_VIDEO_PHOSPHOR_WHITE;
    }
    m->video.phosphor = phosphor;
}

void apple2_video_init(apple2_t *m)
{
    size_t pixels;

    if (m == NULL) {
        return;
    }

    paint_init_luts();

    memset(&m->video, 0, sizeof(m->video));
    m->video.paint_enabled = true;
    pixels = (size_t)APPLE2_VIDEO_WIDTH * (size_t)APPLE2_VIDEO_HEIGHT;
    m->video.fb = (uint32_t *)calloc(pixels, sizeof(uint32_t));
    m->video.last_video_byte = 0x00;
}

void apple2_video_shutdown(apple2_t *m)
{
    if (m == NULL) {
        return;
    }
    free(m->video.fb);
    m->video.fb = NULL;
}

void apple2_video_reset(apple2_t *m)
{
    if (m == NULL) {
        return;
    }
    m->video.cycle_in_line = 0;
    m->video.line = 0;
    m->video.frame_ready = false;
    m->video.last_video_byte = 0x00;
    if (m->video.fb != NULL) {
        memset(m->video.fb, 0,
               (size_t)APPLE2_VIDEO_WIDTH * (size_t)APPLE2_VIDEO_HEIGHT * sizeof(uint32_t));
    }
}

void apple2_video_step(apple2_t *m)
{
    apple2_video *v;

    if (m == NULL) {
        return;
    }
    v = &m->video;

    /* Paint-off: advance H/V only — no scanner RAM peeks. VBL still tracks
       line. Floating-bus is stale until beam paint resumes. */
    if (v->paint_enabled) {
        paint_at_beam(m);
    }

    v->cycle_in_line++;
    if (v->cycle_in_line >= APPLE2_VIDEO_CYCLES_PER_LINE) {
        v->cycle_in_line = 0;
        v->line++;
        if (v->line >= APPLE2_VIDEO_LINES_PER_FRAME) {
            v->line = 0;
            v->frame_number++;
            v->frame_gen++;
            v->frame_ready = true;
        }
    }
}

void apple2_video_advance_alite(apple2_t *m, uint32_t n)
{
    apple2_video *v;
    uint64_t pos;
    uint64_t frames;

    if (m == NULL || n == 0u) {
        return;
    }
    v = &m->video;
    pos = (uint64_t)v->line * (uint64_t)APPLE2_VIDEO_CYCLES_PER_LINE +
        (uint64_t)v->cycle_in_line + (uint64_t)n;
    frames = pos / (uint64_t)APPLE2_VIDEO_CYCLES_PER_FRAME;
    pos %= (uint64_t)APPLE2_VIDEO_CYCLES_PER_FRAME;
    v->line = (uint16_t)(pos / (uint64_t)APPLE2_VIDEO_CYCLES_PER_LINE);
    v->cycle_in_line =
        (uint16_t)(pos % (uint64_t)APPLE2_VIDEO_CYCLES_PER_LINE);
    if (frames > 0u) {
        v->frame_number += frames;
        v->frame_gen += (uint32_t)frames;
        v->frame_ready = true;
    }
}

void apple2_video_step_n(apple2_t *m, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        apple2_video_step(m);
    }
}

void apple2_video_reseed_from_cycles(apple2_t *m)
{
    uint64_t cycles;
    uint64_t in_frame;

    if (m == NULL) {
        return;
    }
    cycles = m->cpu.cpu.cycles;
    in_frame = cycles % (uint64_t)APPLE2_VIDEO_CYCLES_PER_FRAME;
    m->video.line = (uint16_t)(in_frame / (uint64_t)APPLE2_VIDEO_CYCLES_PER_LINE);
    m->video.cycle_in_line =
        (uint16_t)(in_frame % (uint64_t)APPLE2_VIDEO_CYCLES_PER_LINE);
    m->video.frame_ready = false;
}

bool apple2_video_in_vbl(const apple2_t *m)
{
    if (m == NULL) {
        return false;
    }
    return m->video.line >= APPLE2_VIDEO_VBL_START_LINE;
}

bool apple2_video_in_hblank(const apple2_t *m)
{
    if (m == NULL) {
        return true;
    }
    return m->video.cycle_in_line >= APPLE2_VIDEO_H_VISIBLE_CYCLES;
}

uint8_t apple2_video_floating_bus(apple2_t *m)
{
    if (m == NULL) {
        return 0xA0;
    }
    /* During blanking, return last latched video byte (common floating-bus model). */
    if (apple2_video_in_vbl(m) || apple2_video_in_hblank(m)) {
        return m->video.last_video_byte;
    }
    return scanner_fetch(m);
}

const uint32_t *apple2_video_framebuffer(const apple2_t *m)
{
    if (m == NULL) {
        return NULL;
    }
    return m->video.fb;
}

uint32_t apple2_video_frame_gen(const apple2_t *m)
{
    if (m == NULL) {
        return 0;
    }
    return m->video.frame_gen;
}

bool apple2_video_take_frame_ready(apple2_t *m)
{
    bool ready;
    if (m == NULL) {
        return false;
    }
    ready = m->video.frame_ready;
    m->video.frame_ready = false;
    return ready;
}
