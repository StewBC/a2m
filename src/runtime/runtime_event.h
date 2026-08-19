#pragma once

#include "memview.h"
#include "runtime_breakpoint_condition.h"
#include "runtime_history.h"

#include <stdbool.h>
#include <stdint.h>

/* 6502 address space size (Apple II main map). Used by debug memory windows. */
enum { MACHINE_ADDRESS_SPACE = 65536 };

typedef enum runtime_slot_card_type {
    RUNTIME_SLOT_CARD_EMPTY = 0,
    RUNTIME_SLOT_CARD_DISKII,
    RUNTIME_SLOT_CARD_SMARTPORT,
    RUNTIME_SLOT_CARD_MOCKINGBOARD
} runtime_slot_card_type;

enum {
    RUNTIME_APPLE_SLOT_COUNT = 8,
    RUNTIME_SLOT_DEVICE_COUNT = 2,
    RUNTIME_MEDIA_DISPLAY_NAME_MAX = 256
};

typedef struct runtime_machine_config {
    bool pause_on_brk; /* reserved */
    uint8_t apple_model; /* 0 = //e Enhanced, 1 = ][+ */
    runtime_slot_card_type slot_cards[RUNTIME_APPLE_SLOT_COUNT];
} runtime_machine_config;

typedef struct runtime_media_device_snapshot {
    uint8_t mounted;
    uint8_t writable;
    uint8_t dirty;
    uint16_t queue_index; /* 0-based; 0 when empty */
    uint16_t queue_count;
    char display_name[RUNTIME_MEDIA_DISPLAY_NAME_MAX];
} runtime_media_device_snapshot;

typedef struct runtime_slot_snapshot {
    runtime_slot_card_type card_type;
    runtime_media_device_snapshot devices[RUNTIME_SLOT_DEVICE_COUNT];
} runtime_slot_snapshot;

/* Media mount result codes (product wire; not 1541-specific). */
typedef enum runtime_media_result {
    RUNTIME_MEDIA_OK = 0,
    RUNTIME_MEDIA_IO_ERROR,
    RUNTIME_MEDIA_UNSUPPORTED,
    RUNTIME_MEDIA_OTHER_ERROR
} runtime_media_result;


typedef enum runtime_event_type {
    RUNTIME_EVENT_NONE = 0,
    RUNTIME_EVENT_PONG,
    RUNTIME_EVENT_STARTED,
    RUNTIME_EVENT_RUNNING,
    RUNTIME_EVENT_PAUSED,
    RUNTIME_EVENT_STOPPED,
    RUNTIME_EVENT_ERROR,
    RUNTIME_EVENT_RESET_COMPLETE,
    RUNTIME_EVENT_STEP_COMPLETE,
    RUNTIME_EVENT_RUN_COMPLETE,
    RUNTIME_EVENT_CPU_STATE_RESPONSE,
    RUNTIME_EVENT_MACHINE_STATE_RESPONSE,
    RUNTIME_EVENT_MEMORY_RESPONSE,
    /* Solicited bulk/control get-memory: meta only; payload in RPC pool by token. */
    RUNTIME_EVENT_MEMORY_RPC_COMPLETE,
    RUNTIME_EVENT_MEMORY_VIEW_RESPONSE,
    RUNTIME_EVENT_BREAKPOINTS_RESPONSE,
    RUNTIME_EVENT_DISK_STATUS_RESPONSE,
    RUNTIME_EVENT_ASSEMBLE_COMPLETE,
    RUNTIME_EVENT_ASSEMBLE_ERROR,
    RUNTIME_EVENT_FRAME_READY,
    RUNTIME_EVENT_CALL_STACK_RESPONSE,
    RUNTIME_EVENT_DISK_SWAP,
    RUNTIME_EVENT_DEBUG_MEMORY_READY,
    RUNTIME_EVENT_SAVE_STATE_COMPLETE,
    RUNTIME_EVENT_LOAD_STATE_COMPLETE,
    RUNTIME_EVENT_HISTORY_STATUS_RESPONSE,
    RUNTIME_EVENT_HISTORY_RESULT_RESPONSE,
    RUNTIME_EVENT_MEDIA_CHANGED
} runtime_event_type;

typedef enum runtime_media_change_type {
    RUNTIME_MEDIA_CHANGE_INSERT = 0,
    RUNTIME_MEDIA_CHANGE_EJECT,
    RUNTIME_MEDIA_CHANGE_SWAP
} runtime_media_change_type;

