#pragma once

#include "cpu65.h"
#include "diskii.h"
#include "mboard.h"
#include "memview.h"
#include "smrtprt.h"
#include "softswitch.h"
#include "video.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* NTSC Apple II effective CPU frequency. */
#define APPLE2_CPU_FREQUENCY_HZ 1020484.4

#define APPLE2_PAGE_SIZE 256u
#define APPLE2_NUM_PAGES 256u
#define APPLE2_ADDR_SPACE (APPLE2_PAGE_SIZE * APPLE2_NUM_PAGES)
/* Main + aux (//e); ][+ only uses first 64K. */
#define APPLE2_RAM_MAIN_SIZE (128u * 1024u)
/* 16K LC × 2 (main/aux) = 32K as in a2m. */
#define APPLE2_RAM_LC_SIZE (32u * 1024u)

typedef enum {
    APPLE2_MODEL_II_PLUS = 0,
    APPLE2_MODEL_IIE_ENHANCED = 1
} apple2_model;

typedef enum {
    SLOT_TYPE_EMPTY = 0,
    SLOT_TYPE_DISKII,
    SLOT_TYPE_SMARTPORT,
    SLOT_TYPE_MOCKINGBOARD
} apple2_slot_type;

typedef enum apple2_memory_access_type {
    APPLE2_MEMORY_ACCESS_READ = 0,
    APPLE2_MEMORY_ACCESS_WRITE
} apple2_memory_access_type;

typedef void (*apple2_memory_access_fn)(
    void *user,
    apple2_memory_access_type access,
    uint16_t address,
    uint8_t value);

/* Flight-recorder / forensic observer (instruction timeline). Values of
   kind match runtime_history_record_kind (instruction=0, irq=1, nmi=2). */
typedef enum apple2_cpu_observer_record_kind {
    APPLE2_CPU_OBSERVER_INSTRUCTION = 0,
    APPLE2_CPU_OBSERVER_IRQ = 1,
    APPLE2_CPU_OBSERVER_NMI = 2
} apple2_cpu_observer_record_kind;

typedef struct apple2_cpu_observer_begin {
    apple2_cpu_observer_record_kind kind;
    uint64_t machine_cycle;
    uint16_t pc;
    uint8_t a;
    uint8_t x;
    uint8_t y;
    uint8_t sp;
    uint8_t p;
} apple2_cpu_observer_begin;

typedef struct apple2_cpu_observer {
    void (*begin)(void *user, const apple2_cpu_observer_begin *begin);
    void (*access)(
        void *user,
        uint64_t machine_cycle,
        uint16_t address,
        uint8_t value,
        cpu65_bus_access_kind kind);
    void (*complete)(void *user);
} apple2_cpu_observer;

typedef struct apple2_pages {
    uint16_t num_pages;
    uint8_t **read_pages;
    uint8_t **write_pages;
} apple2_pages;

typedef struct apple2 {
    cpu65_t cpu;
    apple2_model model;
    apple2_pages pages;

    uint8_t *ram_main;   /* APPLE2_RAM_MAIN_SIZE */
    uint8_t *ram_lc;     /* APPLE2_RAM_LC_SIZE */
    uint8_t *rom_sink;   /* 12K bit-bucket for writes while ROM is mapped */

    /* ROM images (not owned if pointing into rom_data statics). */
    uint8_t *rom_d000;   /* $D000-$FFFF system ROM (12K) */
    uint8_t *rom_c000;   /* //e $C000-$CFFF internal (16K image; C000 unused) */
    uint8_t *rom_char;   /* character generator */
    size_t rom_d000_size;
    size_t rom_c000_size;
    size_t rom_char_size;

    uint32_t state_flags;
    uint8_t key_held;
    int strobed_slot; /* -1 none, 8 internal C800, 1..7 slot */

    /* Shadow of C100-C7FF read page pointers for CXROM off restore. */
    uint8_t *rom_shadow_pages[8];

    bool ready;
    bool instruction_complete;
    bool speaker_level; /* toggled by $C030; mixed with MB later */

    apple2_video video;

    /* Slot cards (1..7 used; [0] unused). */
    apple2_slot_type slot_type[8];

    /* Disk II: typically slot 6. */
    DISKII_CONTROLLER diskii_controller[8];
    uint8_t diskii_present[8];
    uint8_t diskii_rom_bytes[8][256];

    /* SmartPort block devices (any slot). */
    SP_DEVICE sp_device[8];
    uint8_t smartport_rom_bytes[8][256];

    /* Mockingboard (any slot; mb_slot is primary IRQ source, 0=none). */
    MOCKINGBOARD mockingboard[8];
    uint8_t mb_slot;

    /* Game port: 4 paddles (2 sticks × X/Y) + 3 buttons. Axes are Apple paddle
       units 0..255 (mid=128). button_mask bit0=BUTN0, bit1=BUTN1, bit2=BUTN2.
       Softswitch ORs these with Open/Closed Apple for BUTN0/BUTN1. */
    uint8_t gameport_axis[4];
    uint8_t gameport_buttons;
    uint64_t gameport_ptrig_cycle;

    /* Host clipboard paste: next char is latched on each $C010 (KBDSTRB). */
    char *paste_text;       /* owned NUL-terminated copy; NULL when idle */
    size_t paste_index;

    /*
     * Optional live bus observer (R/W breakpoints / future tools).
     * Invoked only from apple2_bus_read/write (CPU path), not debug_read/write.
     */
    apple2_memory_access_fn memory_access;
    void *memory_access_user;

    /* Optional CPU flight-recorder observer (begin / access / complete). */
    apple2_cpu_observer cpu_observer;
    void *cpu_observer_user;

    /* Last-writer PC pack per logical address (debugger annotation, not BP).
       Each write shifts prior PCs left 16 and ORs the current opcode_pc. */
    uint64_t *write_history; /* 65536 entries when allocated */

    /* Machine-local PRNG (Disk II weak bits / mount jitter). Seeded at init;
       saved/restored with the snapshot. */
    uint32_t prng;

    /* Sealed replay: drop host media write-through and HostFS refresh. */
    bool replay_sealed;

    /* Inspector: guest media write / host-directory change (D10). */
    void (*media_event)(void *user, uint64_t cycle, int slot, int device, int kind);
    void *media_event_user;

    /* Inspector input log (host key / gameport). */
    void (*input_event)(
        void *user, uint64_t cycle, int kind, uint32_t a, uint32_t b, uint32_t c);
    void *input_event_user;
} apple2_t;

