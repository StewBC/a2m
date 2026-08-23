#include "runtime.h"

#include "apple2.h"
#include "message_queue.h"
#include "mutex.h"
#include "runtime_breakpoint_ini.h"
#include "runtime_command.h"
#include "runtime_internal.h"
#include "runtime_timemachine.h"
#include "thread.h"

#include <ctype.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *runtime_copy_string(const char *value)
{
    char *copy;
    size_t length;

    if (value == NULL) {
        return NULL;
    }
    length = strlen(value);
    copy = (char *)malloc(length + 1u);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, value, length + 1u);
    return copy;
}

void runtime_config_init(runtime_config *config)
{
    if (config == NULL) {
        return;
    }
    memset(config, 0, sizeof(*config));
    runtime_config_set_turbo_defaults(config);
    config->apple_model = 0;
    config->mb_slot = 4;
    config->slot_cards[4] = RUNTIME_SLOT_CARD_MOCKINGBOARD;
    config->slot_cards[6] = RUNTIME_SLOT_CARD_DISKII;
    config->slot_cards[7] = RUNTIME_SLOT_CARD_SMARTPORT;
    config->start_running = true;
    config->frame_ring_memory_mb = 0;
    config->diskii_mount_count = 0;
    config->smartport_mount_count = 0;
    config->smartport_boot_slot = 0;
}

bool runtime_turbo_parse_token(const char *token, uint32_t *out_milli_mhz)
{
    const char *s;
    char *end = NULL;
    double mhz;

    if (token == NULL || out_milli_mhz == NULL) {
        return false;
    }
    s = token;
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return false;
    }
    /* max / -1 → free-run max */
    if ((s[0] == 'm' || s[0] == 'M') &&
        (s[1] == 'a' || s[1] == 'A') &&
        (s[2] == 'x' || s[2] == 'X')) {
        const char *tail = s + 3;
        while (isspace((unsigned char)*tail)) {
            tail++;
        }
        if (*tail != '\0') {
            return false;
        }
        *out_milli_mhz = RUNTIME_TURBO_MAX;
        return true;
    }
    if (s[0] == '-' && s[1] == '1') {
        const char *tail = s + 2;
        while (isspace((unsigned char)*tail)) {
            tail++;
        }
        if (*tail != '\0') {
            return false;
        }
        *out_milli_mhz = RUNTIME_TURBO_MAX;
        return true;
    }

    mhz = strtod(s, &end);
    if (end == s || !isfinite(mhz) || mhz <= 0.0 || mhz > 10000.0) {
        return false;
    }
    while (isspace((unsigned char)*end)) {
        end++;
    }
    if (*end != '\0') {
        return false;
    }
    /* Round to nearest milli-MHz; require at least 1 milli-MHz. */
    {
        double milli = mhz * 1000.0 + 0.5;
        if (milli < 1.0) {
            return false;
        }
        if (milli > (double)UINT32_MAX) {
            return false;
        }
        *out_milli_mhz = (uint32_t)milli;
    }
    return true;
}

void runtime_turbo_format_label(uint32_t milli_mhz, char *buf, size_t buf_size)
{
    if (buf == NULL || buf_size == 0u) {
        return;
    }
    if (milli_mhz == RUNTIME_TURBO_MAX) {
        snprintf(buf, buf_size, "max");
        return;
    }
    if ((milli_mhz % 1000u) == 0u) {
        snprintf(buf, buf_size, "%u MHz", (unsigned)(milli_mhz / 1000u));
        return;
    }
    {
        double mhz = (double)milli_mhz / 1000.0;
        snprintf(buf, buf_size, "%.3g MHz", mhz);
    }
}

void runtime_turbo_format_token(uint32_t milli_mhz, char *buf, size_t buf_size)
{
    if (buf == NULL || buf_size == 0u) {
        return;
    }
    if (milli_mhz == RUNTIME_TURBO_MAX) {
        snprintf(buf, buf_size, "max");
        return;
    }
    if ((milli_mhz % 1000u) == 0u) {
        snprintf(buf, buf_size, "%u", (unsigned)(milli_mhz / 1000u));
        return;
    }
    {
        double mhz = (double)milli_mhz / 1000.0;
        snprintf(buf, buf_size, "%.3g", mhz);
    }
}

double runtime_turbo_target_hz(uint32_t milli_mhz)
{
    if (milli_mhz == RUNTIME_TURBO_MAX) {
        return 0.0;
    }
    return ((double)milli_mhz / 1000.0) * APPLE2_CPU_FREQUENCY_HZ;
}

