// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

void utxt_apl2_screen_apple2(UTXT *v);
void utxt_apl2_screen_show_modifiers(UTXT *v);
void utxt_apl2_screen_box(UTXT *v, int start, int end, int width, char *text);
void utxt_apl2_screen_txt(UTXT *v, int start, int end, int width);