enum {
    APPLE2_MEDIA_EVENT_GUEST_WRITE = 1,
    APPLE2_MEDIA_EVENT_HOST_DIRECTORY = 2
};

enum {
    APPLE2_INPUT_KEY = 1,
    APPLE2_INPUT_GAMEPORT_AXIS = 2,
    APPLE2_INPUT_GAMEPORT_BUTTONS = 3
};

uint32_t apple2_rand_u32(apple2_t *machine);
void apple2_set_replay_sealed(apple2_t *machine, bool sealed);
void apple2_set_media_event_callback(
    apple2_t *machine,
    void (*callback)(void *user, uint64_t cycle, int slot, int device, int kind),
    void *user);
void apple2_set_input_event_callback(
    apple2_t *machine,
    void (*callback)(
        void *user, uint64_t cycle, int kind, uint32_t a, uint32_t b, uint32_t c),
    void *user);
void apple2_note_media_event(apple2_t *machine, int slot, int device, int kind);

bool apple2_init(apple2_t *machine);
void apple2_shutdown(apple2_t *machine);
void apple2_set_memory_access_callback(
    apple2_t *machine,
    apple2_memory_access_fn callback,
    void *user);
void apple2_set_cpu_observer(
    apple2_t *machine,
    const apple2_cpu_observer *observer,
    void *user);

/* Last writer PC history for address (0 if none / unallocated). */
uint64_t apple2_debug_read_write_history(const apple2_t *machine, uint16_t address);
/* Warm reset: Apple CTRL+RESET. Does not force power-on cold start. */
void apple2_reset(apple2_t *machine);
/* Cold reset: CTRL+Open-Apple+RESET — power-on style start (banner/boot). */
void apple2_cold_reset(apple2_t *machine);

void apple2_set_cpu_class(apple2_t *machine, uint32_t cpu_class);
void apple2_set_model(apple2_t *machine, apple2_model model);

/* Map helpers (host buffer must outlive mapping). */
void apple2_map_read_host(apple2_t *machine, uint16_t cpu_addr, uint32_t length, uint8_t *host);
void apple2_map_write_host(apple2_t *machine, uint16_t cpu_addr, uint32_t length, uint8_t *host);
void apple2_map_ram_offset(apple2_t *machine, bool for_write, uint32_t host_offset, uint32_t length);

uint8_t apple2_debug_read(const apple2_t *machine, uint16_t address);
void apple2_debug_write(apple2_t *machine, uint16_t address, uint8_t value);

/* Heuristic call stack from page-1 words above SP (legacy a2m Misc view).
 * A stacked return address R counts as a JSR frame when mem[R-2]==$20;
 * jsr_address=R-2 and dest_address=word at R-1. Returns entry count. */
enum { APPLE2_CALL_STACK_MAX = 16 };
typedef struct apple2_call_stack_entry {
    uint16_t jsr_address;
    uint16_t dest_address;
} apple2_call_stack_entry;
uint8_t apple2_debug_call_stack(
    const apple2_t *machine,
    apple2_call_stack_entry *out,
    uint8_t max_entries);

