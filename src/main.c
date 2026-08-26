/*
 * a2m main — c64m host loop shape + Apple II machine.
 *
 * Host-loop discipline:
 *   poll SDL → poll runtime events → gated title → clear → render →
 *   text-input sync → debugger intents → present
 */

#include "a2m_log.h"
#include "app_options.h"
#include "apple2_snapshot.h"
#include "audio_buffer.h"
#include "control_dispatch.h"
#include "control_server.h"
#include "frontend.h"
#include "frontend_input.h"
#include "frontend_joystick_input.h"
#include "platform.h"
#include "platform_audio.h"
#include "runtime.h"
#include "runtime_client.h"
#include "runtime_history_wire.h"
#include "runtime_slot_resolve.h"
#include "version.h"
#include "video.h"
#include "window_title.h"

#include <SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#if defined(_WIN32)
#include <direct.h>
#include <io.h>
#include <windows.h>
#define A2M_STAT_ISREG(mode) (((mode) & _S_IFREG) != 0)
#else
#include <dirent.h>
#include <unistd.h>
#define A2M_STAT_ISREG(mode) S_ISREG(mode)
#endif

/* Mirror a2m_log_level onto SDL's logger so leftover SDL/nuklear lines obey
   the same --log-level / [config] log_level policy. */
static void sdl_log_discard(
    void *userdata,
    int category,
    SDL_LogPriority priority,
    const char *message)
{
    (void)userdata;
    (void)category;
    (void)priority;
    (void)message;
}

static void apply_sdl_log_policy(a2m_log_level level)
{
    switch (level) {
    case A2M_LOG_LEVEL_ALL:
        SDL_LogSetOutputFunction(NULL, NULL);
        SDL_LogSetAllPriority(SDL_LOG_PRIORITY_VERBOSE);
        break;
    case A2M_LOG_LEVEL_ERROR:
        SDL_LogSetOutputFunction(NULL, NULL);
        SDL_LogSetAllPriority(SDL_LOG_PRIORITY_ERROR);
        break;
    case A2M_LOG_LEVEL_NONE:
        SDL_LogSetOutputFunction(sdl_log_discard, NULL);
        break;
    case A2M_LOG_LEVEL_WARN:
    default:
        SDL_LogSetOutputFunction(NULL, NULL);
        SDL_LogSetAllPriority(SDL_LOG_PRIORITY_WARN);
        break;
    }
}

/* ---- Apple gameport host (2 sticks × analog X/Y + buttons) -------------- */

enum {
    A2M_CONTROLLER_MAX = 2,
    /* Below this |axis| SDL reading, treat as center (paddle mid). */
    A2M_CONTROLLER_DEADZONE = 6000,
    A2M_STATE_CHUNK_HEADER_SIZE = 8,
    /* HOST payload: version + port + layout + swap + pad. */
    A2M_STATE_HOST_V1_SIZE = 8,
    /* Matches apple2_snapshot private header size (magic..pad). */
    A2M_STATE_FILE_HEADER_MIN = 32
};

#define A2M_STATE_TAG(a, b, c, d) \
    ((uint32_t)(uint8_t)(a) | ((uint32_t)(uint8_t)(b) << 8) | \
     ((uint32_t)(uint8_t)(c) << 16) | ((uint32_t)(uint8_t)(d) << 24))

enum {
    A2M_STATE_HOST_TAG = A2M_STATE_TAG('H', 'O', 'S', 'T'),
    A2M_STATE_HOST_VERSION = 1u
};

typedef struct sdl_apple_controller {
    SDL_GameController *controller;
    SDL_JoystickID instance_id;
    uint8_t axis_x;
    uint8_t axis_y;
    uint8_t buttons; /* bit0=BUTN0, bit1=BUTN1, bit2=BUTN2 */
} sdl_apple_controller;

typedef struct sdl_apple_controller_state {
    sdl_apple_controller controllers[A2M_CONTROLLER_MAX];
    unsigned single_controller_port; /* 1 or 2 when only one pad is present */
    bool swapped;
    const frontend_joystick_input *kbd_joystick;
    /* Last published snapshot (skip IPC when unchanged). */
    uint8_t last_axis[4];
    uint8_t last_buttons;
    bool published;
} sdl_apple_controller_state;

/* ---- Host helpers (c64m loop parity) ------------------------------------ */

static void request_debug_telemetry(runtime_client *client)
{
    if (client == NULL) {
        return;
    }
    (void)runtime_client_request_machine_state(client);
    (void)runtime_client_request_cpu_state(client);
}

static void request_debug_tables(runtime_client *client)
{
    if (client == NULL) {
        return;
    }
    (void)runtime_client_request_debug_memory(client, false);
    (void)runtime_client_request_breakpoints(client);
}

static void request_debug_state(runtime_client *client)
{
    request_debug_telemetry(client);
    request_debug_tables(client);
}

static void update_window_title(
    platform_window *window,
    const char *product_label,
    uint32_t turbo_multiplier,
    frontend_runtime_state state,
    runtime_stop_reason stop_reason,
    const frontend_debug_state *debug)
{
    char title[160];

    frontend_format_window_title_ex(
        title,
        sizeof(title),
        product_label,
        turbo_multiplier,
        state,
        stop_reason,
        debug != NULL && debug->inspecting,
        debug != NULL ? debug->inspector_focus_cycle : 0u,
        debug != NULL ? debug->inspector_oldest_cycle : 0u,
        debug != NULL ? debug->inspector_newest_cycle : 0u);
    platform_window_set_title(window, title);
}

/* Host typing → runtime (Apple key strobe path under the hood). */
static void handle_keyboard_input(
    frontend_input_mapper *mapper,
    runtime_client *client,
    const SDL_KeyboardEvent *event)
{
    frontend_input_action actions[FRONTEND_INPUT_MAX_ACTIONS];
    size_t count;
    size_t i;

    if (mapper == NULL || client == NULL || event == NULL) {
        return;
    }
    count = frontend_input_map_keyboard_event(
        mapper, event, actions, FRONTEND_INPUT_MAX_ACTIONS);
    for (i = 0; i < count; i++) {
        if (actions[i].type == FRONTEND_INPUT_ACTION_KEY) {
            (void)runtime_client_keyboard_key(client, actions[i].key, actions[i].pressed);
        }
    }
}

/* Map SDL axis (-32768..32767) → Apple paddle 0..254 (a2m formula + clamp).
   255 never clears PTRIG bit7 (timer saturates at 255). */
static uint8_t sdl_axis_to_apple_paddle(Sint16 value)
{
    int mapped;

    if (value > -A2M_CONTROLLER_DEADZONE && value < A2M_CONTROLLER_DEADZONE) {
        return (uint8_t)FRONTEND_JOYSTICK_APPLE_AXIS_MID;
    }
    mapped = (32768 + (int)value) >> 8;
    if (mapped < 0) {
        mapped = 0;
    }
    if (mapped > (int)FRONTEND_JOYSTICK_APPLE_AXIS_MAX) {
        mapped = (int)FRONTEND_JOYSTICK_APPLE_AXIS_MAX;
    }
    return (uint8_t)mapped;
}

static size_t sdl_apple_controller_count(const sdl_apple_controller_state *state)
{
    size_t i;
    size_t count = 0;

    if (state == NULL) {
        return 0;
    }
    for (i = 0; i < A2M_CONTROLLER_MAX; i++) {
        if (state->controllers[i].controller != NULL) {
            count++;
        }
    }
    return count;
}

static int sdl_apple_controller_find_slot(
    const sdl_apple_controller_state *state,
    SDL_JoystickID instance_id)
{
    size_t i;

    if (state == NULL) {
        return -1;
    }
    for (i = 0; i < A2M_CONTROLLER_MAX; i++) {
        if (state->controllers[i].controller != NULL &&
            state->controllers[i].instance_id == instance_id) {
            return (int)i;
        }
    }
    return -1;
}

/* Slot → gameport stick 1 or 2 (same assignment rules as c64m ports). */
static unsigned sdl_apple_controller_slot_port(
    const sdl_apple_controller_state *state,
    size_t slot,
    size_t connected_count)
{
    if (state == NULL || connected_count == 0) {
        return 0;
    }
    if (connected_count == 1) {
        return state->single_controller_port;
    }
    if (slot == 0) {
        return state->swapped ? 2u : 1u;
    }
    if (slot == 1) {
        return state->swapped ? 1u : 2u;
    }
    return 0;
}

static void sdl_apple_controller_read_state(
    SDL_GameController *controller,
    uint8_t *out_x,
    uint8_t *out_y,
    uint8_t *out_buttons)
{
    Sint16 x;
    Sint16 y;
    uint8_t ax = FRONTEND_JOYSTICK_APPLE_AXIS_MID;
    uint8_t ay = FRONTEND_JOYSTICK_APPLE_AXIS_MID;
    uint8_t buttons = 0;

    if (controller == NULL) {
        if (out_x != NULL) {
            *out_x = ax;
        }
        if (out_y != NULL) {
            *out_y = ay;
        }
        if (out_buttons != NULL) {
            *out_buttons = 0;
        }
        return;
    }

    x = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX);
    y = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY);
    ax = sdl_axis_to_apple_paddle(x);
    ay = sdl_axis_to_apple_paddle(y);

    /* D-pad forces extremes (menus / digital snap). */
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) {
        ax = FRONTEND_JOYSTICK_APPLE_AXIS_MIN;
    } else if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) {
        ax = FRONTEND_JOYSTICK_APPLE_AXIS_MAX;
    }
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_UP)) {
        ay = FRONTEND_JOYSTICK_APPLE_AXIS_MIN;
    } else if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) {
        ay = FRONTEND_JOYSTICK_APPLE_AXIS_MAX;
    }

    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_A)) {
        buttons |= 0x01u; /* BUTN0 */
    }
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_B)) {
        buttons |= 0x02u; /* BUTN1 */
    }
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_X)) {
        buttons |= 0x04u; /* BUTN2 */
    }

    if (out_x != NULL) {
        *out_x = ax;
    }
    if (out_y != NULL) {
        *out_y = ay;
    }
    if (out_buttons != NULL) {
        *out_buttons = buttons;
    }
}

static void sdl_apple_gameport_publish(
    sdl_apple_controller_state *state,
    runtime_client *client)
{
    uint8_t axis[4] = {
        FRONTEND_JOYSTICK_APPLE_AXIS_MID,
        FRONTEND_JOYSTICK_APPLE_AXIS_MID,
        FRONTEND_JOYSTICK_APPLE_AXIS_MID,
        FRONTEND_JOYSTICK_APPLE_AXIS_MID
    };
    uint8_t buttons = 0;
    size_t connected_count;
    size_t i;

    if (state == NULL || client == NULL) {
        return;
    }

    connected_count = sdl_apple_controller_count(state);
    for (i = 0; i < A2M_CONTROLLER_MAX; i++) {
        unsigned port;
        int base;

        if (state->controllers[i].controller == NULL) {
            continue;
        }
        port = sdl_apple_controller_slot_port(state, i, connected_count);
        if (port < 1u || port > 2u) {
            continue;
        }
        base = (int)(port - 1u) * 2;
        axis[base] = state->controllers[i].axis_x;
        axis[base + 1] = state->controllers[i].axis_y;
        buttons |= state->controllers[i].buttons;
    }

    /* Keyboard stick overlays its assigned stick (maxed directions / fire). */
    if (state->kbd_joystick != NULL &&
        state->kbd_joystick->port >= 1u &&
        state->kbd_joystick->port <= 2u) {
        uint8_t kx;
        uint8_t ky;
        uint8_t kb;
        int base = (int)(state->kbd_joystick->port - 1u) * 2;
        uint8_t mask = state->kbd_joystick->inputs;

        frontend_joystick_apple_state(state->kbd_joystick, &kx, &ky, &kb);
        /* Only override axes when a direction is held so SDL analog can coexist. */
        if ((mask & (FRONTEND_JOYSTICK_LEFT | FRONTEND_JOYSTICK_RIGHT)) != 0u) {
            axis[base] = kx;
        }
        if ((mask & (FRONTEND_JOYSTICK_UP | FRONTEND_JOYSTICK_DOWN)) != 0u) {
            axis[base + 1] = ky;
        }
        buttons |= kb;
    }

    if (state->published &&
        state->last_axis[0] == axis[0] &&
        state->last_axis[1] == axis[1] &&
        state->last_axis[2] == axis[2] &&
        state->last_axis[3] == axis[3] &&
        state->last_buttons == buttons) {
        return;
    }

    if (runtime_client_set_gameport(client, axis, buttons)) {
        state->last_axis[0] = axis[0];
        state->last_axis[1] = axis[1];
        state->last_axis[2] = axis[2];
        state->last_axis[3] = axis[3];
        state->last_buttons = buttons;
        state->published = true;
    }
}

/* Option/Alt is both a host modifier (Opt+Shift+1 stick assign) and, while the
   keyboard stick is on, a fire key. Sequence that stuck BUTN0:
     stick off → LALT down sets A2S_OPEN_APPLE
     Opt+Shift+1 enables stick
     LALT up is consumed by the stick (never clears OA)
   Release solid-apple whenever stick ownership changes or the stick eats Alt. */
static void release_solid_apple_keys(runtime_client *client)
{
    if (client == NULL) {
        return;
    }
    (void)runtime_client_keyboard_key(client, HOST_KEY_OPEN_APPLE, false);
    (void)runtime_client_keyboard_key(client, HOST_KEY_CLOSED_APPLE, false);
}

static void joystick_handle_key_and_solid_apple(
    frontend_joystick_input *kbd_joystick,
    sdl_apple_controller_state *controllers,
    runtime_client *client,
    const SDL_KeyboardEvent *event)
{
    SDL_Keycode sym;

    if (kbd_joystick == NULL || event == NULL) {
        return;
    }
    if (frontend_joystick_handle_key(kbd_joystick, event) &&
        controllers != NULL &&
        client != NULL) {
        sdl_apple_gameport_publish(controllers, client);
    }
    /* Stick owns Option as fire: keep A2S_OPEN/CLOSED_APPLE from sticking. */
    sym = event->keysym.sym;
    if (sym == SDLK_LALT || sym == SDLK_RALT) {
        release_solid_apple_keys(client);
    }
}

static void sdl_apple_controller_refresh_slot(
    sdl_apple_controller_state *state,
    size_t slot,
    runtime_client *client)
{
    uint8_t ax;
    uint8_t ay;
    uint8_t buttons;

    if (state == NULL || slot >= A2M_CONTROLLER_MAX ||
        state->controllers[slot].controller == NULL) {
        return;
    }

    sdl_apple_controller_read_state(
        state->controllers[slot].controller, &ax, &ay, &buttons);
    if (ax != state->controllers[slot].axis_x ||
        ay != state->controllers[slot].axis_y ||
        buttons != state->controllers[slot].buttons) {
        state->controllers[slot].axis_x = ax;
        state->controllers[slot].axis_y = ay;
        state->controllers[slot].buttons = buttons;
        sdl_apple_gameport_publish(state, client);
    }
}

static void sdl_apple_controller_add(
    sdl_apple_controller_state *state,
    runtime_client *client,
    int device_index)
{
    SDL_GameController *controller;
    SDL_Joystick *joystick;
    SDL_JoystickID instance_id;
    size_t slot;

    if (state == NULL || !SDL_IsGameController(device_index)) {
        return;
    }

    for (slot = 0; slot < A2M_CONTROLLER_MAX; slot++) {
        if (state->controllers[slot].controller == NULL) {
            break;
        }
    }
    if (slot >= A2M_CONTROLLER_MAX) {
        log_info(
            "ignoring extra controller: %s",
            SDL_GameControllerNameForIndex(device_index));
        return;
    }

    controller = SDL_GameControllerOpen(device_index);
    if (controller == NULL) {
        log_error("SDL_GameControllerOpen failed: %s", SDL_GetError());
        return;
    }

    joystick = SDL_GameControllerGetJoystick(controller);
    instance_id = joystick != NULL ? SDL_JoystickInstanceID(joystick) : -1;
    if (instance_id < 0) {
        log_error("SDL_JoystickInstanceID failed: %s", SDL_GetError());
        SDL_GameControllerClose(controller);
        return;
    }
    if (sdl_apple_controller_find_slot(state, instance_id) >= 0) {
        SDL_GameControllerClose(controller);
        return;
    }

    state->controllers[slot].controller = controller;
    state->controllers[slot].instance_id = instance_id;
    sdl_apple_controller_read_state(
        controller,
        &state->controllers[slot].axis_x,
        &state->controllers[slot].axis_y,
        &state->controllers[slot].buttons);
    log_info("controller connected: %s", SDL_GameControllerName(controller));
    sdl_apple_gameport_publish(state, client);
}

