#include "runtime_client.h"

#include "runtime_command.h"
#include "runtime_internal.h"
#include "message_queue.h"
#include "mutex.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool runtime_client_push(runtime_client *client, runtime_command *command)
{
    if (client == NULL || command == NULL) {
        return false;
    }
    command->session_id = client->command_session_id;
    return message_queue_push(client->command_queue, command);
}

static bool runtime_client_send_command_token(
    runtime_client *client,
    runtime_command_type type,
    uint64_t request_token) {
    if (!client) {
        return false;
    }

    runtime_command command = {
        .type = type,
        .request_token = request_token,
    };

    return runtime_client_push(client, &command);
}

void runtime_client_set_command_session(runtime_client *client, uint32_t session_id)
{
    if (client == NULL) {
        return;
    }
    client->command_session_id = session_id;
}

uint32_t runtime_client_get_command_session(const runtime_client *client)
{
    if (client == NULL) {
        return 0u;
    }
    return client->command_session_id;
}

static bool runtime_client_send_command(
    runtime_client *client,
    runtime_command_type type) {
    return runtime_client_send_command_token(client, type, 0u);
}

uint64_t runtime_client_alloc_request_token(runtime_client *client) {
    uint64_t token;

    if (!client) {
        return 0u;
    }
    /* Skip 0 forever; wrap-around to 1 is fine for process lifetime. */
    token = ++client->next_request_token;
    if (token == 0u) {
        token = ++client->next_request_token;
    }
    return token;
}


bool runtime_client_ping(runtime_client *client) {
    return runtime_client_send_command(client, RUNTIME_COMMAND_PING);
}

bool runtime_client_quit(runtime_client *client) {
    return runtime_client_send_command(client, RUNTIME_COMMAND_QUIT);
}

bool runtime_client_reset_with_options(
    runtime_client *client,
    bool cold,
    uint8_t resume_running) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_RESET,
    };

    if (!client) {
        return false;
    }

    command.data.reset.cold = cold ? 1u : 0u;
    command.data.reset.resume_running = resume_running;
    return runtime_client_push(client, &command);
}

bool runtime_client_reset_ex_with_resume(
    runtime_client *client,
    bool resume_running) {
    return runtime_client_reset_with_options(
        client,
        false,
        resume_running ? RUNTIME_RESET_RUNNING : RUNTIME_RESET_PAUSED);
}

bool runtime_client_reset(runtime_client *client) {
    return runtime_client_reset_with_options(
        client, false, RUNTIME_RESET_PRESERVE_STATE);
}

bool runtime_client_cold_reset(runtime_client *client) {
    return runtime_client_reset_with_options(
        client, true, RUNTIME_RESET_PRESERVE_STATE);
}

bool runtime_client_run(runtime_client *client) {
    return runtime_client_send_command(client, RUNTIME_COMMAND_RUN);
}

bool runtime_client_pause(runtime_client *client) {
    return runtime_client_send_command(client, RUNTIME_COMMAND_PAUSE);
}

bool runtime_client_step_cycle(runtime_client *client) {
    return runtime_client_send_command(client, RUNTIME_COMMAND_STEP_CYCLE);
}

bool runtime_client_step_instruction(runtime_client *client) {
    return runtime_client_send_command(client, RUNTIME_COMMAND_STEP_INSTRUCTION);
}

bool runtime_client_run_cycles(runtime_client *client, size_t count) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_RUN_CYCLES,
    };

    if (!client) {
        return false;
    }

    command.data.run_cycles.count = count;
    return runtime_client_push(client, &command);
}

bool runtime_client_run_instructions(runtime_client *client, size_t count) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_RUN_INSTRUCTIONS,
    };

    if (!client) {
        return false;
    }

    command.data.run_instructions.count = count;
    return runtime_client_push(client, &command);
}

bool runtime_client_request_cpu_state(runtime_client *client) {
    return runtime_client_request_cpu_state_token(client, 0u);
}

bool runtime_client_request_cpu_state_token(runtime_client *client, uint64_t request_token) {
    return runtime_client_send_command_token(
        client,
        RUNTIME_COMMAND_REQUEST_CPU_STATE,
        request_token);
}

bool runtime_client_request_machine_state(runtime_client *client) {
    return runtime_client_send_command(client, RUNTIME_COMMAND_REQUEST_MACHINE_STATE);
}

bool runtime_client_request_memory(
    runtime_client *client,
    uint16_t address,
    uint16_t length,
    runtime_memory_mode mode) {
    return runtime_client_request_memory_token(client, address, length, mode, 0u);
}

bool runtime_client_request_memory_token(
    runtime_client *client,
    uint16_t address,
    uint32_t length,
    runtime_memory_mode mode,
    uint64_t request_token) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_REQUEST_MEMORY,
        .request_token = request_token,
    };

    if (!client) {
        return false;
    }
    if (length == 0u || length > (uint32_t)RUNTIME_MEMORY_RPC_MAX_LENGTH) {
        return false;
    }
    if ((uint32_t)address + length > (uint32_t)RUNTIME_MEMORY_RPC_MAX_LENGTH) {
        return false;
    }

    command.data.request_memory.address = address;
    command.data.request_memory.length = length;
    command.data.request_memory.mode = (uint8_t)mode;
    return runtime_client_push(client, &command);
}

