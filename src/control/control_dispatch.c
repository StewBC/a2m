#include "control_dispatch.h"

#include "apple2.h"
#include "control_breakpoint.h"
#include "display_frame.h"
#include "runtime.h"
#include "runtime_event.h"
#include "runtime_history.h"
#include "runtime_slot_resolve.h"
#include "softswitch.h"

#include <SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void control_dispatch_init(
    control_dispatch_t *disp,
    control_server_t *server,
    runtime_client *client)
{
    if (disp == NULL) {
        return;
    }
    memset(disp, 0, sizeof(*disp));
    disp->server = server;
    disp->client = client;
    disp->seen_paused = true;
    disp->machine_running = false;
    disp->turbo_mode = 1000u; /* 1 MHz */
    disp->latch_paused = true;
    strncpy(disp->stop_reason, "none", sizeof(disp->stop_reason) - 1);
}

void control_dispatch_shutdown(control_dispatch_t *disp)
{
    deferred_control_response *d;

    if (disp == NULL) {
        return;
    }
    d = control_deferred_active(&disp->deferred);
    if (d != NULL) {
        if (d->request_token != 0u && disp->client != NULL) {
            (void)runtime_client_cancel_rpc(disp->client, d->request_token);
        }
        control_deferred_clear(d);
    }
    memset(disp, 0, sizeof(*disp));
}

static const char *stop_reason_name(runtime_stop_reason reason)
{
    switch (reason) {
    case RUNTIME_STOP_REASON_NONE:
        return "none";
    case RUNTIME_STOP_REASON_RESET:
        return "reset";
    case RUNTIME_STOP_REASON_PAUSE_COMMAND:
        return "pause";
    case RUNTIME_STOP_REASON_STEP:
        return "step";
    case RUNTIME_STOP_REASON_RUN_COMPLETE:
        return "run-complete";
    case RUNTIME_STOP_REASON_BREAKPOINT:
        return "breakpoint";
    case RUNTIME_STOP_REASON_BRK:
        return "brk";
    case RUNTIME_STOP_REASON_ERROR:
        return "error";
    default:
        return "unknown";
    }
}

static void set_stop_reason(control_dispatch_t *disp, const char *reason)
{
    if (disp == NULL || reason == NULL) {
        return;
    }
    strncpy(disp->stop_reason, reason, sizeof(disp->stop_reason) - 1u);
    disp->stop_reason[sizeof(disp->stop_reason) - 1u] = '\0';
}

static void post_ok(control_dispatch_t *disp, uint32_t id, const char *text)
{
    control_response response;
    control_protocol_format_ok(&response, id, text);
    (void)control_server_post_response(disp->server, &response);
}

static void post_error(
    control_dispatch_t *disp,
    uint32_t id,
    const char *code,
    const char *message)
{
    control_response response;
    control_protocol_format_error(&response, id, code, message, false);
    (void)control_server_post_response(disp->server, &response);
}

static deferred_control_response *begin_deferred(
    control_dispatch_t *disp,
    uint32_t id,
    control_deferred_kind kind,
    uint32_t timeout_ms,
    uint64_t token)
{
    const char *busy = NULL;
    deferred_control_response *d = control_deferred_reserve(&disp->deferred, &busy);
    if (d == NULL) {
        post_error(disp, id, "busy", busy != NULL ? busy : "deferred");
        return NULL;
    }
    d->active = true;
    d->request_id = id;
    d->kind = kind;
    d->request_token = token;
    d->connection_epoch = control_server_connection_epoch(disp->server);
    d->deadline_ms = (uint64_t)SDL_GetTicks() + (uint64_t)timeout_ms;
    return d;
}

static void cache_slot_map_from_machine_state(
    control_dispatch_t *disp,
    const runtime_machine_snapshot *state)
{
    int slot;

    if (disp == NULL || state == NULL) {
        return;
    }
    for (slot = 1; slot <= 7; ++slot) {
        disp->slot_cards[slot] = state->slots[slot].card_type;
    }
    disp->slot_cards[0] = RUNTIME_SLOT_CARD_EMPTY;
    disp->has_slot_map = true;
}

/* Resolve slot 0 → installed Disk II. allow_empty: mount may target empty. */
static bool diskii_pick_slot(
    control_dispatch_t *disp,
    uint8_t requested_slot,
    bool allow_empty,
    uint8_t *out_slot,
    const char **out_error)
{
    uint8_t slot;
    runtime_slot_card_type card;

    if (out_slot == NULL) {
        return false;
    }
    if (requested_slot != 0u) {
        slot = requested_slot;
        if (slot < 1u || slot > 7u) {
            if (out_error != NULL) {
                *out_error = "slot";
            }
            return false;
        }
        if (disp->has_slot_map) {
            card = disp->slot_cards[slot];
            if (card == RUNTIME_SLOT_CARD_DISKII ||
                (allow_empty && card == RUNTIME_SLOT_CARD_EMPTY)) {
                *out_slot = slot;
                return true;
            }
            if (out_error != NULL) {
                *out_error = "wrong-card";
            }
            return false;
        }
        *out_slot = slot;
        return true;
    }

    if (!disp->has_slot_map) {
        if (out_error != NULL) {
            *out_error = "not-ready";
        }
        return false;
    }
    slot = (uint8_t)runtime_resolve_diskii_slot(disp->slot_cards);
    if (slot == 0u) {
        if (out_error != NULL) {
            *out_error = "no-diskii";
        }
        return false;
    }
    *out_slot = slot;
    return true;
}

static void post_diskii_ok(
    control_dispatch_t *disp,
    uint32_t request_id,
    control_command_type op,
    uint8_t slot,
    uint8_t drive,
    const char *path,
    uint32_t disk_index,
    uint8_t writable)
{
    char text[CONTROL_RESPONSE_TEXT_MAX];

    if (op == CONTROL_COMMAND_MOUNT_DISK) {
        snprintf(
            text,
            sizeof(text),
            "accepted=1 slot=%u drive=%u",
            (unsigned)slot,
            (unsigned)drive);
        (void)path;
    } else if (op == CONTROL_COMMAND_SELECT_DISK) {
        snprintf(
            text,
            sizeof(text),
            "accepted=1 slot=%u drive=%u index=%u",
            (unsigned)slot,
            (unsigned)drive,
            (unsigned)disk_index);
    } else {
        snprintf(
            text,
            sizeof(text),
            "accepted=1 slot=%u drive=%u writable=%u",
            (unsigned)slot,
            (unsigned)drive,
            (unsigned)writable);
    }
    post_ok(disp, request_id, text);
}

static bool execute_diskii_op(
    control_dispatch_t *disp,
    uint32_t request_id,
    control_command_type op,
    uint8_t requested_slot,
    uint8_t drive,
    const char *path,
    uint32_t disk_index,
    uint8_t writable)
{
    uint8_t slot = 0;
    const char *err = "bad-args";
    bool allow_empty = (op == CONTROL_COMMAND_MOUNT_DISK);
    runtime_client *client;

    if (disp == NULL || disp->client == NULL) {
        post_error(disp, request_id, "runtime", "no client");
        return true;
    }
    client = disp->client;
    if (drive > 1u) {
        post_error(disp, request_id, "bad-args", "drive");
        return true;
    }
    if (!diskii_pick_slot(disp, requested_slot, allow_empty, &slot, &err)) {
        if (strcmp(err, "not-ready") == 0) {
            return false;
        }
        post_error(disp, request_id, err, "diskii");
        return true;
    }

    if (op == CONTROL_COMMAND_MOUNT_DISK) {
        if (path == NULL || path[0] == '\0') {
            post_error(disp, request_id, "bad-args", "path");
            return true;
        }
        if (!runtime_client_media_insert(
                client, slot, drive, RUNTIME_SLOT_CARD_DISKII, path)) {
            post_error(disp, request_id, "bad-args", "mount");
            return true;
        }
    } else if (op == CONTROL_COMMAND_SELECT_DISK) {
        if (disk_index == 0u ||
            !runtime_client_media_swap(
                client, slot, drive, (int32_t)disk_index, false)) {
            post_error(disp, request_id, "bad-args", "select-disk");
            return true;
        }
    } else if (op == CONTROL_COMMAND_SET_DISK_WRITABLE) {
        if (!runtime_client_set_disk_writable(
                client, slot, drive, writable != 0u)) {
            post_error(disp, request_id, "bad-args", "set-disk-writable");
            return true;
        }
    } else {
        post_error(disp, request_id, "bad-args", "diskii");
        return true;
    }

    post_diskii_ok(
        disp, request_id, op, slot, drive, path, disk_index, writable);
    return true;
}

