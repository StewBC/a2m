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
#define PADDL0          0xC064                              // Paddle / Joystick related
#define PADDL1          0xC065
#define PADDL2          0xC066
#define PADDL3          0xC067
#define PTRIG           0xC070

#define CLRROM          0xCFFF                              // Release C800 ROM

// These values effectively are $C080 + $s0 + value, where s is the slot number

// DISK II
#define IWM_PH0_OFF     0x0  // stepper: phase 0 OFF
#define IWM_PH0_ON      0x1  // stepper: phase 0 ON
#define IWM_PH1_OFF     0x2  // stepper: phase 1 OFF
#define IWM_PH1_ON      0x3  // stepper: phase 1 ON
#define IWM_PH2_OFF     0x4  // stepper: phase 2 OFF
#define IWM_PH2_ON      0x5  // stepper: phase 2 ON
#define IWM_PH3_OFF     0x6  // stepper: phase 3 OFF
#define IWM_PH3_ON      0x7  // stepper: phase 3 ON
#define IWM_MOTOR_OFF   0x8  // motor OFF
#define IWM_MOTOR_ON    0x9  // motor ON
#define IWM_SEL_DRIVE_1 0xA  // select drive 1  (a.k.a. unit 0) - Latch
#define IWM_SEL_DRIVE_2 0xB  // select drive 2  (a.k.a. unit 1) - Partner Latch
#define IWM_Q6_OFF      0xC  // Q6 low
#define IWM_Q6_ON       0xD  // Q6 high
#define IWM_Q7_OFF      0xE  // Q7 low (read: status/data)
#define IWM_Q7_ON       0xF  // Q7 high (read: status/data)

// SMARTPORT
#define SP_DATA         4                                   // SmartPort Data Transfer Port
#define SP_STATUS       5                                   // SmartPort Status update port

