// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

// Registers
enum {
    FD80_HORIZ_TOTAL,                                       // 00
    FD80_HORIZ_DISPLAYED,                                   // 01
    FD80_HORIZ_SYNC_POS,                                    // 02
    FD80_HORIZ_SYNC_WIDTH,                                  // 03
    FD80_VERT_TOTAL,                                        // 04
    FD80_VERT_ADJUST,                                       // 05
    FD80_VERT_DISPLAYED,                                    // 06
    FD80_VERT_SYNC_POS,                                     // 07
    FD80_INTERLACED,                                        // 08
    FD80_MAX_SCAN_LINE,                                     // 09
    FD80_CURSOR_UPPER,                                      // 0A
    FD80_CURSOR_LOWER,                                      // 0B
    FD80_STARTPOS_HI,                                       // 0C
    FD80_STARTPOS_LO,                                       // 0D
    FD80_CURSOR_HI,                                         // 0E
    FD80_CURSOR_LO,                                         // 0F
    FD80_LIGHTPEN_HI,                                       // 10
    FD80_LIGHTPEN_LO,                                       // 11
};

typedef struct FRANKLIN_DISPLAY {
    uint8_t *display_ram;
    uint8_t reg_num;
    uint8_t bank;
    uint8_t registers[18];
} FRANKLIN_DISPLAY;

int franklin_display_init(FRANKLIN_DISPLAY *fd80);
void franklin_display_map_cx_rom(APPLE2 *m, uint16_t address);
void franklin_display_set(APPLE2 *m, uint16_t address, uint8_t value);
void franklin_display_shutdown(FRANKLIN_DISPLAY *fd80);