void runtime_config_set_turbo_defaults(runtime_config *config)
{
    if (config == NULL) {
        return;
    }
    config->turbo_speeds[0] = RUNTIME_TURBO_MHZ_1;
    config->turbo_speeds[1] = RUNTIME_TURBO_MAX;
    config->turbo_speed_count = 2;
    config->active_turbo_multiplier = RUNTIME_TURBO_MHZ_1;
}

bool runtime_config_set_turbo_csv(runtime_config *config, const char *csv)
{
    const char *cursor;
    uint8_t count = 0;

    if (config == NULL) {
        return false;
    }
    runtime_config_set_turbo_defaults(config);
    if (csv == NULL || csv[0] == '\0') {
        return true;
    }
    cursor = csv;
    while (*cursor != '\0' && count < 16) {
        char token[64];
        size_t ti = 0;
        uint32_t milli = 0;

        while (isspace((unsigned char)*cursor)) {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }
        while (*cursor != '\0' && *cursor != ',' && ti + 1u < sizeof(token)) {
            token[ti++] = *cursor++;
        }
        token[ti] = '\0';
        /* Trim trailing whitespace in token. */
        while (ti > 0u && isspace((unsigned char)token[ti - 1u])) {
            token[--ti] = '\0';
        }
        if (ti == 0u || !runtime_turbo_parse_token(token, &milli)) {
            runtime_config_set_turbo_defaults(config);
            return false;
        }
        config->turbo_speeds[count++] = milli;
        if (*cursor == ',') {
            cursor++;
        }
    }
    if (count == 0) {
        runtime_config_set_turbo_defaults(config);
        return false;
    }
    config->turbo_speed_count = count;
    config->active_turbo_multiplier = config->turbo_speeds[0];
    return true;
}

bool runtime_init(void)
{
    return true;
}

void runtime_shutdown(void)
{
}

void runtime_rpc_pool_release_token(runtime_rpc_payload_pool *pool, uint64_t token)
{
    size_t i;
    if (pool == NULL || token == 0) {
        return;
    }
    mutex_lock(pool->mutex);
    for (i = 0; i < RUNTIME_RPC_PAYLOAD_POOL_CAPACITY; i++) {
        if (pool->slots[i].in_use && pool->slots[i].request_token == token) {
            free(pool->slots[i].bytes);
            memset(&pool->slots[i], 0, sizeof(pool->slots[i]));
            break;
        }
    }
    mutex_unlock(pool->mutex);
}

