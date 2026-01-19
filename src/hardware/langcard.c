// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "common.h"
#include "hardware_lib.h"

void language_card(APPLE2 *m, uint16_t address, int write_access) {
    int odd_access = address & 1;

    // Bank select (A3)
    m->state_flags = (m->state_flags & ~A2S_LC_BANK2) | (((address & 0b1000) == 0) ? A2S_LC_BANK2 : 0);

    // HRAMRD (A1/A0)
    int bits2 = address & 0b11;
    m->state_flags = (m->state_flags & ~A2S_LC_READ) | ((bits2 == 0 || bits2 == 3) ? A2S_LC_READ : 0);

    if(!odd_access) {
        // Even access (read or write)
        clr_flags(m->state_flags, A2S_LC_PRE_WRITE | A2S_LC_WRITE);
    } else if(write_access) {
        // Odd write
        clr_flags(m->state_flags, A2S_LC_PRE_WRITE);
    } else {
        // Odd read
        if(tst_flags(m->state_flags, A2S_LC_PRE_WRITE)) {
            set_flags(m->state_flags, A2S_LC_WRITE);
        }
        set_flags(m->state_flags, A2S_LC_PRE_WRITE);
    }
    language_card_map_memory(m);
}


void language_card_init(APPLE2 *m) {
    clr_flags(m->state_flags, A2S_LC_READ);
    set_flags(m->state_flags, A2S_LC_BANK2 | A2S_LC_WRITE | A2S_LC_PRE_WRITE);
    language_card_map_memory(m);
}

void language_card_map_memory(APPLE2 *m) {
    uint16_t bank_addr = tst_flags(m->state_flags, A2S_LC_BANK2) ? 0x1000 : 0x0000;
    uint16_t aux_addr  = tst_flags(m->state_flags, A2S_ALTZP) ? 0x4000 : 0x0000; // Note - offset in LC RAM_LC, not RAM_MAIN
    if(tst_flags(m->state_flags, A2S_LC_READ)) {
        pages_map_lc(&m->pages, PAGE_MAP_READ, 0xD000, 0x1000, bank_addr + aux_addr, &m->ram);
        pages_map_lc(&m->pages, PAGE_MAP_READ, 0xE000, 0x2000, 0x2000 + aux_addr, &m->ram);
    } else {
        pages_map_rom_block(&m->pages, &m->roms.blocks[ROM_APPLE2], &m->ram);
    }

    if(tst_flags(m->state_flags, A2S_LC_WRITE)) {
        pages_map_lc(&m->pages, PAGE_MAP_WRITE, 0xD000, 0x1000, bank_addr + aux_addr, &m->ram);
        pages_map_lc(&m->pages, PAGE_MAP_WRITE, 0xE000, 0x2000, 0x2000 + aux_addr, &m->ram);
    } else {
        // Writes to ROM go here but its a bit-bucket - aux doesn't matter
        pages_map(&m->pages, PAGE_MAP_WRITE, 0xD000, 0x3000, &m->ram);
    }
}

