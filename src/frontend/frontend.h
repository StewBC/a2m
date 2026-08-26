#pragma once

#include "platform.h"
#include "runtime_event.h"
#include "runtime_client.h"
#include "app_options.h"
#include "window_title.h"

#include "display_frame.h"

#include <SDL.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct frontend frontend;

typedef struct frontend_debug_state {
    frontend_runtime_state runtime_state;
    runtime_cpu_snapshot cpu;
    runtime_memory_snapshot memory;
    runtime_memory_snapshot memory_view_snapshots[16];
    int memory_view_snapshot_count;
    runtime_debug_memory_snapshot debug_memory;
    runtime_breakpoint_snapshot breakpoints;
    runtime_disk_status_snapshot disk_status[2];
    runtime_call_stack_snapshot call_stack;
    uint64_t frame_number;
    uint64_t frame_cycle;
    uint64_t dropped_frames;
    uint64_t runtime_seq; /* telemetry stamp from machine snapshot */
    uint64_t machine_cycle;
    uint64_t step_cycle_start;
    uint64_t step_cpu_cycle_start;
    runtime_stop_reason stop_reason;
    uint32_t active_turbo_multiplier;
    bool has_frame;
    bool has_cpu;
    bool has_memory;
    bool has_debug_memory;
    bool has_breakpoints;
    bool has_disk_status[2];
    bool has_call_stack;
    /* Apple soft switches / model. */
    uint32_t apple_state_flags;
    uint8_t apple_model;
    uint8_t disk_motor_mask;
    runtime_slot_snapshot slots[RUNTIME_APPLE_SLOT_COUNT];
    bool has_apple_flags;
    /* Inspector — copied from machine_state. */
    bool inspecting;
    bool inspector_enabled;
    bool inspector_window_valid;
    bool inspector_history_recording;
    bool inspector_frame_recording;
    bool inspector_recorder_recording;
    bool inspector_stopped_for_max;
    uint8_t inspector_window_start_kind;
    uint32_t inspector_window_start_arg1;
    uint64_t inspector_focus_cycle;
    uint64_t inspector_focus_id;
    uint64_t inspector_oldest_cycle;
    uint64_t inspector_newest_cycle;
    uint64_t inspector_focus_ordinal;
    bool inspector_focus_is_sample;
    runtime_inspector_catalog inspector_catalog;
} frontend_debug_state;

const char *frontend_runtime_state_name(frontend_runtime_state state);
const char *frontend_stop_reason_name(runtime_stop_reason reason);

typedef struct frontend_assembler_state {
    bool initialized;
    char file_path[1024];
    char address_buf[8];
    char run_address_buf[8];
    bool use_address;
    bool run_address_user_edited;
    bool auto_run;
    bool mli_launch;
    bool reset_first;
    bool rearm_oneshots;
    bool error_dialog_open;
    bool error_dialog_is_notice;
    char error_text[4096];
    unsigned int error_scroll_x;
    unsigned int error_scroll_y;
} frontend_assembler_state;

