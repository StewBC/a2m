// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

typedef struct VIEWMISC {
    FILE_BROWSER file_browser;
    BREAKPOINT_EDIT breakpoint_edit;
} VIEWMISC;

void unk_misc_show(UNK *v);
