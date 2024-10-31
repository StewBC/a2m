// Apple ][+ emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#include "header.h"

uint8_t ram_card(APPLE2 *m, uint16_t address, uint16_t value) {
    if((address & 0b11) && (address & 0b11) != 3) {
        m->ram_card.read_enable = 0;
        pages_map_memory_block(&m->read_pages, &m->roms.blocks[ROM_APPLE]);
    } else {
        m->ram_card.read_enable = 1;
        pages_map(&m->read_pages, 0xE000 / PAGE_SIZE, 0x2000 / PAGE_SIZE, &m->ram_card.RAM[0x2000]);
    }

    if(address & 0b1000) {
        m->ram_card.bank2_enable = 0;
        if(m->ram_card.read_enable) {
            pages_map(&m->read_pages, 0xD000 / PAGE_SIZE, 0x1000 / PAGE_SIZE, &m->ram_card.RAM[0x0000]);
        }
    } else {
        m->ram_card.bank2_enable = 1;
        if(m->ram_card.read_enable) {
            pages_map(&m->read_pages, 0xD000 / PAGE_SIZE, 0x1000 / PAGE_SIZE, &m->ram_card.RAM[0x1000]);
        }
    }

    if(!(address & 1) || value < 0x100) {
        if(value >= 0x100) {
            pages_map(&m->write_pages, 0xD000 / PAGE_SIZE, 0x3000 / PAGE_SIZE, &m->RAM_MAIN[0xD000]);
        } else {
            m->ram_card.write_enable = 0;
        }
    } else {
        if(++m->ram_card.write_enable >= 2) {
            m->ram_card.write_enable = 2;
            pages_map(&m->write_pages, 0xE000 / PAGE_SIZE, 0x2000 / PAGE_SIZE, &m->ram_card.RAM[0x2000]);
            pages_map(&m->write_pages, 0xD000 / PAGE_SIZE, 0x1000 / PAGE_SIZE, &m->ram_card.RAM[m->ram_card.bank2_enable ? 0x1000 : 0x0000]);
        }
    }

    return m->read_pages.pages[address / PAGE_SIZE].bytes[address % PAGE_SIZE];
}

int ram_card_init(RAM_CARD *ram_card) {
    ram_card->RAM = (uint8_t*)malloc(16*1024);
    if(!ram_card->RAM) {
        return A2_ERR;
    }
    return A2_OK;
}

void ram_card_shutdown(RAM_CARD *ram_card) {
    free(ram_card->RAM);
    ram_card->RAM = 0;
}