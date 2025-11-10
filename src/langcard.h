// Apple ][+ and //e Emhanced emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#pragma once

uint8_t language_card(APPLE2 *m, uint16_t address, uint16_t value);
void language_card_init(APPLE2 *m);
void language_card_map_memory(APPLE2 *m);