runtime *runtime_create(const runtime_config *config)
{
    if (config != NULL) {
        int slot;
        int mockingboards = 0;
        for (slot = 1; slot <= 7; ++slot) {
            runtime_slot_card_type type = config->slot_cards[slot];
            if (type < RUNTIME_SLOT_CARD_EMPTY || type > RUNTIME_SLOT_CARD_MOCKINGBOARD) {
                return NULL;
            }
            if (type == RUNTIME_SLOT_CARD_MOCKINGBOARD) {
                mockingboards++;
            }
        }
        if (config->slot_cards[0] != RUNTIME_SLOT_CARD_EMPTY || mockingboards > 1 ||
            config->smartport_boot_slot < 0 || config->smartport_boot_slot > 7) {
            return NULL;
        }
    }
    runtime *rt = (runtime *)calloc(1, sizeof(*rt));
    if (rt == NULL) {
        return NULL;
    }

    atomic_init(&rt->quit_requested, false);
    rt->command_queue =
        message_queue_create(sizeof(runtime_command), RUNTIME_COMMAND_QUEUE_CAPACITY);
    rt->event_queue =
        message_queue_create(sizeof(runtime_event), RUNTIME_EVENT_QUEUE_CAPACITY);
    rt->frame_slot.mutex = mutex_create();
    rt->debug_memory_slot.mutex = mutex_create();
    rt->breakpoint_slot.mutex = mutex_create();
    rt->symbol_slot.mutex = mutex_create();
    rt->rpc_payload_pool.mutex = mutex_create();

    if (rt->command_queue == NULL || rt->event_queue == NULL ||
        rt->frame_slot.mutex == NULL || rt->debug_memory_slot.mutex == NULL ||
        rt->breakpoint_slot.mutex == NULL || rt->symbol_slot.mutex == NULL ||
        rt->rpc_payload_pool.mutex == NULL) {
        runtime_destroy(rt);
        return NULL;
    }

    rt->client.command_queue = rt->command_queue;
    rt->client.event_queue = rt->event_queue;
    rt->client.frame_slot = &rt->frame_slot;
    rt->client.debug_memory_slot = &rt->debug_memory_slot;
    rt->client.breakpoint_slot = &rt->breakpoint_slot;
    rt->client.symbol_slot = &rt->symbol_slot;
    rt->client.rpc_payload_pool = &rt->rpc_payload_pool;
    rt->client.frame_ring = &rt->frame_ring;
    rt->client.next_request_token = 0;
    rt->next_breakpoint_id = 1;

    /* Default session for omit-session_id commands (compat / single asker). */
    rt->next_session_id = 1u;
    rt->sessions[0].id = 1u;
    rt->sessions[0].kind = RUNTIME_SESSION_KIND_UI;
    rt->sessions[0].active = 1u;
    rt->sessions[0].endpoint_epoch = 0u;
    rt->default_session_id = 1u;
    rt->next_session_id = 2u;

    if (config != NULL) {
        int i;

        rt->config = *config;
        memcpy(rt->turbo_speeds, config->turbo_speeds, sizeof(rt->turbo_speeds));
        rt->turbo_speed_count = config->turbo_speed_count;
        rt->active_turbo_multiplier = config->active_turbo_multiplier;
        rt->audio_out = config->audio_out;
        rt->audio_sample_rate = config->audio_sample_rate;
        rt->frame_ring_memory_mb = config->frame_ring_memory_mb;

        /* Rolling screen log (C2): budget 0 disables. Allocation failure is
           nonfatal — emulation continues without the ring. */
        if (rt->frame_ring_memory_mb > 0u) {
            uint64_t budget =
                (uint64_t)rt->frame_ring_memory_mb * 1024ull * 1024ull;
            if (!runtime_frame_ring_init(&rt->frame_ring, budget)) {
                /* leave zeroed / disabled */
            }
        }

        /* CPU flight recorder (C3): default 256 MiB when configured; 0 = off. */
        rt->history_memory_mb = config->history_memory_mb;
        rt->history_off_on_max = config->history_off_on_max;
        rt->history_paused_for_max = false;
        rt->timemachine_enabled = false;
        rt->timemachine_memory_mb = config->timemachine_memory_mb;
        if (rt->history_memory_mb > 0u) {
            uint64_t hbudget =
                (uint64_t)rt->history_memory_mb * 1024ull * 1024ull;
            rt->history = runtime_history_create(hbudget);
            /* NULL history is nonfatal (allocation failure). */
        }

        /* Breakpoint INI ownership is on runtime (path copied). */
        rt->use_ini = config->use_ini;
        rt->save_ini = config->save_ini;
        if (config->ini_path != NULL && config->ini_path[0] != '\0') {
            rt->ini_path = runtime_copy_string(config->ini_path);
            if (rt->ini_path == NULL) {
                runtime_destroy(rt);
                return NULL;
            }
        }

        rt->diskii_mount_count = 0;
        rt->smartport_mount_count = 0;
        if (config->diskii_mount_count > 0) {
            int n = config->diskii_mount_count;
            if (n > RUNTIME_MAX_DISKII_MOUNTS) {
                n = RUNTIME_MAX_DISKII_MOUNTS;
            }
            for (i = 0; i < n; i++) {
                if (config->diskii_mounts[i].path == NULL ||
                    config->diskii_mounts[i].path[0] == '\0') {
                    continue;
                }
                rt->diskii_paths[rt->diskii_mount_count] =
                    runtime_copy_string(config->diskii_mounts[i].path);
                if (rt->diskii_paths[rt->diskii_mount_count] == NULL) {
                    runtime_destroy(rt);
                    return NULL;
                }
                rt->diskii_slots[rt->diskii_mount_count] = config->diskii_mounts[i].slot;
                rt->diskii_drives[rt->diskii_mount_count] = config->diskii_mounts[i].drive;
                rt->diskii_mount_count++;
            }
        }
        if (config->smartport_mount_count > 0) {
            int n = config->smartport_mount_count;
            if (n > RUNTIME_MAX_SMARTPORT_MOUNTS) {
                n = RUNTIME_MAX_SMARTPORT_MOUNTS;
            }
            for (i = 0; i < n; i++) {
                if (config->smartport_mounts[i].path == NULL ||
                    config->smartport_mounts[i].path[0] == '\0') {
                    continue;
                }
                rt->smartport_paths[rt->smartport_mount_count] =
                    runtime_copy_string(config->smartport_mounts[i].path);
                if (rt->smartport_paths[rt->smartport_mount_count] == NULL) {
                    runtime_destroy(rt);
                    return NULL;
                }
                rt->smartport_slots[rt->smartport_mount_count] =
                    config->smartport_mounts[i].slot;
                rt->smartport_units[rt->smartport_mount_count] =
                    config->smartport_mounts[i].unit;
                rt->smartport_mount_count++;
            }
        }
    } else {
        runtime_config_init(&rt->config);
        runtime_config_set_turbo_defaults(&rt->config);
        memcpy(rt->turbo_speeds, rt->config.turbo_speeds, sizeof(rt->turbo_speeds));
        rt->turbo_speed_count = rt->config.turbo_speed_count;
        rt->active_turbo_multiplier = rt->config.active_turbo_multiplier;
    }
    if (rt->turbo_speed_count == 0) {
        runtime_config_set_turbo_defaults(&rt->config);
        memcpy(rt->turbo_speeds, rt->config.turbo_speeds, sizeof(rt->turbo_speeds));
        rt->turbo_speed_count = rt->config.turbo_speed_count;
        rt->active_turbo_multiplier = rt->config.active_turbo_multiplier;
    }

    host_keyboard_reset(&rt->host_keyboard);
    return rt;
}