static void sdl_apple_controller_remove(
    sdl_apple_controller_state *state,
    runtime_client *client,
    SDL_JoystickID instance_id)
{
    int slot;

    slot = sdl_apple_controller_find_slot(state, instance_id);
    if (slot < 0) {
        return;
    }

    log_info(
        "controller disconnected: %s",
        SDL_GameControllerName(state->controllers[slot].controller));
    SDL_GameControllerClose(state->controllers[slot].controller);
    memset(&state->controllers[slot], 0, sizeof(state->controllers[slot]));
    state->controllers[slot].axis_x = FRONTEND_JOYSTICK_APPLE_AXIS_MID;
    state->controllers[slot].axis_y = FRONTEND_JOYSTICK_APPLE_AXIS_MID;
    sdl_apple_gameport_publish(state, client);
}

static void sdl_apple_controller_handle_event(
    sdl_apple_controller_state *state,
    runtime_client *client,
    const SDL_Event *event)
{
    int slot;

    if (state == NULL || event == NULL) {
        return;
    }

    switch (event->type) {
    case SDL_CONTROLLERDEVICEADDED:
        sdl_apple_controller_add(state, client, event->cdevice.which);
        break;

    case SDL_CONTROLLERDEVICEREMOVED:
        sdl_apple_controller_remove(state, client, event->cdevice.which);
        break;

    case SDL_CONTROLLERAXISMOTION:
        if (event->caxis.axis == SDL_CONTROLLER_AXIS_LEFTX ||
            event->caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
            slot = sdl_apple_controller_find_slot(state, event->caxis.which);
            if (slot >= 0) {
                sdl_apple_controller_refresh_slot(state, (size_t)slot, client);
            }
        }
        break;

    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP:
        switch (event->cbutton.button) {
        case SDL_CONTROLLER_BUTTON_A:
        case SDL_CONTROLLER_BUTTON_B:
        case SDL_CONTROLLER_BUTTON_X:
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            slot = sdl_apple_controller_find_slot(state, event->cbutton.which);
            if (slot >= 0) {
                sdl_apple_controller_refresh_slot(state, (size_t)slot, client);
            }
            break;
        default:
            break;
        }
        break;

    default:
        break;
    }
}

/* Opt+1 / Opt+2: map the single pad to stick N, or swap two pads. */
static void sdl_apple_controller_switch_mapping(
    sdl_apple_controller_state *state,
    runtime_client *client,
    unsigned stick)
{
    size_t connected_count;

    if (state == NULL || (stick != 1u && stick != 2u)) {
        return;
    }

    connected_count = sdl_apple_controller_count(state);
    if (connected_count >= 2) {
        state->swapped = !state->swapped;
        log_info("gamepad sticks swapped");
    } else {
        state->single_controller_port = stick;
        log_info("single gamepad mapped to stick %u", stick);
    }
    state->published = false;
    sdl_apple_gameport_publish(state, client);
}

static void sdl_apple_controllers_open_existing(
    sdl_apple_controller_state *state,
    runtime_client *client)
{
    int i;
    int count;

    count = SDL_NumJoysticks();
    for (i = 0; i < count; i++) {
        sdl_apple_controller_add(state, client, i);
    }
}

static void sdl_apple_controllers_close(
    sdl_apple_controller_state *state,
    runtime_client *client)
{
    size_t i;

    if (state == NULL) {
        return;
    }
    for (i = 0; i < A2M_CONTROLLER_MAX; i++) {
        if (state->controllers[i].controller != NULL) {
            SDL_GameControllerClose(state->controllers[i].controller);
            memset(&state->controllers[i], 0, sizeof(state->controllers[i]));
        }
    }
    sdl_apple_gameport_publish(state, client);
}

/* F10 pause/step and F11 step-over. allow_pause=false for key-repeat (step only).
   Time travel: same live step APIs, sealed and clamped to live. */
static bool handle_step_key_event(
    runtime_client *client,
    frontend_debug_state *debug,
    const SDL_KeyboardEvent *key,
    bool allow_pause)
{
    if (client == NULL || debug == NULL || key == NULL) {
        return false;
    }

    if (key->keysym.sym == SDLK_F10 && !frontend_input_has_shift_modifier(key)) {
        if (debug->runtime_state == FRONTEND_RUNTIME_STATE_RUNNING) {
            if (allow_pause) {
                (void)runtime_client_pause(client);
            }
        } else {
            (void)runtime_client_step_instruction(client);
        }
        return true;
    }
    if (key->keysym.sym == SDLK_F11) {
        (void)runtime_client_step_over(client);
        return true;
    }
    return false;
}

/* One in-flight Forensics HISTORY RPC (mirrors RUNTIME_HISTORY_RPC_REQUEST_ACTIVE). */
typedef struct forensics_history_rpc {
    bool pending;
    uint64_t token;
    frontend_history_verb verb;
    char label[160];
} forensics_history_rpc;

static forensics_history_rpc g_forensics_history_rpc;

static void forensics_history_rpc_clear(void)
{
    g_forensics_history_rpc.pending = false;
    g_forensics_history_rpc.token = 0u;
    g_forensics_history_rpc.verb = FRONTEND_HISTORY_VERB_NONE;
    g_forensics_history_rpc.label[0] = '\0';
}

static void forensics_history_close_cursor(runtime_client *client, frontend *ui)
{
    uint64_t token;
    uint64_t cursor;

    if (client == NULL) {
        return;
    }
    if (g_forensics_history_rpc.pending) {
        (void)runtime_client_cancel_rpc(client, g_forensics_history_rpc.token);
        forensics_history_rpc_clear();
    }
    cursor = frontend_forensics_last_cursor(ui);
    token = runtime_client_alloc_request_token(client);
    if (token == 0u) {
        return;
    }
    /* Fire-and-forget: clear UI session cursor; do not session_close default. */
    (void)runtime_client_history_close(client, 0u, cursor, token);
}

/*
 * Leave Forensics.
 * force_debugger (F9 or successful Land): always debugger, never resume.
 * Otherwise (Opt+R / Close): return to entry surface; resume only if that
 * surface was full-screen CRT and it was running when Forensics opened.
 */
static void leave_forensics_mode(
    platform_window *window,
    runtime_client *client,
    frontend *ui,
    bool *ui_visible,
    bool force_debugger)
{
    bool show_debugger = true;
    bool resume = false;
    int min_w = 0;
    int min_h = 0;

    if (ui == NULL || ui_visible == NULL || !frontend_forensics_is_open(ui)) {
        return;
    }
    forensics_history_close_cursor(client, ui);
    if (!force_debugger && frontend_forensics_entered_from_crt(ui)) {
        show_debugger = false;
        resume = frontend_forensics_crt_was_running(ui);
    }
    frontend_close_forensics(ui);
    *ui_visible = show_debugger;
    if (show_debugger) {
        frontend_debug_min_window_size(ui, &min_w, &min_h);
        request_debug_state(client);
    }
    platform_window_set_minimum_size(window, min_w, min_h);
    if (resume) {
        (void)runtime_client_run(client);
    }
}

static bool forensics_history_begin_rpc(
    runtime_client *client,
    frontend *ui,
    frontend_history_verb verb,
    const char *label,
    uint64_t *out_token)
{
    uint64_t token;

    if (client == NULL || out_token == NULL) {
        return false;
    }
    if (g_forensics_history_rpc.pending) {
        if (ui != NULL) {
            frontend_forensics_apply_rpc_error(
                ui, RUNTIME_HISTORY_RPC_REQUEST_ACTIVE);
        }
        return false;
    }
    token = runtime_client_alloc_request_token(client);
    if (token == 0u) {
        return false;
    }
    g_forensics_history_rpc.pending = true;
    g_forensics_history_rpc.token = token;
    g_forensics_history_rpc.verb = verb;
    if (label != NULL) {
        snprintf(
            g_forensics_history_rpc.label,
            sizeof(g_forensics_history_rpc.label),
            "%s",
            label);
    } else {
        g_forensics_history_rpc.label[0] = '\0';
    }
    *out_token = token;
    return true;
}

static void forensics_handle_history_event(
    runtime_client *client,
    frontend *ui,
    const runtime_event *event)
{
    if (client == NULL || event == NULL || !g_forensics_history_rpc.pending ||
        event->request_token != g_forensics_history_rpc.token) {
        return;
    }

    if (event->type == RUNTIME_EVENT_HISTORY_STATUS_RESPONSE) {
        /* User-typed `info` carries label; open-time refresh uses empty label. */
        bool note = g_forensics_history_rpc.verb == FRONTEND_HISTORY_VERB_INFO &&
            g_forensics_history_rpc.label[0] != '\0';
        frontend_forensics_apply_status(
            ui, &event->data.history_status, note);
        forensics_history_rpc_clear();
        return;
    }

    if (event->type != RUNTIME_EVENT_HISTORY_RESULT_RESPONSE) {
        return;
    }

    {
        const runtime_history_rpc_meta *meta = &event->data.history_rpc;
        if (meta->status != RUNTIME_HISTORY_RPC_OK) {
            frontend_forensics_apply_rpc_error(ui, meta->status);
            forensics_history_rpc_clear();
            return;
        }
        if (meta->byte_length == 0u) {
            /* status-only success (history-close) */
            if (g_forensics_history_rpc.verb == FRONTEND_HISTORY_VERB_CLOSE &&
                frontend_forensics_is_open(ui)) {
                frontend_forensics_apply_result(
                    ui,
                    FRONTEND_HISTORY_VERB_CLOSE,
                    g_forensics_history_rpc.label,
                    meta,
                    NULL,
                    0u,
                    NULL);
            }
            forensics_history_rpc_clear();
            return;
        }
        {
            uint8_t *bytes = NULL;
            uint32_t length = 0u;
            runtime_history_rpc_meta claimed;
            runtime_history_record *records = NULL;
            bool *anchors = NULL;
            size_t count = 0u;
            uint64_t epoch = 0u;

            if (!runtime_client_claim_history_rpc(
                    client,
                    g_forensics_history_rpc.token,
                    &bytes,
                    &length,
                    &claimed)) {
                frontend_forensics_apply_rpc_error(
                    ui, RUNTIME_HISTORY_RPC_ERROR);
                forensics_history_rpc_clear();
                return;
            }
            if (runtime_history_wire_decode(
                    bytes,
                    length,
                    &epoch,
                    &records,
                    &anchors,
                    &count) != RUNTIME_HISTORY_WIRE_OK) {
                free(bytes);
                free(records);
                free(anchors);
                frontend_forensics_apply_rpc_error(
                    ui, RUNTIME_HISTORY_RPC_BAD_ARGS);
                forensics_history_rpc_clear();
                return;
            }
            frontend_forensics_apply_result(
                ui,
                g_forensics_history_rpc.verb,
                g_forensics_history_rpc.label,
                &claimed,
                records,
                count,
                anchors);
            free(bytes);
            free(records);
            free(anchors);
            forensics_history_rpc_clear();
        }
    }
}

static bool path_has_extension(const char *path, const char *ext);

static bool path_is_absolute_local(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return false;
    }
#if defined(_WIN32)
    if (path[0] == '/' || path[0] == '\\') {
        return true;
    }
    return isalpha((unsigned char)path[0]) && path[1] == ':';
#else
    return path[0] == '/';
#endif
}

static bool join_path_local(char *out, size_t out_size, const char *dir, const char *name)
{
    size_t dir_len;
    int written;

    if (out == NULL || out_size == 0 || dir == NULL || name == NULL) {
        return false;
    }
    dir_len = strlen(dir);
    if (dir_len == 0) {
        return snprintf(out, out_size, "%s", name) < (int)out_size;
    }
#if defined(_WIN32)
    if (dir[dir_len - 1u] == '/' || dir[dir_len - 1u] == '\\') {
        written = snprintf(out, out_size, "%s%s", dir, name);
    } else {
        written = snprintf(out, out_size, "%s\\%s", dir, name);
    }
#else
    if (dir[dir_len - 1u] == '/') {
        written = snprintf(out, out_size, "%s%s", dir, name);
    } else {
        written = snprintf(out, out_size, "%s/%s", dir, name);
    }
#endif
    return written > 0 && (size_t)written < out_size;
}

static bool options_ini_dir(const app_options *options, char *out, size_t out_size)
{
    const char *ini_path;
    const char *slash;
    size_t len;

    if (options == NULL || out == NULL || out_size == 0) {
        return false;
    }
    ini_path = options->ini_path;
    if (ini_path == NULL || ini_path[0] == '\0') {
        return false;
    }
    slash = strrchr(ini_path, '/');
#if defined(_WIN32)
    {
        const char *backslash = strrchr(ini_path, '\\');
        if (backslash != NULL && (slash == NULL || backslash > slash)) {
            slash = backslash;
        }
    }
#endif
    if (slash == NULL) {
        return snprintf(out, out_size, ".") < (int)out_size;
    }
    len = (size_t)(slash - ini_path);
    if (len == 0) {
        len = 1; /* root */
    }
    if (len + 1u > out_size) {
        return false;
    }
    memcpy(out, ini_path, len);
    out[len] = '\0';
    return true;
}

static bool quicksave_folder_absolute(
    const app_options *options,
    const char *folder,
    char *out,
    size_t out_size)
{
    char ini_dir[1024];

    if (out == NULL || out_size == 0) {
        return false;
    }
    if (folder == NULL || folder[0] == '\0') {
        folder = ".";
    }
    if (path_is_absolute_local(folder)) {
        return snprintf(out, out_size, "%s", folder) < (int)out_size;
    }
    if (!options_ini_dir(options, ini_dir, sizeof(ini_dir))) {
        return snprintf(out, out_size, "%s", folder) < (int)out_size;
    }
    return join_path_local(out, out_size, ini_dir, folder);
}

static const char *path_basename_local(const char *path)
{
    const char *slash;

    if (path == NULL) {
        return "";
    }
    slash = strrchr(path, '/');
#if defined(_WIN32)
    {
        const char *backslash = strrchr(path, '\\');
        if (backslash != NULL && (slash == NULL || backslash > slash)) {
            slash = backslash;
        }
    }
#endif
    return slash != NULL ? slash + 1 : path;
}

static void sanitize_snapshot_stem(const char *input, char *out, size_t out_size)
{
    const char *base = path_basename_local(input);
    size_t i = 0;

    if (out == NULL || out_size == 0) {
        return;
    }
    if (base == NULL || base[0] == '\0') {
        base = "a2m";
    }
    while (base[i] != '\0' && i + 1 < out_size) {
        if (base[i] == '.') {
            break;
        }
        out[i] = (isalnum((unsigned char)base[i]) || base[i] == '-' || base[i] == '_') ?
            base[i] :
            '_';
        ++i;
    }
    if (i == 0) {
        snprintf(out, out_size, "a2m");
    } else {
        out[i] = '\0';
    }
}

static const char *active_content_path(const app_options *options)
{
    int i;

    if (options == NULL) {
        return NULL;
    }
    if (options->sna_path != NULL && options->sna_path[0] != '\0') {
        return options->sna_path;
    }
    for (i = 0; i < options->diskii_count; ++i) {
        if (options->diskii[i].path != NULL && options->diskii[i].path[0] != '\0') {
            return options->diskii[i].path;
        }
    }
    for (i = 0; i < options->smartport_count; ++i) {
        if (options->smartport[i].path != NULL && options->smartport[i].path[0] != '\0') {
            return options->smartport[i].path;
        }
    }
    if (options->hd_s7d0 != NULL && options->hd_s7d0[0] != '\0') {
        return options->hd_s7d0;
    }
    return NULL;
}

