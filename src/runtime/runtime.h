#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "audio_buffer.h"
#include "runtime_event.h"

typedef struct runtime runtime;
typedef struct runtime_client runtime_client;

/*
 * Turbo ladder encoding (config, commands, machine snapshot):
 *   RUNTIME_TURBO_MAX (0)  — free-run max (presentation paint once Phase 3 lands)
 *   N > 0                  — finite target in milli-MHz
 *                            1000 = 1.0 MHz = real-time Apple
 *                            2500 = 2.5 MHz
 *                            4000 = 4.0 MHz
 *
 * Field name active_turbo_multiplier is historical; the value is milli-MHz
 * (or RUNTIME_TURBO_MAX), not a 1/2/3 mode ID. Blank warp is gone.
 */
#define RUNTIME_TURBO_MAX 0u
#define RUNTIME_TURBO_MHZ_1 1000u

typedef struct runtime_config {
    const char *ini_path;
    const char *symbol_files;
    bool use_ini;
    bool save_ini;
    runtime_machine_config machine_config;
    /* Ladder of milli-MHz values; 0 entry = max. Default: 1 MHz, max. */
    uint32_t turbo_speeds[16];
    uint8_t turbo_speed_count;
    /* Active entry: milli-MHz or RUNTIME_TURBO_MAX. */
    uint32_t active_turbo_multiplier;
    audio_buffer *audio_out;
    int audio_sample_rate;
    const char *audio_record_path;
    double audio_record_start_seconds;
    double audio_record_duration_seconds;
    int audio_smoke;
    bool autorun;
    uint32_t history_memory_mb;
    bool history_memory_mb_configured;
    /* Pause flight-recorder while turbo is max (default true). */
    bool history_off_on_max;
    uint32_t frame_ring_memory_mb;
    bool frame_ring_memory_mb_configured;
    /* Inspector master enable (default off). Off→on arms HST1 + frame ring. */
    bool inspector;
    /* Checkpoint-ring budget in MiB (consumed in TM2). */
    uint32_t inspector_memory_mb;
    bool inspector_memory_mb_configured;

    /* Apple-specific */
    int apple_model; /* 0=//e enh, 1=][+ */
    int mb_slot;     /* 1..7 attach MB; 0 = none */
    runtime_slot_card_type slot_cards[RUNTIME_APPLE_SLOT_COUNT];
    bool start_running;

    /* Multi-mount lists. Paths not owned by runtime_config. */
    struct {
        int slot;
        int drive;
        const char *path;
    } diskii_mounts[16];
    int diskii_mount_count;
    struct {
        int slot;
        int unit;
        const char *path;
    } smartport_mounts[16];
    int smartport_mount_count;
    int smartport_boot_slot; /* 1..7 forces startup at $Cn00 after unit 0 mounts. */
} runtime_config;

void runtime_config_set_turbo_defaults(runtime_config *config);
bool runtime_config_set_turbo_csv(runtime_config *config, const char *csv);
void runtime_config_init(runtime_config *config);

/* Parse one turbo token ("1", "2.5", "max", "-1") → milli-MHz (0 = max). */
bool runtime_turbo_parse_token(const char *token, uint32_t *out_milli_mhz);
/* Format milli-MHz for UI: "max", "1 MHz", "2.5 MHz". */
void runtime_turbo_format_label(uint32_t milli_mhz, char *buf, size_t buf_size);
/* Compact token for control wire (no spaces): "max", "1", "2.5". */
void runtime_turbo_format_token(uint32_t milli_mhz, char *buf, size_t buf_size);
/* Finite target Φ0 Hz; 0 if max. */
double runtime_turbo_target_hz(uint32_t milli_mhz);
static inline bool runtime_turbo_is_max_value(uint32_t milli_mhz)
{
    return milli_mhz == RUNTIME_TURBO_MAX;
}

bool runtime_init(void);
void runtime_shutdown(void);

runtime *runtime_create(const runtime_config *config);
void runtime_destroy(runtime *rt);

bool runtime_start(runtime *rt);
void runtime_stop(runtime *rt);
bool runtime_save_debug_ini(runtime *rt);

runtime_client *runtime_get_client(runtime *rt);
