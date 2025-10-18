// Apple ][+ and //e Emhanced emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#pragma once

#define KBD             0xC000                              // Port where the Apple II reads what keys are down
#define CLR80STORE      0xC000 // Write only, RAMRD/WRT decides $0400–$0BFF, $2000–$3FFF, $4000–$5FFF access
#define SET80STORE      0xC001 // ($C054/$C055) selects main vs aux independently of RAMRD/WRT for display mem
#define CLRRAMRD        0xC002 // CPU reads  from main $0200–$BFFF and LC $D000–$FFFF, see SET80STORE
#define SETRAMRD        0xC003 // CPU reads  from aux  $0200–$BFFF and LC $D000–$FFFF, see SET80STORE
#define CLRRAMWRT       0xC004 // CPU writes from main $0200–$BFFF and LC $D000–$FFFF, see SET80STORE
#define SETRAMWRT       0xC005 // CPU writes from aux  $0200–$BFFF and LC $D000–$FFFF, see SET80STORE
#define CLRCXROM        0xC006 // C100-C7FF slot card roms
#define SETCXROM        0xC007 // C100-C7FF //e       rom
#define CLRALTZP        0xC008 // ZP/stack are in main bank
#define SETALTZP        0xC009 // ZP/stack are in aux  bank
#define CLRC3ROM        0xC00A // C300-C3FF slot card rom
#define SETC3ROM        0xC00B // C300-C3FF //e       rom
#define CLR80VID        0xC00C // 40-column display (turn 80-col display off)
#define SET80VID        0xC00D // 80-column display (turn 80-col display on)
#define CLRALTCHAR      0xC00E // primary/standard character set
#define SETALTCHAR      0xC00F // alternate character set
#define KBDSTRB         0xC010                              // Port where the Apple II acknowledges a key press (clears it)
#define RDRAMRD         0xC011 // 1 = RAMRD on (reads from aux), 0 = off
#define RDRAMWRT        0xC012 // 1 = RAMWRT on (writes to aux), 0 = off
#define RDCXROM         0xC015
#define RDALTZP         0xC016 // 1 = ALTZP on (ZP/stack in aux), 0 = off
#define RDC3ROM         0xc017
#define RD80COL         0xC018
#define RDTEXT          0xC01A // 1 = TEXT mode, 0 = graphics
#define RDMIXED         0xC01B // 1 = MIXED (split screen) on, 0 = off
#define RDPAGE2         0xC01C
#define A2SPEAKER       0xC030                              // Port that toggles the speaker
#define TXTCLR          0xC050                              // Enable graphics (lores or hires)
#define TXTSET          0xC051                              // Enable text
#define MIXCLR          0xC052                              // Enable Full Screen, Disable 4 lines of text
#define MIXSET          0xC053                              // Enable Split Screen, Enable 4 lines of text
#define LOWSCR          0xC054                              // Page 1
#define HISCR           0xC055                              // Page 2
#define LORES           0xC056                              // Enable Lores graphics (Disable Hires)
#define HIRES           0xC057                              // Enable Hires graphics (Disable Lores)
#define SETAN0          0xC058
#define SETAN1          0xC05A
#define CLRAN2          0xC05D
#define CLRAN3          0xC05F
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

