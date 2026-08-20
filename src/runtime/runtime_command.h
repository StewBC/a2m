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
    RUNTIME_COMMAND_STEP_FRAME,
    RUNTIME_COMMAND_RUN_TO_RASTER,
    RUNTIME_COMMAND_REQUEST_CPU_STATE,
    RUNTIME_COMMAND_REQUEST_MACHINE_STATE,
    RUNTIME_COMMAND_REQUEST_FRAME,
    RUNTIME_COMMAND_KEYBOARD_KEY,
    RUNTIME_COMMAND_RESTORE,
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
    RUNTIME_COMMAND_LOAD_PRG,
    RUNTIME_COMMAND_MOUNT_D64,
    RUNTIME_COMMAND_UNMOUNT_DISK,
    RUNTIME_COMMAND_POWER_ON_DRIVE,
    RUNTIME_COMMAND_POWER_OFF_DRIVE,
    RUNTIME_COMMAND_REQUEST_DISK_STATUS,
    RUNTIME_COMMAND_SET_DISK_WRITABLE,
    RUNTIME_COMMAND_ASSEMBLE_FILE,
    RUNTIME_COMMAND_APPLY_MACHINE_CONFIG,
    RUNTIME_COMMAND_CYCLE_TURBO_SPEED,
    RUNTIME_COMMAND_SET_TURBO_MULTIPLIER,
    RUNTIME_COMMAND_PASTE_TEXT,
    RUNTIME_COMMAND_SET_JOYSTICK,
    RUNTIME_COMMAND_SET_GAMEPORT,
    RUNTIME_COMMAND_STEP_OUT,
    RUNTIME_COMMAND_STEP_OVER,
    RUNTIME_COMMAND_RUN_TO_CURSOR,
    RUNTIME_COMMAND_LOAD_BIN,
    RUNTIME_COMMAND_SAVE_BIN,
    RUNTIME_COMMAND_REQUEST_CALL_STACK,
    RUNTIME_COMMAND_REARM_ONESHOT_BREAKPOINTS,
    RUNTIME_COMMAND_REQUEST_DEBUG_MEMORY,
    RUNTIME_COMMAND_LOAD_CRT,
    RUNTIME_COMMAND_SAVE_STATE,
    RUNTIME_COMMAND_LOAD_STATE,
    RUNTIME_COMMAND_HISTORY_INFO,
    RUNTIME_COMMAND_HISTORY_RECORD,
    RUNTIME_COMMAND_HISTORY_CLEAR,
    RUNTIME_COMMAND_HISTORY_FIND,
    RUNTIME_COMMAND_HISTORY_NEXT,
    RUNTIME_COMMAND_HISTORY_READ,
    RUNTIME_COMMAND_HISTORY_CLOSE,
    RUNTIME_COMMAND_SET_HISTORY_OFF_ON_MAX,
    RUNTIME_COMMAND_MEDIA_INSERT,
    RUNTIME_COMMAND_MEDIA_EJECT,
    RUNTIME_COMMAND_MEDIA_SWAP,
    RUNTIME_COMMAND_BOOT_SLOT,
    RUNTIME_COMMAND_SET_DISPLAY_OVERRIDE
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
    union {
        struct {
            uint8_t detach_cartridge; /* legacy C64; ignored on Apple path */
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
        } load_prg;

        struct {
            char path[RUNTIME_COMMAND_PATH_MAX];
        } load_crt;

        struct {
            char path[RUNTIME_COMMAND_PATH_MAX];
        } state_file;

        struct {
            char path[RUNTIME_COMMAND_PATH_MAX];
            uint8_t device;
            uint8_t writable;
        } mount_d64;

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

        struct {
            uint8_t device;
            uint8_t writable;
        } disk_device;

        struct {
            char path[RUNTIME_COMMAND_PATH_MAX];
            uint16_t address;
            uint16_t run_address;
            uint8_t auto_run;
            uint8_t basic_run;
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
            /* When reload_roms is set the runtime replaces its ROM file paths with
               these (empty == unset) and re-reads them before the reset, so ROM
               path changes take effect on the same apply. */
            uint8_t reload_roms;
            char system_rom_path[RUNTIME_COMMAND_PATH_MAX];
            char basic_rom_path[RUNTIME_COMMAND_PATH_MAX];
            char char_rom_path[RUNTIME_COMMAND_PATH_MAX];
            char kernal_rom_path[RUNTIME_COMMAND_PATH_MAX];
            char rom1541_path[RUNTIME_COMMAND_PATH_MAX];
        } apply_machine_config;

        struct {
            /* milli-MHz or RUNTIME_TURBO_MAX (0). */
            uint32_t multiplier;
        } set_turbo_multiplier;

        struct {
            char text[RUNTIME_PASTE_TEXT_MAX];
            size_t length;
        } paste_text;

        struct {
            uint8_t port;
            uint8_t inputs;
        } set_joystick;

        /* Apple game port: 4 paddle axes (0..255) + button mask (bit0–2). */
        struct {
            uint8_t axis[4];
            uint8_t buttons;
        } set_gameport;

        struct {
            uint16_t address;
        } run_to_cursor;

        struct {
            uint16_t raster_line;
            uint16_t cycle_in_line; /* valid when has_cycle != 0 */
            uint8_t has_cycle;
        } run_to_raster;

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
            uint8_t from_kind;
            uint16_t limit;
        } history_find;

        struct {
            uint64_t cursor;
            uint16_t limit;
        } history_next;

        struct {
            uint64_t epoch;
            uint64_t id;
            uint16_t before;
            uint16_t after;
        } history_read;

        struct {
            uint64_t cursor;
        } history_close;

        struct {
            uint8_t enabled;
        } set_history_off_on_max;
    } data;
} runtime_command;
