// Apple ][+ and //e Emhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

// Apple II Applesoft and ROM (D000 - FFFF)
extern uint8_t a2p_rom[];
extern const int a2p_rom_size;

// Character ROM (Not mapped, used by drawing code)
extern uint8_t a2p_character_rom[];
extern const int a2p_character_rom_size;

// Apple //e Enhanced ROM (C000 - FFFF but mapped in parts from C100)
extern uint8_t a2ee_rom[];
extern const int a2ee_rom_size;

// //e Enhanced Character ROM (Not mapped, used by drawing code)
extern uint8_t a2ee_character_rom[];
extern const int a2ee_character_rom_size;

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
