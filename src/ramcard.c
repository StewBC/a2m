// Apple ][+ and //e Emhanced emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#include "header.h"

uint8_t ram_card(APPLE2 *m, uint16_t address, uint16_t value) {
    RAM_CARD *lc = &m->ram_card;
    lc->bank2_enable = (address & 0b1000) ? 0 : 1;
    if((address & 0b11) && (address & 0b11) != 3) {
        lc->read_ram_enable = 0;
    } else {
        lc->read_ram_enable = 1;
    }

    if(!(address & 1) || value < 0x100) {
        // even access or write resets pre-write
        lc->pre_write = 0;
        if(value >= 0x100) {
            lc->write_enable = 0;
        }
    } else {
        // odd access
        if(lc->pre_write) {
            lc->write_enable = 1;
        }
        // odd sets pre_write
        lc->pre_write = 1;
    }
    
    ram_card_map_memory(m);
    return m->read_pages.pages[address / PAGE_SIZE].bytes[address % PAGE_SIZE];
}

int ram_card_init(APPLE2 *m) {
    RAM_CARD *ram_card = &m->ram_card;
    // The ram_card has 16 KB.  It is set up as
    // 4K ($0000 - $0FFF) Bank 1 @ $D000 - $DFFF
    // 4K ($1000 - $1FFF) Bank 2 @ $D000 - $DFFF
    // 8K ($2000 - $3FFF)        @ $E000 - $FFFF
    // That's 16K but 2x for AUX version in IIe (allocated on ][+ as well, atm SQW)
    ram_card->RAM = (uint8_t *) malloc(32 * 1024);
    if(!ram_card->RAM) {
        return A2_ERR;
    }
    ram_card_reinit(m);
    return A2_OK;
}

void ram_card_reinit(APPLE2 *m) {
    RAM_CARD *ram_card = &m->ram_card;
    ram_card->bank2_enable = 1;
    ram_card->read_ram_enable = 0;
    ram_card->pre_write = 1;
    ram_card->write_enable = 1;
    ram_card_map_memory(m);
}

void ram_card_map_memory(APPLE2 *m) {
    RAM_CARD *lc = &m->ram_card;
    uint16_t bank_addr = lc->bank2_enable ? 0x1000 : 0x0000;
    uint16_t aux_addr = m->altzpset ? 0x4000 : 0x0;
    if(lc->read_ram_enable) {
        pages_map(&m->read_pages, 0xD000 / PAGE_SIZE, 0x1000 / PAGE_SIZE, &lc->RAM[bank_addr + aux_addr]);
        pages_map(&m->read_pages, 0xE000 / PAGE_SIZE, 0x2000 / PAGE_SIZE, &lc->RAM[0x2000 + aux_addr]);
    } else {
        pages_map_memory_block(&m->read_pages, &m->roms.blocks[ROM_APPLE2]);
    }

    if(lc->write_enable) {
        pages_map(&m->write_pages, 0xD000 / PAGE_SIZE, 0x1000 / PAGE_SIZE, &lc->RAM[bank_addr + aux_addr]);
        pages_map(&m->write_pages, 0xE000 / PAGE_SIZE, 0x2000 / PAGE_SIZE, &lc->RAM[0x2000 + aux_addr]);
    } else {
        // Writes to ROM go here but its a bit-bucket - aux doesn't matter
        pages_map(&m->write_pages, 0xD000 / PAGE_SIZE, 0x3000 / PAGE_SIZE, &m->RAM_MAIN[0xD000]);
    }
}

void ram_card_shutdown(RAM_CARD *ram_card) {
    free(ram_card->RAM);
    ram_card->RAM = 0;
}
