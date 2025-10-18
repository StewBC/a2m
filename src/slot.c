// Apple ][+ and //e Emhanced emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#include "header.h"

void slot_add_card(APPLE2 *m, uint8_t slot, int slot_type, void *slot_card, uint8_t *card_rom, MAP_CX_ROM map_cx_rom) {
    if(m->slot_cards[slot].slot_type) {
        slot_remove_card(m, slot);
    }
    m->slot_cards[slot].slot_type = slot_type;
    m->slot_cards[slot].slot_card = slot_card;
    m->slot_cards[slot].slot_map_cx_rom = map_cx_rom;
    pages_map(&m->read_pages, (0xC000 + (slot * 0x100)) / PAGE_SIZE, 0x100 / PAGE_SIZE, card_rom);
    // map the device select range
    for(size_t i = 0xC080 + slot * 0x10; i <= 0xC08F + slot * 0x10; i++) {
        m->RAM_WATCH[i] |= 1;
    }
    // Map the slot rom range
    for(size_t i = 0xC000 + slot * 0x100; i < 0xC100 + slot * 0x100; i++) {
        m->RAM_WATCH[i] |= 1;
    }
}

// This is a safty measure.  Cards should not be removed
void slot_remove_card(APPLE2 *m, uint8_t slot) {
    m->slot_cards[slot].slot_type = SLOT_TYPE_EMPTY;
    m->slot_cards[slot].slot_card = 0;                      // This could cause a memory leak
    m->slot_cards[slot].slot_map_cx_rom = 0;
    for(size_t i = 0xC080 + slot * 0x10; i <= 0xC08F + slot * 0x10; i++) {
        m->RAM_WATCH[i] &= ~1;
    }
}