static bool make_quicksave_path(
    const app_options *options,
    const char *snapshot_dir,
    char *out,
    size_t out_size)
{
    char folder[1024];
    char stem[256];
    char filename[512];
    time_t now;
    struct tm tm_value;
    int suffix;

    if (!quicksave_folder_absolute(options, snapshot_dir, folder, sizeof(folder))) {
        return false;
    }
    sanitize_snapshot_stem(active_content_path(options), stem, sizeof(stem));
    now = time(NULL);
#if defined(_WIN32)
    localtime_s(&tm_value, &now);
#else
    {
        struct tm *tmp = localtime(&now);
        if (tmp == NULL) {
            return false;
        }
        tm_value = *tmp;
    }
#endif
    for (suffix = 0; suffix < 1000; ++suffix) {
        int written;
        if (suffix == 0) {
            written = snprintf(
                filename,
                sizeof(filename),
                "%s-%04d%02d%02d-%02d%02d%02d.a2state",
                stem,
                tm_value.tm_year + 1900,
                tm_value.tm_mon + 1,
                tm_value.tm_mday,
                tm_value.tm_hour,
                tm_value.tm_min,
                tm_value.tm_sec);
        } else {
            written = snprintf(
                filename,
                sizeof(filename),
                "%s-%04d%02d%02d-%02d%02d%02d-%d.a2state",
                stem,
                tm_value.tm_year + 1900,
                tm_value.tm_mon + 1,
                tm_value.tm_mday,
                tm_value.tm_hour,
                tm_value.tm_min,
                tm_value.tm_sec,
                suffix);
        }
        if (written < 0 || (size_t)written >= sizeof(filename)) {
            return false;
        }
        if (!join_path_local(out, out_size, folder, filename)) {
            return false;
        }
#if defined(_WIN32)
        if (_access(out, 0) != 0) {
#else
        if (access(out, F_OK) != 0) {
#endif
            return true;
        }
    }
    return false;
}

static bool find_newest_state_file(
    const app_options *options,
    const char *snapshot_dir,
    char *out,
    size_t out_size)
{
    char folder[1024];
    bool found = false;

    if (!quicksave_folder_absolute(options, snapshot_dir, folder, sizeof(folder))) {
        return false;
    }
#if defined(_WIN32)
    {
        char pattern[1200];
        HANDLE handle;
        WIN32_FIND_DATAA data;
        FILETIME newest_time = { 0, 0 };

        if (!join_path_local(pattern, sizeof(pattern), folder, "*.a2state")) {
            return false;
        }
        handle = FindFirstFileA(pattern, &data);
        if (handle == INVALID_HANDLE_VALUE) {
            return false;
        }
        do {
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 &&
                (!found || CompareFileTime(&data.ftLastWriteTime, &newest_time) > 0)) {
                if (join_path_local(out, out_size, folder, data.cFileName)) {
                    newest_time = data.ftLastWriteTime;
                    found = true;
                }
            }
        } while (FindNextFileA(handle, &data));
        FindClose(handle);
    }
#else
    {
        DIR *dir = opendir(folder);
        struct dirent *entry;
        time_t newest_mtime = 0;

        if (dir == NULL) {
            return false;
        }
        while ((entry = readdir(dir)) != NULL) {
            char candidate[1200];
            struct stat st;
            if (!path_has_extension(entry->d_name, "a2state")) {
                continue;
            }
            if (!join_path_local(candidate, sizeof(candidate), folder, entry->d_name)) {
                continue;
            }
            if (stat(candidate, &st) != 0 || !A2M_STAT_ISREG(st.st_mode)) {
                continue;
            }
            if (!found || st.st_mtime > newest_mtime ||
                (st.st_mtime == newest_mtime && strcmp(candidate, out) > 0)) {
                snprintf(out, out_size, "%s", candidate);
                newest_mtime = st.st_mtime;
                found = true;
            }
        }
        closedir(dir);
    }
#endif
    return found;
}

/* c64m muscle memory: Opt+Shift+. save, Opt+Shift+, load */
static bool key_is_quicksave_shortcut(const SDL_KeyboardEvent *key)
{
    if (!frontend_input_has_option_modifier(key) ||
        !frontend_input_has_shift_modifier(key)) {
        return false;
    }
    return key->keysym.sym == SDLK_GREATER || key->keysym.sym == SDLK_PERIOD;
}

static bool key_is_quickload_shortcut(const SDL_KeyboardEvent *key)
{
    if (!frontend_input_has_option_modifier(key) ||
        !frontend_input_has_shift_modifier(key)) {
        return false;
    }
    return key->keysym.sym == SDLK_LESS || key->keysym.sym == SDLK_COMMA;
}

static bool key_is_quick_assemble_shortcut(const SDL_KeyboardEvent *key)
{
    return key != NULL &&
        key->keysym.sym == SDLK_a &&
        frontend_input_has_option_modifier(key) &&
        frontend_input_has_shift_modifier(key);
}

static bool key_is_video_display_shortcut(const SDL_KeyboardEvent *key)
{
    return key != NULL &&
        key->keysym.sym == SDLK_c &&
        frontend_input_has_option_modifier(key) &&
        frontend_input_has_shift_modifier(key);
}

static bool send_quicksave(
    runtime_client *client,
    const app_options *options,
    const frontend *ui)
{
    char path[1200];
    const char *snapshot_dir =
        frontend_get_browse_dir(ui, FRONTEND_BROWSE_SLOT_SNAPSHOT);

    if (!make_quicksave_path(options, snapshot_dir, path, sizeof(path))) {
        log_warn("quicksave: failed to build snapshot path");
        return false;
    }
    log_info("quicksave: %s", path);
    return runtime_client_save_state(client, path);
}

static bool send_quickload(
    runtime_client *client,
    const app_options *options,
    const frontend *ui)
{
    char path[1200];
    const char *snapshot_dir =
        frontend_get_browse_dir(ui, FRONTEND_BROWSE_SLOT_SNAPSHOT);

    if (!find_newest_state_file(options, snapshot_dir, path, sizeof(path))) {
        log_warn("quickload: no .a2state files found");
        return false;
    }
    log_info("quickload: %s", path);
    return runtime_client_load_state(client, path);
}

static bool path_has_extension(const char *path, const char *ext)
{
    size_t path_len;
    size_t ext_len;
    size_t i;

    if (path == NULL || ext == NULL || ext[0] == '\0') {
        return false;
    }
    path_len = strlen(path);
    ext_len = strlen(ext);
    if (path_len <= ext_len + 1u || path[path_len - ext_len - 1u] != '.') {
        return false;
    }
    for (i = 0; i < ext_len; i++) {
        char a = path[path_len - ext_len + i];
        char b = ext[i];
        if (tolower((unsigned char)a) != tolower((unsigned char)b)) {
            return false;
        }
    }
    return true;
}

static void sync_assembler_options_from_frontend(app_options *options, frontend *ui)
{
    frontend_assembler_options assembler;
    char absolute_path[1024];

    if (options == NULL || ui == NULL) {
        return;
    }

    memset(&assembler, 0, sizeof(assembler));
    frontend_get_assembler_options(ui, &assembler);
    if (assembler.file[0] != '\0' &&
        app_options_path_absolute_from_ini(
            options, assembler.file, absolute_path, sizeof(absolute_path))) {
        (void)app_options_set_string(&options->assembler_file, absolute_path);
    } else {
        (void)app_options_set_string(
            &options->assembler_file, assembler.file[0] != '\0' ? assembler.file : NULL);
    }
    (void)app_options_set_string(
        &options->assembler_address,
        assembler.address[0] != '\0' ? assembler.address : NULL);
    (void)app_options_set_string(
        &options->assembler_run_address,
        assembler.run_address[0] != '\0' ? assembler.run_address : NULL);
    options->assembler_use_address = assembler.use_address;
    options->assembler_auto_run = assembler.auto_run;
    options->assembler_mli_launch = assembler.mli_launch;
    options->assembler_reset_first = assembler.reset_first;
    options->assembler_rearm_oneshots = assembler.rearm_oneshots;
}

/* Append path to drive queue + mount (Disk II d0 default). */
static void host_disk_queue_add(
    runtime_client *client,
    frontend *ui,
    app_options *options,
    uint8_t slot_number,
    uint8_t drive,
    const char *path)
{
    if (path == NULL || path[0] == '\0' || slot_number < 1u ||
        slot_number > 7u || drive > 1u) {
        return;
    }
    if (client != NULL) {
        (void)runtime_client_media_insert(
            client, slot_number, drive, RUNTIME_SLOT_CARD_DISKII, path);
    }
    (void)options;
    (void)ui;
}

/* Live machine slots win when available; else Configure slot_cards. */
static void fill_slot_cards_for_drop(
    runtime_slot_card_type cards[RUNTIME_APPLE_SLOT_COUNT],
    const frontend_debug_state *debug,
    const app_options *options)
{
    int slot;

    memset(cards, 0, sizeof(runtime_slot_card_type) * (size_t)RUNTIME_APPLE_SLOT_COUNT);
    for (slot = 1; slot <= 7; ++slot) {
        if (debug != NULL && debug->has_apple_flags) {
            cards[slot] = debug->slots[slot].card_type;
            continue;
        }
        if (options == NULL) {
            continue;
        }
        switch (options->slot_cards[slot]) {
        case APP_SLOT_CARD_DISKII:
            cards[slot] = RUNTIME_SLOT_CARD_DISKII;
            break;
        case APP_SLOT_CARD_SMARTPORT:
            cards[slot] = RUNTIME_SLOT_CARD_SMARTPORT;
            break;
        case APP_SLOT_CARD_MOCKINGBOARD:
            cards[slot] = RUNTIME_SLOT_CARD_MOCKINGBOARD;
            break;
        case APP_SLOT_CARD_EMPTY:
        default:
            cards[slot] = RUNTIME_SLOT_CARD_EMPTY;
            break;
        }
    }
}

static int find_diskii_slot_for_drop(
    const frontend_debug_state *debug,
    const app_options *options)
{
    runtime_slot_card_type cards[RUNTIME_APPLE_SLOT_COUNT];

    fill_slot_cards_for_drop(cards, debug, options);
    return runtime_resolve_diskii_slot(cards);
}

static int find_smartport_slot_for_drop(
    const frontend_debug_state *debug,
    const app_options *options)
{
    runtime_slot_card_type cards[RUNTIME_APPLE_SLOT_COUNT];

    fill_slot_cards_for_drop(cards, debug, options);
    return runtime_resolve_smartport_slot(cards);
}

/* Raw 35-track × 16-sector × 256-byte Disk II image (matches image_load_dsk). */
enum { A2M_DROP_FLOPPY_PO_SIZE = 143360 };

static void drop_floppy_image(
    runtime_client *client,
    frontend *ui,
    app_options *options,
    const frontend_debug_state *debug,
    const char *path)
{
    int disk_slot = find_diskii_slot_for_drop(debug, options);
    if (disk_slot < 1) {
        log_warn(
            "Dropped floppy ignored (no Disk II card installed): %s", path);
        return;
    }
    host_disk_queue_add(client, ui, options, (uint8_t)disk_slot, 0u, path);
    log_info("Dropped floppy queued on Disk II s%dd0: %s", disk_slot, path);
}

static void drop_smartport_image(
    runtime_client *client,
    app_options *options,
    const frontend_debug_state *debug,
    const char *path)
{
    int sp_slot = find_smartport_slot_for_drop(debug, options);
    (void)options;
    if (sp_slot < 1) {
        log_warn(
            "Dropped HD image ignored (no SmartPort card installed): %s", path);
        return;
    }
    if (!runtime_client_media_insert(
            client,
            (uint8_t)sp_slot,
            0u,
            RUNTIME_SLOT_CARD_SMARTPORT,
            path)) {
        log_error(
            "Dropped HD image insert failed (slot %d unit 0): %s",
            sp_slot,
            path);
        return;
    }
    /* Options/INI mounts update via MEDIA_CHANGED. */
    log_info("Dropped HD image inserted on SmartPort s%dd0: %s", sp_slot, path);
}

/* .po is overloaded: classic floppy size → Disk II; anything else → SmartPort. */
static bool drop_po_is_floppy(const char *path, bool *out_ok)
{
    struct stat st;

    if (out_ok != NULL) {
        *out_ok = false;
    }
    if (path == NULL || path[0] == '\0') {
        return false;
    }
    if (stat(path, &st) != 0 || !A2M_STAT_ISREG(st.st_mode)) {
        return false;
    }
    if (out_ok != NULL) {
        *out_ok = true;
    }
    return st.st_size == (off_t)A2M_DROP_FLOPPY_PO_SIZE;
}

/* Drag-and-drop onto the window (c64m host loop feature, Apple extensions). */
static void handle_drop_file(
    runtime_client *client,
    frontend *ui,
    app_options *options,
    const frontend_debug_state *debug,
    char *path)
{
    if (path == NULL) {
        return;
    }
    if (client != NULL) {
        if (path_has_extension(path, "nib") || path_has_extension(path, "dsk") ||
            path_has_extension(path, "do") || path_has_extension(path, "woz")) {
            drop_floppy_image(client, ui, options, debug, path);
        } else if (path_has_extension(path, "po")) {
            bool sized_ok = false;
            bool is_floppy = drop_po_is_floppy(path, &sized_ok);
            if (!sized_ok) {
                log_warn("Dropped .po ignored (unreadable file): %s", path);
            } else if (is_floppy) {
                drop_floppy_image(client, ui, options, debug, path);
            } else {
                drop_smartport_image(client, options, debug, path);
            }
        } else if (path_has_extension(path, "a2state")) {
            (void)runtime_client_load_state(client, path);
        } else if (path_has_extension(path, "hdv") || path_has_extension(path, "2mg")) {
            drop_smartport_image(client, options, debug, path);
        } else {
            log_info("Drop ignored (unsupported type): %s", path);
        }
    }
    SDL_free(path);
}

static void apply_keyboard_joystick_options(
    frontend_joystick_input *kbd_joystick,
    sdl_apple_controller_state *controllers,
    runtime_client *client,
    const app_options *options)
{
    if (kbd_joystick == NULL || options == NULL) {
        return;
    }
    frontend_joystick_set_layout(
        kbd_joystick,
        frontend_joystick_layout_from_string(options->keyboard_joystick_layout));
    frontend_joystick_set_port(
        kbd_joystick,
        (unsigned)(options->keyboard_joystick_port > 0 ?
                       options->keyboard_joystick_port :
                       0));
    frontend_joystick_set_swap_buttons(
        kbd_joystick, options->keyboard_joystick_swap_buttons);
    if (controllers != NULL && client != NULL) {
        /* Force re-publish so release-to-center / port Off take effect. */
        controllers->published = false;
        sdl_apple_gameport_publish(controllers, client);
    }
}

/* ---- .a2state HOST trailer (kbd joy port/layout/swap; host-owned) ------- */

static uint32_t read_le32_local(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
        ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) |
        ((uint32_t)bytes[3] << 24);
}

static void write_le32_local(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value & 0xffu);
    bytes[1] = (uint8_t)((value >> 8) & 0xffu);
    bytes[2] = (uint8_t)((value >> 16) & 0xffu);
    bytes[3] = (uint8_t)(value >> 24);
}

typedef struct host_state_loaded {
    uint8_t port;
    frontend_joystick_layout layout;
    bool swap_buttons;
    bool has_joystick;
} host_state_loaded;

static bool append_host_state_chunk(
    const char *path,
    const app_options *options,
    const frontend_joystick_input *kbd_joystick)
{
    FILE *file;
    uint8_t bytes[A2M_STATE_CHUNK_HEADER_SIZE + A2M_STATE_HOST_V1_SIZE];
    uint8_t *cursor = bytes;
    uint8_t port;
    uint8_t layout;
    uint8_t swap;

    if (path == NULL || path[0] == '\0') {
        return false;
    }
    port = kbd_joystick != NULL ? (uint8_t)kbd_joystick->port :
        (uint8_t)(options != NULL ? options->keyboard_joystick_port : 0);
    if (port > 2u) {
        port = 0;
    }
    layout = kbd_joystick != NULL ? (uint8_t)kbd_joystick->layout :
        (uint8_t)frontend_joystick_layout_from_string(
            options != NULL ? options->keyboard_joystick_layout : NULL);
    if (layout > (uint8_t)FRONTEND_JOYSTICK_LAYOUT_WASD) {
        layout = (uint8_t)FRONTEND_JOYSTICK_LAYOUT_NUMPAD;
    }
    swap = kbd_joystick != NULL ? (kbd_joystick->swap_buttons ? 1u : 0u) :
        (uint8_t)(options != NULL && options->keyboard_joystick_swap_buttons ? 1u : 0u);

    write_le32_local(cursor, A2M_STATE_HOST_TAG);
    cursor += 4;
    write_le32_local(cursor, (uint32_t)A2M_STATE_HOST_V1_SIZE);
    cursor += 4;
    write_le32_local(cursor, A2M_STATE_HOST_VERSION);
    cursor += 4;
    *cursor++ = port;
    *cursor++ = layout;
    *cursor++ = swap;
    *cursor++ = 0;

    file = fopen(path, "ab");
    if (file == NULL) {
        return false;
    }
    if (fwrite(bytes, 1, sizeof(bytes), file) != sizeof(bytes)) {
        fclose(file);
        return false;
    }
    fclose(file);
    return true;
}

