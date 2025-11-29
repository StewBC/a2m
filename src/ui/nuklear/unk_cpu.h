// Apple ][+ and //e Emhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

typedef struct VIEWCPU {
    char registers[5][5];
    int register_lengths[5];
    char flags[8][2];
    int flag_lengths[8];
} VIEWCPU;

void unk_cpu_show(UNK *v, int dirty);
void unk_cpu_update(UNK *v);