bool runtime_client_claim_memory_rpc(
    runtime_client *client,
    uint64_t request_token,
    uint8_t **out_bytes,
    uint32_t *out_length,
    uint16_t *out_address,
    runtime_memory_mode *out_mode) {
    runtime_rpc_payload_pool *pool;
    size_t i;

    if (client == NULL ||
        client->rpc_payload_pool == NULL ||
        request_token == 0u ||
        out_bytes == NULL) {
        return false;
    }
    pool = client->rpc_payload_pool;
    if (pool->mutex == NULL) {
        return false;
    }

    mutex_lock(pool->mutex);
    for (i = 0; i < RUNTIME_RPC_PAYLOAD_POOL_CAPACITY; ++i) {
        if (pool->slots[i].in_use &&
            pool->slots[i].request_token == request_token &&
            pool->slots[i].kind == RUNTIME_RPC_PAYLOAD_MEMORY) {
            *out_bytes = pool->slots[i].bytes;
            if (out_length != NULL) {
                *out_length = pool->slots[i].length;
            }
            if (out_address != NULL) {
                *out_address = pool->slots[i].meta.memory.address;
            }
            if (out_mode != NULL) {
                *out_mode = pool->slots[i].meta.memory.mode;
            }
            pool->slots[i].bytes = NULL;
            pool->slots[i].in_use = 0u;
            pool->slots[i].request_token = 0u;
            pool->slots[i].kind = RUNTIME_RPC_PAYLOAD_NONE;
            mutex_unlock(pool->mutex);
            return true;
        }
    }
    mutex_unlock(pool->mutex);
    return false;
}

bool runtime_client_claim_history_rpc(
    runtime_client *client,
    uint64_t request_token,
    uint8_t **out_bytes,
    uint32_t *out_length,
    runtime_history_rpc_meta *out_meta) {
    runtime_rpc_payload_pool *pool;
    size_t i;

    if (client == NULL || client->rpc_payload_pool == NULL ||
        request_token == 0u || out_bytes == NULL) {
        return false;
    }
    pool = client->rpc_payload_pool;
    if (pool->mutex == NULL) {
        return false;
    }
    mutex_lock(pool->mutex);
    for (i = 0u; i < RUNTIME_RPC_PAYLOAD_POOL_CAPACITY; ++i) {
        if (pool->slots[i].in_use &&
            pool->slots[i].request_token == request_token &&
            pool->slots[i].kind == RUNTIME_RPC_PAYLOAD_HISTORY) {
            *out_bytes = pool->slots[i].bytes;
            if (out_length != NULL) {
                *out_length = pool->slots[i].length;
            }
            if (out_meta != NULL) {
                *out_meta = pool->slots[i].meta.history;
            }
            pool->slots[i].bytes = NULL;
            memset(&pool->slots[i], 0, sizeof(pool->slots[i]));
            mutex_unlock(pool->mutex);
            return true;
        }
    }
    mutex_unlock(pool->mutex);
    return false;
}

bool runtime_client_cancel_rpc(
    runtime_client *client,
    uint64_t request_token) {
    runtime_rpc_payload_pool *pool;
    size_t i;

    if (client == NULL || client->rpc_payload_pool == NULL ||
        request_token == 0u) {
        return false;
    }
    pool = client->rpc_payload_pool;
    if (pool->mutex == NULL) {
        return false;
    }
    mutex_lock(pool->mutex);
    for (i = 0u; i < RUNTIME_RPC_PAYLOAD_POOL_CAPACITY; ++i) {
        if (pool->slots[i].in_use &&
            pool->slots[i].request_token == request_token) {
            free(pool->slots[i].bytes);
            memset(&pool->slots[i], 0, sizeof(pool->slots[i]));
            mutex_unlock(pool->mutex);
            return true;
        }
    }
    mutex_unlock(pool->mutex);
    return true;
}

bool runtime_client_request_memory_view(
    runtime_client *client,
    uint16_t address,
    uint16_t length,
    runtime_memory_mode mode) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_REQUEST_MEMORY_VIEW,
    };

    if (!client) {
        return false;
    }

    command.data.request_memory.address = address;
    command.data.request_memory.length = length;
    command.data.request_memory.mode = (uint8_t)mode;
    return runtime_client_push(client, &command);
}

bool runtime_client_request_debug_memory(runtime_client *client, bool include_write_history) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_REQUEST_DEBUG_MEMORY,
    };

    if (!client) {
        return false;
    }

    command.data.request_debug_memory.include_write_history = include_write_history ? 1u : 0u;
    return runtime_client_push(client, &command);
}

bool runtime_client_request_frame(runtime_client *client) {
    return runtime_client_send_command(client, RUNTIME_COMMAND_REQUEST_FRAME);
}

bool runtime_client_keyboard_key(runtime_client *client, host_key key, bool pressed) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_KEYBOARD_KEY,
    };

    if (!client) {
        return false;
    }

    command.data.keyboard_key.key = key;
    command.data.keyboard_key.pressed = pressed ? 1u : 0u;
    return runtime_client_push(client, &command);
}

