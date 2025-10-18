// Apple ][+ and //e Emhanced emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#pragma once

// 16K Ram card (Slot 0 card - Language Card)
typedef struct RAM_CARD {
    uint8_t *RAM;
    int bank2_enable:1;
    int read_ram_enable:1;
    int pre_write:1;
    int write_enable:1;
} RAM_CARD;

uint8_t ram_card(APPLE2 * m, uint16_t address, uint16_t value);
int ram_card_init(RAM_CARD * ram_card);
void ram_card_shutdown(RAM_CARD * ram_card);
