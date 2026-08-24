#pragma once

#include "runtime_event.h"
#include "apple2_file.h"
#include "keyboard.h"

#include <stddef.h>
#include <stdint.h>

typedef enum runtime_command_type {
    RUNTIME_COMMAND_NONE = 0,
    RUNTIME_COMMAND_PING,
    RUNTIME_COMMAND_QUIT,
    RUNTIME_COMMAND_RESET,
    RUNTIME_COMMAND_RUN,
    RUNTIME_COMMAND_PAUSE,
    RUNTIME_COMMAND_STEP_CYCLE,
    RUNTIME_COMMAND_STEP_INSTRUCTION,
    RUNTIME_COMMAND_RUN_CYCLES,
    RUNTIME_COMMAND_RUN_INSTRUCTIONS,
    RUNTIME_COMMAND_REQUEST_CPU_STATE,
    RUNTIME_COMMAND_REQUEST_MACHINE_STATE,
    RUNTIME_COMMAND_REQUEST_FRAME,
    RUNTIME_COMMAND_KEYBOARD_KEY,
    RUNTIME_COMMAND_SET_CPU_REGISTER,
    RUNTIME_COMMAND_REQUEST_MEMORY,
    RUNTIME_COMMAND_REQUEST_MEMORY_VIEW,
    RUNTIME_COMMAND_WRITE_MEMORY_BYTE,
    RUNTIME_COMMAND_WRITE_MEMORY,
    RUNTIME_COMMAND_SET_EXECUTE_BREAKPOINT,
    RUNTIME_COMMAND_CLEAR_BREAKPOINT,
    RUNTIME_COMMAND_CLEAR_ALL_BREAKPOINTS,
    RUNTIME_COMMAND_SET_BREAKPOINT_ENABLED,
    RUNTIME_COMMAND_CREATE_BREAKPOINT,
    RUNTIME_COMMAND_UPDATE_BREAKPOINT,
    RUNTIME_COMMAND_DUPLICATE_BREAKPOINT,
    RUNTIME_COMMAND_REQUEST_BREAKPOINTS,
    RUNTIME_COMMAND_SET_DISK_WRITABLE,
    RUNTIME_COMMAND_ASSEMBLE_FILE,
    RUNTIME_COMMAND_APPLY_MACHINE_CONFIG,
    RUNTIME_COMMAND_CYCLE_TURBO_SPEED,
    RUNTIME_COMMAND_SET_TURBO_MULTIPLIER,
    RUNTIME_COMMAND_PASTE_TEXT,
    RUNTIME_COMMAND_SET_GAMEPORT,
    RUNTIME_COMMAND_STEP_OUT,
    RUNTIME_COMMAND_STEP_OVER,
    RUNTIME_COMMAND_RUN_TO_CURSOR,
    RUNTIME_COMMAND_LOAD_BIN,
    RUNTIME_COMMAND_SAVE_BIN,
    RUNTIME_COMMAND_REQUEST_CALL_STACK,
    RUNTIME_COMMAND_REARM_ONESHOT_BREAKPOINTS,
    RUNTIME_COMMAND_REQUEST_DEBUG_MEMORY,
    RUNTIME_COMMAND_SAVE_STATE,
    RUNTIME_COMMAND_LOAD_STATE,
    RUNTIME_COMMAND_HISTORY_INFO,
    RUNTIME_COMMAND_HISTORY_RECORD,
    RUNTIME_COMMAND_HISTORY_CLEAR,
    RUNTIME_COMMAND_HISTORY_FIND,
    RUNTIME_COMMAND_HISTORY_NEXT,
    RUNTIME_COMMAND_HISTORY_READ,
    RUNTIME_COMMAND_HISTORY_CLOSE,
    RUNTIME_COMMAND_SESSION_OPEN,
    RUNTIME_COMMAND_SESSION_CLOSE,
    RUNTIME_COMMAND_SET_HISTORY_OFF_ON_MAX,
    RUNTIME_COMMAND_MEDIA_INSERT,
    RUNTIME_COMMAND_MEDIA_EJECT,
    RUNTIME_COMMAND_MEDIA_SWAP,
    RUNTIME_COMMAND_BOOT_SLOT,
    RUNTIME_COMMAND_SET_DISPLAY_OVERRIDE,
    RUNTIME_COMMAND_INSPECTOR_SET_ENABLED,
    RUNTIME_COMMAND_INSPECTOR_ENTER,
    RUNTIME_COMMAND_INSPECTOR_LEAVE,
    RUNTIME_COMMAND_INSPECTOR_LAND,
    RUNTIME_COMMAND_INSPECTOR_FRAME_STEP
} runtime_command_type;

enum {
    RUNTIME_COMMAND_PATH_MAX = 1024,
    RUNTIME_PASTE_TEXT_MAX = 4096
};

typedef enum runtime_cpu_register {
    RUNTIME_CPU_REGISTER_PC = 0,
    RUNTIME_CPU_REGISTER_SP,
    RUNTIME_CPU_REGISTER_A,
    RUNTIME_CPU_REGISTER_X,
    RUNTIME_CPU_REGISTER_Y,
    RUNTIME_CPU_REGISTER_STATUS
} runtime_cpu_register;

enum {
    RUNTIME_RESET_PAUSED = 0,
    RUNTIME_RESET_RUNNING = 1,
    RUNTIME_RESET_PRESERVE_STATE = 2
};

