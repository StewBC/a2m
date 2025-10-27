// Apple ][+ and //e Emhanced emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#pragma once

// Apple II sim related
// From John Brooks
// The correct CPU frequency for an NTSC Apple II is 1020484.4.
// The calc is 14,318,181 Hz crystal / 912 ticks per scanline (65 PH0 *14 ticks + 2 tick stretched clock) *65 PH0 per scanline.
// Meaning the effective average during display when the video logic stretches the bus, since
// roughly every 65 ϕ0 clocks you pay one extra 14 MHz tick (equivalently, +2 ticks per scanline),
// so the average becomes, under video contention, 14,318,181 Hz × (65 / 912) = 1,020,484.391... Hz
#define CPU_FREQUENCY   1020484.4

// Supported Apple II Models
enum {
    MODEL_APPLE_II_PLUS,
    MODEL_APPLE_IIEE,
};

// RAM
enum {
    MAIN_RAM,
    AUX_RAM,
};

// ROM
enum {
    ROM_APPLE2,
    ROM_APPLE2_SLOTS,
    ROM_APPLE2_CHARACTER,
    ROM_DISKII_13SECTOR,
    ROM_DISKII_16SECTOR,
    ROM_FRANKLIN_ACE_DISPLAY,
    ROM_FRANKLIN_ACE_CHARACTER,
    ROM_SMARTPORT,
    ROM_SSC,
    ROM_NUM_ROMS
};

// SLOT cards supported
enum {
    SLOT_TYPE_EMPTY,
    SLOT_TYPE_DISKII,
    SLOT_TYPE_SMARTPORT,
    SLOT_TYPE_VIDEX_API,
};

// The mask for the bits in the RAM_WATCH array 
enum RAM_WATCH_MASK {
    WATCH_NONE             = 0,                             // Nothing to watch
    WATCH_IO_PORT          = 1<<0,                          // Call an IO callback function
    WATCH_READ_BREAKPOINT  = 1<<1,                          // When !use_pc
    WATCH_WRITE_BREAKPOINT = 1<<2,                          // When !use_pc
};

// Prototypes for callbacks when cpu accesses a port
typedef uint8_t(*CALLBACK_READ)(APPLE2 *m, uint16_t address);
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
    RAM_CARD ram_card[2];                                   // State for which pages are visible
    DISKII_CONTROLLER diskii_controller[8];                 // Any slot can have a disk ii controller (and drives)
    SP_DEVICE sp_device[8];                                 // All slots could be made smartport
    FRANKLIN_DISPLAY franklin_display;                      // Franklin Display 80 col card

    // Base setup
    uint32_t ram_size;                                      // How much ram this machine has
    uint8_t *RAM_MAIN;                                      // The ram_size MEMORY - addressable in max 64k chunks
    uint8_t *RAM_WATCH;                                     // 64K of IO port "mask" (0 = is not a port). See RAM_WATCH_MASK
    uint8_t *rom_shadow_pages[(0xC800-0xC000)/PAGE_SIZE];   // Slot ram page mappings when SETC?ROM active
    uint8_t mapped_slot;                                    // 0 = not mapped, 1-7 means that slot card is strobe mapped (to C800)

    // keyboard
    uint8_t open_apple;
    uint8_t closed_apple;

    // Screen State
    uint8_t screen_mode;                                    // lores, text hgr, etc. See viewapl2_screen_apple2
    uint8_t monitor_type;                                   // 0 = color; 1 = mono

    // Status flags
    uint32_t altcharset: 1;                                 // 1 = mousetext
    uint32_t altzpset: 1;                                   // 1 = 0x0000 - 0x0200 in aux
    uint32_t c3slotrom: 1;                                  // 1 = C300-C3FF slot card rom (not internal //e S3 ROM)
    uint32_t col80set: 1;                                   // 1 = 80 col display active
    uint32_t cxromset: 1;                                   // 1 = 0xc100 - cfff - from rom
    uint32_t debug_view: 1;                                 // Apple ][ is not full-screen, debugger visible
    uint32_t disk_activity_read: 1;                         // 0 = no read/write (here for convenience)
    uint32_t disk_activity_write: 1;                        // 0 = no read/write
    uint32_t franklin80active: 1;                           // Videx/Franklin Ace Display active
    uint32_t ioudclr :1;                                    // reverse of normal - ioudclr = 1, ioudset = 0
    uint32_t model: 1;                                      // (0) II+ or (1) //e
    uint32_t original_del: 1;                               // backspace key does crsr left if 0
    uint32_t page2set: 1;                                   // 1 = 0x0800 text or 0x4000 HGR
    uint32_t ramrdset: 1;                                   // 1 = 0x0200 - 0xbfff (with 80store excptions) read from aux
    uint32_t ramwrtset: 1;                                  // 1 = 0x0200 - 0xbfff (with 80store excptions) write to aux
    uint32_t step: 1;                                       // Emulation halted but one instruction is "stepped"
    uint32_t stopped: 1;                                    // Emulation is halted
    uint32_t store80set: 1;                                 // 1 - Page 2 text, and if hgr also 4000-6000, mapped from aux
    uint32_t strobed: 1;                                    // 1 - C800-CFFF is mapped, 0 - it is floating bus

    // Configuration
    INI_STORE ini_store;

    // Turbo (1MHz+ settings)
    double turbo_active;                                     // turbo[turbo_index]
    uint32_t turbo_index;                                   // active entry in turbo array
    uint32_t turbo_count;                                   // entries in turbo array
    double *turbo;                                           // Array of multipliers (* 1MHz) to run at

    // Clipboard
    char *clipboard_text;                                   // when not 0 points at a clipboard text UTF-8
    size_t clipboard_index;                                 // index into clipboard_text, where to paste from next

    // Additional Info
    VIEWPORT *viewport;                                     // 0 (no view) or active view for this instance

} APPLE2;

// Seems it's about 3.0 ms to get to paddel to 255, expressed as cycles
#define paddl_normalized    ((CPU_FREQUENCY / 1000.0) * 3.0)
static inline uint8_t clamp_u8(uint32_t x, uint8_t lo, uint8_t hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

int apple2_configure(APPLE2 *m);
void apple2_machine_setup(APPLE2 *m);
void apple2_slot_setup(APPLE2 *m);
void apple2_shutdown(APPLE2 *m);
void apple2_slot_configure(APPLE2 *m, int slot, uint8_t type);
uint8_t apple2_softswitch_read_callback(APPLE2 *m, uint16_t address);
void apple2_softswitch_write_callback(APPLE2 *m, uint16_t address, uint8_t value);