static void begin_diskii_op_deferred(
    control_dispatch_t *disp,
    uint32_t request_id,
    control_command_type op,
    uint8_t requested_slot,
    uint8_t drive,
    const char *path,
    uint32_t disk_index,
    uint8_t writable)
{
    deferred_control_response *d;

    d = begin_deferred(disp, request_id, CONTROL_DEFERRED_DISKII_OP, 2000u, 0u);
    if (d == NULL) {
        return;
    }
    d->diskii_op = op;
    d->diskii_slot = requested_slot;
    d->diskii_drive = drive;
    d->diskii_index = disk_index;
    d->diskii_writable = writable;
    d->diskii_path[0] = '\0';
    if (path != NULL) {
        strncpy(d->diskii_path, path, sizeof(d->diskii_path) - 1u);
        d->diskii_path[sizeof(d->diskii_path) - 1u] = '\0';
    }
    if (!runtime_client_request_machine_state(disp->client)) {
        post_error(disp, request_id, "runtime", "machine-state");
        control_deferred_clear(d);
    }
}

static void handle_diskii_command(
    control_dispatch_t *disp,
    uint32_t request_id,
    control_command_type op,
    uint8_t requested_slot,
    uint8_t drive,
    const char *path,
    uint32_t disk_index,
    uint8_t writable)
{
    if (execute_diskii_op(
            disp,
            request_id,
            op,
            requested_slot,
            drive,
            path,
            disk_index,
            writable)) {
        return;
    }
    begin_diskii_op_deferred(
        disp,
        request_id,
        op,
        requested_slot,
        drive,
        path,
        disk_index,
        writable);
}

static runtime_memory_mode to_runtime_memory_mode(uint8_t mode)
{
    switch (mode) {
    case CONTROL_MEMORY_MODE_MAIN:
        return RUNTIME_MEMORY_MODE_MAIN;
    case CONTROL_MEMORY_MODE_AUX:
        return RUNTIME_MEMORY_MODE_AUX;
    case CONTROL_MEMORY_MODE_LC1:
        return RUNTIME_MEMORY_MODE_LC1;
    case CONTROL_MEMORY_MODE_LC2:
        return RUNTIME_MEMORY_MODE_LC2;
    case CONTROL_MEMORY_MODE_ROM:
        return RUNTIME_MEMORY_MODE_ROM;
    case CONTROL_MEMORY_MODE_MAP:
    default:
        return RUNTIME_MEMORY_MODE_MAP;
    }
}

static void clear_execution_latches(control_dispatch_t *disp)
{
    disp->latch_paused = false;
    disp->latch_running = false;
    disp->latch_step_complete = false;
    disp->latch_run_complete = false;
    disp->latch_breakpoints = false;
}

/* Instantaneous softswitch / beam dump (Apple analogue of c64m get-vic).
   get-memory of $C0xx is meaningless — it peeks RAM, not latched flags. */
static void format_softswitches_text(
    char *text,
    size_t text_size,
    const runtime_machine_snapshot *ms)
{
    uint32_t f;
    const char *model;

    if (text == NULL || text_size == 0u || ms == NULL) {
        return;
    }
    f = ms->apple_state_flags;
    model = (ms->apple_model == (uint8_t)APPLE2_MODEL_IIE_ENHANCED) ? "//e-enh" : "][+";
    snprintf(
        text,
        text_size,
        "model=%s flags=$%08X motors=$%02X "
        "TEXT=%u MIXED=%u PAGE2=%u HIRES=%u COL80=%u DHIRES=%u ALTCHAR=%u 80STORE=%u "
        "RAMRD=%u RAMWRT=%u ALTZP=%u LC_READ=%u LC_WRITE=%u LC_BANK2=%u "
        "CXROM=%u C3ROM_OFF=%u LC_PRE_WRITE=%u OA=%u CA=%u KEY_HELD=%u "
        "line=%u cycle=%u frame=%llu",
        model,
        (unsigned)f,
        (unsigned)ms->disk_motor_mask,
        (f & A2S_TEXT) != 0u ? 1u : 0u,
        (f & A2S_MIXED) != 0u ? 1u : 0u,
        (f & A2S_PAGE2) != 0u ? 1u : 0u,
        (f & A2S_HIRES) != 0u ? 1u : 0u,
        (f & A2S_COL80) != 0u ? 1u : 0u,
        (f & A2S_DHIRES) != 0u ? 1u : 0u,
        (f & A2S_ALTCHARSET) != 0u ? 1u : 0u,
        (f & A2S_80STORE) != 0u ? 1u : 0u,
        (f & A2S_RAMRD) != 0u ? 1u : 0u,
        (f & A2S_RAMWRT) != 0u ? 1u : 0u,
        (f & A2S_ALTZP) != 0u ? 1u : 0u,
        (f & A2S_LC_READ) != 0u ? 1u : 0u,
        (f & A2S_LC_WRITE) != 0u ? 1u : 0u,
        (f & A2S_LC_BANK2) != 0u ? 1u : 0u,
        (f & A2S_CXSLOTROM_MB_ENABLE) != 0u ? 1u : 0u,
        (f & A2S_SLOT3ROM_MB_DISABLE) != 0u ? 1u : 0u,
        (f & A2S_LC_PRE_WRITE) != 0u ? 1u : 0u,
        (f & A2S_OPEN_APPLE) != 0u ? 1u : 0u,
        (f & A2S_CLOSED_APPLE) != 0u ? 1u : 0u,
        (f & A2S_KEY_HELD) != 0u ? 1u : 0u,
        (unsigned)ms->video_line,
        (unsigned)ms->video_cycle_in_line,
        (unsigned long long)ms->frame_number);
}

static bool try_post_frame(control_dispatch_t *disp, uint32_t request_id)
{
    control_response response;
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t frame_number = 0;
    size_t pixel_count = (size_t)DISPLAY_FRAME_WIDTH * (size_t)DISPLAY_FRAME_HEIGHT;
    uint32_t *pixels = (uint32_t *)malloc(pixel_count * sizeof(uint32_t));
    char meta[CONTROL_RESPONSE_TEXT_MAX];

    if (pixels == NULL) {
        post_error(disp, request_id, "memory", "allocation-failed");
        return true;
    }

    if (!runtime_client_poll_argb_frame(
            disp->client,
            pixels,
            (uint32_t)pixel_count,
            &width,
            &height,
            &frame_number)) {
        free(pixels);
        return false;
    }

    if (width == 0u || height == 0u) {
        width = DISPLAY_FRAME_WIDTH;
        height = DISPLAY_FRAME_HEIGHT;
    }
    disp->frame_number = frame_number;
    snprintf(
        meta,
        sizeof(meta),
        "width=%u height=%u stride=%u format=argb8888 frame=%llu",
        width,
        height,
        width * 4u,
        (unsigned long long)frame_number);
    control_protocol_format_data(
        &response,
        request_id,
        "frame",
        meta,
        (uint8_t *)pixels,
        (size_t)width * (size_t)height * 4u);
    if (!control_server_post_response(disp->server, &response)) {
        free(pixels);
    }
    return true;
}

