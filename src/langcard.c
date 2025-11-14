// Apple ][+ and //e Emhanced emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#include "header.h"

uint8_t language_card(APPLE2 *m, uint16_t address, uint16_t value) {
    m->lc_bank2_enable = (address & 0b1000) ? 0 : 1;
    if((address & 0b11) && (address & 0b11) != 3) {
        m->lc_read_ram_enable = 0;
    } else {
        m->lc_read_ram_enable = 1;
    }

    if(!(address & 1) || value < 0x100) {
        // even access or write resets pre-write
        m->lc_pre_write = 0;
        if(value >= 0x100) {
            m->lc_write_enable = 0;
        }
    } else {
        // odd access
        if(m->lc_pre_write) {
            m->lc_write_enable = 1;
        }
        // odd sets pre_write
        m->lc_pre_write = 1;
    }

    language_card_map_memory(m);
    return m->read_pages.pages[address / PAGE_SIZE].bytes[address % PAGE_SIZE];
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
        pages_map(&m->read_pages, 0xD000 / PAGE_SIZE, 0x1000 / PAGE_SIZE, &m->RAM_LC[bank_addr + aux_addr]);
        pages_map(&m->read_pages, 0xE000 / PAGE_SIZE, 0x2000 / PAGE_SIZE, &m->RAM_LC[0x2000 + aux_addr]);
    } else {
        pages_map_memory_block(&m->read_pages, &m->roms.blocks[ROM_APPLE2]);
    }

    if(m->lc_write_enable) {
        pages_map(&m->write_pages, 0xD000 / PAGE_SIZE, 0x1000 / PAGE_SIZE, &m->RAM_LC[bank_addr + aux_addr]);
        pages_map(&m->write_pages, 0xE000 / PAGE_SIZE, 0x2000 / PAGE_SIZE, &m->RAM_LC[0x2000 + aux_addr]);
    } else {
        // Writes to ROM go here but its a bit-bucket - aux doesn't matter
        pages_map(&m->write_pages, 0xD000 / PAGE_SIZE, 0x3000 / PAGE_SIZE, &m->RAM_MAIN[0xD000]);
    }
}

