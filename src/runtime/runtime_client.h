#pragma once

#include "runtime_event.h"
#include "runtime.h"
#include "runtime_frame_ring.h"
#include "apple2_file.h"

#include "display_frame.h"
#include "keyboard.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    RUNTIME_SYMBOL_NAME_MAX    = 64,
    RUNTIME_SYMBOL_SNAPSHOT_MAX = 4096
};

typedef struct runtime_symbol_snapshot_entry {
    uint16_t address;
    char name[RUNTIME_SYMBOL_NAME_MAX];
} runtime_symbol_snapshot_entry;

typedef struct runtime_symbol_snapshot {
    size_t count;
    size_t total;
    runtime_symbol_snapshot_entry entries[RUNTIME_SYMBOL_SNAPSHOT_MAX];
} runtime_symbol_snapshot;

typedef struct runtime_client runtime_client;

bool runtime_client_ping(runtime_client *client);
bool runtime_client_quit(runtime_client *client);
/* Warm reset (CTRL+RESET). Preserves run/pause. */
bool runtime_client_reset(runtime_client *client);
/* Cold reset (CTRL+Open-Apple+RESET). Preserves run/pause. */
bool runtime_client_cold_reset(runtime_client *client);
bool runtime_client_reset_ex_with_resume(
    runtime_client *client,
    bool resume_running);
bool runtime_client_reset_with_options(
    runtime_client *client,
    bool cold,
    uint8_t resume_running);
bool runtime_client_run(runtime_client *client);
bool runtime_client_pause(runtime_client *client);
bool runtime_client_step_cycle(runtime_client *client);
bool runtime_client_step_instruction(runtime_client *client);
bool runtime_client_run_cycles(runtime_client *client, size_t count);
bool runtime_client_run_instructions(runtime_client *client, size_t count);
/* Allocate a non-zero request_token for solicited control/UI RPC. */
uint64_t runtime_client_alloc_request_token(runtime_client *client);

/* Token 0: unsolicited / UI telemetry (must not complete control deferred). */
bool runtime_client_request_cpu_state(runtime_client *client);
/* Solicited CPU read; runtime echoes request_token on CPU_STATE_RESPONSE. */
bool runtime_client_request_cpu_state_token(runtime_client *client, uint64_t request_token);
bool runtime_client_request_machine_state(runtime_client *client);
/* UI/legacy: token 0, inline snapshot up to RUNTIME_MEMORY_SNAPSHOT_MAX. */
bool runtime_client_request_memory(
    runtime_client *client,
    uint16_t address,
    uint16_t length,
    runtime_memory_mode mode);
/* Solicited bulk/control get-memory (token non-zero). length 1..65536. */
bool runtime_client_request_memory_token(
    runtime_client *client,
    uint16_t address,
    uint32_t length,
    runtime_memory_mode mode,
    uint64_t request_token);
/* Claim pool payload for token; caller owns *out_bytes (malloc). */
bool runtime_client_claim_memory_rpc(
    runtime_client *client,
    uint64_t request_token,
    uint8_t **out_bytes,
    uint32_t *out_length,
    uint16_t *out_address,
    runtime_memory_mode *out_mode);
bool runtime_client_claim_history_rpc(
    runtime_client *client,
    uint64_t request_token,
    uint8_t **out_bytes,
    uint32_t *out_length,
    runtime_history_rpc_meta *out_meta);
bool runtime_client_cancel_rpc(
    runtime_client *client,
    uint64_t request_token);
bool runtime_client_request_memory_view(
    runtime_client *client,
    uint16_t address,
    uint16_t length,
    runtime_memory_mode mode);
bool runtime_client_request_debug_memory(runtime_client *client, bool include_write_history);
bool runtime_client_request_frame(runtime_client *client);
bool runtime_client_keyboard_key(runtime_client *client, host_key key, bool pressed);
/* Apple game port: axes[4] paddle units 0..255 (PDL0..PDL3), buttons bit0–2. */
bool runtime_client_set_gameport(
    runtime_client *client,
    const uint8_t axis[4],
    uint8_t button_mask);
bool runtime_client_set_pc(runtime_client *client, uint16_t value);
bool runtime_client_set_sp(runtime_client *client, uint8_t value);
bool runtime_client_set_a(runtime_client *client, uint8_t value);
bool runtime_client_set_x(runtime_client *client, uint8_t value);
bool runtime_client_set_y(runtime_client *client, uint8_t value);
bool runtime_client_set_status(runtime_client *client, uint8_t value);
bool runtime_client_write_memory_byte(
    runtime_client *client,
    uint16_t address,
    uint8_t value,
    runtime_memory_mode mode);
bool runtime_client_write_memory(
    runtime_client *client,
    uint16_t address,
    uint16_t length,
    runtime_memory_mode mode,
    const uint8_t *bytes);
bool runtime_client_set_execute_breakpoint(runtime_client *client, uint16_t address);
bool runtime_client_create_breakpoint(
    runtime_client *client,
    const runtime_breakpoint_definition *definition);
bool runtime_client_update_breakpoint(
    runtime_client *client,
    uint32_t id,
    const runtime_breakpoint_definition *definition);
bool runtime_client_duplicate_breakpoint(runtime_client *client, uint32_t id);
bool runtime_client_clear_breakpoint(runtime_client *client, uint32_t id);
bool runtime_client_clear_all_breakpoints(runtime_client *client);
bool runtime_client_set_breakpoint_enabled(runtime_client *client, uint32_t id, bool enabled);
bool runtime_client_rearm_oneshot_breakpoints(runtime_client *client);
bool runtime_client_request_breakpoints(runtime_client *client);
bool runtime_client_save_state(runtime_client *client, const char *path);
bool runtime_client_load_state(runtime_client *client, const char *path);
/* Disk II write-protect notch (slot 1–7, drive 0/1). */
bool runtime_client_set_disk_writable(
    runtime_client *client,
    uint8_t slot,
    uint8_t device,
    bool writable);