bool runtime_client_set_gameport(
    runtime_client *client,
    const uint8_t axis[4],
    uint8_t button_mask) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_SET_GAMEPORT,
    };

    if (!client || axis == NULL) {
        return false;
    }

    command.data.set_gameport.axis[0] = axis[0];
    command.data.set_gameport.axis[1] = axis[1];
    command.data.set_gameport.axis[2] = axis[2];
    command.data.set_gameport.axis[3] = axis[3];
    command.data.set_gameport.buttons = (uint8_t)(button_mask & 0x07u);
    return runtime_client_push(client, &command);
}

static bool runtime_client_set_cpu_register(
    runtime_client *client,
    runtime_cpu_register reg,
    uint16_t value) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_SET_CPU_REGISTER,
    };

    if (!client) {
        return false;
    }

    command.data.set_cpu_register.reg = reg;
    command.data.set_cpu_register.value = value;
    return runtime_client_push(client, &command);
}

bool runtime_client_set_pc(runtime_client *client, uint16_t value) {
    return runtime_client_set_cpu_register(client, RUNTIME_CPU_REGISTER_PC, value);
}

bool runtime_client_set_sp(runtime_client *client, uint8_t value) {
    return runtime_client_set_cpu_register(client, RUNTIME_CPU_REGISTER_SP, value);
}

bool runtime_client_set_a(runtime_client *client, uint8_t value) {
    return runtime_client_set_cpu_register(client, RUNTIME_CPU_REGISTER_A, value);
}

bool runtime_client_set_x(runtime_client *client, uint8_t value) {
    return runtime_client_set_cpu_register(client, RUNTIME_CPU_REGISTER_X, value);
}

bool runtime_client_set_y(runtime_client *client, uint8_t value) {
    return runtime_client_set_cpu_register(client, RUNTIME_CPU_REGISTER_Y, value);
}

bool runtime_client_set_status(runtime_client *client, uint8_t value) {
    return runtime_client_set_cpu_register(client, RUNTIME_CPU_REGISTER_STATUS, value);
}

bool runtime_client_write_memory_byte(
    runtime_client *client,
    uint16_t address,
    uint8_t value,
    runtime_memory_mode mode) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_WRITE_MEMORY_BYTE,
    };

    if (!client) {
        return false;
    }

    command.data.write_memory_byte.address = address;
    command.data.write_memory_byte.value = value;
    command.data.write_memory_byte.mode = (uint8_t)mode;
    return runtime_client_push(client, &command);
}

bool runtime_client_write_memory(
    runtime_client *client,
    uint16_t address,
    uint16_t length,
    runtime_memory_mode mode,
    const uint8_t *bytes) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_WRITE_MEMORY,
    };

    if (!client || bytes == NULL || length == 0 ||
        length > RUNTIME_MEMORY_SNAPSHOT_MAX) {
        return false;
    }

    command.data.write_memory.address = address;
    command.data.write_memory.length = length;
    command.data.write_memory.mode = (uint8_t)mode;
    memcpy(command.data.write_memory.bytes, bytes, length);
    return runtime_client_push(client, &command);
}

bool runtime_client_set_execute_breakpoint(runtime_client *client, uint16_t address) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_SET_EXECUTE_BREAKPOINT,
    };

    if (!client) {
        return false;
    }

    command.data.set_execute_breakpoint.address = address;
    command.data.set_execute_breakpoint.enabled = 1u;
    return runtime_client_push(client, &command);
}

bool runtime_client_create_breakpoint(
    runtime_client *client,
    const runtime_breakpoint_definition *definition) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_CREATE_BREAKPOINT,
    };

    if (!client || !definition) {
        return false;
    }

    command.data.create_breakpoint.definition = *definition;
    return runtime_client_push(client, &command);
}

bool runtime_client_update_breakpoint(
    runtime_client *client,
    uint32_t id,
    const runtime_breakpoint_definition *definition) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_UPDATE_BREAKPOINT,
    };

    if (!client || !definition) {
        return false;
    }

    command.data.update_breakpoint.id = id;
    command.data.update_breakpoint.definition = *definition;
    return runtime_client_push(client, &command);
}

bool runtime_client_duplicate_breakpoint(runtime_client *client, uint32_t id) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_DUPLICATE_BREAKPOINT,
    };

    if (!client) {
        return false;
    }

    command.data.duplicate_breakpoint.id = id;
    return runtime_client_push(client, &command);
}

bool runtime_client_clear_breakpoint(runtime_client *client, uint32_t id) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_CLEAR_BREAKPOINT,
    };

    if (!client) {
        return false;
    }

    command.data.clear_breakpoint.id = id;
    return runtime_client_push(client, &command);
}

bool runtime_client_clear_all_breakpoints(runtime_client *client) {
    return runtime_client_send_command(client, RUNTIME_COMMAND_CLEAR_ALL_BREAKPOINTS);
}

bool runtime_client_set_breakpoint_enabled(runtime_client *client, uint32_t id, bool enabled) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_SET_BREAKPOINT_ENABLED,
    };

    if (!client) {
        return false;
    }

    command.data.set_breakpoint_enabled.id = id;
    command.data.set_breakpoint_enabled.enabled = enabled ? 1u : 0u;
    return runtime_client_push(client, &command);
}