static const char *event_name_for_type(runtime_event_type type)
{
    switch (type) {
    case RUNTIME_EVENT_RUNNING:
        return "running";
    case RUNTIME_EVENT_PAUSED:
        return "paused";
    case RUNTIME_EVENT_STEP_COMPLETE:
        return "step-complete";
    case RUNTIME_EVENT_RUN_COMPLETE:
        return "run-complete";
    case RUNTIME_EVENT_RESET_COMPLETE:
        return "reset-complete";
    case RUNTIME_EVENT_BREAKPOINTS_RESPONSE:
        return "breakpoints";
    case RUNTIME_EVENT_FRAME_READY:
        return "frame";
    default:
        return NULL;
    }
}

static bool latch_matches_event(const control_dispatch_t *disp, const char *name)
{
    if (name == NULL) {
        return false;
    }
    if (strcmp(name, "paused") == 0) {
        return disp->latch_paused;
    }
    if (strcmp(name, "running") == 0) {
        return disp->latch_running;
    }
    if (strcmp(name, "step-complete") == 0) {
        return disp->latch_step_complete;
    }
    if (strcmp(name, "run-complete") == 0) {
        return disp->latch_run_complete;
    }
    if (strcmp(name, "reset-complete") == 0) {
        return disp->latch_reset_complete;
    }
    if (strcmp(name, "breakpoints") == 0) {
        return disp->latch_breakpoints;
    }
    if (strcmp(name, "frame") == 0) {
        return disp->latch_frame;
    }
    return false;
}

static void consume_latch(control_dispatch_t *disp, const char *name)
{
    if (name == NULL) {
        return;
    }
    if (strcmp(name, "paused") == 0) {
        disp->latch_paused = false;
    } else if (strcmp(name, "running") == 0) {
        disp->latch_running = false;
    } else if (strcmp(name, "step-complete") == 0) {
        disp->latch_step_complete = false;
    } else if (strcmp(name, "run-complete") == 0) {
        disp->latch_run_complete = false;
    } else if (strcmp(name, "reset-complete") == 0) {
        disp->latch_reset_complete = false;
    } else if (strcmp(name, "breakpoints") == 0) {
        disp->latch_breakpoints = false;
    } else if (strcmp(name, "frame") == 0) {
        disp->latch_frame = false;
    }
}