static bool read_host_state_chunk(const char *path, host_state_loaded *out)
{
    FILE *file;
    uint8_t *bytes = NULL;
    long length;
    size_t pos;
    uint32_t magic;
    uint32_t version;
    uint32_t header_size;
    bool found = false;

    if (path == NULL || out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }
    length = ftell(file);
    if (length < (long)A2M_STATE_FILE_HEADER_MIN || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }
    bytes = (uint8_t *)malloc((size_t)length);
    if (bytes == NULL) {
        fclose(file);
        return false;
    }
    if (fread(bytes, 1, (size_t)length, file) != (size_t)length) {
        free(bytes);
        fclose(file);
        return false;
    }
    fclose(file);

    magic = read_le32_local(bytes);
    version = read_le32_local(bytes + 4);
    header_size = read_le32_local(bytes + 8);
    if (magic != A2_SNAPSHOT_MAGIC || version < A2_SNAPSHOT_VERSION_MIN ||
        version > A2_SNAPSHOT_VERSION || header_size < 12u ||
        header_size > (uint32_t)length) {
        free(bytes);
        return false;
    }

    pos = header_size;
    while (pos + A2M_STATE_CHUNK_HEADER_SIZE <= (size_t)length) {
        uint32_t tag = read_le32_local(bytes + pos);
        uint32_t chunk_len = read_le32_local(bytes + pos + 4);
        const uint8_t *payload;

        pos += A2M_STATE_CHUNK_HEADER_SIZE;
        if (chunk_len > (uint32_t)((size_t)length - pos)) {
            break;
        }
        payload = bytes + pos;
        if (tag == A2M_STATE_HOST_TAG && chunk_len >= A2M_STATE_HOST_V1_SIZE) {
            uint32_t host_version = read_le32_local(payload);
            if (host_version == A2M_STATE_HOST_VERSION) {
                uint8_t port = payload[4];
                uint8_t layout = payload[5];
                uint8_t swap = payload[6];
                if (port <= 2u && layout <= (uint8_t)FRONTEND_JOYSTICK_LAYOUT_WASD) {
                    out->port = port;
                    out->layout = (frontend_joystick_layout)layout;
                    out->swap_buttons = swap != 0;
                    out->has_joystick = true;
                    found = true; /* last valid HOST wins */
                }
            }
        }
        pos += chunk_len;
    }
    free(bytes);
    return found;
}

static void apply_loaded_host_state(
    const char *path,
    app_options *options,
    frontend *ui,
    runtime_client *client,
    sdl_apple_controller_state *controllers,
    frontend_joystick_input *kbd_joystick)
{
    host_state_loaded host;

    memset(&host, 0, sizeof(host));
    if (!read_host_state_chunk(path, &host) || !host.has_joystick) {
        return;
    }

    if (kbd_joystick != NULL) {
        frontend_joystick_set_layout(kbd_joystick, host.layout);
        frontend_joystick_set_port(kbd_joystick, host.port);
        frontend_joystick_set_swap_buttons(kbd_joystick, host.swap_buttons);
    }
    if (options != NULL) {
        options->keyboard_joystick_port = (int)host.port;
        options->keyboard_joystick_swap_buttons = host.swap_buttons;
        (void)app_options_set_string(
            &options->keyboard_joystick_layout,
            frontend_joystick_layout_to_string(host.layout));
    }
    if (controllers != NULL && client != NULL) {
        controllers->published = false;
        sdl_apple_gameport_publish(controllers, client);
    }
    if (ui != NULL && options != NULL && !frontend_config_dialog_is_open(ui)) {
        frontend_set_config_state(ui, options);
    }
    log_info(
        "loaded host keyboard joystick: port %u (%s)%s",
        (unsigned)host.port,
        frontend_joystick_layout_to_string(host.layout),
        host.swap_buttons ? ", swap fire" : "");
}

static bool intent_mutates_in_inspect(frontend_debugger_intent_type type)
{
    switch (type) {
    case FRONTEND_DEBUGGER_INTENT_REGISTER_SET_PC:
    case FRONTEND_DEBUGGER_INTENT_REGISTER_SET_SP:
    case FRONTEND_DEBUGGER_INTENT_REGISTER_SET_A:
    case FRONTEND_DEBUGGER_INTENT_REGISTER_SET_X:
    case FRONTEND_DEBUGGER_INTENT_REGISTER_SET_Y:
    case FRONTEND_DEBUGGER_INTENT_REGISTER_SET_STATUS:
    case FRONTEND_DEBUGGER_INTENT_MEMORY_WRITE_BYTE:
    case FRONTEND_DEBUGGER_INTENT_MACHINE_RESET:
    case FRONTEND_DEBUGGER_INTENT_ASSEMBLE_RUN:
    case FRONTEND_DEBUGGER_INTENT_LOAD_BIN_EXECUTE:
    case FRONTEND_DEBUGGER_INTENT_SAVE_BIN_EXECUTE:
    case FRONTEND_DEBUGGER_INTENT_STATE_SAVE_AS_DIALOG:
    case FRONTEND_DEBUGGER_INTENT_STATE_LOAD_DIALOG:
    case FRONTEND_DEBUGGER_INTENT_MEDIA_INSERT_DIALOG:
    case FRONTEND_DEBUGGER_INTENT_MEDIA_EJECT:
    case FRONTEND_DEBUGGER_INTENT_MEDIA_SWAP:
    case FRONTEND_DEBUGGER_INTENT_BOOT_SLOT:
    case FRONTEND_DEBUGGER_INTENT_DISK_MOUNT_DIALOG:
    case FRONTEND_DEBUGGER_INTENT_DISK_ADD_DIALOG:
    case FRONTEND_DEBUGGER_INTENT_DISK_UNMOUNT:
    case FRONTEND_DEBUGGER_INTENT_CONFIG_APPLY:
    case FRONTEND_DEBUGGER_INTENT_INSPECTOR_SET_ENABLED:
        return true;
    default:
        return false;
    }
}

static void machine_config_from_options(
    const app_options *options,
    runtime_machine_config *out)
{
    int slot;

    memset(out, 0, sizeof(*out));
    if (options == NULL) {
        return;
    }
    out->pause_on_brk = options->pause_on_brk;
    out->apple_model = (uint8_t)options->apple_model;
    for (slot = 1; slot <= 7; ++slot) {
        switch (options->slot_cards[slot]) {
        case APP_SLOT_CARD_DISKII:
            out->slot_cards[slot] = RUNTIME_SLOT_CARD_DISKII;
            break;
        case APP_SLOT_CARD_SMARTPORT:
            out->slot_cards[slot] = RUNTIME_SLOT_CARD_SMARTPORT;
            break;
        case APP_SLOT_CARD_MOCKINGBOARD:
            out->slot_cards[slot] = RUNTIME_SLOT_CARD_MOCKINGBOARD;
            break;
        case APP_SLOT_CARD_EMPTY:
        default:
            out->slot_cards[slot] = RUNTIME_SLOT_CARD_EMPTY;
            break;
        }
    }
}

