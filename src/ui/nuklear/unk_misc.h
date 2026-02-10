// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

typedef struct {
    int dragging;
    BREAKPOINT_EDIT breakpoint_edit;
    MACHINE_CONFIG machine_config;
    MACHINE_CONFIG machine_config_original;
} VIEWMISC;

int unk_misc_init(VIEWMISC *viewmisc);
void unk_misc_show(UNK *v);
