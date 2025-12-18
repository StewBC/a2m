// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

void utxt_apl2_screen_apple2(UTXT *v);
// void utxt_apl2_screen_dlores(UTXT *v, int start, int end);
// void utxt_apl2_screen_dlores_mono(UTXT *v, int start, int end);
// void utxt_apl2_screen_lores(UTXT *v, int start, int end);
// void utxt_apl2_screen_lores_mono(UTXT *v, int start, int end);
// void utxt_apl2_screen_dhgr(UTXT *v, int start, int end);
// void utxt_apl2_screen_dhgr_rgb(UTXT *v, int start, int end);
// void utxt_apl2_screen_hgr(UTXT *v, int start, int end);
// void utxt_apl2_screen_hgr_mono(UTXT *v, int start, int end);
void utxt_apl2_screen_txt40(UTXT *v, int start, int end);
void utxt_apl2_screen_txt80(UTXT *v, int start, int end);
// void utxt_apl2_screen_franklin80col(UTXT *v, int start, int end);
