// Apple ][+ emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#pragma once

#define KBD             0xC000                              // Port where the Apple II reads what keys are down
#define KBDSTRB         0xC010                              // Port where the Apple II acknowledges a key press (clears it)
#define A2SPEAKER       0xC030                              // Port that toggles the speaker
#define TXTCLR          0xC050                              // Enable graphics (lores or hires)
#define TXTSET          0xC051                              // Enable text
#define MIXCLR          0xC052                              // Enable Full Screen, Disable 4 lines of text
#define MIXSET          0xC053                              // Enable Split Screen, Enable 4 lines of text
#define LOWSCR          0xC054                              // Page 1
#define HISCR           0xC055                              // Page 2
#define LORES           0xC056                              // Enable Lores graphics (Disable Hires)
#define HIRES           0xC057                              // Enable Hires graphics (Disable Lores)
#define BUTN0           0xC061                              // Open-Apple key
#define BUTN1           0xC062                              // Closed-Apple key
#define PADDL0          0xC064
#define PADDL1          0xC065
#define PADDL2          0xC066
#define PADDL3          0xC067
#define PTRIG           0xC070
// These values effectively are $C080 + $s0 + value, where s is the slot number
#define SP_DATA         4                                   // SmartPort Data Transfer Port
#define SP_STATUS       5                                   // SmartPort Status update port
