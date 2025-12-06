// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

void language_card(APPLE2 *m, uint16_t address, int write_access);
void language_card_init(APPLE2 *m);
void language_card_map_memory(APPLE2 *m);
