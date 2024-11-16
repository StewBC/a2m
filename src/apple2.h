// Apple ][+ emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#pragma once

// Apple II sim related
// From John Brooks
// The correct CPU frequency for an NTSC Apple II is 1020484.4.
// The calc is 14,318,181 Hz crystal / 912 ticks per scanline (65 PH0 *14 ticks + 2 tick stretched clock) *65 PH0 per scanline.
#define CPU_FREQUENCY   1020484.4

// RAM
enum {
    MAIN_RAM,
    AUX_RAM,
};

// ROM
enum {
    ROM_APPLE,
    ROM_CHARACTER,
    ROM_DISKII,
    ROM_FRANKLIN_ACE_DISPLAY,
    ROM_FRANKLIN_ACE_CHARACTER,
    ROM_SMARTPORT,
    ROM_SSC,
    ROM_NUM_ROMS
};

// SLOT cards supported
enum {
    SLOT_TYPE_EMPTY,
    SLOT_TYPE_SMARTPORT,
    SLOT_TYPE_VIDEX_API,
};

// Prototypes for callbacks when cpu accesses a port
typedef uint8_t(*CALLBACK_READ) (APPLE2 *m, uint16_t address);
typedef void (*CALLBACK_WRITE)(APPLE2 *m, uint16_t address, uint8_t value);
typedef void (*CALLBACK_BREAKP)(APPLE2 *m, uint16_t address);

// The emulated apple2 (computer)
typedef struct APPLE2 {
    // Hardware
    CPU cpu;                                                // 6502
    PAGES read_pages;                                       // Up to 64K of bytes currently visible to CPU when reading
    PAGES write_pages;                                      // Up to 64K of bytes currently visible to CPU when writing
    PAGES watch_pages;                                      // Up to 64K of bytes where 0 means this is MEMORY/ROM and 1 means it is a port
    CALLBACK_READ callback_read;                            // The callback when reading from a port
    CALLBACK_WRITE callback_write;                          // The callback when writing to a port
    CALLBACK_BREAKP callback_breakpoint;                    // The callback when a breakpoint memory read/write triggers
    MEMORY ram;                                             // All MEMORY in the system (may be > 64K but up to 64K) mapped in throug pages
    MEMORY roms;                                            // All MEMORY in the system, may be mapped into 64K, through read_pages
    SLOT_CARDS slot_cards[8];                               // The 8 slots and their status and option ROMs
    SPEAKER speaker;                                        // 1 bit audio speaker
    RAM_CARD ram_card;                                      // State for which pages are visible
    SP_DEVICE sp_device[8];                                 // All slots could be made smartport
    FRANKLIN_DISPLAY franklin_display;                      // Franklin Display 80 col card

    // Base setup
    uint8_t *RAM_MAIN;                                      // The 64K MEMORY
    uint8_t *RAM_WATCH;                                     // 64K of IO port "mask" (0 = is not a port)

    // keyboard
    uint8_t open_apple;
    uint8_t closed_apple;

    // Screen State
    int screen_updated;                                     // Counter - at TARGET_FPS forces screen update
    int screen_mode;
    int monitor_type;

    // Status flags
    int original_del:1;                                     // backspace key does crsr left if 0
    int cols80active:1;                                      // Videx/Franklin Ace Display active
    int active_page:1;                                      // 0x2000 or 0x4000 - active hires bytes page
    int free_run:1;                                         // 0 - 1 Mhz, 1 - as fast as possible
    int debug_view:1;                                       // Apple ][ is not full-screen, debugger visible
    int stopped:1;                                          // Emulation is halted
    int step:1;                                             // Emulation halted but one instruction is "stepped"

    // Additiona Info
    VIEWPORT *viewport;                                     // 0 (no view) or active view for this instance
} APPLE2;

int apple2_configure(APPLE2 *m);
void apple2_ini_load_callback(void *user_data, char *section, char *key, char *value);
void apple2_shutdown(APPLE2 *m);
void apple2_slot_configure(APPLE2 *m, int slot, uint8_t type);
uint8_t apple2_softswitch_read_callback(APPLE2 *m, uint16_t address);
void apple2_softswitch_write_callback(APPLE2 *m, uint16_t address, uint8_t value);