static void dispatch_intent(
    runtime_client *client,
    frontend *ui,
    app_options *options,
    frontend_joystick_input *kbd_joystick,
    sdl_apple_controller_state *controllers,
    bool runtime_running,
    bool inspecting,
    const frontend_debugger_intent *intent)
{
    if (client == NULL || intent == NULL) {
        return;
    }
    if (inspecting && intent_mutates_in_inspect(intent->type)) {
        return;
    }

    switch (intent->type) {
    case FRONTEND_DEBUGGER_INTENT_REGISTER_SET_PC:
        (void)runtime_client_set_pc(client, intent->value);
        break;
    case FRONTEND_DEBUGGER_INTENT_REGISTER_SET_SP:
        (void)runtime_client_set_sp(client, (uint8_t)intent->value);
        break;
    case FRONTEND_DEBUGGER_INTENT_REGISTER_SET_A:
        (void)runtime_client_set_a(client, (uint8_t)intent->value);
        break;
    case FRONTEND_DEBUGGER_INTENT_REGISTER_SET_X:
        (void)runtime_client_set_x(client, (uint8_t)intent->value);
        break;
    case FRONTEND_DEBUGGER_INTENT_REGISTER_SET_Y:
        (void)runtime_client_set_y(client, (uint8_t)intent->value);
        break;
    case FRONTEND_DEBUGGER_INTENT_REGISTER_SET_STATUS:
        (void)runtime_client_set_status(client, (uint8_t)intent->value);
        break;
    case FRONTEND_DEBUGGER_INTENT_REQUEST_MEMORY:
        (void)runtime_client_request_memory(
            client, intent->address, intent->length, intent->memory_mode);
        break;
    case FRONTEND_DEBUGGER_INTENT_REQUEST_MEMORY_VIEW:
        (void)runtime_client_request_memory_view(
            client, intent->address, intent->length, intent->memory_mode);
        break;
    case FRONTEND_DEBUGGER_INTENT_REQUEST_DEBUG_MEMORY:
        (void)runtime_client_request_debug_memory(client, intent->include_write_history);
        break;
    case FRONTEND_DEBUGGER_INTENT_REQUEST_CALL_STACK:
        (void)runtime_client_request_call_stack(client);
        break;
    case FRONTEND_DEBUGGER_INTENT_MEMORY_WRITE_BYTE:
        (void)runtime_client_write_memory_byte(
            client, intent->address, (uint8_t)intent->value, intent->memory_mode);
        break;
    case FRONTEND_DEBUGGER_INTENT_BREAKPOINT_SET_EXECUTE:
        (void)runtime_client_set_execute_breakpoint(client, intent->address);
        (void)runtime_client_request_breakpoints(client);
        break;
    case FRONTEND_DEBUGGER_INTENT_BREAKPOINT_CLEAR:
        (void)runtime_client_clear_breakpoint(client, intent->id);
        (void)runtime_client_request_breakpoints(client);
        break;
    case FRONTEND_DEBUGGER_INTENT_BREAKPOINT_CLEAR_ALL:
        (void)runtime_client_clear_all_breakpoints(client);
        (void)runtime_client_request_breakpoints(client);
        break;
    case FRONTEND_DEBUGGER_INTENT_BREAKPOINT_SET_ENABLED:
        (void)runtime_client_set_breakpoint_enabled(client, intent->id, intent->enabled);
        (void)runtime_client_request_breakpoints(client);
        break;
    case FRONTEND_DEBUGGER_INTENT_BREAKPOINT_CREATE:
        (void)runtime_client_create_breakpoint(client, &intent->breakpoint);
        (void)runtime_client_request_breakpoints(client);
        break;
    case FRONTEND_DEBUGGER_INTENT_BREAKPOINT_UPDATE:
        (void)runtime_client_update_breakpoint(client, intent->id, &intent->breakpoint);
        (void)runtime_client_request_breakpoints(client);
        break;
    case FRONTEND_DEBUGGER_INTENT_BREAKPOINT_REQUEST_SNAPSHOT:
        (void)runtime_client_request_breakpoints(client);
        break;
    case FRONTEND_DEBUGGER_INTENT_MACHINE_RESET:
        (void)runtime_client_reset_ex_with_resume(
            client,
            intent->machine_reset_resume_running);
        if (ui != NULL) {
            frontend_clear_disk_activity_leds(ui);
        }
        break;
    case FRONTEND_DEBUGGER_INTENT_INSPECTOR_SET_ENABLED: {
        uint64_t token = runtime_client_alloc_request_token(client);
        (void)runtime_client_inspector_set_enabled(client, intent->enabled, token);
        break;
    }
    case FRONTEND_DEBUGGER_INTENT_INSPECTOR_ENTER: {
        uint64_t token = runtime_client_alloc_request_token(client);
        (void)runtime_client_inspector_enter(client, token);
        (void)runtime_client_request_breakpoints(client);
        break;
    }
    case FRONTEND_DEBUGGER_INTENT_INSPECTOR_LEAVE: {
        uint64_t token = runtime_client_alloc_request_token(client);
        (void)runtime_client_inspector_leave(client, token);
        break;
    }
    case FRONTEND_DEBUGGER_INTENT_INSPECTOR_LAND: {
        uint64_t token = runtime_client_alloc_request_token(client);
        (void)runtime_client_inspector_land(client, intent->inspector_cycle, token);
        break;
    }
    case FRONTEND_DEBUGGER_INTENT_INSPECTOR_LAND_TO_CYCLE: {
        uint64_t token = runtime_client_alloc_request_token(client);
        (void)runtime_client_inspector_land_to_cycle(
            client, intent->inspector_cycle, token);
        break;
    }
    case FRONTEND_DEBUGGER_INTENT_INSPECTOR_PAUSE:
        (void)runtime_client_pause(client);
        break;
    case FRONTEND_DEBUGGER_INTENT_RUN:
        (void)runtime_client_run(client);
        break;
    case FRONTEND_DEBUGGER_INTENT_INSPECTOR_FRAME_STEP: {
        uint64_t token = runtime_client_alloc_request_token(client);
        (void)runtime_client_inspector_frame_step(
            client, intent->enabled ? 1 : -1, token);
        break;
    }
    case FRONTEND_DEBUGGER_INTENT_HISTORY_FIND: {
        uint64_t token = 0u;
        if (!forensics_history_begin_rpc(
                client, ui, FRONTEND_HISTORY_VERB_FIND, intent->history_label,
                &token)) {
            break;
        }
        if (!runtime_client_history_find(
                client,
                0u,
                &intent->history_query,
                intent->history_from_kind,
                intent->history_from_id,
                intent->history_limit != 0u ? intent->history_limit : 64u,
                token)) {
            forensics_history_rpc_clear();
            if (ui != NULL) {
                frontend_forensics_apply_rpc_error(
                    ui, RUNTIME_HISTORY_RPC_BAD_ARGS);
            }
        }
        break;
    }
    case FRONTEND_DEBUGGER_INTENT_HISTORY_NEXT: {
        uint64_t token = 0u;
        uint64_t cursor = frontend_forensics_last_cursor(ui);
        if (cursor == 0u) {
            if (ui != NULL) {
                frontend_forensics_apply_rpc_error(
                    ui, RUNTIME_HISTORY_RPC_BAD_ARGS);
            }
            break;
        }
        if (!forensics_history_begin_rpc(
                client, ui, FRONTEND_HISTORY_VERB_NEXT, intent->history_label,
                &token)) {
            break;
        }
        if (!runtime_client_history_next(
                client,
                0u,
                cursor,
                intent->history_limit != 0u ? intent->history_limit : 64u,
                token)) {
            forensics_history_rpc_clear();
            if (ui != NULL) {
                frontend_forensics_apply_rpc_error(
                    ui, RUNTIME_HISTORY_RPC_BAD_ARGS);
            }
        }
        break;
    }
    case FRONTEND_DEBUGGER_INTENT_HISTORY_READ: {
        uint64_t token = 0u;
        if (!forensics_history_begin_rpc(
                client, ui, FRONTEND_HISTORY_VERB_READ, intent->history_label,
                &token)) {
            break;
        }
        if (!runtime_client_history_read(
                client,
                0u,
                intent->history_read_epoch,
                intent->history_read_id,
                intent->history_before,
                intent->history_after,
                token)) {
            forensics_history_rpc_clear();
            if (ui != NULL) {
                frontend_forensics_apply_rpc_error(
                    ui, RUNTIME_HISTORY_RPC_BAD_ARGS);
            }
        }
        break;
    }
    case FRONTEND_DEBUGGER_INTENT_HISTORY_INFO: {
        uint64_t token = 0u;
        if (!forensics_history_begin_rpc(
                client, ui, FRONTEND_HISTORY_VERB_INFO, intent->history_label,
                &token)) {
            break;
        }
        if (!runtime_client_history_info(client, token)) {
            forensics_history_rpc_clear();
            if (ui != NULL) {
                frontend_forensics_apply_rpc_error(
                    ui, RUNTIME_HISTORY_RPC_BAD_ARGS);
            }
        }
        break;
    }
    case FRONTEND_DEBUGGER_INTENT_HISTORY_CLOSE: {
        uint64_t token = 0u;
        uint64_t cursor = frontend_forensics_last_cursor(ui);
        if (!forensics_history_begin_rpc(
                client, ui, FRONTEND_HISTORY_VERB_CLOSE, intent->history_label,
                &token)) {
            break;
        }
        if (!runtime_client_history_close(client, 0u, cursor, token)) {
            forensics_history_rpc_clear();
            if (ui != NULL) {
                frontend_forensics_apply_rpc_error(
                    ui, RUNTIME_HISTORY_RPC_BAD_ARGS);
            }
        }
        break;
    }
    case FRONTEND_DEBUGGER_INTENT_SET_DISPLAY_OVERRIDE:
        (void)runtime_client_set_display_override(
            client, intent->enabled, intent->display_override_flags);
        break;
    case FRONTEND_DEBUGGER_INTENT_SET_VIDEO_DISPLAY:
        (void)runtime_client_set_video_display(
            client, intent->enabled, (uint8_t)intent->value);
        break;
    case FRONTEND_DEBUGGER_INTENT_ASSEMBLE_RUN:
        if (intent->assemble_rearm_oneshots) {
            (void)runtime_client_rearm_oneshot_breakpoints(client);
        }
        (void)runtime_client_assemble_file_full(
            client,
            intent->assemble_path,
            intent->assemble_address,
            intent->assemble_run_address,
            intent->assemble_auto_run,
            intent->assemble_mli_launch,
            intent->assemble_reset_first,
            false);
        break;
    case FRONTEND_DEBUGGER_INTENT_ASSEMBLE_BROWSE:
        if (ui != NULL) {
            frontend_open_file_browser(
                ui,
                FRONTEND_DEBUGGER_INTENT_ASSEMBLE_BROWSE,
                "Select Assembler Source",
                false,
                NULL,
                NULL,
                0);
        }
        break;
    case FRONTEND_DEBUGGER_INTENT_LOAD_BIN_BROWSE:
        if (ui != NULL) {
            frontend_machine_file_kind kind =
                (frontend_machine_file_kind)intent->load_file_kind;
            frontend_open_file_browser(
                ui,
                FRONTEND_DEBUGGER_INTENT_LOAD_BIN_BROWSE,
                kind == FRONTEND_MACHINE_FILE_SNAPSHOT ?
                    "Load machine snapshot" :
                    kind == FRONTEND_MACHINE_FILE_APPLESOFT_TEXT ?
                        "Load Applesoft listing" : "Load machine file",
                false,
                kind == FRONTEND_MACHINE_FILE_SNAPSHOT ? "a2state" : NULL,
                NULL,
                0);
        }
        break;
    case FRONTEND_DEBUGGER_INTENT_SAVE_BIN_BROWSE:
        if (ui != NULL) {
            frontend_machine_file_kind kind =
                (frontend_machine_file_kind)intent->save_file_kind;
            frontend_open_file_browser(
                ui,
                FRONTEND_DEBUGGER_INTENT_SAVE_BIN_BROWSE,
                kind == FRONTEND_MACHINE_FILE_SNAPSHOT ?
                    "Save machine snapshot" :
                    kind == FRONTEND_MACHINE_FILE_APPLESOFT_TEXT ?
                        "Save Applesoft listing" : "Save machine file",
                true,
                kind == FRONTEND_MACHINE_FILE_SNAPSHOT ? "a2state" : NULL,
                kind == FRONTEND_MACHINE_FILE_SNAPSHOT ? "a2state" :
                    kind == FRONTEND_MACHINE_FILE_APPLESOFT_TEXT ? "bas" :
                    intent->save_bin_format == APPLE2_BINARY_FORMAT_APPLESINGLE ? "as" :
                    intent->save_bin_format == APPLE2_BINARY_FORMAT_RAW ? "bin" : NULL,
                0);
        }
        break;
    case FRONTEND_DEBUGGER_INTENT_LOAD_BIN_EXECUTE: {
        frontend_machine_file_kind kind =
            (frontend_machine_file_kind)intent->load_file_kind;
        if (kind == FRONTEND_MACHINE_FILE_SNAPSHOT ||
            (kind == FRONTEND_MACHINE_FILE_AUTO &&
             path_has_extension(intent->load_bin_path, "a2state"))) {
            (void)runtime_client_load_state(client, intent->load_bin_path);
        } else {
            (void)runtime_client_load_bin(
                client,
                intent->load_bin_path,
                intent->load_bin_address,
                intent->load_bin_format,
                intent->load_bin_reset_first,
                kind == FRONTEND_MACHINE_FILE_APPLESOFT_TEXT,
                intent->load_bin_run_after_load);
        }
        break;
    }
    case FRONTEND_DEBUGGER_INTENT_SAVE_BIN_EXECUTE: {
        frontend_machine_file_kind kind =
            (frontend_machine_file_kind)intent->save_file_kind;
        if (kind == FRONTEND_MACHINE_FILE_SNAPSHOT) {
            (void)runtime_client_save_state(client, intent->save_bin_path);
        } else {
            (void)runtime_client_save_bin(
                client,
                intent->save_bin_path,
                intent->save_bin_start,
                intent->save_bin_end,
                intent->save_bin_format,
                kind == FRONTEND_MACHINE_FILE_APPLESOFT_TEXT);
        }
        break;
    }
    case FRONTEND_DEBUGGER_INTENT_DISK_MOUNT_DIALOG:
        if (ui != NULL) {
            frontend_open_file_browser(
                ui,
                FRONTEND_DEBUGGER_INTENT_DISK_MOUNT_DIALOG,
                "Mount Disk II image",
                false,
                "nib,dsk,po,do,woz",
                NULL,
                intent->disk_device);
        }
        break;
    case FRONTEND_DEBUGGER_INTENT_DISK_ADD_DIALOG:
        if (ui != NULL) {
            frontend_open_file_browser(
                ui,
                FRONTEND_DEBUGGER_INTENT_DISK_ADD_DIALOG,
                "Add Disk II image to queue",
                false,
                "nib,dsk,po,do,woz",
                NULL,
                intent->disk_device);
        }
        break;
    case FRONTEND_DEBUGGER_INTENT_DISK_UNMOUNT:
        (void)runtime_client_media_eject(
            client, 6u, intent->disk_device > 1u ? 0u : intent->disk_device);
        break;
    case FRONTEND_DEBUGGER_INTENT_MEDIA_INSERT_DIALOG:
        if (ui != NULL) {
            frontend_open_media_file_browser(
                ui,
                intent->disk_card_type == RUNTIME_SLOT_CARD_DISKII ?
                    "Insert Disk II image" : "Insert SmartPort media",
                intent->disk_card_type == RUNTIME_SLOT_CARD_DISKII ?
                    "nib,dsk,po,do,woz" : NULL,
                intent->disk_slot,
                intent->disk_device,
                intent->disk_card_type);
        }
        break;
    case FRONTEND_DEBUGGER_INTENT_MEDIA_EJECT:
        (void)runtime_client_media_eject(
            client, intent->disk_slot, intent->disk_device);
        break;
    case FRONTEND_DEBUGGER_INTENT_MEDIA_SWAP:
        (void)runtime_client_media_swap(
            client, intent->disk_slot, intent->disk_device, 1, true);
        break;
    case FRONTEND_DEBUGGER_INTENT_BOOT_SLOT:
        (void)runtime_client_boot_slot(client, intent->disk_slot);
        break;
    case FRONTEND_DEBUGGER_INTENT_STATE_SAVE_AS_DIALOG:
        if (ui != NULL) {
            frontend_open_file_browser(
                ui,
                FRONTEND_DEBUGGER_INTENT_STATE_SAVE_AS_DIALOG,
                "Save machine state",
                true,
                "a2state",
                "a2state",
                0);
        }
        break;
    case FRONTEND_DEBUGGER_INTENT_STATE_LOAD_DIALOG:
        if (ui != NULL) {
            frontend_open_file_browser(
                ui,
                FRONTEND_DEBUGGER_INTENT_STATE_LOAD_DIALOG,
                "Load machine state",
                false,
                "a2state",
                NULL,
                0);
        }
        break;
    case FRONTEND_DEBUGGER_INTENT_CONFIG_PICK_INI_DIALOG:
        if (ui != NULL) {
            frontend_open_file_browser(
                ui,
                FRONTEND_DEBUGGER_INTENT_CONFIG_PICK_INI_DIALOG,
                "Select INI File",
                false,
                "ini",
                NULL,
                0);
        }
        break;
    case FRONTEND_DEBUGGER_INTENT_CONFIG_PICK_PATH_DIALOG:
        if (ui != NULL) {
            frontend_open_file_browser(
                ui,
                FRONTEND_DEBUGGER_INTENT_CONFIG_PICK_PATH_DIALOG,
                "Select Folder",
                false,
                NULL,
                NULL,
                0);
        }
        break;
    case FRONTEND_DEBUGGER_INTENT_CONFIG_PICK_SYMBOL_DIALOG:
        if (ui != NULL) {
            frontend_open_file_browser(
                ui,
                FRONTEND_DEBUGGER_INTENT_CONFIG_PICK_SYMBOL_DIALOG,
                "Select Symbol File",
                false,
                NULL,
                NULL,
                0);
        }
        break;
    case FRONTEND_DEBUGGER_INTENT_CONFIG_APPLY:
    case FRONTEND_DEBUGGER_INTENT_SAVE_INI_NOW:
        if (options != NULL) {
            int slot;
            bool save_now = (intent->type == FRONTEND_DEBUGGER_INTENT_SAVE_INI_NOW);
            /* Configure edits host/machine settings, not live Disk II / SmartPort
               mounts (those live under Misc -> Machine). Keep the live media so
               eject/insert is not undone by Save INI / OK from a stale dialog. */
            app_options media_live;
            app_options_init(&media_live);
            if (!app_options_replace_media_mounts(&media_live, options)) {
                app_options_destroy(&media_live);
                break;
            }
            app_options_destroy(options);
            if (!app_options_copy(options, &intent->config)) {
                app_options_destroy(&media_live);
                break;
            }
            if (!app_options_replace_media_mounts(options, &media_live)) {
                app_options_destroy(&media_live);
                break;
            }
            app_options_destroy(&media_live);
            options->save_ini = options->remember;
            app_options_reconcile_slot_cards(options);
            if (client != NULL) {
                runtime_machine_config machine_config;
                runtime_config turbo_cfg;
                const runtime_config *turbo_arg = NULL;

                machine_config_from_options(options, &machine_config);
                runtime_config_init(&turbo_cfg);
                if (options->turbo_multipliers != NULL &&
                    runtime_config_set_turbo_csv(
                        &turbo_cfg, options->turbo_multipliers)) {
                    turbo_arg = &turbo_cfg;
                } else if (options->turbo_multipliers != NULL) {
                    log_warn(
                        "Configure: invalid turbo ladder, leaving live list unchanged");
                }
                if (!runtime_client_apply_machine_config(
                        client,
                        &machine_config,
                        turbo_arg,
                        NULL,
                        NULL,
                        intent->config_result.machine_changed,
                        false,
                        runtime_running)) {
                    log_warn(
                        intent->config_result.machine_changed ?
                            "Configure: could not queue machine power cycle" :
                            "Configure: could not queue turbo ladder");
                }
            }
            if (ui != NULL) {
                for (slot = 0; slot < FRONTEND_BROWSE_SLOT_COUNT && slot < APP_BROWSE_DIR_COUNT;
                     ++slot) {
                    const char *dir = frontend_get_browse_dir(ui, (frontend_browse_slot)slot);
                    app_options_set_string(&options->browse_dirs[slot], dir[0] ? dir : NULL);
                }
                sync_assembler_options_from_frontend(options, ui);
                frontend_set_config_state(ui, options);
            }
            if (save_now || options->remember) {
                if (options->no_save_ini) {
                    log_info("Save INI: disabled (--nosaveini)");
                } else if (!app_options_save_shutdown(options)) {
                    log_error(
                        "Save INI failed: %s",
                        options->ini_path != NULL ? options->ini_path : "(null)");
                } else if (save_now) {
                    log_info(
                        "Save INI now: wrote %s",
                        options->ini_path != NULL ? options->ini_path : "(null)");
                }
            }
            /* Host-only settings apply without resetting the Apple. */
            apply_keyboard_joystick_options(
                kbd_joystick, controllers, client, options);
            if (client != NULL) {
                (void)runtime_client_set_history_off_on_max(
                    client, options->history_off_on_max);
                (void)runtime_client_set_video_display(
                    client,
                    options->colour_display,
                    (uint8_t)options->mono_mode);
            }
        }
        break;
    case FRONTEND_DEBUGGER_INTENT_SAVE_PATHS_ONLY:
        if (options != NULL && ui != NULL) {
            int slot;
            for (slot = 0; slot < FRONTEND_BROWSE_SLOT_COUNT && slot < APP_BROWSE_DIR_COUNT;
                 ++slot) {
                const char *dir = frontend_get_browse_dir(ui, (frontend_browse_slot)slot);
                app_options_set_string(&options->browse_dirs[slot], dir[0] ? dir : NULL);
            }
            if (!app_options_save_paths_only(options)) {
                log_error("Save Paths Only failed");
            } else {
                log_info("Save Paths Only: wrote %s",
                        options->ini_path != NULL ? options->ini_path : "(null)");
            }
        }
        break;
    case FRONTEND_DEBUGGER_INTENT_FILE_BROWSER_RESULT:
        if (intent->file_browser_path[0] == '\0') {
            break;
        }
        switch (intent->file_browser_purpose) {
        case FRONTEND_DEBUGGER_INTENT_DISK_MOUNT_DIALOG:
        case FRONTEND_DEBUGGER_INTENT_DISK_ADD_DIALOG: {
            /* drive: 0/1. Mount and ADD both append to the machine queue;
               host queues track the multi-image list. */
            uint8_t drive = intent->disk_device > 1u ? 0u : intent->disk_device;
            if (intent->file_browser_purpose == FRONTEND_DEBUGGER_INTENT_DISK_MOUNT_DIALOG &&
                options != NULL) {
                app_disk_slot *queue = app_options_diskii_queue(options, 6, drive);
                int eject_count = queue != NULL ? queue->count : 0;
                int eject_index;
                for (eject_index = 0; eject_index < eject_count; ++eject_index) {
                    (void)runtime_client_media_eject(client, 6u, drive);
                }
            }
            host_disk_queue_add(
                client, ui, options, 6u, drive, intent->file_browser_path);
            break;
        }
        case FRONTEND_DEBUGGER_INTENT_MEDIA_INSERT_DIALOG:
            if (intent->disk_card_type == RUNTIME_SLOT_CARD_DISKII) {
                host_disk_queue_add(
                    client,
                    ui,
                    options,
                    intent->disk_slot,
                    intent->disk_device,
                    intent->file_browser_path);
            } else if (intent->disk_card_type == RUNTIME_SLOT_CARD_SMARTPORT) {
                (void)runtime_client_media_insert(
                    client,
                    intent->disk_slot,
                    intent->disk_device,
                    RUNTIME_SLOT_CARD_SMARTPORT,
                    intent->file_browser_path);
            }
            break;
        case FRONTEND_DEBUGGER_INTENT_CONFIG_PICK_PATH_DIALOG:
            if (ui != NULL) {
                frontend_set_picked_browse_dir(ui, intent->file_browser_path);
            }
            break;
        case FRONTEND_DEBUGGER_INTENT_CONFIG_PICK_INI_DIALOG:
            if (options != NULL) {
                (void)app_options_set_string(&options->ini_path, intent->file_browser_path);
                if (ui != NULL) {
                    frontend_set_config_state(ui, options);
                }
            }
            break;
        case FRONTEND_DEBUGGER_INTENT_CONFIG_PICK_SYMBOL_DIALOG:
            if (options != NULL) {
                (void)app_options_set_string(&options->symbol_files, intent->file_browser_path);
                if (ui != NULL) {
                    frontend_set_config_state(ui, options);
                }
            }
            break;
        case FRONTEND_DEBUGGER_INTENT_STATE_SAVE_AS_DIALOG:
            (void)runtime_client_save_state(client, intent->file_browser_path);
            break;
        case FRONTEND_DEBUGGER_INTENT_STATE_LOAD_DIALOG:
            (void)runtime_client_load_state(client, intent->file_browser_path);
            break;
        case FRONTEND_DEBUGGER_INTENT_LOAD_BIN_BROWSE:
            if (ui != NULL) frontend_set_load_bin_path(ui, intent->file_browser_path);
            break;
        case FRONTEND_DEBUGGER_INTENT_SAVE_BIN_BROWSE:
            if (ui != NULL) frontend_set_save_bin_path(ui, intent->file_browser_path);
            break;
        case FRONTEND_DEBUGGER_INTENT_ASSEMBLE_BROWSE:
            if (ui != NULL) frontend_set_assembler_path(ui, intent->file_browser_path);
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }
}

static void apply_event_to_debug(
    frontend_debug_state *debug,
    const runtime_event *event)
{
    if (debug == NULL || event == NULL) {
        return;
    }
    switch (event->type) {
    case RUNTIME_EVENT_CPU_STATE_RESPONSE:
        debug->cpu = event->data.cpu_state;
        debug->has_cpu = true;
        break;
    case RUNTIME_EVENT_FRAME_READY:
        /* Per-frame motor mask keeps disk LEDs current while free-running
           (full machine snapshots are only polled periodically). */
        debug->disk_motor_mask = event->data.frame_ready.disk_motor_mask;
        break;
    case RUNTIME_EVENT_MACHINE_STATE_RESPONSE:
        debug->machine_cycle = event->data.machine_state.cycle;
        debug->frame_number = event->data.machine_state.frame_number;
        debug->dropped_frames = event->data.machine_state.dropped_frames;
        debug->runtime_seq = event->data.machine_state.runtime_seq;
        debug->stop_reason = event->data.machine_state.stop_reason;
        debug->active_turbo_multiplier = event->data.machine_state.active_turbo_multiplier;
        debug->apple_state_flags = event->data.machine_state.apple_state_flags;
        debug->apple_model = event->data.machine_state.apple_model;
        debug->disk_motor_mask = event->data.machine_state.disk_motor_mask;
        memcpy(debug->slots, event->data.machine_state.slots, sizeof(debug->slots));
        debug->has_apple_flags = true;
        debug->inspecting = event->data.machine_state.inspector_mode != 0u;
        debug->inspector_enabled = event->data.machine_state.inspector_enabled != 0u;
        debug->inspector_window_valid = event->data.machine_state.inspector_window_valid != 0u;
        debug->inspector_history_recording =
            event->data.machine_state.inspector_history_recording != 0u;
        debug->inspector_frame_recording =
            event->data.machine_state.inspector_frame_recording != 0u;
        debug->inspector_recorder_recording =
            event->data.machine_state.inspector_recorder_recording != 0u;
        debug->inspector_stopped_for_max =
            event->data.machine_state.inspector_stopped_for_max != 0u;
        debug->inspector_window_start_kind = event->data.machine_state.inspector_window_start_kind;
        debug->inspector_window_start_arg1 = event->data.machine_state.inspector_window_start_arg1;
        debug->inspector_focus_cycle = event->data.machine_state.inspector_focus_cycle;
        debug->inspector_focus_id = event->data.machine_state.inspector_focus_id;
        debug->inspector_oldest_cycle = event->data.machine_state.inspector_oldest_cycle;
        debug->inspector_newest_cycle = event->data.machine_state.inspector_newest_cycle;
        /* Always refresh the CPU snapshot from machine state (c64m). */
        debug->cpu.pc = event->data.machine_state.pc;
        debug->cpu.a = event->data.machine_state.a;
        debug->cpu.x = event->data.machine_state.x;
        debug->cpu.y = event->data.machine_state.y;
        debug->cpu.sp = event->data.machine_state.sp;
        debug->cpu.p = event->data.machine_state.p;
        debug->cpu.cycles = event->data.machine_state.cpu_cycles;
        debug->has_cpu = true;
        if (debug->runtime_state != FRONTEND_RUNTIME_STATE_ERROR) {
            debug->runtime_state = event->data.machine_state.running ?
                FRONTEND_RUNTIME_STATE_RUNNING :
                FRONTEND_RUNTIME_STATE_PAUSED;
        }
        break;
    case RUNTIME_EVENT_MEMORY_RESPONSE:
        debug->memory = event->data.memory;
        debug->has_memory = true;
        break;
    case RUNTIME_EVENT_RUNNING:
        debug->runtime_state = FRONTEND_RUNTIME_STATE_RUNNING;
        break;
    case RUNTIME_EVENT_PAUSED:
    case RUNTIME_EVENT_RESET_COMPLETE:
    case RUNTIME_EVENT_STEP_COMPLETE:
    case RUNTIME_EVENT_RUN_COMPLETE:
        debug->runtime_state = FRONTEND_RUNTIME_STATE_PAUSED;
        break;
    case RUNTIME_EVENT_ERROR:
        debug->runtime_state = FRONTEND_RUNTIME_STATE_ERROR;
        break;
    case RUNTIME_EVENT_DEBUG_MEMORY_READY:
        debug->has_debug_memory = true;
        break;
    case RUNTIME_EVENT_BREAKPOINTS_RESPONSE:
        debug->has_breakpoints = true;
        break;
    case RUNTIME_EVENT_CALL_STACK_RESPONSE:
        debug->call_stack = event->data.call_stack;
        debug->has_call_stack = true;
        break;
    default:
        break;
    }
}

static bool apply_options_to_runtime_config(const app_options *options, runtime_config *rt_config)
{
    int i;
    int n;

    runtime_config_init(rt_config);
    runtime_config_set_turbo_defaults(rt_config);
    if (options->turbo_multipliers != NULL) {
        (void)runtime_config_set_turbo_csv(rt_config, options->turbo_multipliers);
    }
    rt_config->start_running = !options->headless;
    if (!options->headless) {
        rt_config->start_running = true;
    }
    rt_config->apple_model = options->apple_model;
    rt_config->mb_slot = 0;
    for (i = 1; i <= 7; ++i) {
        switch (options->slot_cards[i]) {
        case APP_SLOT_CARD_DISKII:
            rt_config->slot_cards[i] = RUNTIME_SLOT_CARD_DISKII;
            break;
        case APP_SLOT_CARD_SMARTPORT:
            rt_config->slot_cards[i] = RUNTIME_SLOT_CARD_SMARTPORT;
            break;
        case APP_SLOT_CARD_MOCKINGBOARD:
            rt_config->slot_cards[i] = RUNTIME_SLOT_CARD_MOCKINGBOARD;
            rt_config->mb_slot = i;
            break;
        case APP_SLOT_CARD_EMPTY:
        default:
            rt_config->slot_cards[i] = RUNTIME_SLOT_CARD_EMPTY;
            break;
        }
    }

    /* Breakpoint INI [DEBUG] break.* (P4e): load when use_ini; save with remember/saveini. */
    rt_config->ini_path = options->ini_path;
    rt_config->use_ini = options->use_ini;
    rt_config->save_ini =
        (options->save_ini || options->remember) && !options->no_save_ini;

    /* Frame ring budget (0 = off). Default from app_options is 128 MiB. */
    if (options->frame_ring_memory_mb > 0) {
        rt_config->frame_ring_memory_mb = (uint32_t)options->frame_ring_memory_mb;
        rt_config->frame_ring_memory_mb_configured = true;
    } else {
        rt_config->frame_ring_memory_mb = 0u;
        rt_config->frame_ring_memory_mb_configured = true;
    }

    /* CPU history budget (0 = off). Default from app_options is 256 MiB. */
    if (options->history_memory_mb > 0) {
        rt_config->history_memory_mb = (uint32_t)options->history_memory_mb;
        rt_config->history_memory_mb_configured = true;
    } else {
        rt_config->history_memory_mb = 0u;
        rt_config->history_memory_mb_configured = true;
    }
    rt_config->history_off_on_max = options->history_off_on_max;
    rt_config->inspector = options->inspector;
    if (options->inspector_memory_mb > 0) {
        rt_config->inspector_memory_mb = (uint32_t)options->inspector_memory_mb;
        rt_config->inspector_memory_mb_configured = true;
    } else {
        rt_config->inspector_memory_mb = 0u;
        rt_config->inspector_memory_mb_configured = true;
    }

    n = options->diskii_count;
    if (n > 16) {
        n = 16;
    }
    for (i = 0; i < n; i++) {
        rt_config->diskii_mounts[i].slot = options->diskii[i].slot;
        rt_config->diskii_mounts[i].drive = options->diskii[i].drive;
        rt_config->diskii_mounts[i].path = options->diskii[i].path;
    }
    rt_config->diskii_mount_count = n;

    n = options->smartport_count;
    if (n > 16) {
        n = 16;
    }
    for (i = 0; i < n; i++) {
        rt_config->smartport_mounts[i].slot = options->smartport[i].slot;
        rt_config->smartport_mounts[i].unit = options->smartport[i].unit;
        rt_config->smartport_mounts[i].path = options->smartport[i].path;
    }
    rt_config->smartport_mount_count = n;
    rt_config->smartport_boot_slot = options->smartport_boot_slot;
    rt_config->video_colour = options->colour_display;
    rt_config->video_phosphor = (uint8_t)options->mono_mode;
    return true;
}

int main(int argc, char **argv)
{
    app_options options;
    runtime_config rt_config;
    runtime *rt = NULL;
    runtime_client *client = NULL;
    frontend *ui = NULL;
    platform_window *window = NULL;
    platform_window_config window_config;
    frontend_debug_state debug;
    frontend_input_mapper input_mapper;
    frontend_joystick_input kbd_joystick;
    sdl_apple_controller_state controllers;
    audio_buffer *audio_buf = NULL;
    platform_audio *host_audio = NULL;
    control_server_t *control = NULL;
    control_dispatch_t control_disp;
    bool control_active = false;
    int exit_code = EXIT_FAILURE;
    bool running = true;
    /* Match c64m: start display-only; F9 toggles debugger chrome. */
    bool ui_visible = false;
    /* Title bar: update only when run-state / turbo / model change (c64m). */
    bool title_set = false;
    frontend_runtime_state last_title_state = FRONTEND_RUNTIME_STATE_UNKNOWN;
    runtime_stop_reason last_title_stop_reason = RUNTIME_STOP_REASON_NONE;
    uint32_t last_title_turbo = 0u;
    int last_title_model = -1;
    bool last_title_inspecting = false;
    uint64_t last_title_focus = 0u;
    /* Keep SDL text input off unless a UI field is focused (c64m / macOS). */
    bool text_input_active = false;
    /* After Opt+letter host chords, swallow TEXTINPUT until Option is released
       so e.g. Opt+R opening Forensics does not type 'r' into the query field. */
    bool suppress_text_after_option_chord = false;
    uint32_t pixels[APPLE2_VIDEO_WIDTH * APPLE2_VIDEO_HEIGHT];

    memset(&options, 0, sizeof(options));
    memset(&debug, 0, sizeof(debug));
    memset(&kbd_joystick, 0, sizeof(kbd_joystick));
    memset(&controllers, 0, sizeof(controllers));
    memset(&control_disp, 0, sizeof(control_disp));
    debug.runtime_state = FRONTEND_RUNTIME_STATE_RUNNING;
    debug.active_turbo_multiplier = RUNTIME_TURBO_MHZ_1;
    frontend_input_mapper_reset(&input_mapper);
    controllers.single_controller_port = 1u;
    controllers.kbd_joystick = &kbd_joystick;
    controllers.last_axis[0] = FRONTEND_JOYSTICK_APPLE_AXIS_MID;
    controllers.last_axis[1] = FRONTEND_JOYSTICK_APPLE_AXIS_MID;
    controllers.last_axis[2] = FRONTEND_JOYSTICK_APPLE_AXIS_MID;
    controllers.last_axis[3] = FRONTEND_JOYSTICK_APPLE_AXIS_MID;

    a2m_log_init();

    if (!app_options_load_startup(&options, argc, argv)) {
        return EXIT_FAILURE;
    }
    /* INI/CLI may override the WARN default; apply before further host work. */
    a2m_log_apply(options.log_level);
    frontend_input_mapper_set_original_del(&input_mapper, options.original_del);
    if (options.show_version) {
        printf("%s %s\n", A2M_NAME, A2M_VERSION);
        app_options_destroy(&options);
        return EXIT_SUCCESS;
    }

    if (options.headless) {
        if (!platform_init_headless()) {
            /* Fall back to full platform when headless helpers are thin. */
            if (!platform_init()) {
                fprintf(stderr, "a2m: platform_init failed\n");
                app_options_destroy(&options);
                return EXIT_FAILURE;
            }
        }
    } else if (!platform_init()) {
        fprintf(stderr, "a2m: platform_init failed\n");
        app_options_destroy(&options);
        return EXIT_FAILURE;
    }

    apply_sdl_log_policy(options.log_level);

    (void)app_options_apply_convenience_paths(&options);
    if (!apply_options_to_runtime_config(&options, &rt_config)) {
        goto done;
    }

    /* Host audio before runtime_create so the actual sample rate is known
       (c64m / pre-wholesale product path). Windowed only. */
    if (!options.headless) {
        /* Interleaved stereo L,R floats: ~8192 host frames of headroom. */
        audio_buf = audio_buffer_create(8192u * 2u);
        if (audio_buf != NULL) {
            platform_audio_desc audio_desc;

            memset(&audio_desc, 0, sizeof(audio_desc));
            audio_desc.requested_rate = 48000;
            audio_desc.requested_channels = 2;
            audio_desc.requested_callback_samples = 512;
            audio_desc.buffer = audio_buf;
            host_audio = platform_audio_create(&audio_desc);
            if (host_audio == NULL) {
                log_warn("audio: failed to open device, running without audio");
                audio_buffer_destroy(audio_buf);
                audio_buf = NULL;
            } else {
                rt_config.audio_out = audio_buf;
                rt_config.audio_sample_rate = platform_audio_actual_rate(host_audio);
                if (rt_config.audio_sample_rate <= 0) {
                    rt_config.audio_sample_rate = 48000;
                }
            }
        } else {
            log_warn("audio: failed to allocate buffer, running without audio");
        }
    }

    rt = runtime_create(&rt_config);
    if (rt == NULL || !runtime_start(rt)) {
        fprintf(stderr, "a2m: runtime start failed\n");
        goto done;
    }
    client = runtime_get_client(rt);

    /* Optional control port (A2M/2): windowed or headless coop/automation. */
    if (options.control_port > 0) {
        control = control_server_create((uint16_t)options.control_port);
        if (control == NULL || !control_server_start(control)) {
            fprintf(
                stderr,
                "a2m: control server failed on port %d\n",
                options.control_port);
            if (control != NULL) {
                control_server_destroy(control);
                control = NULL;
            }
        } else {
            control_dispatch_init(&control_disp, control, client);
            control_active = true;
            /* Seed slot map for mount-disk / select-disk resolve defaults. */
            (void)runtime_client_request_machine_state(client);
            log_info(
                "control port %d (protocol %s)",
                options.control_port,
                CONTROL_PROTOCOL_VERSION);
        }
    }

    frontend_joystick_set_layout(
        &kbd_joystick,
        frontend_joystick_layout_from_string(options.keyboard_joystick_layout));
    frontend_joystick_set_port(
        &kbd_joystick,
        (unsigned)(options.keyboard_joystick_port > 0 ?
                       options.keyboard_joystick_port :
                       0));
    frontend_joystick_set_swap_buttons(
        &kbd_joystick, options.keyboard_joystick_swap_buttons);
    if (!options.headless) {
        sdl_apple_controllers_open_existing(&controllers, client);
        sdl_apple_gameport_publish(&controllers, client);
    }

    /* Unpause SDL audio only after the runtime thread is producing samples. */
    if (host_audio != NULL) {
        platform_audio_start(host_audio);
    }

    if (options.breakpoint != NULL && options.breakpoint[0] != '\0') {
        unsigned long addr = strtoul(options.breakpoint, NULL, 0);
        (void)runtime_client_set_execute_breakpoint(client, (uint16_t)addr);
    }

    /* After worker start + initial Disk II / SmartPort mounts from config. */
    if (options.sna_path != NULL && options.sna_path[0] != '\0') {
        if (!runtime_client_load_state(client, options.sna_path)) {
            log_warn("snapshot: failed to queue load for --sna %s", options.sna_path);
        } else {
            log_info("snapshot: loading --sna %s", options.sna_path);
        }
    }

    if (options.headless) {
        if (!control_active) {
            /* Short smoke: drain a few events then exit. */
            Uint32 start = SDL_GetTicks();
            runtime_event revent;
            printf(
                "a2m headless  model=%s  mb_slot=%d  diskii=%d  smartport=%d\n",
                app_model_label(options.apple_model),
                options.mb_slot,
                options.diskii_count,
                options.smartport_count);
            while (SDL_GetTicks() - start < 80u) {
                while (runtime_client_poll_event(client, &revent)) {
                    if (revent.type == RUNTIME_EVENT_ERROR) {
                        fprintf(stderr, "a2m: runtime: %s\n", revent.data.error.message);
                    }
                }
                SDL_Delay(1);
            }
            exit_code = EXIT_SUCCESS;
            goto done;
        }

        /* Headless + control-port: long-lived loop (no window). */
        printf(
            "a2m headless control  port=%d  model=%s\n",
            options.control_port,
            app_model_label(options.apple_model));
        (void)runtime_client_request_frame(client);
        while (running) {
            runtime_event revent;
            while (runtime_client_poll_event(client, &revent)) {
                control_dispatch_on_runtime_event(&control_disp, &revent);
                if (revent.type == RUNTIME_EVENT_SAVE_STATE_COMPLETE) {
                    if (!append_host_state_chunk(
                            revent.data.state_file.path, &options, &kbd_joystick)) {
                        log_warn(
                            "save state host settings append failed: %s",
                            revent.data.state_file.path);
                    }
                    log_info("save state complete: %s", revent.data.state_file.path);
                } else if (revent.type == RUNTIME_EVENT_LOAD_STATE_COMPLETE) {
                    apply_loaded_host_state(
                        revent.data.state_file.path,
                        &options,
                        NULL,
                        client,
                        &controllers,
                        &kbd_joystick);
                    log_info("load state complete: %s", revent.data.state_file.path);
                } else if (revent.type == RUNTIME_EVENT_ERROR) {
                    fprintf(stderr, "a2m: runtime: %s\n", revent.data.error.message);
                } else if (revent.type == RUNTIME_EVENT_STOPPED) {
                    running = false;
                }
            }
            control_dispatch_poll(&control_disp);
            control_dispatch_check_session(&control_disp);
            if (control_deferred_active(&control_disp.deferred) == NULL) {
                control_dispatch_poll(&control_disp);
            }
            SDL_Delay(control_deferred_active(&control_disp.deferred) != NULL ? 0 : 1);
        }
        exit_code = EXIT_SUCCESS;
        goto done;
    }

    window_config.window_width = options.window_width > 0 ? options.window_width : 1280;
    window_config.window_height = options.window_height > 0 ? options.window_height : 800;
    window = platform_window_create(&window_config);
    if (window == NULL) {
        fprintf(stderr, "a2m: window create failed\n");
        goto done;
    }

    ui = frontend_create(window);
    if (ui == NULL) {
        fprintf(stderr, "a2m: frontend_create failed\n");
        goto done;
    }
    frontend_set_config_state(ui, &options);
    /* Browse folders are live frontend state rather than fields in the copied
       Configure options. Seed them explicitly from the INI at startup. */
    {
        int slot;
        for (slot = 0; slot < FRONTEND_BROWSE_SLOT_COUNT && slot < APP_BROWSE_DIR_COUNT;
             ++slot) {
            frontend_set_browse_dir(
                ui,
                (frontend_browse_slot)slot,
                options.browse_dirs[slot] != NULL ? options.browse_dirs[slot] : "");
        }
    }
    {
        frontend_assembler_options assembler;
        memset(&assembler, 0, sizeof(assembler));
        if (options.assembler_file != NULL) {
            snprintf(
                assembler.file, sizeof(assembler.file), "%s", options.assembler_file);
        }
        if (options.assembler_address != NULL) {
            snprintf(
                assembler.address, sizeof(assembler.address), "%s", options.assembler_address);
        }
        if (options.assembler_run_address != NULL) {
            snprintf(
                assembler.run_address,
                sizeof(assembler.run_address),
                "%s",
                options.assembler_run_address);
        }
        assembler.use_address = options.assembler_use_address;
        assembler.auto_run = options.assembler_auto_run;
        assembler.mli_launch = options.assembler_mli_launch;
        assembler.reset_first = options.assembler_reset_first;
        assembler.rearm_oneshots = options.assembler_rearm_oneshots;
        frontend_set_assembler_options(ui, &assembler);
    }
    app_options_rebuild_diskii_queues(&options);
    frontend_set_disk_queue(ui, 0u, app_options_diskii_queue(&options, 6, 0));
    frontend_set_disk_queue(ui, 1u, app_options_diskii_queue(&options, 6, 1));
    {
        frontend_layout_state layout;
        layout.split_display_right = options.layout_split_display_right;
        layout.split_top_bottom = options.layout_split_top_bottom;
        layout.split_memory_misc = options.layout_split_memory_misc;
        frontend_set_layout_state(ui, &layout);
    }

    debug.apple_model = options.apple_model;
    platform_window_set_minimum_size(window, 0, 0);
    text_input_active = (SDL_IsTextInputActive() == SDL_TRUE);

    /* Seed telemetry before the first frame (c64m request_debug_state). */
    request_debug_state(client);
    (void)runtime_client_request_frame(client);

    while (running) {
        SDL_Event event;
        runtime_event revent;
        frontend_debugger_intent intent;
        uint32_t w = 0;
        uint32_t h = 0;
        uint64_t fn = 0;

        frontend_begin_input(ui);
        while (SDL_PollEvent(&event)) {
            bool send_event_to_frontend =
                ui_visible || frontend_help_is_open(ui) ||
                frontend_forensics_is_open(ui);

            if (event.type == SDL_TEXTINPUT && suppress_text_after_option_chord) {
                send_event_to_frontend = false;
            } else if (event.type == SDL_KEYUP &&
                       (SDL_GetModState() & KMOD_ALT) == 0) {
                suppress_text_after_option_chord = false;
            }

            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_CONTROLLERDEVICEADDED ||
                       event.type == SDL_CONTROLLERDEVICEREMOVED ||
                       event.type == SDL_CONTROLLERAXISMOTION ||
                       event.type == SDL_CONTROLLERBUTTONDOWN ||
                       event.type == SDL_CONTROLLERBUTTONUP) {
                sdl_apple_controller_handle_event(&controllers, client, &event);
                send_event_to_frontend = false;
            } else if (event.type == SDL_KEYDOWN && event.key.repeat != 0 &&
                       frontend_handle_help_key(
                           ui, &event.key, options.scroll_wheel_lines)) {
                send_event_to_frontend = false;
            } else if (event.type == SDL_KEYDOWN && event.key.repeat != 0 &&
                       !frontend_help_is_open(ui) &&
                       handle_step_key_event(client, &debug, &event.key, false)) {
                /* Hold-to-step (F10/F11) without re-pausing while running. */
                send_event_to_frontend = false;
            } else if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
                SDL_Keycode sym = event.key.keysym.sym;

                if (frontend_input_is_host_quit_shortcut(&event.key)) {
                    running = false;
                } else if (sym == SDLK_m &&
                           frontend_input_has_option_modifier(&event.key) &&
                           frontend_input_has_shift_modifier(&event.key)) {
                    /* Opt+Shift+M: toggle keyboard-stick layout numpad ↔ WASD.
                       (Bare Opt+M is memory-area cycle, handled by frontend.) */
                    frontend_joystick_layout next_layout =
                        kbd_joystick.layout == FRONTEND_JOYSTICK_LAYOUT_NUMPAD ?
                            FRONTEND_JOYSTICK_LAYOUT_WASD :
                            FRONTEND_JOYSTICK_LAYOUT_NUMPAD;
                    frontend_joystick_set_layout(&kbd_joystick, next_layout);
                    (void)app_options_set_string(
                        &options.keyboard_joystick_layout,
                        frontend_joystick_layout_to_string(next_layout));
                    /* Chord holds Option; don't leave Open-Apple latched. */
                    release_solid_apple_keys(client);
                    controllers.published = false;
                    sdl_apple_gameport_publish(&controllers, client);
                    if (!frontend_config_dialog_is_open(ui)) {
                        frontend_set_config_state(ui, &options);
                    }
                    log_info(
                        "keyboard joystick layout: %s",
                        frontend_joystick_layout_to_string(next_layout));
                    send_event_to_frontend = false;
                } else if ((sym == SDLK_0 || sym == SDLK_1 || sym == SDLK_2) &&
                           frontend_input_has_option_modifier(&event.key) &&
                           frontend_input_has_shift_modifier(&event.key)) {
                    /* Opt+Shift+0/1/2: assign keyboard stick (0=off); re-press
                       the active stick toggles it off. */
                    unsigned requested = sym == SDLK_1 ? 1u :
                                        sym == SDLK_2 ? 2u : 0u;
                    unsigned next =
                        kbd_joystick.port == requested ? 0u : requested;
                    frontend_joystick_set_port(&kbd_joystick, next);
                    options.keyboard_joystick_port = (int)next;
                    /* LALT was pressed while stick was off → OA set; stick now
                       owns Alt so key-up would never clear it. Release here. */
                    release_solid_apple_keys(client);
                    controllers.published = false;
                    sdl_apple_gameport_publish(&controllers, client);
                    if (!frontend_config_dialog_is_open(ui)) {
                        frontend_set_config_state(ui, &options);
                    }
                    if (next == 0u) {
                        log_info("keyboard joystick disabled");
                    } else {
                        log_info(
                            "keyboard joystick assigned to stick %u (%s)",
                            next,
                            frontend_joystick_layout_to_string(kbd_joystick.layout));
                    }
                    send_event_to_frontend = false;
                } else if ((sym == SDLK_1 || sym == SDLK_2) &&
                           frontend_input_has_option_modifier(&event.key) &&
                           !frontend_input_has_shift_modifier(&event.key)) {
                    /* Opt+1 / Opt+2: map single pad to stick, or swap two pads. */
                    sdl_apple_controller_switch_mapping(
                        &controllers,
                        client,
                        sym == SDLK_1 ? 1u : 2u);
                    send_event_to_frontend = false;
                } else if (sym == SDLK_INSERT &&
                           frontend_input_has_option_modifier(&event.key)) {
                    /* Opt+Insert (and Opt+Shift+Insert): simple OS clipboard
                       paste into Apple $C000 via KBDSTRB feed (a2m-style). */
                    if (!debug.inspecting) {
                        char *text = SDL_GetClipboardText();
                        if (text != NULL && text[0] != '\0') {
                            (void)runtime_client_paste_text(
                                client, text, strlen(text));
                        }
                        if (text != NULL) {
                            SDL_free(text);
                        }
                    }
                    send_event_to_frontend = false;
                } else if (!frontend_help_is_open(ui) &&
                           key_is_quick_assemble_shortcut(&event.key)) {
                    if (!debug.inspecting) {
                        if (!frontend_trigger_assembler(ui)) {
                            log_warn("quick assemble: not queued (configure an assembler source)");
                        }
                    }
                    send_event_to_frontend = false;
                } else if (!debug.inspecting &&
                           frontend_joystick_consumes(&kbd_joystick, sym) &&
                           (!ui_visible || frontend_routes_keyboard_to_machine(ui)) &&
                           !frontend_help_is_open(ui)) {
                    joystick_handle_key_and_solid_apple(
                        &kbd_joystick, &controllers, client, &event.key);
                    send_event_to_frontend = false;
                } else if (sym == SDLK_F9) {
                    if (frontend_forensics_is_open(ui)) {
                        /* F9 from Forensics → debugger, always paused. */
                        leave_forensics_mode(
                            window, client, ui, &ui_visible, true);
                    } else {
                        ui_visible = !ui_visible;
                        {
                            int min_w = 0;
                            int min_h = 0;
                            if (ui_visible) {
                                frontend_debug_min_window_size(
                                    ui, &min_w, &min_h);
                                request_debug_state(client);
                            }
                            platform_window_set_minimum_size(
                                window, min_w, min_h);
                        }
                    }
                    send_event_to_frontend = false;
                } else if (sym == SDLK_F8) {
                    /* F8 stands in for CTRL+RESET (macOS eats Control+F*).
                       Option+F8 = CTRL+Open-Apple+RESET (cold). Closed-Apple TBD. */
                    if (!debug.inspecting) {
                        if (frontend_input_has_option_modifier(&event.key)) {
                            (void)runtime_client_cold_reset(client);
                        } else {
                            (void)runtime_client_reset(client);
                        }
                        frontend_clear_disk_activity_leds(ui);
                        debug.disk_motor_mask = 0;
                        request_debug_telemetry(client);
                    }
                    send_event_to_frontend = false;
                } else if (sym == SDLK_h &&
                           frontend_input_has_option_modifier(&event.key)) {
                    if (frontend_help_is_open(ui)) {
                        bool paused_by_help = frontend_close_help(ui);
                        if (paused_by_help) {
                            (void)runtime_client_run(client);
                        }
                    } else {
                        /* frontend_open_help closes Forensics and transfers latch. */
                        bool was_running =
                            debug.runtime_state == FRONTEND_RUNTIME_STATE_RUNNING;
                        if (was_running) {
                            (void)runtime_client_pause(client);
                        }
                        frontend_open_help(ui, was_running);
                    }
                    suppress_text_after_option_chord = true;
                    send_event_to_frontend = false;
                } else if (
                    sym == SDLK_r &&
                    frontend_input_has_option_modifier(&event.key) &&
                    !frontend_input_has_shift_modifier(&event.key)) {
                    /* Opt+R: toggle Forensics; leave returns to entry surface. */
                    if (frontend_forensics_is_open(ui)) {
                        leave_forensics_mode(
                            window, client, ui, &ui_visible, false);
                    } else {
                        bool from_debugger = ui_visible;
                        bool was_running =
                            debug.runtime_state == FRONTEND_RUNTIME_STATE_RUNNING;
                        frontend_open_forensics(
                            ui, from_debugger, was_running);
                    }
                    /* KEYDOWN is consumed, but SDL may still emit TEXTINPUT "r"
                       later in this pump; block it until Option is released. */
                    suppress_text_after_option_chord = true;
                    send_event_to_frontend = false;
                } else if (sym == SDLK_ESCAPE && frontend_help_is_open(ui)) {
                    bool paused_by_help = frontend_close_help(ui);
                    if (paused_by_help) {
                        (void)runtime_client_run(client);
                    }
                    send_event_to_frontend = false;
                } else if (frontend_handle_forensics_key(ui, &event.key)) {
                    send_event_to_frontend = false;
                } else if (frontend_handle_help_key(
                               ui, &event.key, options.scroll_wheel_lines)) {
                    send_event_to_frontend = false;
                } else if (handle_step_key_event(client, &debug, &event.key, true)) {
                    /* F10/F11 work while Forensics is open (before catch-all). */
                    send_event_to_frontend = false;
                } else if (sym == SDLK_F10 && frontend_input_has_shift_modifier(&event.key)) {
                    (void)runtime_client_step_out(client);
                    send_event_to_frontend = false;
                } else if (sym == SDLK_F12 && !frontend_input_has_shift_modifier(&event.key)) {
                    (void)runtime_client_run(client);
                    send_event_to_frontend = false;
                } else if (sym == SDLK_F12 && frontend_input_has_shift_modifier(&event.key)) {
                    uint16_t addr = 0;
                    if (frontend_get_disassembly_cursor(ui, &addr)) {
                        (void)runtime_client_run_to_cursor(client, addr);
                    }
                    send_event_to_frontend = false;
                } else if (
                    frontend_help_is_open(ui) || frontend_forensics_is_open(ui)) {
                    send_event_to_frontend = true;
                } else if (key_is_quicksave_shortcut(&event.key)) {
                    if (!debug.inspecting) {
                        (void)send_quicksave(client, &options, ui);
                    }
                    send_event_to_frontend = false;
                } else if (key_is_quickload_shortcut(&event.key)) {
                    if (!debug.inspecting) {
                        (void)send_quickload(client, &options, ui);
                    }
                    send_event_to_frontend = false;
                } else if (key_is_video_display_shortcut(&event.key)) {
                    release_solid_apple_keys(client);
                    if (ui == NULL ||
                        !frontend_config_toggle_colour_preview(ui)) {
                        options.colour_display = !options.colour_display;
                        if (client != NULL) {
                            (void)runtime_client_set_video_display(
                                client,
                                options.colour_display,
                                (uint8_t)options.mono_mode);
                        }
                        if (ui != NULL) {
                            frontend_set_config_state(ui, &options);
                        }
                    }
                    send_event_to_frontend = false;
                } else if (sym == SDLK_t &&
                           frontend_input_has_option_modifier(&event.key)) {
                    (void)runtime_client_cycle_turbo_speed(client);
                    (void)runtime_client_request_machine_state(client);
                    send_event_to_frontend = false;
                } else if (ui_visible && frontend_handle_view_cycle_key(ui, &event.key)) {
                    send_event_to_frontend = false;
                } else if (!ui_visible || frontend_routes_keyboard_to_machine(ui)) {
                    if (!debug.inspecting) {
                        handle_keyboard_input(&input_mapper, client, &event.key);
                    }
                    send_event_to_frontend = false;
                }
            } else if (event.type == SDL_KEYUP &&
                       !frontend_help_is_open(ui) &&
                       (!ui_visible || frontend_routes_keyboard_to_machine(ui))) {
                if (!debug.inspecting) {
                    if (frontend_joystick_consumes(&kbd_joystick, event.key.keysym.sym)) {
                        joystick_handle_key_and_solid_apple(
                            &kbd_joystick, &controllers, client, &event.key);
                    } else {
                        handle_keyboard_input(&input_mapper, client, &event.key);
                    }
                }
                send_event_to_frontend = false;
            } else if (event.type == SDL_DROPFILE) {
                handle_drop_file(client, ui, &options, &debug, event.drop.file);
                send_event_to_frontend = false;
            }

            if (send_event_to_frontend) {
                frontend_handle_event(ui, &event);
            }
        }
        frontend_end_input(ui);

        while (runtime_client_poll_event(client, &revent)) {
            if (control_active) {
                control_dispatch_on_runtime_event(&control_disp, &revent);
            }
            apply_event_to_debug(&debug, &revent);
            if (revent.type == RUNTIME_EVENT_DEBUG_MEMORY_READY) {
                runtime_debug_memory_snapshot snap;
                if (runtime_client_poll_debug_memory(client, &snap)) {
                    debug.debug_memory = snap;
                    debug.has_debug_memory = true;
                }
            }
            if (revent.type == RUNTIME_EVENT_BREAKPOINTS_RESPONSE) {
                runtime_breakpoint_snapshot bps;
                if (runtime_client_poll_breakpoints(client, &bps)) {
                    debug.breakpoints = bps;
                    debug.has_breakpoints = true;
                }
            }
            if (revent.type == RUNTIME_EVENT_SAVE_STATE_COMPLETE) {
                if (!append_host_state_chunk(
                        revent.data.state_file.path, &options, &kbd_joystick)) {
                    log_warn(
                        "save state host settings append failed: %s",
                        revent.data.state_file.path);
                }
                log_info("save state complete: %s", revent.data.state_file.path);
            }
            if (revent.type == RUNTIME_EVENT_LOAD_STATE_COMPLETE) {
                apply_loaded_host_state(
                    revent.data.state_file.path,
                    &options,
                    ui,
                    client,
                    &controllers,
                    &kbd_joystick);
                log_info("load state complete: %s", revent.data.state_file.path);
            }
            if (revent.type == RUNTIME_EVENT_ERROR) {
                fprintf(
                    stderr,
                    "a2m: runtime: %s\n",
                    revent.data.error.message);
            }
            if (revent.type == RUNTIME_EVENT_ASSEMBLE_ERROR) {
                frontend_show_assembler_errors(ui, revent.data.error.message);
            }
            if (revent.type == RUNTIME_EVENT_HISTORY_STATUS_RESPONSE ||
                revent.type == RUNTIME_EVENT_HISTORY_RESULT_RESPONSE) {
                forensics_handle_history_event(client, ui, &revent);
            }
            if (revent.type == RUNTIME_EVENT_ASSEMBLE_COMPLETE) {
                runtime_symbol_snapshot symbols;
                /* Control dispatch owns the single-consumer symbol poll when
                   the control port is active; otherwise poll here for the UI. */
                if (control_active &&
                    control_dispatch_copy_symbols(&control_disp, &symbols)) {
                    frontend_update_symbols(ui, &symbols);
                } else if (!control_active &&
                           runtime_client_poll_symbols(client, &symbols)) {
                    frontend_update_symbols(ui, &symbols);
                }
                frontend_invalidate_disassembly_cache(ui);
                if (revent.data.assemble.notice[0] != '\0') {
                    frontend_show_assembler_notice(
                        ui, revent.data.assemble.notice);
                }
                request_debug_state(client);
            }
            if (revent.type == RUNTIME_EVENT_DISK_SWAP) {
                /* Worker already stepped the live machine queue; mirror host
                   queue current for UI. device = drive 0/1. */
                uint8_t drive = revent.data.disk_swap.device;
                uint8_t slot_number = revent.data.disk_swap.slot;
                int32_t param = revent.data.disk_swap.swap_param;
                uint8_t relative = revent.data.disk_swap.swap_relative;
                app_disk_slot *slot;

                if (drive > 1u) {
                    drive = 0u;
                }
                slot = app_options_diskii_queue(&options, slot_number, drive);
                if (slot == NULL) {
                    continue;
                }
                if (param != 0 && slot->count > 0) {
                    int new_index;
                    if (relative) {
                        new_index =
                            ((slot->current + (int)param) % slot->count + slot->count) %
                            slot->count;
                    } else {
                        new_index =
                            (((int)param - 1) % slot->count + slot->count) % slot->count;
                    }
                    (void)app_disk_slot_select(slot, new_index);
                    frontend_set_disk_queue(ui, drive, slot);
                }
            }
            if (revent.type == RUNTIME_EVENT_MEDIA_CHANGED &&
                revent.data.media_changed.success) {
                const uint8_t slot_number = revent.data.media_changed.slot;
                const uint8_t device = revent.data.media_changed.device;
                const runtime_slot_card_type card_type =
                    (runtime_slot_card_type)revent.data.media_changed.card_type;
                const runtime_media_change_type change_type =
                    (runtime_media_change_type)revent.data.media_changed.change_type;

                if (card_type == RUNTIME_SLOT_CARD_DISKII) {
                    app_disk_slot *queue = app_options_diskii_queue(
                        &options, slot_number, device);
                    if (change_type == RUNTIME_MEDIA_CHANGE_INSERT) {
                        if (app_options_diskii_append_path(
                                &options,
                                slot_number,
                                device,
                                revent.data.media_changed.path)) {
                            queue = app_options_diskii_queue(
                                &options, slot_number, device);
                            if (queue != NULL && queue->count > 0) {
                                (void)app_disk_slot_select(queue, queue->count - 1);
                            }
                        }
                    } else if (change_type == RUNTIME_MEDIA_CHANGE_EJECT) {
                        (void)app_options_diskii_eject_current(
                            &options, slot_number, device);
                    } else if (change_type == RUNTIME_MEDIA_CHANGE_SWAP) {
                        /* Queue current is mirrored by RUNTIME_EVENT_DISK_SWAP
                           (absolute and relative). Do not assume +1 here. */
                        (void)queue;
                    }
                    if (slot_number == 6u) {
                        frontend_set_disk_queue(ui, device, queue);
                    }
                } else if (card_type == RUNTIME_SLOT_CARD_SMARTPORT) {
                    if (change_type == RUNTIME_MEDIA_CHANGE_INSERT) {
                        (void)app_options_smartport_set_path(
                            &options,
                            slot_number,
                            device,
                            revent.data.media_changed.path);
                    } else if (change_type == RUNTIME_MEDIA_CHANGE_EJECT) {
                        app_options_smartport_clear_path(
                            &options, slot_number, device);
                    }
                }
                app_options_sync_convenience_paths(&options);
                /* Keep Configure's options snapshot aligned with live media so a
                   later Save INI does not revive ejected mounts. When the dialog
                   is open, only refresh mounts (preserve in-progress edits). */
                if (ui != NULL) {
                    if (frontend_config_dialog_is_open(ui)) {
                        frontend_sync_config_media_mounts(ui, &options);
                    } else {
                        frontend_set_config_state(ui, &options);
                    }
                }
            }
            /* After stop-class events, refresh tables like c64m request_debug_state. */
            if (revent.type == RUNTIME_EVENT_PAUSED ||
                revent.type == RUNTIME_EVENT_STEP_COMPLETE ||
                revent.type == RUNTIME_EVENT_RESET_COMPLETE ||
                revent.type == RUNTIME_EVENT_RUN_COMPLETE) {
                request_debug_state(client);
            }
        }

        if (control_active) {
            control_dispatch_poll(&control_disp);
            control_dispatch_check_session(&control_disp);
            if (control_deferred_active(&control_disp.deferred) == NULL) {
                control_dispatch_poll(&control_disp);
            }
        }

        if (runtime_client_poll_argb_frame(
                client,
                pixels,
                APPLE2_VIDEO_WIDTH * APPLE2_VIDEO_HEIGHT,
                &w,
                &h,
                &fn)) {
            (void)frontend_submit_argb_frame(ui, pixels, w, h, fn);
            debug.has_frame = true;
            debug.frame_number = fn;
        }
        {
            uint64_t preview_cycle = 0u;
            if (frontend_inspector_preview(ui, &preview_cycle)) {
                static runtime_ring_frame film;
                if (runtime_client_copy_frame_at(
                        client, preview_cycle, true, &film)) {
                    (void)frontend_submit_argb_frame(
                        ui,
                        film.pixels,
                        film.width,
                        film.height,
                        film.frame_number);
                    debug.has_frame = true;
                    debug.frame_number = film.frame_number;
                } else {
                    static uint32_t pink[
                        APPLE2_VIDEO_WIDTH * APPLE2_VIDEO_HEIGHT];
                    static int pink_init = 0;
                    if (!pink_init) {
                        size_t i;
                        for (i = 0;
                             i < (size_t)APPLE2_VIDEO_WIDTH *
                                 (size_t)APPLE2_VIDEO_HEIGHT;
                             ++i) {
                            pink[i] = 0xffff00ffu;
                        }
                        pink_init = 1;
                    }
                    (void)frontend_submit_argb_frame(
                        ui,
                        pink,
                        APPLE2_VIDEO_WIDTH,
                        APPLE2_VIDEO_HEIGHT,
                        0u);
                    debug.has_frame = true;
                }
            }
        }

        {
            static uint32_t tick;
            tick++;
            if ((tick % 30u) == 0u) {
                (void)runtime_client_request_machine_state(client);
                if (debug.runtime_state != FRONTEND_RUNTIME_STATE_RUNNING) {
                    (void)runtime_client_request_cpu_state(client);
                }
            }
        }

        /* Gated title update — only when contents change (c64m). */
        {
            int model = options.apple_model;
            if (!title_set ||
                debug.runtime_state != last_title_state ||
                debug.stop_reason != last_title_stop_reason ||
                debug.active_turbo_multiplier != last_title_turbo ||
                model != last_title_model ||
                debug.inspecting != last_title_inspecting ||
                debug.inspector_focus_cycle != last_title_focus) {
                update_window_title(
                    window,
                    app_model_label(model),
                    debug.active_turbo_multiplier,
                    debug.runtime_state,
                    debug.stop_reason,
                    &debug);
                last_title_state = debug.runtime_state;
                last_title_stop_reason = debug.stop_reason;
                last_title_turbo = debug.active_turbo_multiplier;
                last_title_model = model;
                last_title_inspecting = debug.inspecting;
                last_title_focus = debug.inspector_focus_cycle;
                title_set = true;
            }
        }

        if (!platform_window_clear(window)) {
            running = false;
            break;
        }
        frontend_render(ui, ui_visible, &debug);
        if (frontend_forensics_consume_pause_request(ui)) {
            if (debug.runtime_state == FRONTEND_RUNTIME_STATE_RUNNING) {
                (void)runtime_client_pause(client);
            }
        }
        if (frontend_forensics_consume_close_request(ui)) {
            /* Close button == Opt+R (return to entry surface). */
            leave_forensics_mode(window, client, ui, &ui_visible, false);
        }
        if (frontend_forensics_consume_leave_debugger_request(ui)) {
            /* Successful Land before/exact → debugger paused + Inspector tab. */
            leave_forensics_mode(window, client, ui, &ui_visible, true);
        }
        {
            /* After render, edit-focus is current — sync macOS text input
               so hold-to-type on machine keys does not pop accent menus. */
            bool want_text_input = frontend_wants_text_input(ui);
            if (want_text_input != text_input_active) {
                if (want_text_input) {
                    SDL_StartTextInput();
                } else {
                    SDL_StopTextInput();
                }
                text_input_active = want_text_input;
            }
        }

        /* Intents after render (c64m): UI finished building this frame. */
        while (frontend_poll_debugger_intent(ui, &intent)) {
            dispatch_intent(
                client,
                ui,
                &options,
                &kbd_joystick,
                &controllers,
                debug.runtime_state == FRONTEND_RUNTIME_STATE_RUNNING,
                debug.inspecting,
                &intent);
            if (intent.type == FRONTEND_DEBUGGER_INTENT_CONFIG_APPLY ||
                intent.type == FRONTEND_DEBUGGER_INTENT_SAVE_INI_NOW) {
                frontend_input_mapper_set_original_del(
                    &input_mapper, options.original_del);
            }
            if (intent.type == FRONTEND_DEBUGGER_INTENT_CONFIG_APPLY ||
                intent.type == FRONTEND_DEBUGGER_INTENT_SAVE_INI_NOW ||
                intent.type == FRONTEND_DEBUGGER_INTENT_SAVE_PATHS_ONLY ||
                intent.type == FRONTEND_DEBUGGER_INTENT_FILE_BROWSER_RESULT) {
                app_options_destroy((app_options *)&intent.config);
            }
        }

        platform_window_present(window);
        SDL_Delay(1);
    }

    if ((options.save_ini || options.remember) && !options.no_save_ini) {
        if (window != NULL) {
            platform_window_get_size(window, &options.window_width, &options.window_height);
        }
        if (ui != NULL) {
            frontend_layout_state layout_state;
            int slot;
            frontend_get_layout_state(ui, &layout_state);
            options.layout_split_display_right = layout_state.split_display_right;
            options.layout_split_top_bottom = layout_state.split_top_bottom;
            options.layout_split_memory_misc = layout_state.split_memory_misc;
            for (slot = 0; slot < FRONTEND_BROWSE_SLOT_COUNT && slot < APP_BROWSE_DIR_COUNT;
                 ++slot) {
                const char *dir = frontend_get_browse_dir(ui, (frontend_browse_slot)slot);
                app_options_set_string(&options.browse_dirs[slot], dir[0] ? dir : NULL);
            }
            sync_assembler_options_from_frontend(&options, ui);
        }
    }

    exit_code = EXIT_SUCCESS;