typedef enum frontend_debugger_intent_type {
    FRONTEND_DEBUGGER_INTENT_NONE = 0,
    FRONTEND_DEBUGGER_INTENT_REGISTER_SET_PC,
    FRONTEND_DEBUGGER_INTENT_REGISTER_SET_SP,
    FRONTEND_DEBUGGER_INTENT_REGISTER_SET_A,
    FRONTEND_DEBUGGER_INTENT_REGISTER_SET_X,
    FRONTEND_DEBUGGER_INTENT_REGISTER_SET_Y,
    FRONTEND_DEBUGGER_INTENT_REGISTER_SET_STATUS,
    FRONTEND_DEBUGGER_INTENT_REQUEST_MEMORY,
    FRONTEND_DEBUGGER_INTENT_REQUEST_MEMORY_VIEW,
    FRONTEND_DEBUGGER_INTENT_REQUEST_DEBUG_MEMORY,
    FRONTEND_DEBUGGER_INTENT_MEMORY_WRITE_BYTE,
    FRONTEND_DEBUGGER_INTENT_BREAKPOINT_SET_EXECUTE,
    FRONTEND_DEBUGGER_INTENT_BREAKPOINT_CLEAR,
    FRONTEND_DEBUGGER_INTENT_BREAKPOINT_CLEAR_ALL,
    FRONTEND_DEBUGGER_INTENT_BREAKPOINT_SET_ENABLED,
    FRONTEND_DEBUGGER_INTENT_BREAKPOINT_CREATE,
    FRONTEND_DEBUGGER_INTENT_BREAKPOINT_UPDATE,
    FRONTEND_DEBUGGER_INTENT_BREAKPOINT_REQUEST_SNAPSHOT,
    FRONTEND_DEBUGGER_INTENT_DISK_MOUNT_DIALOG,
    FRONTEND_DEBUGGER_INTENT_DISK_ADD_DIALOG,
    FRONTEND_DEBUGGER_INTENT_DISK_UNMOUNT,
    FRONTEND_DEBUGGER_INTENT_MACHINE_RESET,
    FRONTEND_DEBUGGER_INTENT_CONFIG_PICK_INI_DIALOG,
    FRONTEND_DEBUGGER_INTENT_CONFIG_PICK_SYMBOL_DIALOG,
    FRONTEND_DEBUGGER_INTENT_CONFIG_APPLY,
    FRONTEND_DEBUGGER_INTENT_SAVE_INI_NOW,
    FRONTEND_DEBUGGER_INTENT_ASSEMBLE_BROWSE,
    FRONTEND_DEBUGGER_INTENT_ASSEMBLE_RUN,
    FRONTEND_DEBUGGER_INTENT_LOAD_BIN_BROWSE,
    FRONTEND_DEBUGGER_INTENT_LOAD_BIN_EXECUTE,
    FRONTEND_DEBUGGER_INTENT_SAVE_BIN_BROWSE,
    FRONTEND_DEBUGGER_INTENT_SAVE_BIN_EXECUTE,
    FRONTEND_DEBUGGER_INTENT_STATE_SAVE_AS_DIALOG,
    FRONTEND_DEBUGGER_INTENT_STATE_LOAD_DIALOG,
    FRONTEND_DEBUGGER_INTENT_REQUEST_CALL_STACK,
    FRONTEND_DEBUGGER_INTENT_SAVE_PATHS_ONLY,
    FRONTEND_DEBUGGER_INTENT_CONFIG_PICK_PATH_DIALOG,
    FRONTEND_DEBUGGER_INTENT_FILE_BROWSER_RESULT,
    FRONTEND_DEBUGGER_INTENT_MEDIA_INSERT_DIALOG,
    FRONTEND_DEBUGGER_INTENT_MEDIA_EJECT,
    FRONTEND_DEBUGGER_INTENT_MEDIA_SWAP,
    FRONTEND_DEBUGGER_INTENT_BOOT_SLOT,
    FRONTEND_DEBUGGER_INTENT_SET_DISPLAY_OVERRIDE,
    FRONTEND_DEBUGGER_INTENT_SET_VIDEO_DISPLAY,
    FRONTEND_DEBUGGER_INTENT_INSPECTOR_SET_ENABLED,
    FRONTEND_DEBUGGER_INTENT_INSPECTOR_ENTER,
    FRONTEND_DEBUGGER_INTENT_INSPECTOR_LEAVE,
    FRONTEND_DEBUGGER_INTENT_INSPECTOR_LAND,
    FRONTEND_DEBUGGER_INTENT_INSPECTOR_LAND_SAMPLE,
    FRONTEND_DEBUGGER_INTENT_INSPECTOR_LAND_TO_CYCLE,
    FRONTEND_DEBUGGER_INTENT_INSPECTOR_PAUSE,
    FRONTEND_DEBUGGER_INTENT_INSPECTOR_SAMPLE_STEP,
    FRONTEND_DEBUGGER_INTENT_RUN,
    FRONTEND_DEBUGGER_INTENT_HISTORY_FIND,
    FRONTEND_DEBUGGER_INTENT_HISTORY_NEXT,
    FRONTEND_DEBUGGER_INTENT_HISTORY_READ,
    FRONTEND_DEBUGGER_INTENT_HISTORY_INFO,
    FRONTEND_DEBUGGER_INTENT_HISTORY_CLOSE
} frontend_debugger_intent_type;

typedef enum frontend_history_verb {
    FRONTEND_HISTORY_VERB_NONE = 0,
    FRONTEND_HISTORY_VERB_FIND = 1,
    FRONTEND_HISTORY_VERB_NEXT,
    FRONTEND_HISTORY_VERB_READ,
    FRONTEND_HISTORY_VERB_INFO,
    FRONTEND_HISTORY_VERB_CLOSE
} frontend_history_verb;

/* File-browser "default folder" slots. Each remembers the last directory used by
   a family of callers so the browser reopens there. The order must match the
   [browse] key order in app_options.c. */
typedef enum frontend_browse_slot {
    FRONTEND_BROWSE_SLOT_ASSEMBLER = 0,
    FRONTEND_BROWSE_SLOT_FLOPPY,
    FRONTEND_BROWSE_SLOT_SMARTPORT,
    FRONTEND_BROWSE_SLOT_BINARY,
    FRONTEND_BROWSE_SLOT_BASIC,
    FRONTEND_BROWSE_SLOT_SNAPSHOT,
    FRONTEND_BROWSE_SLOT_COUNT
} frontend_browse_slot;