void runtime_destroy(runtime *rt)
{
    size_t i;
    int j;
    if (rt == NULL) {
        return;
    }
    runtime_stop(rt);
    if (rt->trace_file != NULL) {
        fclose(rt->trace_file);
        rt->trace_file = NULL;
    }
    runtime_frame_ring_destroy(&rt->frame_ring);
    runtime_tm_forensic_destroy(rt);
    runtime_tm_recorder_destroy(rt);
    runtime_history_destroy(rt->history);
    rt->history = NULL;
    free(rt->frame_slot.argb);
    free(rt->ini_path);
    rt->ini_path = NULL;
    for (j = 0; j < rt->diskii_mount_count; j++) {
        free(rt->diskii_paths[j]);
        rt->diskii_paths[j] = NULL;
    }
    for (j = 0; j < rt->smartport_mount_count; j++) {
        free(rt->smartport_paths[j]);
        rt->smartport_paths[j] = NULL;
    }
    if (rt->rpc_payload_pool.mutex != NULL) {
        mutex_lock(rt->rpc_payload_pool.mutex);
        for (i = 0; i < RUNTIME_RPC_PAYLOAD_POOL_CAPACITY; i++) {
            free(rt->rpc_payload_pool.slots[i].bytes);
        }
        mutex_unlock(rt->rpc_payload_pool.mutex);
    }
    mutex_destroy(rt->frame_slot.mutex);
    mutex_destroy(rt->debug_memory_slot.mutex);
    mutex_destroy(rt->breakpoint_slot.mutex);
    mutex_destroy(rt->symbol_slot.mutex);
    mutex_destroy(rt->rpc_payload_pool.mutex);
    message_queue_destroy(rt->event_queue);
    message_queue_destroy(rt->command_queue);
    free(rt);
}

bool runtime_start(runtime *rt)
{
    if (rt == NULL) {
        return false;
    }
    if (rt->started) {
        return true;
    }
    rt->thread = thread_create("a2m-runtime", runtime_thread_main, rt);
    if (rt->thread == NULL) {
        return false;
    }
    rt->started = true;
    return true;
}

bool runtime_quit_requested(const runtime *rt)
{
    return rt != NULL && atomic_load(&rt->quit_requested);
}

void runtime_stop(runtime *rt)
{
    runtime_command command;
    if (rt == NULL || !rt->started) {
        return;
    }
    /* Tape seeks can fill the 256-deep command queue. A failing QUIT push
       then thread_join beachballs. Drop the backlog, force QUIT in. */
    atomic_store(&rt->quit_requested, true);
    message_queue_clear(rt->command_queue);
    memset(&command, 0, sizeof(command));
    command.type = RUNTIME_COMMAND_QUIT;
    (void)message_queue_push(rt->command_queue, &command);
    message_queue_wake_all(rt->command_queue);
    thread_join(rt->thread);
    thread_destroy(rt->thread);
    rt->thread = NULL;
    rt->started = false;
}

bool runtime_save_debug_ini(runtime *rt)
{
    /* Call only after runtime_stop (worker joined) so the BP table is stable. */
    return runtime_save_breakpoints_to_ini(rt);
}

runtime_client *runtime_get_client(runtime *rt)
{
    return rt != NULL ? &rt->client : NULL;
}