typedef struct runtime_command {
    runtime_command_type type;
    /* Opaque correlation id for solicited work. 0 = unsolicited / lossy telemetry.
       Echoed on matching completion and error events. See agents/runtime.md. */
    uint64_t request_token;
    /* Source asker for state-changed inform; 0 = unknown / internal. */
    uint32_t session_id;
    union {
        struct {
            /* 1 = CTRL+Open-Apple+RESET (cold); 0 = CTRL+RESET (warm). */
            uint8_t cold;
            /* 0 = leave paused, 1 = resume, 2 = preserve runtime state. */
            uint8_t resume_running;
        } reset;

        struct {
            size_t count;
        } run_cycles;

        struct {
            size_t count;
        } run_instructions;

        struct {
            host_key key;
            uint8_t pressed;
        } keyboard_key;

        struct {
            runtime_cpu_register reg;
            uint16_t value;
        } set_cpu_register;

        struct {
            uint16_t address;
            uint32_t length; /* 1..RUNTIME_MEMORY_RPC_MAX_LENGTH for get-memory RPC */
            uint8_t mode;
        } request_memory;

        struct {
            uint16_t address;
            uint8_t value;
            uint8_t mode;
        } write_memory_byte;

        struct {
            uint16_t address;
            uint16_t length;
            uint8_t mode;
            uint8_t bytes[RUNTIME_MEMORY_SNAPSHOT_MAX];
        } write_memory;

        struct {
            uint16_t address;
            uint8_t enabled;
        } set_execute_breakpoint;

        struct {
            uint32_t id;
        } clear_breakpoint;

        struct {
            uint32_t id;
            uint8_t enabled;
        } set_breakpoint_enabled;

        struct {
            runtime_breakpoint_definition definition;
        } create_breakpoint;

        struct {
            uint32_t id;
            runtime_breakpoint_definition definition;
        } update_breakpoint;

        struct {
            uint32_t id;
        } duplicate_breakpoint;

        struct {
            char path[RUNTIME_COMMAND_PATH_MAX];
        } state_file;

        struct {
            char path[RUNTIME_COMMAND_PATH_MAX];
            uint8_t slot;
            uint8_t device;
            uint8_t card_type;
        } media_insert;

        struct {
            uint8_t slot;
            uint8_t device;
        } media_device;

        struct {
            int32_t param;
            uint8_t slot;
            uint8_t device;
            uint8_t relative;
        } media_swap;

        struct {
            uint8_t slot;
        } boot_slot;

        struct {
            uint32_t flags;
            uint8_t enabled;
        } set_display_override;

        /* Disk II write-protect notch (slot 1–7, drive 0/1). */
        struct {
            uint8_t slot;
            uint8_t device;
            uint8_t writable; /* 1 = writable, 0 = read-only */
        } disk_writable;

        struct {
            char path[RUNTIME_COMMAND_PATH_MAX];
            uint16_t address;
            uint16_t run_address;
            uint8_t auto_run;
            uint8_t mli_launch;
            uint8_t reset_first;
            uint8_t auto_adjust_segments;
        } assemble_file;

        struct {
            runtime_machine_config config;
            char ini_path[RUNTIME_COMMAND_PATH_MAX];
            char symbol_files[RUNTIME_COMMAND_PATH_MAX];
            uint32_t turbo_speeds[16];
            uint8_t turbo_speed_count;
            uint32_t active_turbo_multiplier;
            uint8_t reset;
            uint8_t save_ini;
            uint8_t resume_running;
        } apply_machine_config;

        struct {
            /* milli-MHz or RUNTIME_TURBO_MAX (0). */
            uint32_t multiplier;
        } set_turbo_multiplier;

        struct {
            char text[RUNTIME_PASTE_TEXT_MAX];
            size_t length;
        } paste_text;

        /* Apple game port: 4 paddle axes (0..255) + button mask (bit0–2). */
        struct {
            uint8_t axis[4];
            uint8_t buttons;
        } set_gameport;

        struct {
            uint16_t address;
        } run_to_cursor;

        struct {
            char path[RUNTIME_COMMAND_PATH_MAX];
            uint16_t address;
            uint8_t format;
            uint8_t reset_first;
            uint8_t is_basic_text;
            uint8_t run_after_load;
        } load_bin;

        struct {
            char path[RUNTIME_COMMAND_PATH_MAX];
            uint16_t start_address;
            uint16_t end_address;
            uint8_t format;
            uint8_t is_basic_text;
        } save_bin;

        struct {
            uint8_t include_write_history;
        } request_debug_memory;

        struct {
            uint8_t enabled;
        } history_record;

        struct {
            runtime_history_query query;
            uint64_t from_id;
            uint32_t session_id; /* 0 = default session */
            uint8_t from_kind;
            uint16_t limit;
        } history_find;

        struct {
            uint64_t cursor;
            uint32_t session_id; /* 0 = default session */
            uint16_t limit;
        } history_next;

        struct {
            uint64_t epoch;
            uint64_t id;
            uint32_t session_id; /* 0 = default session; ignored for absolute read */
            uint16_t before;
            uint16_t after;
        } history_read;

        struct {
            uint64_t cursor;
            uint32_t session_id; /* 0 = default session */
        } history_close;

        struct {
            uint8_t kind; /* runtime_session_kind: ui or control */
            uint64_t endpoint_epoch; /* control connection_epoch; 0 if unused */
        } session_open;

        struct {
            uint32_t session_id;
        } session_close;

        struct {
            uint8_t enabled;
        } set_history_off_on_max;

        struct {
            uint8_t enabled;
        } inspector_set_enabled;

        struct {
            uint64_t cycle;
        } inspector_land;

        struct {
            int8_t direction; /* +1 forward frame, -1 previous frame */
        } inspector_frame_step;
    } data;
} runtime_command;
