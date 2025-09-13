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

// 2 disk ii roms - 13 sector and 16 sector
extern uint8_t disk2_rom[2][256];

// 80 Col auto soft switch display card ROM
extern uint8_t franklin_ace_display_rom[];
extern const int franklin_ace_display_rom_size;

// Character ROM for 80 col franklin ace display
extern uint8_t franklin_ace_character_rom[];
extern const int franklin_ace_character_rom_size;

// This rom contains code for all slots.  Map as needed only.
extern uint8_t smartport_rom[];
extern const int smartport_rom_size;
