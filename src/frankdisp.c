// Apple ][+ emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#include "header.h"

int franklin_display_init(FRANKLIN_DISPLAY *fd80) {
    memset(fd80, 0, sizeof(FRANKLIN_DISPLAY));
    if(!(fd80->display_ram = (uint8_t *) malloc(2048))) {
        return A2_ERR;
    }
    memset(fd80->display_ram, 32, 2048);
    fd80->registers[FD80_HORIZ_TOTAL] = 0x7B;
    fd80->registers[FD80_HORIZ_DISPLAYED] = 0x50;
    fd80->registers[FD80_HORIZ_SYNC_POS] = 0x62;
    fd80->registers[FD80_HORIZ_SYNC_WIDTH] = 0x29;
    fd80->registers[FD80_VERT_TOTAL] = 0x1B;
    fd80->registers[FD80_VERT_ADJUST] = 0x08;
    fd80->registers[FD80_VERT_DISPLAYED] = 0x18;
    fd80->registers[FD80_VERT_SYNC_POS] = 0x19;
    fd80->registers[FD80_MAX_SCAN_LINE] = 0x08;
    fd80->registers[FD80_CURSOR_UPPER] = 0xC0;
    fd80->registers[FD80_CURSOR_LOWER] = 0x08;
    return A2_OK;
}

void franklin_display_map_cx_rom(APPLE2 *m, uint16_t address) {
    pages_map(&m->read_pages, 0xC800 / PAGE_SIZE, 0x800 / PAGE_SIZE, m->roms.blocks[ROM_FRANKLIN_ACE_DISPLAY].bytes);
}

void franklin_display_set(APPLE2 *m, uint16_t address, uint8_t value) {
    FRANKLIN_DISPLAY *fd80 = &m->franklin_display;

    uint8_t reg = address & 0x0f;
    if(!reg) {
        fd80->reg_num = value;
        return;
    }
    fd80->registers[fd80->reg_num] = value;
}
