// Apple ][+ emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#pragma once

// Apple II Applesoft and ROM (D000 - FFFF)
extern uint8_t apple2_rom[];
extern const int apple2_rom_size;

// Character ROM (Not mapped, used by drawing code)
extern uint8_t apple_character_rom[];
extern const int apple_character_rom_size;

// Not used - maybe in the future?
extern uint8_t disk2_rom[];
extern const int disk2_rom_size;

// This rom contains code for all slots.  Map as needed only.
extern uint8_t smartport_rom[];
extern const int smartport_rom_size;