bool runtime_client_rearm_oneshot_breakpoints(runtime_client *client) {
    return runtime_client_send_command(client, RUNTIME_COMMAND_REARM_ONESHOT_BREAKPOINTS);
}

bool runtime_client_request_breakpoints(runtime_client *client) {
    return runtime_client_send_command(client, RUNTIME_COMMAND_REQUEST_BREAKPOINTS);
}

bool runtime_client_save_state(runtime_client *client, const char *path) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_SAVE_STATE,
    };

    if (!client || !path || path[0] == '\0') {
        return false;
    }

    snprintf(command.data.state_file.path, sizeof(command.data.state_file.path), "%s", path);
    return runtime_client_push(client, &command);
}

bool runtime_client_load_state(runtime_client *client, const char *path) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_LOAD_STATE,
    };

    if (!client || !path || path[0] == '\0') {
        return false;
    }

    snprintf(command.data.state_file.path, sizeof(command.data.state_file.path), "%s", path);
    return runtime_client_push(client, &command);
}

bool runtime_client_set_disk_writable(
    runtime_client *client,
    uint8_t slot,
    uint8_t device,
    bool writable) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_SET_DISK_WRITABLE,
    };

    if (!client || slot < 1u || slot > 7u || device > 1u) {
        return false;
    }

    command.data.disk_writable.slot = slot;
    command.data.disk_writable.device = device;
    command.data.disk_writable.writable = writable ? 1u : 0u;
    return runtime_client_push(client, &command);
}

bool runtime_client_media_insert(
    runtime_client *client,
    uint8_t slot,
    uint8_t device,
    runtime_slot_card_type card_type,
    const char *path) {
    runtime_command command = { .type = RUNTIME_COMMAND_MEDIA_INSERT };

    if (!client || slot < 1u || slot > 7u || device > 1u || !path || path[0] == '\0' ||
        (card_type != RUNTIME_SLOT_CARD_DISKII &&
         card_type != RUNTIME_SLOT_CARD_SMARTPORT)) {
        return false;
    }
    command.data.media_insert.slot = slot;
    command.data.media_insert.device = device;
    command.data.media_insert.card_type = (uint8_t)card_type;
    snprintf(command.data.media_insert.path, sizeof(command.data.media_insert.path), "%s", path);
    return runtime_client_push(client, &command);
}

bool runtime_client_media_eject(runtime_client *client, uint8_t slot, uint8_t device) {
    runtime_command command = { .type = RUNTIME_COMMAND_MEDIA_EJECT };
    if (!client || slot < 1u || slot > 7u || device > 1u) {
        return false;
    }
    command.data.media_device.slot = slot;
    command.data.media_device.device = device;
    return runtime_client_push(client, &command);
}

bool runtime_client_media_swap(
    runtime_client *client,
    uint8_t slot,
    uint8_t device,
    int32_t param,
    bool relative) {
    runtime_command command = { .type = RUNTIME_COMMAND_MEDIA_SWAP };
    if (!client || slot < 1u || slot > 7u || device > 1u) {
        return false;
    }
    command.data.media_swap.slot = slot;
    command.data.media_swap.device = device;
    command.data.media_swap.param = param;
    command.data.media_swap.relative = relative ? 1u : 0u;
    return runtime_client_push(client, &command);
}

bool runtime_client_boot_slot(runtime_client *client, uint8_t slot) {
    runtime_command command = { .type = RUNTIME_COMMAND_BOOT_SLOT };
    if (!client || slot < 1u || slot > 7u) {
        return false;
    }
    command.data.boot_slot.slot = slot;
    return runtime_client_push(client, &command);
}

bool runtime_client_set_display_override(
    runtime_client *client,
    bool enabled,
    uint32_t flags) {
    runtime_command command = { .type = RUNTIME_COMMAND_SET_DISPLAY_OVERRIDE };

    if (!client) {
        return false;
    }
    command.data.set_display_override.enabled = enabled ? 1u : 0u;
    command.data.set_display_override.flags = flags;
    return runtime_client_push(client, &command);
}

bool runtime_client_assemble_file(runtime_client *client, const char *path, uint16_t address) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_ASSEMBLE_FILE,
    };

    if (!client || !path || path[0] == '\0') {
        return false;
    }

    snprintf(command.data.assemble_file.path, sizeof(command.data.assemble_file.path), "%s", path);
    command.data.assemble_file.address = address;
    return runtime_client_push(client, &command);
}

bool runtime_client_assemble_file_full(
    runtime_client *client,
    const char *path,
    uint16_t address,
    uint16_t run_address,
    bool auto_run,
    bool mli_launch,
    bool reset_first,
    bool auto_adjust_segments) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_ASSEMBLE_FILE,
    };

    if (!client || !path || path[0] == '\0') {
        return false;
    }

    snprintf(command.data.assemble_file.path, sizeof(command.data.assemble_file.path), "%s", path);
    command.data.assemble_file.address = address;
    command.data.assemble_file.run_address = run_address;
    command.data.assemble_file.auto_run = auto_run ? 1u : 0u;
    command.data.assemble_file.mli_launch = mli_launch ? 1u : 0u;
    command.data.assemble_file.reset_first = reset_first ? 1u : 0u;
    command.data.assemble_file.auto_adjust_segments =
        auto_adjust_segments ? 1u : 0u;
    return runtime_client_push(client, &command);
}