void control_dispatch_on_runtime_event(
    control_dispatch_t *disp,
    const runtime_event *event)
{
    deferred_control_response *d;
    const char *ename;

    if (disp == NULL || event == NULL) {
        return;
    }

    if (event->type == RUNTIME_EVENT_RUNNING) {
        disp->machine_running = true;
        disp->seen_paused = false;
        disp->latch_running = true;
        set_stop_reason(disp, "none");
    } else if (event->type == RUNTIME_EVENT_MACHINE_STATE_RESPONSE) {
        /* Authoritative stop reason (breakpoint vs pause command vs step). */
        set_stop_reason(
            disp, stop_reason_name(event->data.machine_state.stop_reason));
        disp->cycle = event->data.machine_state.cpu_cycles;
        disp->frame_number = event->data.machine_state.frame_number;
        disp->has_cpu = true;
        disp->last_pc = event->data.machine_state.pc;
        disp->last_a = event->data.machine_state.a;
        disp->last_x = event->data.machine_state.x;
        disp->last_y = event->data.machine_state.y;
        disp->last_sp = event->data.machine_state.sp;
        disp->last_p = event->data.machine_state.p;
        disp->turbo_mode = event->data.machine_state.active_turbo_multiplier;
        cache_slot_map_from_machine_state(disp, &event->data.machine_state);
        if (event->data.machine_state.running == 0u) {
            disp->machine_running = false;
            disp->seen_paused = true;
        }
    } else if (event->type == RUNTIME_EVENT_PAUSED) {
        disp->machine_running = false;
        disp->seen_paused = true;
        disp->latch_paused = true;
        /* Do not overwrite MACHINE_STATE stop_reason (e.g. breakpoint). */
        if (strcmp(disp->stop_reason, "none") == 0 ||
            disp->stop_reason[0] == '\0') {
            set_stop_reason(disp, "pause");
        }
    } else if (event->type == RUNTIME_EVENT_STEP_COMPLETE) {
        disp->machine_running = false;
        disp->seen_paused = true;
        disp->latch_paused = true;
        disp->latch_step_complete = true;
        set_stop_reason(
            disp, stop_reason_name(event->data.step_complete.reason));
        if (strcmp(disp->stop_reason, "none") == 0) {
            set_stop_reason(disp, "step");
        }
    } else if (event->type == RUNTIME_EVENT_RUN_COMPLETE) {
        disp->machine_running = false;
        disp->seen_paused = true;
        disp->latch_paused = true;
        disp->latch_run_complete = true;
        set_stop_reason(disp, "run-complete");
    } else if (event->type == RUNTIME_EVENT_RESET_COMPLETE) {
        disp->latch_reset_complete = true;
    } else if (event->type == RUNTIME_EVENT_BREAKPOINTS_RESPONSE) {
        disp->latch_breakpoints = true;
    } else if (event->type == RUNTIME_EVENT_FRAME_READY) {
        disp->latch_frame = true;
        disp->frame_number += 1u;
    }

    if (event->type == RUNTIME_EVENT_CPU_STATE_RESPONSE) {
        disp->has_cpu = true;
        disp->last_pc = event->data.cpu_state.pc;
        disp->last_a = event->data.cpu_state.a;
        disp->last_x = event->data.cpu_state.x;
        disp->last_y = event->data.cpu_state.y;
        disp->last_sp = event->data.cpu_state.sp;
        disp->last_p = event->data.cpu_state.p;
        disp->cycle = event->data.cpu_state.cycles;
    }

    d = control_deferred_active(&disp->deferred);
    if (d == NULL) {
        return;
    }

    if (d->kind == CONTROL_DEFERRED_DISKII_OP &&
        event->type == RUNTIME_EVENT_MACHINE_STATE_RESPONSE) {
        /* Slot map already cached above; resolve and run the pending op. */
        (void)execute_diskii_op(
            disp,
            d->request_id,
            d->diskii_op,
            d->diskii_slot,
            d->diskii_drive,
            d->diskii_path,
            d->diskii_index,
            d->diskii_writable);
        control_deferred_clear(d);
        return;
    }

    if (d->kind == CONTROL_DEFERRED_WAIT_PAUSED) {
        if (event->type == RUNTIME_EVENT_PAUSED ||
            event->type == RUNTIME_EVENT_STEP_COMPLETE ||
            event->type == RUNTIME_EVENT_RUN_COMPLETE) {
            char text[CONTROL_RESPONSE_TEXT_MAX];
            snprintf(
                text,
                sizeof(text),
                "state=paused frame=%llu stop=%s",
                (unsigned long long)disp->frame_number,
                disp->stop_reason);
            post_ok(disp, d->request_id, text);
            /* Consume edge after delivery (same as sticky immediate path). */
            disp->seen_paused = false;
            disp->latch_paused = false;
            control_deferred_clear(d);
        }
        return;
    }

    if (d->kind == CONTROL_DEFERRED_WAIT_RUNNING) {
        if (event->type == RUNTIME_EVENT_RUNNING) {
            post_ok(disp, d->request_id, "state=running");
            control_deferred_clear(d);
        }
        return;
    }

    if (d->kind == CONTROL_DEFERRED_WAIT_FRAME) {
        if (event->type == RUNTIME_EVENT_FRAME_READY) {
            uint64_t delta = disp->frame_number - d->wait_frame_start;
            if (delta >= (uint64_t)d->wait_frame_delta) {
                char text[CONTROL_RESPONSE_TEXT_MAX];
                snprintf(
                    text,
                    sizeof(text),
                    "frame=%llu delta=%llu",
                    (unsigned long long)disp->frame_number,
                    (unsigned long long)delta);
                post_ok(disp, d->request_id, text);
                control_deferred_clear(d);
            }
        }
        return;
    }

    if (d->kind == CONTROL_DEFERRED_WAIT_EVENT) {
        ename = event_name_for_type(event->type);
        if (ename != NULL && strcmp(ename, d->event_name) == 0) {
            char text[CONTROL_RESPONSE_TEXT_MAX];
            snprintf(text, sizeof(text), "event=%s", ename);
            post_ok(disp, d->request_id, text);
            consume_latch(disp, ename);
            control_deferred_clear(d);
        }
        return;
    }

    if (d->kind == CONTROL_DEFERRED_GET_FRAME &&
        event->type == RUNTIME_EVENT_FRAME_READY) {
        if (try_post_frame(disp, d->request_id)) {
            control_deferred_clear(d);
        }
        return;
    }

    if (d->kind == CONTROL_DEFERRED_GET_CPU &&
        event->type == RUNTIME_EVENT_CPU_STATE_RESPONSE &&
        event->request_token == d->request_token) {
        char text[CONTROL_RESPONSE_TEXT_MAX];
        snprintf(
            text,
            sizeof(text),
            "pc=%04X a=%02X x=%02X y=%02X sp=%02X p=%02X cycles=%llu",
            event->data.cpu_state.pc,
            event->data.cpu_state.a,
            event->data.cpu_state.x,
            event->data.cpu_state.y,
            event->data.cpu_state.sp,
            event->data.cpu_state.p,
            (unsigned long long)event->data.cpu_state.cycles);
        post_ok(disp, d->request_id, text);
        control_deferred_clear(d);
        return;
    }

    if (d->kind == CONTROL_DEFERRED_GET_SOFTSWITCHES &&
        event->type == RUNTIME_EVENT_MACHINE_STATE_RESPONSE) {
        char text[CONTROL_RESPONSE_TEXT_MAX];
        format_softswitches_text(text, sizeof(text), &event->data.machine_state);
        post_ok(disp, d->request_id, text);
        control_deferred_clear(d);
        return;
    }

    if (d->kind == CONTROL_DEFERRED_GET_MEMORY &&
        event->type == RUNTIME_EVENT_MEMORY_RPC_COMPLETE &&
        event->request_token == d->request_token) {
        control_response response;
        uint8_t *bytes = NULL;
        uint32_t length = 0;
        uint16_t address = 0;
        runtime_memory_mode mode = RUNTIME_MEMORY_MODE_MAP;
        char meta[CONTROL_RESPONSE_TEXT_MAX];

        if (event->data.memory_rpc.status != RUNTIME_MEMORY_RPC_OK) {
            post_error(disp, d->request_id, "rpc", "memory-failed");
            control_deferred_clear(d);
            return;
        }

        if (!runtime_client_claim_memory_rpc(
                disp->client,
                d->request_token,
                &bytes,
                &length,
                &address,
                &mode)) {
            post_error(disp, d->request_id, "rpc", "claim-failed");
            control_deferred_clear(d);
            return;
        }

        snprintf(
            meta,
            sizeof(meta),
            "addr=%04X length=%u mode=%s",
            address,
            length,
            control_protocol_memory_mode_name(d->memory_mode));
        control_protocol_format_data(
            &response, d->request_id, "memory", meta, bytes, length);
        if (!control_server_post_response(disp->server, &response)) {
            free(bytes);
        }
        control_deferred_clear(d);
        return;
    }

    if (d->kind == CONTROL_DEFERRED_BREAK_LIST &&
        event->type == RUNTIME_EVENT_BREAKPOINTS_RESPONSE) {
        control_response response;
        uint8_t *payload = NULL;
        size_t payload_size = 0;
        char meta[CONTROL_RESPONSE_TEXT_MAX];
        runtime_breakpoint_snapshot snap;

        if (!runtime_client_poll_breakpoints(disp->client, &snap)) {
            /* Fall back to event inline snapshot if client poll missed. */
            snap = event->data.breakpoints;
        }
        if (!control_format_breakpoints_payload(
                &snap, &payload, &payload_size, meta, sizeof(meta))) {
            post_error(disp, d->request_id, "memory", "allocation-failed");
            control_deferred_clear(d);
            return;
        }
        control_protocol_format_data(
            &response, d->request_id, "breakpoints", meta, payload, payload_size);
        if (!control_server_post_response(disp->server, &response)) {
            free(payload);
        }
        control_deferred_clear(d);
        return;
    }

    if (d->kind == CONTROL_DEFERRED_SAVE_STATE &&
        event->type == RUNTIME_EVENT_SAVE_STATE_COMPLETE) {
        /* Worker publishes completion only on success for now. */
        post_ok(disp, d->request_id, "saved");
        control_deferred_clear(d);
        return;
    }

    if (d->kind == CONTROL_DEFERRED_LOAD_STATE &&
        event->type == RUNTIME_EVENT_LOAD_STATE_COMPLETE) {
        post_ok(disp, d->request_id, "loaded");
        control_deferred_clear(d);
        return;
    }

    if (d->kind == CONTROL_DEFERRED_HISTORY_STATUS &&
        event->type == RUNTIME_EVENT_HISTORY_STATUS_RESPONSE &&
        event->request_token == d->request_token) {
        char text[CONTROL_RESPONSE_TEXT_MAX];
        const runtime_history_status *st = &event->data.history_status;
        if (!st->available) {
            snprintf(
                text,
                sizeof(text),
                "available=0 recording=0 requested_bytes=%llu capacity_bytes=0 "
                "reason=disabled-or-unavailable",
                (unsigned long long)st->requested_bytes);
        } else {
            snprintf(
                text,
                sizeof(text),
                "available=1 recording=%u requested_bytes=%llu "
                "capacity_bytes=%llu used_bytes=%llu epoch=%llu timeline=%u "
                "records=%llu oldest=%llu newest=%llu wrapped=%llu partial=%llu "
                "truncated_accesses=%llu",
                st->recording ? 1u : 0u,
                (unsigned long long)st->requested_bytes,
                (unsigned long long)st->capacity_bytes,
                (unsigned long long)st->used_bytes,
                (unsigned long long)st->epoch,
                st->timeline,
                (unsigned long long)st->record_count,
                (unsigned long long)st->oldest_id,
                (unsigned long long)st->newest_id,
                (unsigned long long)st->wrap_count,
                (unsigned long long)st->partial_records,
                (unsigned long long)st->truncated_accesses);
        }
        post_ok(disp, d->request_id, text);
        control_deferred_clear(d);
        return;
    }

    if (d->kind == CONTROL_DEFERRED_HISTORY_DATA &&
        event->type == RUNTIME_EVENT_HISTORY_RESULT_RESPONSE &&
        event->request_token == d->request_token) {
        const runtime_history_rpc_meta *meta = &event->data.history_rpc;
        if (meta->status != RUNTIME_HISTORY_RPC_OK) {
            const char *code = "runtime";
            const char *message = "history-query-failed";
            switch (meta->status) {
            case RUNTIME_HISTORY_RPC_UNAVAILABLE:
                code = "unavailable";
                message = "history-recorder-unavailable";
                break;
            case RUNTIME_HISTORY_RPC_MACHINE_RUNNING:
                code = "busy";
                message = "machine-running";
                break;
            case RUNTIME_HISTORY_RPC_REQUEST_ACTIVE:
                code = "busy";
                message = "history-request-active";
                break;
            case RUNTIME_HISTORY_RPC_BAD_ARGS:
                code = "bad-args";
                message = "history-query-invalid";
                break;
            case RUNTIME_HISTORY_RPC_CURSOR_STALE:
                code = "stale";
                message = "history-cursor-stale";
                break;
            case RUNTIME_HISTORY_RPC_EPOCH_MISMATCH:
                code = "stale";
                message = "history-epoch-mismatch";
                break;
            case RUNTIME_HISTORY_RPC_RECORD_NOT_RETAINED:
                code = "not-found";
                message = "history-record-not-retained";
                break;
            default:
                break;
            }
            post_error(disp, d->request_id, code, message);
            control_deferred_clear(d);
            return;
        }
        if (meta->byte_length == 0u) {
            /* status-only success (history-close) */
            post_ok(disp, d->request_id, "");
            control_deferred_clear(d);
            return;
        }
        {
            control_response response;
            uint8_t *bytes = NULL;
            uint32_t length = 0;
            runtime_history_rpc_meta claimed;
            char mtext[CONTROL_RESPONSE_TEXT_MAX];

            if (!runtime_client_claim_history_rpc(
                    disp->client, d->request_token, &bytes, &length, &claimed)) {
                post_error(disp, d->request_id, "rpc", "claim-failed");
                control_deferred_clear(d);
                return;
            }
            snprintf(
                mtext,
                sizeof(mtext),
                "epoch=%llu count=%u cursor=%llu more=%u oldest=%llu newest=%llu",
                (unsigned long long)claimed.epoch,
                claimed.count,
                (unsigned long long)claimed.cursor,
                claimed.more ? 1u : 0u,
                (unsigned long long)claimed.oldest,
                (unsigned long long)claimed.newest);
            control_protocol_format_data(
                &response, d->request_id, "history", mtext, bytes, length);
            if (!control_server_post_response(disp->server, &response)) {
                free(bytes);
            }
            control_deferred_clear(d);
        }
        return;
    }
}

