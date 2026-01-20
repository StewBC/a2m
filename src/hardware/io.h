#pragma once

enum {
    KBD             = 0xC000, // Port where the Apple II reads what keys are down
    CLR80STORE      = 0xC000, // Write only, RAMRD/WRT decides $0400-$0BFF, $2000-$3FFF
    SET80STORE      = 0xC001, // ($C054/$C055) selects main vs aux independently of RAMRD/WRT for display mem
    CLRRAMRD        = 0xC002, // CPU reads  from main $0200-$BFFF and LC $D000-$FFFF, see SET80STORE
    SETRAMRD        = 0xC003, // CPU reads  from aux  $0200-$BFFF and LC $D000-$FFFF, see SET80STORE
    CLRRAMWRT       = 0xC004, // CPU writes from main $0200-$BFFF and LC $D000-$FFFF, see SET80STORE
    SETRAMWRT       = 0xC005, // CPU writes from aux  $0200-$BFFF and LC $D000-$FFFF, see SET80STORE
    CLRCXROM        = 0xC006, // C100-C7FF slot card roms
    SETCXROM        = 0xC007, // C100-CFFF //e rom (overrides c3rom and cfff)
    CLRALTZP        = 0xC008, // ZP/stack are in main bank
    SETALTZP        = 0xC009, // ZP/stack are in aux  bank
    CLRC3ROM        = 0xC00A, // Turn internal rom on,  turn Slot rom off
    SETC3ROM        = 0xC00B, // Turn internal rom off, turn Slot rom on
    CLR80COL        = 0xC00C, // 40-column display (turn 80-col display off)
    SET80COL        = 0xC00D, // 80-column display (turn 80-col display on)
    CLRALTCHAR      = 0xC00E, // primary/standard character set
    SETALTCHAR      = 0xC00F, // alternate character set
    KBDSTRB         = 0xC010, // Port where the Apple II acknowledges a key press (clears it)
    HRAMRD          = 0xC011, // 1 = HRAMRD // SQW - need to understand
    HRAMWRT         = 0xC012, // 1 = HRAMWRT // SQW - need to understand
    RDRAMRD         = 0xC013, // 1 = RAMRD on (reads from aux), 0 = off
    RDRAMWRT        = 0xC014, // 1 = RAMWRT on (writes to aux), 0 = off
    RDCXROM         = 0xC015,
    RDALTZP         = 0xC016, // 1 = ALTZP on (ZP/stack in aux), 0 = off
    RDC3ROM         = 0xC017,
    RD80STORE       = 0xC018,
    RDVBL           = 0xC019,
    RDTEXT          = 0xC01A, // 1 = TEXT mode, 0 = graphics
    RDMIXED         = 0xC01B, // 1 = MIXED (split screen) on, 0 = off
    RDPAGE2         = 0xC01C,
    RDHIRES         = 0xC01D,
    RDALTCHAR       = 0xC01E,
    RD80COL         = 0xC01F,
    A2SPEAKER       = 0xC030, // Port that toggles the speaker
    CYAREG          = 0xC036, // Bits 0-3 disk detect; 4 shadow all banks; 7 fast
    RDVBLMSK        = 0xC041, // 128 if VBL interrupts enabled
    TXTCLR          = 0xC050, // Enable graphics (lores or hires)
    TXTSET          = 0xC051, // Enable text
    MIXCLR          = 0xC052, // Enable Full Screen, Disable 4 lines of text
    MIXSET          = 0xC053, // Enable Split Screen, Enable 4 lines of text
    CLRPAGE2        = 0xC054, // Page 1
    SETPAGE2        = 0xC055, // Page 2
    CLRHIRES        = 0xC056, // Enable Lores graphics (Disable Hires)
    SETHIRES        = 0xC057, // Enable Hires graphics (Disable Lores)
    CLRAN0          = 0xC058,
    SETAN0          = 0xC059,
    CLRAN1          = 0xC05A, // Disable VBL?
    SETAN1          = 0xC05B, // Enable VBL?
    CLRAN2          = 0xC05C,
    SETAN2          = 0xC05D,
    CLRAN3          = 0xC05E, // Enable double-width graphics (clr single gr)
    SETAN3          = 0xC05F, // Disable double-width graphics (set single gr)
    TAPEIN          = 0xC060, // Read casette input / Switch input 3
    BUTN0           = 0xC061, // Open-Apple key
    BUTN1           = 0xC062, // Closed-Apple key
    BUTN2           = 0xC063,
    PADDL0          = 0xC064, // Paddle / Joystick related
    PADDL1          = 0xC065,
    PADDL2          = 0xC066,
    PADDL3          = 0xC067,
    PTRIG           = 0xC070,
    RDWCLRIOUD      = 0xC07E, // Read = IOUD state, Write = Disable IOU - //c only, I think
    RDHGR_WSETIOUD  = 0xC07F, // Read = DHGR, Write = Enable IOU - //c only, I think
    ROMIN           = 0xC081, // Swap in D000-FFFF ROM
    LCBANK2         = 0xC083, // Swap in LC bank 2
    LCBANK1         = 0xC08B, // Swap in LC bank 1
    CLRROM          = 0xCFFF, // Release C800 ROM
} ;
// These values effectively are $C080 + $s0 + value, where s is the slot number

// DISK II
enum {
    IWM_PH0_OFF     = 0x0,    // stepper: phase 0 OFF
    IWM_PH0_ON      = 0x1,    // stepper: phase 0 ON
    IWM_PH1_OFF     = 0x2,    // stepper: phase 1 OFF
    IWM_PH1_ON      = 0x3,    // stepper: phase 1 ON
    IWM_PH2_OFF     = 0x4,    // stepper: phase 2 OFF
    IWM_PH2_ON      = 0x5,    // stepper: phase 2 ON
    IWM_PH3_OFF     = 0x6,    // stepper: phase 3 OFF
    IWM_PH3_ON      = 0x7,    // stepper: phase 3 ON
    IWM_MOTOR_OFF   = 0x8,    // motor OFF
    IWM_MOTOR_ON    = 0x9,    // motor ON
    IWM_SEL_DRIVE_1 = 0xA,    // select drive 1  (a.k.a. unit 0) - Latch
    IWM_SEL_DRIVE_2 = 0xB,    // select drive 2  (a.k.a. unit 1) - Partner Latch
    IWM_Q6_OFF      = 0xC,    // Q6 low
    IWM_Q6_ON       = 0xD,    // Q6 high
    IWM_Q7_OFF      = 0xE,    // Q7 low (read: status/data)
    IWM_Q7_ON       = 0xF,    // Q7 high (read: status/data)
};

// SMARTPORT
enum {
    SP_DATA         = 0x4,    // SmartPort Data Transfer Port
    SP_STATUS       = 0x5,    // SmartPort Status update port
};

typedef uint8_t (*ss_read)(APPLE2 *m, uint16_t a);
typedef void (*ss_write)(APPLE2 *m, uint16_t a, uint8_t v);

typedef struct A2_C0_TABLE {
    ss_read r[256];
    ss_write w[256];
} A2_C0_TABLE;

extern A2_C0_TABLE *io_c0_machine_table;

void io_c0_table_init(void);
uint8_t io_callback_r(APPLE2 *m, uint16_t address);
void io_callback_w(APPLE2 *m, uint16_t address, uint8_t value);
void io_setup(APPLE2 *m);