bool runtime_client_poll_symbols(runtime_client *client, runtime_symbol_snapshot *out) {
    runtime_symbol_slot *slot;

    if (!client || !out || !client->symbol_slot) {
        return false;
    }

    slot = client->symbol_slot;
    mutex_lock(slot->mutex);
    if (!slot->has_symbols) {
        mutex_unlock(slot->mutex);
        return false;
    }

    *out = slot->snapshot;
    slot->has_symbols = false;
    mutex_unlock(slot->mutex);
    return true;
}

bool runtime_client_cycle_turbo_speed(runtime_client *client) {
    return runtime_client_send_command(client, RUNTIME_COMMAND_CYCLE_TURBO_SPEED);
}

bool runtime_client_set_turbo_multiplier(runtime_client *client, uint32_t milli_mhz) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_SET_TURBO_MULTIPLIER,
    };

    if (!client) {
        return false;
    }

    /* 0 = max; any positive milli-MHz is a finite target (no upper mode ID). */
    command.data.set_turbo_multiplier.multiplier = milli_mhz;
    return runtime_client_push(client, &command);
}

bool runtime_client_apply_machine_config(
    runtime_client *client,
    const runtime_machine_config *config,
    const runtime_config *runtime_options,
    const char *ini_path,
    const char *symbol_files,
    bool reset,
    bool save_ini,
    bool resume_running) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_APPLY_MACHINE_CONFIG,
    };

    if (!client || !config) {
        return false;
    }

    command.data.apply_machine_config.config = *config;
    if (runtime_options != NULL) {
        memcpy(
            command.data.apply_machine_config.turbo_speeds,
            runtime_options->turbo_speeds,
            sizeof(command.data.apply_machine_config.turbo_speeds));
        command.data.apply_machine_config.turbo_speed_count = runtime_options->turbo_speed_count;
        command.data.apply_machine_config.active_turbo_multiplier =
            runtime_options->active_turbo_multiplier;
    }
    if (ini_path != NULL) {
        snprintf(command.data.apply_machine_config.ini_path, sizeof(command.data.apply_machine_config.ini_path), "%s", ini_path);
    }
    if (symbol_files != NULL) {
        snprintf(
            command.data.apply_machine_config.symbol_files,
            sizeof(command.data.apply_machine_config.symbol_files),
            "%s",
            symbol_files);
    }
    command.data.apply_machine_config.reset = reset ? 1u : 0u;
    command.data.apply_machine_config.save_ini = save_ini ? 1u : 0u;
    command.data.apply_machine_config.resume_running = resume_running ? 1u : 0u;
    return runtime_client_push(client, &command);
}

bool runtime_client_poll_argb_frame(
    runtime_client *client,
    uint32_t *out_pixels,
    uint32_t max_pixels,
    uint32_t *out_width,
    uint32_t *out_height,
    uint64_t *out_frame_number)
{
    runtime_frame_slot *slot;
    size_t n;

    if (client == NULL || out_pixels == NULL || client->frame_slot == NULL) {
        return false;
    }
    slot = client->frame_slot;
    mutex_lock(slot->mutex);
    if (!slot->has_frame || slot->argb == NULL) {
        mutex_unlock(slot->mutex);
        return false;
    }
    n = (size_t)slot->width * (size_t)slot->height;
    if (n > max_pixels) {
        mutex_unlock(slot->mutex);
        return false;
    }
    memcpy(out_pixels, slot->argb, n * sizeof(uint32_t));
    if (out_width != NULL) {
        *out_width = slot->width;
    }
    if (out_height != NULL) {
        *out_height = slot->height;
    }
    if (out_frame_number != NULL) {
        *out_frame_number = slot->frame_number;
    }
    slot->has_frame = false;
    slot->consumed_frames++;
    mutex_unlock(slot->mutex);
    return true;
}

bool runtime_client_poll_debug_memory(runtime_client *client, runtime_debug_memory_snapshot *out_snapshot) {
    runtime_debug_memory_slot *slot;

    if (!client || !out_snapshot || !client->debug_memory_slot) {
        return false;
    }

    slot = client->debug_memory_slot;
    mutex_lock(slot->mutex);
    if (!slot->has_snapshot) {
        mutex_unlock(slot->mutex);
        return false;
    }

    *out_snapshot = slot->snapshot;
    slot->has_snapshot = false;
    mutex_unlock(slot->mutex);
    return true;
}

bool runtime_client_poll_breakpoints(
    runtime_client *client,
    runtime_breakpoint_snapshot *out_snapshot) {
    runtime_breakpoint_slot *slot;

    if (!client || !out_snapshot || !client->breakpoint_slot) {
        return false;
    }

    slot = client->breakpoint_slot;
    mutex_lock(slot->mutex);
    if (!slot->has_snapshot) {
        mutex_unlock(slot->mutex);
        return false;
    }

    /* Latest-wins copy; leave slot filled so multiple consumers (UI + control
       deferred + tests) can read the same generation after one notify event. */
    *out_snapshot = slot->snapshot;
    mutex_unlock(slot->mutex);
    return true;
}