static bool parse_u16_range_token(
    const char *value,
    uint16_t *first,
    uint16_t *last)
{
    char *dash;
    char *end = NULL;
    unsigned long a;
    unsigned long b;
    char buf[32];

    if (value == NULL || first == NULL || last == NULL) {
        return false;
    }
    strncpy(buf, value, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    dash = strchr(buf, '-');
    if (dash != NULL) {
        *dash = '\0';
        if (buf[0] == '$') {
            a = strtoul(buf + 1, &end, 16);
        } else {
            a = strtoul(buf, &end, 0);
        }
        if (end == buf || *end != '\0' || a > 0xfffful) {
            return false;
        }
        if (dash[1] == '$') {
            b = strtoul(dash + 2, &end, 16);
        } else {
            b = strtoul(dash + 1, &end, 0);
        }
        if (end == dash + 1 || *end != '\0' || b > 0xfffful || b < a) {
            return false;
        }
        *first = (uint16_t)a;
        *last = (uint16_t)b;
        return true;
    }
    if (buf[0] == '$') {
        a = strtoul(buf + 1, &end, 16);
    } else {
        a = strtoul(buf, &end, 0);
    }
    if (end == buf || *end != '\0' || a > 0xfffful) {
        return false;
    }
    *first = (uint16_t)a;
    *last = (uint16_t)a;
    return true;
}

static uint16_t history_access_mask_from_name(const char *name)
{
    if (strcmp(name, "execute") == 0) {
        return (uint16_t)(1u << C6510_BUS_ACCESS_OPCODE_FETCH); /* not used; special */
    }
    if (strcmp(name, "write") == 0 || strcmp(name, "data-write") == 0) {
        return (uint16_t)((1u << C6510_BUS_ACCESS_DATA_WRITE) |
                          (1u << C6510_BUS_ACCESS_RMW_DUMMY_WRITE) |
                          (1u << C6510_BUS_ACCESS_STACK_WRITE));
    }
    if (strcmp(name, "read") == 0 || strcmp(name, "data-read") == 0) {
        return (uint16_t)((1u << C6510_BUS_ACCESS_DATA_READ) |
                          (1u << C6510_BUS_ACCESS_OPCODE_FETCH) |
                          (1u << C6510_BUS_ACCESS_OPERAND_READ) |
                          (1u << C6510_BUS_ACCESS_DUMMY_READ) |
                          (1u << C6510_BUS_ACCESS_STACK_READ) |
                          (1u << C6510_BUS_ACCESS_VECTOR_READ));
    }
    if (strcmp(name, "opcode") == 0) {
        return (uint16_t)(1u << C6510_BUS_ACCESS_OPCODE_FETCH);
    }
    if (strcmp(name, "data") == 0) {
        return (uint16_t)((1u << C6510_BUS_ACCESS_DATA_READ) |
                          (1u << C6510_BUS_ACCESS_DATA_WRITE));
    }
    return 0;
}

/* Minimal history-find option parse: pc, address, access, direction, limit, from. */
static bool parse_history_find_options(
    const char *text,
    runtime_history_query *query,
    runtime_history_from_kind *from_kind,
    uint64_t *from_id,
    uint16_t *limit)
{
    char buf[CONTROL_LINE_MAX];
    char *cursor;
    char *token;

    if (query == NULL || from_kind == NULL || from_id == NULL || limit == NULL) {
        return false;
    }
    memset(query, 0, sizeof(*query));
    query->direction = RUNTIME_HISTORY_QUERY_BACKWARD;
    *from_kind = RUNTIME_HISTORY_FROM_DEFAULT;
    *from_id = 0u;
    *limit = 64u;

    if (text == NULL || text[0] == '\0') {
        return true;
    }
    strncpy(buf, text, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    cursor = buf;
    while ((token = strtok(cursor, " \t")) != NULL) {
        char *eq = strchr(token, '=');
        char *key;
        char *value;
        cursor = NULL;
        if (eq == NULL) {
            return false;
        }
        *eq = '\0';
        key = token;
        value = eq + 1;
        if (strcmp(key, "pc") == 0) {
            if (!parse_u16_range_token(value, &query->pc_first, &query->pc_last)) {
                return false;
            }
            query->has_pc = true;
        } else if (strcmp(key, "address") == 0) {
            if (!parse_u16_range_token(
                    value, &query->address_first, &query->address_last)) {
                return false;
            }
            query->has_address = true;
        } else if (strcmp(key, "access") == 0) {
            uint16_t mask = history_access_mask_from_name(value);
            if (mask == 0 && strcmp(value, "execute") != 0) {
                return false;
            }
            if (strcmp(value, "execute") == 0) {
                /* PC predicate via has_pc only when address also set elsewhere;
                   execute without address means all instructions — leave mask
                   empty and rely on empty filter / pc filter. */
                query->has_access = false;
            } else {
                query->has_access = true;
                query->access_mask = mask;
            }
        } else if (strcmp(key, "direction") == 0) {
            if (strcmp(value, "forward") == 0) {
                query->direction = RUNTIME_HISTORY_QUERY_FORWARD;
            } else if (strcmp(value, "backward") == 0) {
                query->direction = RUNTIME_HISTORY_QUERY_BACKWARD;
            } else {
                return false;
            }
        } else if (strcmp(key, "limit") == 0) {
            unsigned long v = strtoul(value, NULL, 0);
            if (v < 1ul || v > 256ul) {
                return false;
            }
            *limit = (uint16_t)v;
        } else if (strcmp(key, "from") == 0) {
            if (strcmp(value, "oldest") == 0) {
                *from_kind = RUNTIME_HISTORY_FROM_OLDEST;
            } else if (strcmp(value, "newest") == 0) {
                *from_kind = RUNTIME_HISTORY_FROM_NEWEST;
            } else {
                char *end = NULL;
                unsigned long v = strtoul(value, &end, 0);
                if (end == value || *end != '\0' || v == 0ul) {
                    return false;
                }
                *from_kind = RUNTIME_HISTORY_FROM_ID;
                *from_id = (uint64_t)v;
            }
        } else {
            return false;
        }
    }
    return true;
}

static void handle_request(control_dispatch_t *disp, control_request *req)
{
    runtime_client *client = disp->client;

    switch (req->type) {
    case CONTROL_COMMAND_RESET:
        clear_execution_latches(disp);
        (void)runtime_client_reset(client);
        post_ok(disp, req->id, "accepted=1");
        break;
    case CONTROL_COMMAND_RUN:
        clear_execution_latches(disp);
        (void)runtime_client_run(client);
        disp->machine_running = true;
        disp->seen_paused = false;
        post_ok(disp, req->id, "accepted=1");
        break;
    case CONTROL_COMMAND_PAUSE:
        clear_execution_latches(disp);
        (void)runtime_client_pause(client);
        post_ok(disp, req->id, "accepted=1");
        break;
    case CONTROL_COMMAND_STEP_CYCLE:
        clear_execution_latches(disp);
        (void)runtime_client_step_cycle(client);
        post_ok(disp, req->id, "accepted=1");
        break;
    case CONTROL_COMMAND_STEP_INSTRUCTION:
        clear_execution_latches(disp);
        (void)runtime_client_step_instruction(client);
        post_ok(disp, req->id, "accepted=1");
        break;
    case CONTROL_COMMAND_STEP_OVER:
        clear_execution_latches(disp);
        (void)runtime_client_step_over(client);
        post_ok(disp, req->id, "accepted=1");
        break;
    case CONTROL_COMMAND_STEP_OUT:
        clear_execution_latches(disp);
        (void)runtime_client_step_out(client);
        post_ok(disp, req->id, "accepted=1");
        break;

    case CONTROL_COMMAND_SET_TURBO: {
        char text[CONTROL_RESPONSE_TEXT_MAX];
        char turbo_label[32];
        disp->turbo_mode = req->args.turbo_mode;
        (void)runtime_client_set_turbo_multiplier(client, req->args.turbo_mode);
        runtime_turbo_format_token(req->args.turbo_mode, turbo_label, sizeof(turbo_label));
        snprintf(text, sizeof(text), "accepted=1 turbo=%s", turbo_label);
        post_ok(disp, req->id, text);
        break;
    }

    case CONTROL_COMMAND_GET_STATE: {
        char text[CONTROL_RESPONSE_TEXT_MAX];
        char turbo_label[32];
        runtime_turbo_format_token(disp->turbo_mode, turbo_label, sizeof(turbo_label));
        snprintf(
            text,
            sizeof(text),
            "state=%s has_cpu=%d frame=%llu cycle=%llu stop=%s turbo=%s",
            disp->machine_running ? "running" : "paused",
            disp->has_cpu ? 1 : 0,
            (unsigned long long)disp->frame_number,
            (unsigned long long)disp->cycle,
            disp->stop_reason,
            turbo_label);
        post_ok(disp, req->id, text);
        break;
    }

    case CONTROL_COMMAND_GET_CPU: {
        uint64_t token = runtime_client_alloc_request_token(client);
        deferred_control_response *d =
            begin_deferred(disp, req->id, CONTROL_DEFERRED_GET_CPU, 2000u, token);
        if (d == NULL) {
            break;
        }
        if (!runtime_client_request_cpu_state_token(client, token)) {
            post_error(disp, req->id, "busy", "queue");
            control_deferred_clear(d);
        }
        break;
    }

    case CONTROL_COMMAND_GET_SOFTSWITCHES: {
        /* Snapshot via machine state (state_flags + beam). Not $C0xx memory. */
        deferred_control_response *d = begin_deferred(
            disp, req->id, CONTROL_DEFERRED_GET_SOFTSWITCHES, 2000u, 0u);
        if (d == NULL) {
            break;
        }
        if (!runtime_client_request_machine_state(client)) {
            post_error(disp, req->id, "busy", "queue");
            control_deferred_clear(d);
        }
        break;
    }

    case CONTROL_COMMAND_GET_MEMORY: {
        uint64_t token = runtime_client_alloc_request_token(client);
        runtime_memory_mode mode = to_runtime_memory_mode(req->args.memory_mode);
        deferred_control_response *d =
            begin_deferred(disp, req->id, CONTROL_DEFERRED_GET_MEMORY, 2000u, token);
        if (d == NULL) {
            break;
        }
        d->memory_address = req->args.address;
        d->memory_length = req->args.length;
        d->memory_mode = req->args.memory_mode;
        if (!runtime_client_request_memory_token(
                client, req->args.address, req->args.length, mode, token)) {
            post_error(disp, req->id, "busy", "queue");
            control_deferred_clear(d);
        }
        break;
    }

    case CONTROL_COMMAND_SET_MEMORY: {
        runtime_memory_mode mode = to_runtime_memory_mode(req->args.memory_mode);
        if (req->payload == NULL || req->payload_size != req->args.length) {
            post_error(disp, req->id, "bad-payload", "length");
            break;
        }
        if (req->args.length <= RUNTIME_MEMORY_SNAPSHOT_MAX) {
            (void)runtime_client_write_memory(
                client,
                req->args.address,
                (uint16_t)req->args.length,
                mode,
                req->payload);
        } else {
            uint32_t i;
            for (i = 0; i < req->args.length; i++) {
                (void)runtime_client_write_memory_byte(
                    client,
                    (uint16_t)(req->args.address + i),
                    req->payload[i],
                    mode);
            }
        }
        {
            char text[CONTROL_RESPONSE_TEXT_MAX];
            snprintf(
                text,
                sizeof(text),
                "addr=%04X length=%u mode=%s",
                req->args.address,
                req->args.length,
                control_protocol_memory_mode_name(req->args.memory_mode));
            post_ok(disp, req->id, text);
        }
        break;
    }

    case CONTROL_COMMAND_GET_FRAME: {
        if (try_post_frame(disp, req->id)) {
            break;
        }
        {
            deferred_control_response *d =
                begin_deferred(disp, req->id, CONTROL_DEFERRED_GET_FRAME, 2000u, 0u);
            if (d == NULL) {
                break;
            }
            (void)runtime_client_request_frame(client);
        }
        break;
    }

    case CONTROL_COMMAND_FRAME_RING_INFO: {
        runtime_frame_ring_info info;
        char text[CONTROL_RESPONSE_TEXT_MAX];
        runtime_client_get_frame_ring_info(client, &info);
        snprintf(
            text,
            sizeof(text),
            "capacity=%u count=%u dropped=%llu recording=%d bytes=%llu "
            "oldest_frame=%llu newest_frame=%llu oldest_cycle=%llu newest_cycle=%llu",
            info.capacity,
            info.count,
            (unsigned long long)info.dropped,
            info.recording ? 1 : 0,
            (unsigned long long)info.bytes,
            (unsigned long long)info.oldest_frame,
            (unsigned long long)info.newest_frame,
            (unsigned long long)info.oldest_cycle,
            (unsigned long long)info.newest_cycle);
        post_ok(disp, req->id, text);
        break;
    }

    case CONTROL_COMMAND_FRAME_RING_RECORD:
        runtime_client_set_frame_ring_recording(
            client, req->args.frame_ring_record_enabled);
        post_ok(
            disp,
            req->id,
            req->args.frame_ring_record_enabled ? "recording=1" : "recording=0");
        break;

    case CONTROL_COMMAND_FRAME_RING_CLEAR:
        runtime_client_clear_frame_ring(client);
        post_ok(disp, req->id, "cleared");
        break;

    case CONTROL_COMMAND_GET_FRAME_AT: {
        runtime_ring_frame frame;
        control_response response;
        uint8_t *payload;
        size_t nbytes;
        char meta[CONTROL_RESPONSE_TEXT_MAX];

        if (!runtime_client_copy_frame_at(
                client,
                req->args.frame_ring_target,
                req->args.frame_ring_by_cycle,
                &frame)) {
            post_error(disp, req->id, "not-found", "frame-not-retained");
            break;
        }
        nbytes = (size_t)frame.width * (size_t)frame.height * 4u;
        payload = (uint8_t *)malloc(nbytes);
        if (payload == NULL) {
            post_error(disp, req->id, "memory", "allocation-failed");
            break;
        }
        memcpy(payload, frame.pixels, nbytes);
        snprintf(
            meta,
            sizeof(meta),
            "width=%u height=%u stride=%u format=argb8888 frame=%llu cycle=%llu "
            "target=%llu target_kind=%s",
            frame.width,
            frame.height,
            frame.stride_bytes,
            (unsigned long long)frame.frame_number,
            (unsigned long long)frame.machine_cycle,
            (unsigned long long)req->args.frame_ring_target,
            req->args.frame_ring_by_cycle ? "cycle" : "frame");
        control_protocol_format_data(
            &response, req->id, "frame", meta, payload, nbytes);
        if (!control_server_post_response(disp->server, &response)) {
            free(payload);
        }
        break;
    }

    case CONTROL_COMMAND_SET_REG: {
        const char *n = req->args.reg_name;
        uint16_t v = req->args.reg_value;
        if (strcmp(n, "pc") == 0) {
            (void)runtime_client_set_pc(client, v);
        } else if (strcmp(n, "sp") == 0) {
            (void)runtime_client_set_sp(client, (uint8_t)v);
        } else if (strcmp(n, "a") == 0) {
            (void)runtime_client_set_a(client, (uint8_t)v);
        } else if (strcmp(n, "x") == 0) {
            (void)runtime_client_set_x(client, (uint8_t)v);
        } else if (strcmp(n, "y") == 0) {
            (void)runtime_client_set_y(client, (uint8_t)v);
        } else if (strcmp(n, "p") == 0 || strcmp(n, "status") == 0) {
            (void)runtime_client_set_status(client, (uint8_t)v);
        } else {
            post_error(disp, req->id, "bad-args", "reg");
            break;
        }
        post_ok(disp, req->id, "accepted=1");
        break;
    }

    case CONTROL_COMMAND_BREAK_EXEC: {
        runtime_breakpoint_definition def;
        memset(&def, 0, sizeof(def));
        def.enabled = 1u;
        def.start_address = req->args.address;
        def.end_address = req->args.address;
        def.access = RUNTIME_BREAKPOINT_ACCESS_EXECUTE;
        def.mapping = 0u;
        def.actions = RUNTIME_BREAKPOINT_ACTION_BREAK;
        def.reset_count = 1u;
        {
            deferred_control_response *d =
                begin_deferred(disp, req->id, CONTROL_DEFERRED_BREAK_LIST, 2000u, 0u);
            if (d == NULL) {
                break;
            }
            if (!runtime_client_create_breakpoint(client, &def) ||
                !runtime_client_request_breakpoints(client)) {
                post_error(disp, req->id, "runtime", "command rejected");
                control_deferred_clear(d);
            }
        }
        break;
    }
    case CONTROL_COMMAND_BREAK_CLEAR:
    case CONTROL_COMMAND_BREAK_CLEAR_ALL: {
        deferred_control_response *d =
            begin_deferred(disp, req->id, CONTROL_DEFERRED_BREAK_LIST, 2000u, 0u);
        bool ok;
        if (d == NULL) {
            break;
        }
        if (req->type == CONTROL_COMMAND_BREAK_CLEAR_ALL || req->args.break_id == 0u) {
            ok = runtime_client_clear_all_breakpoints(client);
        } else {
            ok = runtime_client_clear_breakpoint(client, req->args.break_id);
        }
        if (!ok || !runtime_client_request_breakpoints(client)) {
            post_error(disp, req->id, "runtime", "command rejected");
            control_deferred_clear(d);
        }
        break;
    }
    case CONTROL_COMMAND_BREAK_ENABLE: {
        deferred_control_response *d =
            begin_deferred(disp, req->id, CONTROL_DEFERRED_BREAK_LIST, 2000u, 0u);
        if (d == NULL) {
            break;
        }
        if (!runtime_client_set_breakpoint_enabled(
                client, req->args.break_id, req->args.break_enable != 0u) ||
            !runtime_client_request_breakpoints(client)) {
            post_error(disp, req->id, "runtime", "command rejected");
            control_deferred_clear(d);
        }
        break;
    }
    case CONTROL_COMMAND_BREAK_CREATE: {
        runtime_breakpoint_definition def;
        char definition_error[192];
        deferred_control_response *d;
        if (!control_parse_breakpoint_definition(
                req->args.text, &def, definition_error, sizeof(definition_error))) {
            char message[CONTROL_RESPONSE_TEXT_MAX];
            snprintf(
                message,
                sizeof(message),
                "invalid breakpoint definition%s%s",
                definition_error[0] != '\0' ? ": " : "",
                definition_error);
            post_error(disp, req->id, "bad-args", message);
            break;
        }
        d = begin_deferred(disp, req->id, CONTROL_DEFERRED_BREAK_LIST, 2000u, 0u);
        if (d == NULL) {
            break;
        }
        if (!runtime_client_create_breakpoint(client, &def) ||
            !runtime_client_request_breakpoints(client)) {
            post_error(disp, req->id, "runtime", "command rejected");
            control_deferred_clear(d);
        }
        break;
    }
    case CONTROL_COMMAND_BREAK_UPDATE: {
        runtime_breakpoint_definition def;
        char definition_error[192];
        deferred_control_response *d;
        if (!control_parse_breakpoint_definition(
                req->args.text, &def, definition_error, sizeof(definition_error))) {
            char message[CONTROL_RESPONSE_TEXT_MAX];
            snprintf(
                message,
                sizeof(message),
                "invalid breakpoint definition%s%s",
                definition_error[0] != '\0' ? ": " : "",
                definition_error);
            post_error(disp, req->id, "bad-args", message);
            break;
        }
        d = begin_deferred(disp, req->id, CONTROL_DEFERRED_BREAK_LIST, 2000u, 0u);
        if (d == NULL) {
            break;
        }
        if (!runtime_client_update_breakpoint(client, req->args.break_id, &def) ||
            !runtime_client_request_breakpoints(client)) {
            post_error(disp, req->id, "runtime", "command rejected");
            control_deferred_clear(d);
        }
        break;
    }
    case CONTROL_COMMAND_REARM_ONESHOTS: {
        deferred_control_response *d =
            begin_deferred(disp, req->id, CONTROL_DEFERRED_BREAK_LIST, 2000u, 0u);
        if (d == NULL) {
            break;
        }
        if (!runtime_client_rearm_oneshot_breakpoints(client) ||
            !runtime_client_request_breakpoints(client)) {
            post_error(disp, req->id, "runtime", "command rejected");
            control_deferred_clear(d);
        }
        break;
    }
    case CONTROL_COMMAND_BREAK_LIST: {
        deferred_control_response *d =
            begin_deferred(disp, req->id, CONTROL_DEFERRED_BREAK_LIST, 2000u, 0u);
        if (d == NULL) {
            break;
        }
        if (!runtime_client_request_breakpoints(client)) {
            post_error(disp, req->id, "busy", "queue");
            control_deferred_clear(d);
        }
        break;
    }

    case CONTROL_COMMAND_WAIT_PAUSED: {
        if (disp->seen_paused && !disp->machine_running) {
            char text[CONTROL_RESPONSE_TEXT_MAX];
            snprintf(
                text,
                sizeof(text),
                "state=paused frame=%llu stop=%s",
                (unsigned long long)disp->frame_number,
                disp->stop_reason);
            post_ok(disp, req->id, text);
            /* Consume sticky latch so a second wait-paused waits for a new edge
               (run → pause / BP). First attach still sees startup paused once. */
            disp->seen_paused = false;
            disp->latch_paused = false;
            break;
        }
        (void)begin_deferred(
            disp, req->id, CONTROL_DEFERRED_WAIT_PAUSED, req->args.timeout_ms, 0u);
        break;
    }

    case CONTROL_COMMAND_WAIT_RUNNING: {
        if (disp->machine_running) {
            post_ok(disp, req->id, "state=running");
            break;
        }
        (void)begin_deferred(
            disp, req->id, CONTROL_DEFERRED_WAIT_RUNNING, req->args.timeout_ms, 0u);
        break;
    }

    case CONTROL_COMMAND_WAIT_FRAME: {
        deferred_control_response *d = begin_deferred(
            disp, req->id, CONTROL_DEFERRED_WAIT_FRAME, req->args.timeout_ms, 0u);
        if (d == NULL) {
            break;
        }
        d->wait_frame_delta = req->args.wait_frame_delta;
        d->wait_frame_start = disp->frame_number;
        break;
    }

    case CONTROL_COMMAND_WAIT_EVENT: {
        if (latch_matches_event(disp, req->args.event_name)) {
            char text[CONTROL_RESPONSE_TEXT_MAX];
            snprintf(text, sizeof(text), "event=%s", req->args.event_name);
            post_ok(disp, req->id, text);
            consume_latch(disp, req->args.event_name);
            break;
        }
        {
            deferred_control_response *d = begin_deferred(
                disp, req->id, CONTROL_DEFERRED_WAIT_EVENT, req->args.timeout_ms, 0u);
            if (d == NULL) {
                break;
            }
            strncpy(d->event_name, req->args.event_name, sizeof(d->event_name) - 1);
        }
        break;
    }

    case CONTROL_COMMAND_SAVE_STATE: {
        deferred_control_response *d =
            begin_deferred(disp, req->id, CONTROL_DEFERRED_SAVE_STATE, 5000u, 0u);
        if (d == NULL) {
            break;
        }
        if (!runtime_client_save_state(client, req->args.path)) {
            post_error(disp, req->id, "bad-args", "path");
            control_deferred_clear(d);
        }
        break;
    }

    case CONTROL_COMMAND_LOAD_STATE: {
        deferred_control_response *d =
            begin_deferred(disp, req->id, CONTROL_DEFERRED_LOAD_STATE, 5000u, 0u);
        if (d == NULL) {
            break;
        }
        if (!runtime_client_load_state(client, req->args.path)) {
            post_error(disp, req->id, "bad-args", "path");
            control_deferred_clear(d);
        }
        break;
    }

    case CONTROL_COMMAND_KEY: {
        /* Apple: inject one keystroke via paste (ASCII / $C000 path). */
        char ch = (char)req->args.key;
        if (req->args.key == 0x8Du || req->args.key == '\r') {
            ch = '\n';
        }
        if (!runtime_client_paste_text(client, &ch, 1u)) {
            post_error(disp, req->id, "busy", "key");
        } else {
            post_ok(disp, req->id, "accepted=1");
        }
        break;
    }

    case CONTROL_COMMAND_MOUNT_DISK:
        handle_diskii_command(
            disp,
            req->id,
            CONTROL_COMMAND_MOUNT_DISK,
            req->args.slot,
            req->args.drive,
            req->args.path,
            0u,
            0u);
        break;

    case CONTROL_COMMAND_SELECT_DISK:
        handle_diskii_command(
            disp,
            req->id,
            CONTROL_COMMAND_SELECT_DISK,
            req->args.slot,
            req->args.drive,
            NULL,
            req->args.disk_index,
            0u);
        break;

    case CONTROL_COMMAND_SET_DISK_WRITABLE:
        handle_diskii_command(
            disp,
            req->id,
            CONTROL_COMMAND_SET_DISK_WRITABLE,
            req->args.slot,
            req->args.drive,
            NULL,
            0u,
            req->args.disk_writable);
        break;

    case CONTROL_COMMAND_HISTORY_INFO: {
        uint64_t token = runtime_client_alloc_request_token(client);
        deferred_control_response *d = begin_deferred(
            disp, req->id, CONTROL_DEFERRED_HISTORY_STATUS, 2000u, token);
        if (d == NULL) {
            break;
        }
        if (!runtime_client_history_info(client, token)) {
            post_error(disp, req->id, "runtime", "command rejected");
            control_deferred_clear(d);
        }
        break;
    }

    case CONTROL_COMMAND_HISTORY_RECORD: {
        uint64_t token = runtime_client_alloc_request_token(client);
        deferred_control_response *d = begin_deferred(
            disp, req->id, CONTROL_DEFERRED_HISTORY_STATUS, 2000u, token);
        if (d == NULL) {
            break;
        }
        if (!runtime_client_history_record(
                client, req->args.history_record_enabled, token)) {
            post_error(disp, req->id, "runtime", "command rejected");
            control_deferred_clear(d);
        }
        break;
    }

    case CONTROL_COMMAND_HISTORY_CLEAR: {
        uint64_t token = runtime_client_alloc_request_token(client);
        deferred_control_response *d = begin_deferred(
            disp, req->id, CONTROL_DEFERRED_HISTORY_STATUS, 2000u, token);
        if (d == NULL) {
            break;
        }
        if (!runtime_client_history_clear(client, token)) {
            post_error(disp, req->id, "runtime", "command rejected");
            control_deferred_clear(d);
        }
        break;
    }

    case CONTROL_COMMAND_HISTORY_FIND: {
        runtime_history_query query;
        runtime_history_from_kind from_kind = RUNTIME_HISTORY_FROM_DEFAULT;
        uint64_t from_id = 0;
        uint16_t limit = 64;
        uint64_t token;
        deferred_control_response *d;

        if (!parse_history_find_options(
                req->args.history_find_text,
                &query,
                &from_kind,
                &from_id,
                &limit)) {
            post_error(disp, req->id, "bad-args", "history-find options");
            break;
        }
        token = runtime_client_alloc_request_token(client);
        d = begin_deferred(
            disp, req->id, CONTROL_DEFERRED_HISTORY_DATA, 10000u, token);
        if (d == NULL) {
            break;
        }
        if (!runtime_client_history_find(
                client, &query, from_kind, from_id, limit, token)) {
            post_error(disp, req->id, "runtime", "command rejected");
            control_deferred_clear(d);
        }
        break;
    }

    case CONTROL_COMMAND_HISTORY_NEXT: {
        uint64_t token = runtime_client_alloc_request_token(client);
        deferred_control_response *d = begin_deferred(
            disp, req->id, CONTROL_DEFERRED_HISTORY_DATA, 2000u, token);
        if (d == NULL) {
            break;
        }
        if (!runtime_client_history_next(
                client,
                req->args.history_cursor,
                req->args.history_limit,
                token)) {
            post_error(disp, req->id, "runtime", "command rejected");
            control_deferred_clear(d);
        }
        break;
    }

    case CONTROL_COMMAND_HISTORY_READ: {
        uint64_t token = runtime_client_alloc_request_token(client);
        deferred_control_response *d = begin_deferred(
            disp, req->id, CONTROL_DEFERRED_HISTORY_DATA, 2000u, token);
        if (d == NULL) {
            break;
        }
        if (!runtime_client_history_read(
                client,
                req->args.history_epoch,
                req->args.history_id,
                req->args.history_before,
                req->args.history_after,
                token)) {
            post_error(disp, req->id, "runtime", "command rejected");
            control_deferred_clear(d);
        }
        break;
    }

    case CONTROL_COMMAND_HISTORY_CLOSE: {
        uint64_t token = runtime_client_alloc_request_token(client);
        deferred_control_response *d = begin_deferred(
            disp, req->id, CONTROL_DEFERRED_HISTORY_DATA, 2000u, token);
        if (d == NULL) {
            break;
        }
        if (!runtime_client_history_close(
                client, req->args.history_cursor, token)) {
            post_error(disp, req->id, "runtime", "command rejected");
            control_deferred_clear(d);
        }
        break;
    }

    default:
        post_error(disp, req->id, "unknown-command", "command");
        break;
    }

    control_request_release(req);
}

void control_dispatch_poll(control_dispatch_t *disp)
{
    control_request request;

    if (disp == NULL || disp->server == NULL || disp->client == NULL) {
        return;
    }

    if (!control_server_poll_request(disp->server, &request)) {
        return;
    }
    handle_request(disp, &request);
}

void control_dispatch_check_session(control_dispatch_t *disp)
{
    deferred_control_response *d;
    uint64_t now;

    if (disp == NULL) {
        return;
    }

    d = control_deferred_active(&disp->deferred);
    if (d == NULL) {
        return;
    }

    if (!control_server_has_client(disp->server) ||
        d->connection_epoch != control_server_connection_epoch(disp->server)) {
        if (d->request_token != 0u) {
            (void)runtime_client_cancel_rpc(disp->client, d->request_token);
        }
        control_deferred_clear(d);
        return;
    }

    now = (uint64_t)SDL_GetTicks();
    if (now >= d->deadline_ms) {
        post_error(disp, d->request_id, "timeout", "deferred response timed out");
        if (d->request_token != 0u) {
            (void)runtime_client_cancel_rpc(disp->client, d->request_token);
        }
        control_deferred_clear(d);
    }
}
