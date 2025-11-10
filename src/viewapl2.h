// Apple ][+ and //e Emhanced emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#pragma once

// RGB values
typedef struct mRGB {
    union {
        struct {
            uint8_t r, g, b;
        };
        uint8_t c[3];
    };
} mRGB;

void viewapl2_feed_clipboard_key(APPLE2 *m);
void viewapl2_init_color_table(APPLE2 *m);
void viewapl2_process_event(APPLE2 *m, SDL_Event *e);
void viewapl2_screen_apple2(APPLE2 *m);
void viewapl2_screen_dlores(APPLE2 *m, int start, int end);
void viewapl2_screen_dlores_mono(APPLE2 *m, int start, int end);
void viewapl2_screen_lores(APPLE2 *m, int start, int end);
void viewapl2_screen_lores_mono(APPLE2 *m, int start, int end);
void viewapl2_screen_dhgr(APPLE2 *m, int start, int end);
void viewapl2_screen_dhgr_rgb(APPLE2 *m, int start, int end);
void viewapl2_screen_hgr(APPLE2 *m, int start, int end);
void viewapl2_screen_hgr_mono(APPLE2 *m, int start, int end);
void viewapl2_screen_txt40(APPLE2 *m, int start, int end);
void viewapl2_screen_txt80(APPLE2 *m, int start, int end);
void viewapl2_screen_franklin80col(APPLE2 *m, int start, int end);
