// Apple ][+ emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#pragma once

typedef void (*MAP_CX_ROM)(APPLE2 * m, uint16_t address);   // Callback prototype to map C800 ROM

typedef struct SLOT_CARDS {
    int slot_type;                                          // 1 when a slot contains a card, 0 if not
    void *slot_card;                                        // a handle to the installed card
    MAP_CX_ROM slot_map_cx_rom;                             // Callback to active C800 ROM on card (NULL = no ROM)
    int cx_rom_mapped;                                      // 1 - C800 is mapped, 0 - C800 not mapped
} SLOT_CARDS;

void slot_add_card(APPLE2 * m, uint8_t slot, int slot_type, void *slot_card, uint8_t * card_rom, MAP_CX_ROM map_cx_rom);
void slot_remove_card(APPLE2 * m, uint8_t slot);