/* Apple II memory areas (VIEW_FLAGS presets).
 * Values: 0 Map, 1 Main, 2 ROM, 3 Aux, 4 LC1, 5 LC2
 */
typedef enum runtime_memory_mode {
    RUNTIME_MEMORY_MODE_MAP = 0,  /* CPU soft-switch reality */
    RUNTIME_MEMORY_MODE_MAIN = 1, /* Main 48K */
    RUNTIME_MEMORY_MODE_ROM = 2,  /* System ROM */
    RUNTIME_MEMORY_MODE_AUX = 3,  /* Aux 48K (//e) */
    RUNTIME_MEMORY_MODE_LC1 = 4,  /* Language card bank 1 */
    RUNTIME_MEMORY_MODE_LC2 = 5   /* Language card bank 2 */
} runtime_memory_mode;

/* Legacy aliases (control wire / older names). */
enum {
    RUNTIME_MEMORY_MODE_CPU_MAP = RUNTIME_MEMORY_MODE_MAP,
    RUNTIME_MEMORY_MODE_RAM = RUNTIME_MEMORY_MODE_MAIN,
    RUNTIME_MEMORY_MODE_DRIVE8_MAP = RUNTIME_MEMORY_MODE_AUX,
    RUNTIME_MEMORY_MODE_DRIVE9_MAP = RUNTIME_MEMORY_MODE_LC1
};

typedef enum runtime_stop_reason {
    RUNTIME_STOP_REASON_NONE = 0,
    RUNTIME_STOP_REASON_RESET,
    RUNTIME_STOP_REASON_PAUSE_COMMAND,
    RUNTIME_STOP_REASON_STEP,
    RUNTIME_STOP_REASON_RUN_COMPLETE,
    RUNTIME_STOP_REASON_BREAKPOINT,
    RUNTIME_STOP_REASON_BRK,
    RUNTIME_STOP_REASON_ERROR
} runtime_stop_reason;

enum {
    RUNTIME_MEMORY_SNAPSHOT_MAX = 1024,
    /* Full 16-bit address space dump in one get-memory RPC. */
    RUNTIME_MEMORY_RPC_MAX_LENGTH = 65536,
    RUNTIME_RPC_PAYLOAD_POOL_CAPACITY = 16,
    RUNTIME_RPC_MEMORY_POOL_CAPACITY =
        RUNTIME_RPC_PAYLOAD_POOL_CAPACITY,
    RUNTIME_BREAKPOINT_SNAPSHOT_MAX = 64,
    RUNTIME_CALL_STACK_MAX = 16,
    RUNTIME_BREAKPOINT_TRON_PATH_MAX = 256,
    RUNTIME_BREAKPOINT_TYPE_TEXT_MAX = 256
};

typedef enum runtime_memory_rpc_status {
    RUNTIME_MEMORY_RPC_OK = 0,
    RUNTIME_MEMORY_RPC_BUSY = 1,
    RUNTIME_MEMORY_RPC_BAD_ARGS = 2,
    RUNTIME_MEMORY_RPC_ERROR = 3
} runtime_memory_rpc_status;

typedef enum runtime_history_rpc_status {
    RUNTIME_HISTORY_RPC_OK = 0,
    RUNTIME_HISTORY_RPC_UNAVAILABLE,
    RUNTIME_HISTORY_RPC_MACHINE_RUNNING,
    RUNTIME_HISTORY_RPC_REQUEST_ACTIVE,
    RUNTIME_HISTORY_RPC_BAD_ARGS,
    RUNTIME_HISTORY_RPC_CURSOR_STALE,
    RUNTIME_HISTORY_RPC_EPOCH_MISMATCH,
    RUNTIME_HISTORY_RPC_RECORD_NOT_RETAINED,
    RUNTIME_HISTORY_RPC_ERROR
} runtime_history_rpc_status;

typedef struct runtime_history_rpc_meta {
    runtime_history_rpc_status status;
    uint32_t byte_length;
    uint64_t epoch;
    uint32_t count;
    uint64_t cursor;
    uint64_t oldest;
    uint64_t newest;
    uint8_t more;
} runtime_history_rpc_meta;

typedef enum runtime_breakpoint_access {
    RUNTIME_BREAKPOINT_ACCESS_EXECUTE = 1u << 0,
    RUNTIME_BREAKPOINT_ACCESS_READ = 1u << 1,
    RUNTIME_BREAKPOINT_ACCESS_WRITE = 1u << 2
} runtime_breakpoint_access;

