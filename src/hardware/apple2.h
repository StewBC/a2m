// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
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

enum {
    MONITOR_COLOR,
    MONITOR_MONO,
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
enum WATCH_MASK {
    WATCH_NONE             = 0,                     // Nothing to watch
    WATCH_IO_PORT          = 1 << 0,                // Call an IO callback function
    WATCH_EXEC_BREAKPOINT  = 1 << 1,                // break on execute
    WATCH_READ_BREAKPOINT  = 1 << 2,                // break on read access
    WATCH_WRITE_BREAKPOINT = 1 << 3,                // break on write access
};

// The joystick interface to runtime
enum {
    APPLE2_BUTTON_0,
    APPLE2_BUTTON_1,
    APPLE2_BUTTON_2,
};

enum {
    APPLE2_AXIS_X,
    APPLE2_AXIS_Y,
};

// Callback routines with a context
// The context is a pointer to the installer of the cb - RUNTIME normally
typedef struct CB_SOFTREAD_CTX {
    void *user;
    uint8_t(*cb_softread)(void *user, uint16_t address);
} CB_SOFTREAD_CTX;

typedef struct CB_SOFTWRITE_CTX {
    void *user;
    void (*cb_softwrite)(void *user, uint16_t address, uint8_t value);
} CB_SOFTWRITE_CTX;

typedef void (*cb_breakpoint)(void *user, uint16_t address, uint8_t mask);
typedef struct CB_BREAKPOINT_CTX {
    void *user;
    void (*cb_breakpoint)(void *user, uint16_t address, uint8_t mask);
} CB_BREAKPOINT_CTX;

typedef void (*cb_speaker)(void *user);
typedef struct CB_SPEAKER_CTX {
    void *user;
    void (*cb_speaker)(void *user);
} CB_SPEAKER_CTX;

typedef void (*cb_diskread)(void *user);
typedef void (*cb_diskwrite)(void *user);

typedef struct CB_DISKACTIVITY_CTX {
    void *user;
    void (*cb_diskread)(void *user);
    void (*cb_diskwrite)(void *user);
} CB_DISKACTIVITY_CTX;

typedef int (*cb_clipboard)(void *user);
typedef struct CB_CLIPBOARD_CTX {
    void *user;
    int (*cb_clipboard)(void *user);
} CB_CLIPBOARD_CTX;

typedef int (*cb_trace)(void *user);
typedef struct CB_TRACE_CTX {
    void *user;
    int (*cb_trace)(void *user);
} CB_TRACE_CTX;

typedef uint8_t (*cb_read_button)(void *user, int controller_id, int button_id);
typedef uint8_t (*cb_read_axis)(void *user, int controller_id, int axis_id, uint64_t cycle);
typedef void (*cb_ptrig)(void *user, uint64_t cycle);

typedef struct CB_INPUTDEVICE_CTX {
    void *user;
    cb_ptrig cb_ptrig;
    cb_read_button cb_read_button;
    cb_read_axis cb_read_axis;
} CB_INPUTDEVICE_CTX;

// Callbacks that runtime will provide to the hardware
typedef struct A2OUT_CB {
    CB_BREAKPOINT_CTX       cb_breakpoint_ctx;              // Callback when a breakpoint memory read/write triggers
    CB_BREAKPOINT_CTX       cb_brk_ctx;                     // Callback when brk instruction executed
    CB_DISKACTIVITY_CTX     cb_diskactivity_ctx;            // Callback for disk activity (show LEDs)
    CB_INPUTDEVICE_CTX      cb_inputdevice_ctx;             // Callbacks for input (joysticks)
    CB_SPEAKER_CTX          cb_speaker_ctx;                 // Callback for speaker toggles
    CB_CLIPBOARD_CTX        cb_clipboard_ctx;               // Callback for pasting from clipboard
    CB_TRACE_CTX            cb_trace_ctx;                   // Callback for writing a trace
} A2OUT_CB;

// The emulated apple2 (computer)
typedef struct APPLE2 {
    // Hardware
    CPU cpu;                                                // 6502
    PAGES pages;
    ROMS roms;                                              // All ROM in the system, may be mapped into 64K, through read_pages
    SLOT_CARDS slot_cards[8];                               // The 8 slots and their status and option ROMs
    DISKII_CONTROLLER diskii_controller[8];                 // Any slot can have a disk ii controller (and drives)
    SP_DEVICE sp_device[8];                                 // All slots could be made smartport
    FRANKLIN_DISPLAY franklin_display;                      // Franklin Display 80 col card
    A2OUT_CB a2out_cb;                                      // Apple2->external callbacks

    // Base setup
    uint32_t ram_size;
    RAM ram;                                                // Holds main, aux, lc and helper ram arrays

    uint8_t *rom_shadow_pages[(0xC800 - 0xC000) / PAGE_SIZE]; // Slot ram page mappings when SETC?ROM active
    uint8_t mapped_slot;                                    // 0 = not mapped, 1-7 means that slot card is strobe mapped (to C800)

    // A2 Status flags
    union {
        uint32_t state_flags;
        struct {
            a2_flags_def;
        };
    };

    // Trace
    UTIL_FILE trace_log;                                   // file that CPU traces get logged to
} APPLE2;

// Seems it's about 3.0 ms to get to paddel to 255, expressed as cycles
#define paddl_normalized    ((CPU_FREQUENCY / 1000.0) * 3.0)
static inline uint8_t clamp_u8(uint32_t x, uint8_t lo, uint8_t hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

uint8_t apple2_softswitch_read_callback(APPLE2 *m, uint16_t address);
void apple2_softswitch_write_callback(APPLE2 *m, uint16_t address, uint8_t value);

int apple2_init(APPLE2 *m, INI_STORE *ini_store);
void apple2_machine_reset(APPLE2 *m);
void apple2_set_callbacks(APPLE2 *m, A2OUT_CB *cbp);
void apple2_shutdown(APPLE2 *m);