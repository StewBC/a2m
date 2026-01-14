// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "common.h"
#include "hardware_lib.h"

void language_card(APPLE2 *m, uint16_t address, int write_access) {
    int odd_access = address & 1;

    // Bank select (A3)
    m->lc_bank2_enable = (address & 0b1000) ? 0 : 1;

    // HRAMRD (A1/A0)
    int bits2 = address & 0b11;
    m->lc_read_ram_enable = (bits2 == 0 || bits2 == 3);

    if (!odd_access) {
        // Even access (read or write)
        m->lc_pre_write = 0;
        m->lc_write_enable = 0;
    } else if (write_access) {
        // Odd write
        m->lc_pre_write = 0;
    } else {
        // Odd read
        if (m->lc_pre_write) {
            m->lc_write_enable = 1;
        }
        m->lc_pre_write = 1;
    }
    language_card_map_memory(m);
}


void language_card_init(APPLE2 *m) {
    m->lc_bank2_enable = 1;
    m->lc_read_ram_enable = 0;
    m->lc_pre_write = 1;
    m->lc_write_enable = 1;
    language_card_map_memory(m);
}

void language_card_map_memory(APPLE2 *m) {
    uint16_t bank_addr = m->lc_bank2_enable ? 0x1000 : 0x0000;
    uint16_t aux_addr  =        m->altzpset ? 0x4000 : 0x0000; // Note - offset in LC RAM_LC, not RAM_MAIN
    if(m->lc_read_ram_enable) {
        pages_map_lc(&m->pages, PAGE_MAP_READ, 0xD000, 0x1000, bank_addr + aux_addr, &m->ram);
        pages_map_lc(&m->pages, PAGE_MAP_READ, 0xE000, 0x2000, 0x2000 + aux_addr, &m->ram);
    } else {
        pages_map_rom_block(&m->pages, &m->roms.blocks[ROM_APPLE2], &m->ram);
    }

    if(m->lc_write_enable) {
        pages_map_lc(&m->pages, PAGE_MAP_WRITE, 0xD000, 0x1000, bank_addr + aux_addr, &m->ram);
        pages_map_lc(&m->pages, PAGE_MAP_WRITE, 0xE000, 0x2000, 0x2000 + aux_addr, &m->ram);
    } else {
        // Writes to ROM go here but its a bit-bucket - aux doesn't matter
        pages_map(&m->pages, PAGE_MAP_WRITE, 0xD000, 0x3000, &m->ram);
    }
}