typedef struct frontend_config_apply_result {
    bool accepted;
    bool needs_reboot;
    bool machine_changed;
    bool symbols_changed;
    bool roms_changed;
} frontend_config_apply_result;

typedef struct frontend_debugger_intent {
    frontend_debugger_intent_type type;
    uint16_t address;
    uint16_t length;
    uint16_t value;
    uint32_t id;
    bool enabled;
    uint32_t display_override_flags;
    bool include_write_history;
    runtime_memory_mode memory_mode;
    runtime_breakpoint_definition breakpoint;
    app_options config;
    frontend_config_apply_result config_result;
    /* Assembler */
    char assemble_path[1024];
    uint16_t assemble_address;
    uint16_t assemble_run_address;
    bool assemble_auto_run;
    bool assemble_mli_launch;
    bool assemble_reset_first;
    bool assemble_rearm_oneshots;
    uint8_t disk_device;
    uint8_t disk_slot;
    runtime_slot_card_type disk_card_type;
    /* Load */
    char load_bin_path[1024];
    uint16_t load_bin_address;
    uint8_t load_file_kind;
    apple2_binary_format load_bin_format;
    bool load_bin_reset_first;
    bool load_bin_run_after_load;
    /* Save */
    char save_bin_path[1024];
    uint16_t save_bin_start;
    uint16_t save_bin_end;
    uint8_t save_file_kind;
    apple2_binary_format save_bin_format;
    /* Machine reset */
    bool machine_reset_resume_running;
    /* File browser result */
    frontend_debugger_intent_type file_browser_purpose;
    char file_browser_path[1024];
    /* Inspector tab */
    uint64_t inspector_cycle;
    /* Forensics HISTORY_* (structured; main does not re-parse find options). */
    frontend_history_verb history_verb;
    runtime_history_query history_query;
    runtime_history_from_kind history_from_kind;
    uint64_t history_from_id;
    uint16_t history_limit;
    uint64_t history_read_id;
    uint64_t history_read_epoch; /* 0 = current */
    uint16_t history_before;
    uint16_t history_after;
    char history_label[160];
} frontend_debugger_intent;

typedef enum frontend_machine_file_kind {
    FRONTEND_MACHINE_FILE_AUTO = 0,
    FRONTEND_MACHINE_FILE_SNAPSHOT,
    FRONTEND_MACHINE_FILE_BINARY,
    FRONTEND_MACHINE_FILE_APPLESOFT_TEXT
} frontend_machine_file_kind;

typedef struct frontend_load_bin_dialog_state {
    bool open;
    bool initialized;
    char path[1024];
    frontend_machine_file_kind kind;
    apple2_binary_format binary_format;
    char address_buf[5];
    bool reset_first;
    bool run_after_load;
    char detected[96];
    char error[128];
    /* NAPS type from the selected path (for Run-after-load soft warnings). */
    bool has_naps_type;
    uint8_t naps_type;
    bool decode_ok;
} frontend_load_bin_dialog_state;

typedef struct frontend_save_bin_dialog_state {
    bool open;
    bool initialized;
    char path[1024];
    frontend_machine_file_kind kind;
    apple2_binary_format binary_format;
    char start_address_buf[5];
    char end_address_buf[5];
    char error[128];
} frontend_save_bin_dialog_state;

typedef struct frontend_layout_state {
    float split_display_right;
    float split_top_bottom;
    float split_memory_misc;
} frontend_layout_state;

typedef struct frontend_assembler_options {
    char file[1024];
    char address[8];
    char run_address[8];
    bool use_address;
    bool auto_run;
    bool mli_launch;
    bool reset_first;
    bool rearm_oneshots;
} frontend_assembler_options;

frontend *frontend_create(platform_window *window);
void frontend_destroy(frontend *ui);

void frontend_begin_input(frontend *ui);
void frontend_handle_event(frontend *ui, SDL_Event *event);
void frontend_end_input(frontend *ui);
bool frontend_routes_keyboard_to_machine(const frontend *ui);
bool frontend_wants_text_input(const frontend *ui);
bool frontend_handle_view_cycle_key(frontend *ui, const SDL_KeyboardEvent *key);
/* Apple II ARGB path: 560×192 active display from machine video. */
bool frontend_submit_argb_frame(
    frontend *ui,
    const uint32_t *pixels,
    uint32_t width,
    uint32_t height,
    uint64_t frame_number);
/* True while the Inspector thumb is down (film/pink preview). */
bool frontend_inspector_preview(
    const frontend *ui,
    uint64_t *out_picture_id,
    bool *out_keep_current_on_missing);
