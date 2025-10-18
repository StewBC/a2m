// Apple ][+ and //e Emhanced emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#pragma once

typedef struct VIEWCPU {
    char registers[5][5];
    int register_lengths[5];
    char flags[8][2];
    int flag_lengths[8];
} VIEWCPU;

void viewcpu_show(APPLE2 * m);
int viewcpu_process_event(APPLE2 * m, SDL_Event * e);
void viewcpu_update(APPLE2 * m);