bool runtime_client_media_insert(
    runtime_client *client,
    uint8_t slot,
    uint8_t device,
    runtime_slot_card_type card_type,
    const char *path);
bool runtime_client_media_eject(runtime_client *client, uint8_t slot, uint8_t device);
bool runtime_client_media_swap(
    runtime_client *client,
    uint8_t slot,
    uint8_t device,
    int32_t param,
    bool relative);
bool runtime_client_boot_slot(runtime_client *client, uint8_t slot);
/* Debugger-only presentation override; does not modify Apple soft switches. */
bool runtime_client_set_display_override(
    runtime_client *client,
    bool enabled,
    uint32_t flags);
bool runtime_client_assemble_file(runtime_client *client, const char *path, uint16_t address);
bool runtime_client_assemble_file_full(
    runtime_client *client,
    const char *path,
    uint16_t address,
    uint16_t run_address,
    bool auto_run,
    bool mli_launch,
    bool reset_first,
    bool auto_adjust_segments);
bool runtime_client_poll_symbols(runtime_client *client, runtime_symbol_snapshot *out);
bool runtime_client_paste_text(runtime_client *client, const char *text, size_t length);
bool runtime_client_cycle_turbo_speed(runtime_client *client);
/* milli_mhz: RUNTIME_TURBO_MAX (0) = max free-run; else target MHz × 1000 (1000 = 1 MHz). */
bool runtime_client_set_turbo_multiplier(runtime_client *client, uint32_t milli_mhz);
bool runtime_client_apply_machine_config(
    runtime_client *client,
    const runtime_machine_config *config,
    const runtime_config *runtime_options,
    const char *ini_path,
    const char *symbol_files,
    bool reset,
    bool save_ini,
    bool resume_running);
/* Apple ARGB frame handoff. Caller provides buffer large enough for w*h. */
bool runtime_client_poll_argb_frame(
    runtime_client *client,
    uint32_t *out_pixels,
    uint32_t max_pixels,
    uint32_t *out_width,
    uint32_t *out_height,
    uint64_t *out_frame_number);
bool runtime_client_poll_debug_memory(runtime_client *client, runtime_debug_memory_snapshot *out_snapshot);
bool runtime_client_poll_breakpoints(
    runtime_client *client,
    runtime_breakpoint_snapshot *out_snapshot);

bool runtime_client_step_out(runtime_client *client);
bool runtime_client_step_over(runtime_client *client);
bool runtime_client_run_to_cursor(runtime_client *client, uint16_t address);
bool runtime_client_history_info(
    runtime_client *client,
    uint64_t request_token);
bool runtime_client_history_record(
    runtime_client *client,
    bool enabled,
    uint64_t request_token);
/* Pause flight-recorder automatically while turbo is max (live policy). */
bool runtime_client_set_history_off_on_max(runtime_client *client, bool enabled);
bool runtime_client_history_clear(
    runtime_client *client,
    uint64_t request_token);
/* session_id 0 = default session (compat). */
bool runtime_client_history_find(
    runtime_client *client,
    uint32_t session_id,
    const runtime_history_query *query,
    runtime_history_from_kind from_kind,
    uint64_t from_id,
    uint16_t limit,
    uint64_t request_token);
bool runtime_client_history_next(
    runtime_client *client,
    uint32_t session_id,
    uint64_t cursor,
    uint16_t limit,
    uint64_t request_token);
bool runtime_client_history_read(
    runtime_client *client,
    uint32_t session_id,
    uint64_t epoch,
    uint64_t id,
    uint16_t before,
    uint16_t after,
    uint64_t request_token);
bool runtime_client_history_close(
    runtime_client *client,
    uint32_t session_id,
    uint64_t cursor,
    uint64_t request_token);
/* Worker-allocated session; reply via RUNTIME_EVENT_SESSION_RESPONSE.
   endpoint_epoch is stored for control binding (0 if unused). */
bool runtime_client_session_open(
    runtime_client *client,
    runtime_session_kind kind,
    uint64_t endpoint_epoch,
    uint64_t request_token);
bool runtime_client_session_close(
    runtime_client *client,
    uint32_t session_id,
    uint64_t request_token);
bool runtime_client_request_call_stack(runtime_client *client);

bool runtime_client_load_bin(
    runtime_client *client,
    const char *path,
    uint16_t address,
    apple2_binary_format format,
    bool reset_first,
    bool is_basic_text,
    bool run_after_load);
bool runtime_client_save_bin(
    runtime_client *client,
    const char *path,
    uint16_t start_address,
    uint16_t end_address,
    apple2_binary_format format,
    bool is_basic_text);

bool runtime_client_poll_event(
    runtime_client *client,
    runtime_event *out_event);

/* Frame ring (rolling framebuffer black box). These read the runtime's ring
   directly; it carries its own mutex, so they are safe to call from the main
   thread while the runtime thread keeps pushing frames. */
void runtime_client_get_frame_ring_info(
    runtime_client *client,
    runtime_frame_ring_info *out_info);

bool runtime_client_copy_frame_at(
    runtime_client *client,
    uint64_t target,
    bool by_cycle,
    runtime_ring_frame *out_frame);

void runtime_client_set_frame_ring_recording(runtime_client *client, bool recording);

void runtime_client_clear_frame_ring(runtime_client *client);