/* Same composite Apple memory view used by the Memory window. */
typedef view_flags_t runtime_breakpoint_mapping;

typedef enum runtime_breakpoint_action {
    RUNTIME_BREAKPOINT_ACTION_BREAK = 1u << 0,
    RUNTIME_BREAKPOINT_ACTION_FAST = 1u << 1,
    RUNTIME_BREAKPOINT_ACTION_SLOW = 1u << 2,
    RUNTIME_BREAKPOINT_ACTION_TRON = 1u << 3,
    RUNTIME_BREAKPOINT_ACTION_TROFF = 1u << 4,
    RUNTIME_BREAKPOINT_ACTION_TYPE = 1u << 5,
    RUNTIME_BREAKPOINT_ACTION_SWAP = 1u << 6
} runtime_breakpoint_action;

typedef struct runtime_breakpoint_definition {
    uint8_t enabled;
    uint16_t start_address;
    uint16_t end_address;
    uint8_t has_end_address;
    uint32_t access;
    runtime_breakpoint_mapping mapping;
    uint32_t actions;
    uint8_t use_counter;
    uint32_t initial_count;
    uint32_t reset_count;
    uint8_t swap_slot;     /* Disk II controller slot, 0..7 (card checked when hit) */
    int32_t swap_param;    /* disk queue target: negative=backward, positive+swap_relative=forward, positive+!swap_relative=absolute 1-based */
    uint8_t swap_relative; /* 1 if +/- was explicit (relative movement), 0 if bare number (absolute index) */
    char tron_path[RUNTIME_BREAKPOINT_TRON_PATH_MAX]; /* trace file path; empty = default "trace.log" */
    char type_text[RUNTIME_BREAKPOINT_TYPE_TEXT_MAX]; /* text to inject via Type action */
    /* Guard evaluated after address/access/mapping already matched. An empty
       condition (term_count 0) is an unguarded breakpoint. */
    runtime_bp_condition condition;
} runtime_breakpoint_definition;

typedef struct runtime_cpu_snapshot {
    uint16_t pc;
    uint8_t a;
    uint8_t x;
    uint8_t y;
    uint8_t sp;
    uint8_t p;
    uint64_t cycles;
} runtime_cpu_snapshot;

typedef struct runtime_machine_snapshot {
    /* Monotonic runtime publish sequence (telemetry coherence for cache/barriers). */
    uint64_t runtime_seq;
    uint64_t cycle;
    uint64_t cpu_cycles;
    uint16_t pc;
    uint8_t a;
    uint8_t x;
    uint8_t y;
    uint8_t sp;
    uint8_t p;
    uint8_t ready;
    uint8_t running;
    runtime_stop_reason stop_reason;
    uint64_t frame_number;
    uint64_t frame_cycle;
    uint64_t dropped_frames;
    /* Active turbo: milli-MHz or RUNTIME_TURBO_MAX (0). See runtime.h. */
    uint32_t active_turbo_multiplier;
    uint8_t turbo_speed_count;
    /* Apple soft switches + presentation. */
    uint32_t apple_state_flags;
    uint8_t apple_model; /* UI/options convention: 0=//e Enhanced, 1=][+ */
    uint8_t disk_motor_mask;
    runtime_slot_snapshot slots[RUNTIME_APPLE_SLOT_COUNT];
    /* Beam position (Φ0 domain; same units as BP line/cycle_in_line). */
    uint16_t video_line;         /* 0..261 */
    uint16_t video_cycle_in_line; /* 0..64 */
} runtime_machine_snapshot;

typedef struct runtime_memory_snapshot {
    uint16_t address;
    runtime_memory_mode mode;
    uint16_t length;
    uint8_t bytes[RUNTIME_MEMORY_SNAPSHOT_MAX];
    uint64_t write_history[RUNTIME_MEMORY_SNAPSHOT_MAX];
} runtime_memory_snapshot;

/* Slim completion for bulk RPC memory; bytes live in the token-keyed pool. */
typedef struct runtime_memory_rpc_meta {
    uint16_t address;
    uint32_t length;
    runtime_memory_mode mode;
    runtime_memory_rpc_status status;
} runtime_memory_rpc_meta;

