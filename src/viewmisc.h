// Apple ][+ emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#pragma once

typedef struct VIEWMISC {
    FILE_BROWSER    file_browser;
} VIEWMISC;

void viewmisc_show(APPLE2* m);
