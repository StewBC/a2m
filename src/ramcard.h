// Apple ][+ emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#pragma once

// 16K Ram card (Slot 0 card - Language Card)
typedef struct RAM_CARD {
    int     bank2_enable;
    int     read_enable;
    int     write_enable;
    uint8_t *RAM;
} RAM_CARD;

uint8_t ram_card(APPLE2 *m, uint16_t address, uint16_t value);
int ram_card_init(RAM_CARD *ram_card);
void ram_card_shutdown(RAM_CARD *ram_card);