bool runtime_client_poll_event(
    runtime_client *client,
    runtime_event *out_event) {
    if (!client || !out_event) {
        return false;
    }

    return message_queue_try_pop(client->event_queue, out_event);
}

bool runtime_client_step_out(runtime_client *client) {
    return runtime_client_send_command(client, RUNTIME_COMMAND_STEP_OUT);
}

bool runtime_client_step_over(runtime_client *client) {
    return runtime_client_send_command(client, RUNTIME_COMMAND_STEP_OVER);
}

bool runtime_client_run_to_cursor(runtime_client *client, uint16_t address) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_RUN_TO_CURSOR,
    };

    if (!client) {
        return false;
    }

    command.data.run_to_cursor.address = address;
    return runtime_client_push(client, &command);
}

bool runtime_client_history_info(
    runtime_client *client,
    uint64_t request_token) {
    return runtime_client_send_command_token(
        client, RUNTIME_COMMAND_HISTORY_INFO, request_token);
}

bool runtime_client_history_record(
    runtime_client *client,
    bool enabled,
    uint64_t request_token) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_HISTORY_RECORD,
        .request_token = request_token,
    };
    if (client == NULL) {
        return false;
    }
    command.data.history_record.enabled = enabled ? 1u : 0u;
    return runtime_client_push(client, &command);
}

bool runtime_client_set_history_off_on_max(runtime_client *client, bool enabled) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_SET_HISTORY_OFF_ON_MAX,
    };
    if (client == NULL) {
        return false;
    }
    command.data.set_history_off_on_max.enabled = enabled ? 1u : 0u;
    return runtime_client_push(client, &command);
}

bool runtime_client_tm_set_enabled(
    runtime_client *client,
    bool enabled,
    uint64_t request_token) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_TM_SET_ENABLED,
        .request_token = request_token,
    };
    if (client == NULL) {
        return false;
    }
    command.data.tm_set_enabled.enabled = enabled ? 1u : 0u;
    return runtime_client_push(client, &command);
}

bool runtime_client_tm_query(
    runtime_client *client,
    runtime_tm_query_op op,
    const runtime_tm_query_args *args,
    uint64_t request_token) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_TM_QUERY,
        .request_token = request_token,
    };
    if (client == NULL) {
        return false;
    }
    command.data.tm_query.op = (uint8_t)op;
    if (args != NULL) {
        command.data.tm_query.direction =
            args->direction < 0 ? (int8_t)-1 : (int8_t)(args->direction > 0 ? 1 : 0);
        command.data.tm_query.target_pc = args->target_pc;
        command.data.tm_query.cycle_ceiling = args->cycle_ceiling;
        command.data.tm_query.history_id = args->history_id;
        command.data.tm_query.cycle = args->cycle;
        command.data.tm_query.epoch = args->epoch;
    }
    return runtime_client_push(client, &command);
}

bool runtime_client_tm_step(
    runtime_client *client,
    int direction,
    uint64_t request_token) {
    runtime_tm_query_args args;
    memset(&args, 0, sizeof(args));
    args.direction = direction < 0 ? -1 : 1;
    return runtime_client_tm_query(
        client, RUNTIME_TM_QUERY_STEP, &args, request_token);
}

bool runtime_client_tm_step_over(
    runtime_client *client,
    uint64_t request_token) {
    return runtime_client_tm_query(
        client, RUNTIME_TM_QUERY_STEP_OVER, NULL, request_token);
}

bool runtime_client_tm_step_out(
    runtime_client *client,
    uint64_t request_token) {
    return runtime_client_tm_query(
        client, RUNTIME_TM_QUERY_STEP_OUT, NULL, request_token);
}

bool runtime_client_tm_run_to(
    runtime_client *client,
    uint16_t pc,
    uint64_t cycle_ceiling,
    uint64_t request_token) {
    runtime_tm_query_args args;
    memset(&args, 0, sizeof(args));
    args.target_pc = pc;
    args.cycle_ceiling = cycle_ceiling;
    return runtime_client_tm_query(
        client, RUNTIME_TM_QUERY_RUN_TO_PC, &args, request_token);
}

bool runtime_client_tm_seek_id(
    runtime_client *client,
    uint64_t epoch,
    uint64_t history_id,
    uint64_t request_token) {
    runtime_tm_query_args args;
    memset(&args, 0, sizeof(args));
    args.epoch = epoch;
    args.history_id = history_id;
    return runtime_client_tm_query(
        client, RUNTIME_TM_QUERY_SEEK_ID, &args, request_token);
}

bool runtime_client_tm_seek_cycle(
    runtime_client *client,
    uint64_t cycle,
    uint64_t request_token) {
    runtime_tm_query_args args;
    memset(&args, 0, sizeof(args));
    args.cycle = cycle;
    return runtime_client_tm_query(
        client, RUNTIME_TM_QUERY_SEEK_CYCLE, &args, request_token);
}

bool runtime_client_tm_enter_forensic(
    runtime_client *client,
    uint64_t request_token) {
    return runtime_client_send_command_token(
        client, RUNTIME_COMMAND_TM_ENTER_FORENSIC, request_token);
}