typedef struct runtime_debug_memory_snapshot {
    uint64_t generation;
    uint8_t has_write_history;
    uint8_t map[MACHINE_ADDRESS_SPACE];
    uint8_t ram[MACHINE_ADDRESS_SPACE];       /* Main */
    uint8_t rom[MACHINE_ADDRESS_SPACE];
    uint8_t aux[MACHINE_ADDRESS_SPACE]; /* Aux */
    uint8_t lc1[MACHINE_ADDRESS_SPACE]; /* LC1 */
    uint8_t lc2[MACHINE_ADDRESS_SPACE];        /* LC2 */
    uint8_t aux_valid[MACHINE_ADDRESS_SPACE];
    uint8_t lc1_valid[MACHINE_ADDRESS_SPACE];
    uint8_t lc2_valid[MACHINE_ADDRESS_SPACE];
    uint64_t write_history[MACHINE_ADDRESS_SPACE];
} runtime_debug_memory_snapshot;

typedef struct runtime_breakpoint_snapshot_entry {
    uint32_t id;
    uint16_t start_address;
    uint16_t end_address;
    uint8_t has_end_address;
    runtime_breakpoint_access access;
    runtime_breakpoint_mapping mapping;
    uint32_t actions;
    uint8_t enabled;
    uint8_t use_counter;
    uint32_t current_hits;
    uint32_t initial_count;
    uint32_t reset_count;
    uint32_t counter;
    uint8_t swap_slot;
    int32_t swap_param;
    uint8_t swap_relative;
    char tron_path[RUNTIME_BREAKPOINT_TRON_PATH_MAX];
    char type_text[RUNTIME_BREAKPOINT_TYPE_TEXT_MAX];
    runtime_bp_condition condition;

    /* Phase 12 compatibility aliases. Prefer start_address and initial_count. */
    uint16_t address;
    uint32_t target_hits;
} runtime_breakpoint_snapshot_entry;

typedef struct runtime_breakpoint_snapshot {
    uint16_t count;
    runtime_breakpoint_snapshot_entry entries[RUNTIME_BREAKPOINT_SNAPSHOT_MAX];
} runtime_breakpoint_snapshot;

typedef struct runtime_call_stack_entry {
    uint16_t jsr_address;
    uint16_t dest_address;
} runtime_call_stack_entry;

typedef struct runtime_call_stack_snapshot {
    uint8_t sp;
    uint8_t count;
    runtime_call_stack_entry entries[RUNTIME_CALL_STACK_MAX];
} runtime_call_stack_snapshot;

typedef struct runtime_disk_status_snapshot {
    uint8_t device;
    uint8_t mounted;
    uint8_t powered;
    uint8_t writable;
    uint8_t dirty;
    uint8_t image_kind; /* reserved; product uses paths from options */
    uint8_t last_result; /* runtime_media_result */
    char display_name[256];
    char disk_title[32];
} runtime_disk_status_snapshot;

typedef struct runtime_event {
    runtime_event_type type;
    /* Echo of runtime_command.request_token for solicited completions; 0 for
       unsolicited notifications (frame ready, free-run pause/running, etc.). */
    uint64_t request_token;

    union {
        struct {
            int unused;
        } pong;

        struct {
            char message[1024];
        } error;

        struct {
            uint64_t frame_number;
            uint64_t machine_cycle;
            uint64_t dropped_frames;
            /* Slot bits for Disk II motors on this frame (frontend LEDs). */
            uint8_t disk_motor_mask;
        } frame_ready;

        runtime_cpu_snapshot cpu_state;
        runtime_machine_snapshot machine_state;
        runtime_memory_snapshot memory;
        runtime_memory_rpc_meta memory_rpc;
        /* Full snapshot (preferred by nested-step tests) or ready meta. */
        runtime_breakpoint_snapshot breakpoints;
        struct {
            uint16_t count;
            uint64_t generation;
        } breakpoints_ready;
        struct {
            runtime_stop_reason reason;
            runtime_cpu_snapshot cpu;
        } step_complete;
        runtime_disk_status_snapshot disk_status;
        struct {
            char path[1024];
            uint8_t slot;
            uint8_t device;
            uint8_t card_type;
            uint8_t change_type;
            uint8_t success;
        } media_changed;
        runtime_call_stack_snapshot call_stack;
        struct {
            uint16_t address;
            char path[1024];
            char notice[4096];
        } assemble;

        struct {
            char path[1024];
        } state_file;

        struct {
            uint8_t slot;
            int32_t swap_param;
            uint8_t swap_relative;
            uint8_t device; /* Disk II drive 0/1 */
        } disk_swap;
        struct {
            uint64_t generation;
            uint8_t has_write_history;
        } debug_memory_ready;

        runtime_history_status history_status;
        runtime_history_rpc_meta history_rpc;
    } data;
} runtime_event;
