// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "common.h"
#include "hardware_lib.h"

// Slot card is not owned by the slot, and will not be "deleted"
void slot_add_card(APPLE2 *m, uint8_t slot, int slot_type, void *slot_card, uint8_t *card_rom, MAP_CX_ROM map_cx_rom) {
    if(m->slot_cards[slot].slot_type) {
        slot_remove_card(m, slot);
    }
    m->slot_cards[slot].slot_type = slot_type;
    m->slot_cards[slot].slot_card = slot_card;
    m->slot_cards[slot].slot_map_cx_rom = map_cx_rom;
    pages_map_rom(&m->pages, (0xC000 + (slot * 0x100)), 0x100, card_rom, &m->ram);
    m->rom_shadow_pages[slot] = card_rom;
    // map the device select range
    for(size_t i = 0xC080 + slot * 0x10; i <= 0xC08F + slot * 0x10; i++) {
        m->ram.RAM_WATCH[i] |= WATCH_IO_PORT;
    }
    // Map the slot rom range
    for(size_t i = 0xC000 + slot * 0x100; i < 0xC100 + slot * 0x100; i++) {
        m->ram.RAM_WATCH[i] |= WATCH_IO_PORT;
    }
}

// This is a safty measure.  Cards should not be removed
void slot_remove_card(APPLE2 *m, uint8_t slot) {
    m->slot_cards[slot].slot_type = SLOT_TYPE_EMPTY;
    m->slot_cards[slot].slot_card = 0;
    m->slot_cards[slot].slot_map_cx_rom = 0;
    for(size_t i = 0xC080 + slot * 0x10; i <= 0xC08F + slot * 0x10; i++) {
        m->ram.RAM_WATCH[i] &= ~WATCH_IO_PORT;
    }
    for(size_t i = 0xC000 + slot * 0x100; i < 0xC100 + slot * 0x100; i++) {
        m->ram.RAM_WATCH[i] &= ~WATCH_IO_PORT;
    }
}