bool runtime_client_tm_exit_forensic(
    runtime_client *client,
    uint64_t request_token) {
    return runtime_client_send_command_token(
        client, RUNTIME_COMMAND_TM_EXIT_FORENSIC, request_token);
}

bool runtime_client_tm_bp_create(
    runtime_client *client,
    const runtime_breakpoint_definition *definition) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_TM_BP_CREATE,
    };

    if (!client || !definition) {
        return false;
    }
    command.data.create_breakpoint.definition = *definition;
    return runtime_client_push(client, &command);
}

bool runtime_client_tm_bp_update(
    runtime_client *client,
    uint32_t id,
    const runtime_breakpoint_definition *definition) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_TM_BP_UPDATE,
    };

    if (!client || !definition) {
        return false;
    }
    command.data.update_breakpoint.id = id;
    command.data.update_breakpoint.definition = *definition;
    return runtime_client_push(client, &command);
}

bool runtime_client_tm_bp_clear(runtime_client *client, uint32_t id) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_TM_BP_CLEAR,
    };

    if (!client) {
        return false;
    }
    command.data.clear_breakpoint.id = id;
    return runtime_client_push(client, &command);
}

bool runtime_client_tm_bp_clear_all(runtime_client *client) {
    return runtime_client_send_command(client, RUNTIME_COMMAND_TM_BP_CLEAR_ALL);
}

bool runtime_client_tm_bp_set_enabled(
    runtime_client *client, uint32_t id, bool enabled) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_TM_BP_SET_ENABLED,
    };

    if (!client) {
        return false;
    }
    command.data.set_breakpoint_enabled.id = id;
    command.data.set_breakpoint_enabled.enabled = enabled ? 1u : 0u;
    return runtime_client_push(client, &command);
}

bool runtime_client_tm_bp_request(runtime_client *client) {
    return runtime_client_send_command(client, RUNTIME_COMMAND_TM_BP_REQUEST);
}

bool runtime_client_tm_set_execute_breakpoint(
    runtime_client *client, uint16_t address) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_TM_SET_EXECUTE_BREAKPOINT,
    };

    if (!client) {
        return false;
    }
    command.data.set_execute_breakpoint.address = address;
    command.data.set_execute_breakpoint.enabled = 1u;
    return runtime_client_push(client, &command);
}

bool runtime_client_tm_run_until_break(
    runtime_client *client, uint64_t request_token) {
    return runtime_client_send_command_token(
        client, RUNTIME_COMMAND_TM_RUN_UNTIL_BREAK, request_token);
}

bool runtime_client_history_clear(
    runtime_client *client,
    uint64_t request_token) {
    return runtime_client_send_command_token(
        client, RUNTIME_COMMAND_HISTORY_CLEAR, request_token);
}

bool runtime_client_history_find(
    runtime_client *client,
    uint32_t session_id,
    const runtime_history_query *query,
    runtime_history_from_kind from_kind,
    uint64_t from_id,
    uint16_t limit,
    uint64_t request_token) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_HISTORY_FIND,
        .request_token = request_token,
    };

    if (client == NULL || query == NULL || request_token == 0u ||
        from_kind > RUNTIME_HISTORY_FROM_NEWEST ||
        limit == 0u || limit > RUNTIME_HISTORY_MAX_QUERY_RECORDS) {
        return false;
    }
    command.data.history_find.query = *query;
    command.data.history_find.from_id = from_id;
    command.data.history_find.session_id = session_id;
    command.data.history_find.from_kind = (uint8_t)from_kind;
    command.data.history_find.limit = limit;
    return runtime_client_push(client, &command);
}

bool runtime_client_history_next(
    runtime_client *client,
    uint32_t session_id,
    uint64_t cursor,
    uint16_t limit,
    uint64_t request_token) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_HISTORY_NEXT,
        .request_token = request_token,
    };

    if (client == NULL || cursor == 0u || request_token == 0u ||
        limit == 0u || limit > RUNTIME_HISTORY_MAX_QUERY_RECORDS) {
        return false;
    }
    command.data.history_next.cursor = cursor;
    command.data.history_next.session_id = session_id;
    command.data.history_next.limit = limit;
    return runtime_client_push(client, &command);
}

bool runtime_client_history_read(
    runtime_client *client,
    uint32_t session_id,
    uint64_t epoch,
    uint64_t id,
    uint16_t before,
    uint16_t after,
    uint64_t request_token) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_HISTORY_READ,
        .request_token = request_token,
    };

    if (client == NULL || id == 0u || request_token == 0u ||
        before > RUNTIME_HISTORY_MAX_QUERY_RECORDS ||
        after > RUNTIME_HISTORY_MAX_QUERY_RECORDS) {
        return false;
    }
    command.data.history_read.epoch = epoch;
    command.data.history_read.id = id;
    command.data.history_read.session_id = session_id;
    command.data.history_read.before = before;
    command.data.history_read.after = after;
    return runtime_client_push(client, &command);
}

bool runtime_client_history_close(
    runtime_client *client,
    uint32_t session_id,
    uint64_t cursor,
    uint64_t request_token) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_HISTORY_CLOSE,
        .request_token = request_token,
    };

    if (client == NULL || request_token == 0u) {
        return false;
    }
    command.data.history_close.cursor = cursor;
    command.data.history_close.session_id = session_id;
    return runtime_client_push(client, &command);
}

