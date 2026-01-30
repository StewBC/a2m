// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

// RGB values
typedef struct {
    union {
        struct {
            uint8_t r, g, b;
        };
        uint8_t c[3];
    };
} mRGB;

void unk_apl2_init_color_table(UNK *v);
void unk_apl2_process_event(UNK *v, SDL_Event *e);
void unk_apl2_screen_apple2(UNK *v);
void unk_apl2_screen_dlores(UNK *v, int start, int end);
void unk_apl2_screen_dlores_mono(UNK *v, int start, int end);
void unk_apl2_screen_lores(UNK *v, int start, int end);
void unk_apl2_screen_lores_mono(UNK *v, int start, int end);
void unk_apl2_screen_dhgr(UNK *v, int start, int end);
void unk_apl2_screen_hgr(UNK *v, int start, int end);
void unk_apl2_screen_hgr_mono(UNK *v, int start, int end);
void unk_apl2_screen_txt40(UNK *v, int start, int end);
void unk_apl2_screen_txt80(UNK *v, int start, int end);
void unk_apl2_screen_franklin80col(UNK *v, int start, int end);