void frontend_render(frontend *ui, bool ui_visible, const frontend_debug_state *debug_state);
/* Force disk activity LEDs off (e.g. machine reset). */
void frontend_clear_disk_activity_leds(frontend *ui);
void frontend_open_help(frontend *ui, bool paused_by_help);
bool frontend_close_help(frontend *ui);
bool frontend_help_is_open(const frontend *ui);
bool frontend_help_paused_by_help(const frontend *ui);
bool frontend_handle_help_key(frontend *ui, const SDL_KeyboardEvent *key, int scroll_wheel_lines);
/* Forensics full-window mode (mutually exclusive with Help).
   from_debugger: opened with F9 debugger up; else full-screen CRT.
   crt_was_running: recorded only for CRT entry (restore on Opt+R/Close). */
void frontend_open_forensics(
    frontend *ui,
    bool from_debugger,
    bool crt_was_running);
void frontend_close_forensics(frontend *ui); /* Misc→Inspector; no run/pause */
bool frontend_forensics_is_open(const frontend *ui);
bool frontend_forensics_entered_from_crt(const frontend *ui);
bool frontend_forensics_crt_was_running(const frontend *ui);
bool frontend_forensics_consume_close_request(frontend *ui);
/* After successful Land: leave like F9 (debugger paused; Inspector tab). */
bool frontend_forensics_consume_leave_debugger_request(frontend *ui);
bool frontend_forensics_consume_pause_request(frontend *ui);
bool frontend_handle_forensics_key(frontend *ui, const SDL_KeyboardEvent *key);
/* Apply HISTORY RPC results (main.c after claim/decode). */
void frontend_forensics_apply_result(
    frontend *ui,
    frontend_history_verb verb,
    const char *label,
    const runtime_history_rpc_meta *meta,
    const runtime_history_record *records,
    size_t record_count,
    const bool *anchor_matches);
void frontend_forensics_apply_status(
    frontend *ui,
    const runtime_history_status *status,
    bool append_transcript_note);
void frontend_forensics_apply_rpc_error(
    frontend *ui,
    runtime_history_rpc_status status);
uint64_t frontend_forensics_last_cursor(const frontend *ui);
bool frontend_poll_debugger_intent(frontend *ui, frontend_debugger_intent *out_intent);
void frontend_set_layout_state(frontend *ui, const frontend_layout_state *state);
void frontend_get_layout_state(frontend *ui, frontend_layout_state *out_state);
void frontend_debug_min_window_size(const frontend *ui, int *out_min_w, int *out_min_h);
void frontend_set_config_state(frontend *ui, const app_options *options);
/* Refresh Disk II / SmartPort mounts in an open Configure dialog from live
   options without discarding other in-progress Configure edits. */
void frontend_sync_config_media_mounts(frontend *ui, const app_options *options);
bool frontend_config_dialog_is_open(const frontend *ui);
/* If Configure is open, flip the edited Colour/Mono radios and queue a live
   preview. Returns true when the dialog handled it (Cancel still undoes). */
bool frontend_config_toggle_colour_preview(frontend *ui);
bool frontend_trigger_assembler(frontend *ui);
/* device: Disk II drive 0 or 1. */
void frontend_set_disk_queue(frontend *ui, uint8_t device, const app_disk_slot *slot);
bool frontend_apply_selected_ini(frontend *ui, const app_options *options);
bool frontend_get_disassembly_cursor(const frontend *ui, uint16_t *out_address);
void frontend_append_symbol_file(frontend *ui, const char *path);
void frontend_set_assembler_path(frontend *ui, const char *path);
void frontend_show_assembler_errors(frontend *ui, const char *errors);
void frontend_show_assembler_notice(frontend *ui, const char *notice);
void frontend_update_symbols(frontend *ui, const runtime_symbol_snapshot *snapshot);
void frontend_set_load_bin_path(frontend *ui, const char *path);
void frontend_set_save_bin_path(frontend *ui, const char *path);
void frontend_invalidate_disassembly_cache(frontend *ui);
void frontend_set_assembler_options(frontend *ui, const frontend_assembler_options *opts);
void frontend_get_assembler_options(frontend *ui, frontend_assembler_options *out);
void frontend_open_file_browser(
    frontend *ui,
    frontend_debugger_intent_type purpose,
    const char *title,
    bool save_mode,
    const char *filter_extension,
    const char *default_extension,
    uint8_t disk_device);
void frontend_open_media_file_browser(
    frontend *ui,
    const char *title,
    const char *filter_extension,
    uint8_t slot,
    uint8_t device,
    runtime_slot_card_type card_type);

/* Seed / read the remembered default folder for a browse slot. main.c uses these
   to bridge the slots to the INI file: seed from options at startup, read back
   before saving. get returns "" (never NULL) when the slot is unset. */
void frontend_set_browse_dir(frontend *ui, frontend_browse_slot slot, const char *dir);
const char *frontend_get_browse_dir(const frontend *ui, frontend_browse_slot slot);
/* Stores a folder picked via a Paths-tab [...] button into its pending slot. */
void frontend_set_picked_browse_dir(frontend *ui, const char *path);