bool runtime_client_session_open(
    runtime_client *client,
    runtime_session_kind kind,
    uint64_t endpoint_epoch,
    uint64_t request_token) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_SESSION_OPEN,
        .request_token = request_token,
    };

    if (client == NULL || request_token == 0u ||
        (kind != RUNTIME_SESSION_KIND_UI &&
         kind != RUNTIME_SESSION_KIND_CONTROL)) {
        return false;
    }
    command.data.session_open.kind = (uint8_t)kind;
    command.data.session_open.endpoint_epoch = endpoint_epoch;
    return runtime_client_push(client, &command);
}

bool runtime_client_session_close(
    runtime_client *client,
    uint32_t session_id,
    uint64_t request_token) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_SESSION_CLOSE,
        .request_token = request_token,
    };

    if (client == NULL || session_id == 0u || request_token == 0u) {
        return false;
    }
    command.data.session_close.session_id = session_id;
    return runtime_client_push(client, &command);
}

bool runtime_client_paste_text(runtime_client *client, const char *text, size_t length) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_PASTE_TEXT,
    };

    if (!client || !text || length == 0) {
        return false;
    }

    if (length > RUNTIME_PASTE_TEXT_MAX) {
        length = RUNTIME_PASTE_TEXT_MAX;
    }

    memcpy(command.data.paste_text.text, text, length);
    command.data.paste_text.length = length;
    return runtime_client_push(client, &command);
}

bool runtime_client_load_bin(
    runtime_client *client,
    const char *path,
    uint16_t address,
    apple2_binary_format format,
    bool reset_first,
    bool is_basic_text,
    bool run_after_load) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_LOAD_BIN,
    };

    if (!client || !path || path[0] == '\0') {
        return false;
    }

    snprintf(command.data.load_bin.path, sizeof(command.data.load_bin.path), "%s", path);
    command.data.load_bin.address = address;
    command.data.load_bin.format = (uint8_t)format;
    command.data.load_bin.reset_first = reset_first ? 1u : 0u;
    command.data.load_bin.is_basic_text = is_basic_text ? 1u : 0u;
    command.data.load_bin.run_after_load = run_after_load ? 1u : 0u;
    return runtime_client_push(client, &command);
}

bool runtime_client_save_bin(
    runtime_client *client,
    const char *path,
    uint16_t start_address,
    uint16_t end_address,
    apple2_binary_format format,
    bool is_basic_text) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_SAVE_BIN,
    };

    if (!client || !path || path[0] == '\0') {
        return false;
    }

    snprintf(command.data.save_bin.path, sizeof(command.data.save_bin.path), "%s", path);
    command.data.save_bin.start_address = start_address;
    command.data.save_bin.end_address = end_address;
    command.data.save_bin.format = (uint8_t)format;
    command.data.save_bin.is_basic_text = is_basic_text ? 1u : 0u;
    return runtime_client_push(client, &command);
}

bool runtime_client_request_call_stack(runtime_client *client) {
    return runtime_client_send_command(client, RUNTIME_COMMAND_REQUEST_CALL_STACK);
}

/* -- frame ring ---------------------------------------------------------- */

void runtime_client_get_frame_ring_info(
    runtime_client *client,
    runtime_frame_ring_info *out_info) {
    if (out_info == NULL) {
        return;
    }
    if (client == NULL || client->frame_ring == NULL) {
        memset(out_info, 0, sizeof(*out_info));
        return;
    }
    runtime_frame_ring_get_info(client->frame_ring, out_info);
}

bool runtime_client_copy_frame_at(
    runtime_client *client,
    uint64_t target,
    bool by_cycle,
    runtime_ring_frame *out_frame) {
    if (client == NULL || client->frame_ring == NULL || out_frame == NULL) {
        return false;
    }
    return by_cycle ?
        runtime_frame_ring_copy_by_cycle(client->frame_ring, target, out_frame) :
        runtime_frame_ring_copy_by_frame(client->frame_ring, target, out_frame);
}

bool runtime_client_copy_frame_at_index(
    runtime_client *client,
    uint32_t index,
    runtime_ring_frame *out_frame) {
    if (client == NULL || client->frame_ring == NULL || out_frame == NULL) {
        return false;
    }
    return runtime_frame_ring_copy_by_index(client->frame_ring, index, out_frame);
}

bool runtime_client_frame_meta_at_index(
    runtime_client *client,
    uint32_t index,
    uint64_t *out_frame_number,
    uint64_t *out_machine_cycle) {
    if (client == NULL || client->frame_ring == NULL) {
        return false;
    }
    return runtime_frame_ring_meta_at_index(
        client->frame_ring, index, out_frame_number, out_machine_cycle);
}

void runtime_client_set_frame_ring_recording(runtime_client *client, bool recording) {
    if (client == NULL || client->frame_ring == NULL) {
        return;
    }
    runtime_frame_ring_set_recording(client->frame_ring, recording);
}

void runtime_client_clear_frame_ring(runtime_client *client) {
    if (client == NULL || client->frame_ring == NULL) {
        return;
    }
    runtime_frame_ring_clear(client->frame_ring);
}