done:
    if (control_active) {
        control_dispatch_shutdown(&control_disp);
        control_active = false;
    }
    if (control != NULL) {
        control_server_stop(control);
        control_server_destroy(control);
        control = NULL;
    }
    if (client != NULL) {
        sdl_apple_controllers_close(&controllers, client);
    }
    if (ui != NULL) {
        frontend_destroy(ui);
    }
    if (window != NULL) {
        platform_window_destroy(window);
    }
    if (rt != NULL) {
        /* Stop worker first so the BP table is quiescent, then persist [DEBUG]
           break.* before app_options rewrites the same INI. */
        runtime_stop(rt);
        if ((options.save_ini || options.remember) && !options.no_save_ini) {
            if (!runtime_save_debug_ini(rt)) {
                log_warn(
                    "failed to save debug ini (breakpoints): %s",
                    options.ini_path != NULL ? options.ini_path : "(null)");
            }
        }
        runtime_destroy(rt);
        rt = NULL;
    }
    if ((options.save_ini || options.remember) && !options.no_save_ini) {
        (void)app_options_save_shutdown(&options);
    }
    if (host_audio != NULL) {
        platform_audio_destroy(host_audio);
        host_audio = NULL;
    }
    if (audio_buf != NULL) {
        audio_buffer_destroy(audio_buf);
        audio_buf = NULL;
    }
    platform_shutdown();
    app_options_destroy(&options);
    return exit_code;
}