/* Debug R/W with a2m VIEW_FLAGS banking (Map / Main / Aux / LC / ROM). */
uint8_t apple2_read_in_view(const apple2_t *machine, view_flags_t vf, uint16_t address);
void apple2_write_in_view(apple2_t *machine, view_flags_t vf, uint16_t address, uint8_t value);

void apple2_load(apple2_t *machine, uint16_t address, const uint8_t *bytes, size_t length);

void apple2_set_key(apple2_t *machine, uint8_t key_with_strobe);

size_t apple2_step_instruction(apple2_t *machine);
/*
 * Max free-run: complete one instruction (or finish current micro) with no
 * per-Φ0 beam paint. Advances A-lite H/V/VBL and peripherals once by Φ0 ran.
 * Returns Φ0 executed.
 */
size_t apple2_step_instruction_max(apple2_t *machine);
bool apple2_step_cycle(apple2_t *machine);
bool apple2_step_cycles(apple2_t *machine, uint32_t count, uint32_t *out_ran);

uint64_t apple2_cycles(const apple2_t *machine);
uint32_t apple2_state_flags(const apple2_t *machine);

void apple2_pages_map_ram(apple2_t *m, bool for_write, uint32_t host_offset, uint32_t length);
void apple2_pages_map_rom(apple2_t *m, uint16_t cpu_addr, uint32_t length, uint8_t *rom_bytes);
void apple2_pages_map_lc(apple2_t *m, bool for_write, uint16_t cpu_addr, uint32_t length, uint32_t lc_offset);

/* Disk II */
void apple2_install_diskii_rom(apple2_t *m, int slot, int encoding);
bool apple2_attach_diskii(apple2_t *m, int slot);
/* Append path to the drive's multi-image queue and make it active. */
int apple2_disk_mount(apple2_t *m, int slot, int drive, const char *path);
int apple2_disk_eject(apple2_t *m, int slot, int drive);
/* Select active image by 0-based index in the drive queue. */
int apple2_disk_select_image(apple2_t *m, int slot, int drive, int index);
/*
 * Step multi-image queue on a drive (default card: slot 6, drive 0/1).
 * relative: step by param (wrap). absolute: 1-based index (wrap).
 * param 0 is treated as relative +1 (next floppy).
 */
int apple2_disk_swap(
    apple2_t *m,
    int slot,
    int drive,
    int32_t param,
    bool relative);
/* Remote/host write-protect notch: writable=true clears protect. */
int apple2_disk_set_writable(apple2_t *m, int slot, int drive, bool writable);

/* Mockingboard (default slot 4). */
bool apple2_attach_mockingboard(apple2_t *m, int slot);
void apple2_detach_slot_card(apple2_t *m, int slot);

/* SmartPort block device. */
bool apple2_attach_smartport(apple2_t *m, int slot);
int apple2_smartport_mount(apple2_t *m, int slot, int device, const char *path);
int apple2_smartport_eject(apple2_t *m, int slot, int device);
bool apple2_flush_media(apple2_t *m);

/* Per-cycle peripheral advance (VIA timers + AY queue). */
void apple2_peripherals_step(apple2_t *m, uint32_t cycles);

/* Game port (paddles + buttons). Axes 0..3 = PDL0..PDL3; values 0..255. */
enum {
    APPLE2_GAMEPORT_BUTTON0 = 0x01u,
    APPLE2_GAMEPORT_BUTTON1 = 0x02u,
    APPLE2_GAMEPORT_BUTTON2 = 0x04u
};

void apple2_gameport_set_axis(apple2_t *m, int axis /*0..3*/, uint8_t value);
void apple2_gameport_set_axes(apple2_t *m, const uint8_t axis[4]);
void apple2_gameport_set_buttons(apple2_t *m, uint8_t mask);
/* Latch PTRIG start time (also done via softswitch $C070). */
void apple2_gameport_ptrig(apple2_t *m);

/*
 * Clipboard paste into $C000 (a2m-style).
 * Copies text, latches the first key immediately; further keys are latched when
 * software hits KBDSTRB ($C010). ][+ uppercases printable ASCII; //e keeps case.
 * Newlines (LF / CR / CRLF) → Return ($0D); TAB → space; UTF-8 multi-byte dropped.
 */
bool apple2_paste_begin(apple2_t *m, const char *text, size_t length);
void apple2_paste_cancel(apple2_t *m);
bool apple2_paste_active(const apple2_t *m);
/* Called from softswitch KBDSTRB: true if a new key was latched (strobe stays). */
bool apple2_paste_on_kbdstrb(apple2_t *m);
