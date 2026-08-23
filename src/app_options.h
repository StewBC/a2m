#pragma once

#include <stdbool.h>
#include <stddef.h>

#define A2M_DISK_SLOT_COUNT 16

/* Remembered file-browser default folders, one per browse slot. The order and
   count must match frontend_browse_slot in frontend/frontend.h. */
#define APP_BROWSE_DIR_COUNT 6

/* Apple multi-mount capacity (matches runtime). */
enum {
    APP_OPTIONS_MAX_DISKII = 16,
    APP_OPTIONS_MAX_SMARTPORT = 16
};

typedef enum app_slot_card_type {
    APP_SLOT_CARD_EMPTY = 0,
    APP_SLOT_CARD_DISKII,
    APP_SLOT_CARD_SMARTPORT,
    APP_SLOT_CARD_MOCKINGBOARD
} app_slot_card_type;

enum { APP_SLOT_CARD_COUNT = 8 };

/* Ordered multi-image queue for one Disk II drive (paths[0..count-1], current).
   Product uses disk_slots[0]/[1] for controller drives d0/d1 (default slot 6). */
typedef struct {
    char **paths;
    bool  *writable;
    int    count;
    int    current; /* index of the disk currently (or last) mounted; 0 by default */
    /* CLI `-d N=` (empty path): power the unit on without mounting an image. */
    bool   power_on_only;
} app_disk_slot;

typedef struct app_diskii_mount {
    int slot;  /* 1..7 */
    int drive; /* 0..1 */
    char *path;
} app_diskii_mount;

typedef struct app_smartport_mount {
    int slot; /* 1..7 */
    int unit; /* 0..1 */
    char *path;
} app_smartport_mount;

typedef struct app_options {
    bool use_ini;
    bool save_ini;
    bool remember;
    bool defaults;
    bool no_save_ini;
    bool show_version;
    int scroll_wheel_lines;
    bool original_del;
    char *ini_path;
    app_disk_slot disk_slots[A2M_DISK_SLOT_COUNT];
    char *breakpoint;
    char *turbo_multipliers;
    char *symbol_files;
    char *video_standard;
    /* Optional frontend-only CRT presentation. Strength/amount are 1..100. */
    bool true_aspect;
    /* Filter the display instead of showing hard pixel edges. Scanlines and
       curvature both force this on: they are presented through a filtered
       texture and cannot be drawn without it. */
    bool crt_smoothing;
    bool crt_scanlines;
    int crt_scanline_strength;
    bool crt_curvature;
    int crt_curvature_amount;
    int window_width;
    int window_height;
    float layout_split_display_right;
    float layout_split_top_bottom;
    float layout_split_memory_misc;
    char *basic_path; /* Applesoft text path convenience */
    /* Startup machine snapshot (.a2state). Loaded after mount/setup when set. */
    char *sna_path;
    /* Remembered file-browser folders, indexed by frontend_browse_slot. */
    char *browse_dirs[APP_BROWSE_DIR_COUNT];
    /* When true, runtime emits a 440 Hz square wave via the audio path to
       verify that samples reach the host audio device. */
    bool audio_smoke;
    char *audio_record_path;
    float audio_record_start_seconds;
    float audio_record_duration_seconds;
    /* When true, automatically run after a binary/BASIC load where supported. */
    bool autorun;
    /* When true, draw shared disk activity LEDs in the UI window corner. */
    bool show_disk_leds;
    /* When true, free-run auto-pauses when the next opcode is BRK ($00). */
    bool pause_on_brk;
    /* Assembler tab persistent state */
    char *assembler_file;
    char *assembler_address;
    char *assembler_run_address;
    bool assembler_use_address;
    bool assembler_auto_run;
    bool assembler_mli_launch;
    bool assembler_reset_first;
    bool assembler_rearm_oneshots;
    bool assembler_auto_adjust_segments;
    int control_port;
    bool headless;
    /* Always-on CPU flight-recorder startup budget in MiB: 0 or 16..4096. */
    int history_memory_mb;
    /*
     * When true (default), pause flight-recorder while turbo is max for free-run
     * speed; restore previous recording state on leave max.
     */
    bool history_off_on_max;
    int frame_ring_memory_mb;
    /* TimeMachine master enable (default off). Off→on arms HST1 + frame ring. */
    bool timemachine;
    /* Checkpoint-ring budget in MiB (consumed in TM2). 0 or 16..4096; default 128. */
    int timemachine_memory_mb;
    /* Host-keyboard joystick: layout name ("numpad" or "wasd") and the Apple
       gameport stick it drives (0 = disabled, 1 or 2 = active).
       swap_buttons: when stick is on, Space↔Option (FIRE2↔FIRE) for ergonomics. */
    char *keyboard_joystick_layout;
    int keyboard_joystick_port;
    bool keyboard_joystick_swap_buttons;

    /* ---- Apple II product fields (W4) ---- */
    int apple_model; /* 0 = //e Enhanced, 1 = ][+ */
    int mb_slot;     /* CLI/runtime mirror of the Mockingboard slot; 0 = off. */
    app_slot_card_type slot_cards[APP_SLOT_CARD_COUNT];
    app_diskii_mount diskii[APP_OPTIONS_MAX_DISKII];
    int diskii_count;
    app_smartport_mount smartport[APP_OPTIONS_MAX_SMARTPORT];
    int smartport_count;
    int smartport_boot_slot; /* [SmartPort] boot_slot; 0 disables forced slot boot. */
    /* Configure-dialog convenience path buffers (owned). Synced to/from the
       multi-mount lists when loading options or applying config. */
    char *disk_s6d0;
    char *disk_s6d1;
    char *hd_s7d0;
    char *hd_s5d0;
} app_options;

/* Apple model helpers. */
const char *app_model_label(int apple_model);
bool app_model_from_string(const char *s, int *out_model);
const char *app_slot_card_name(app_slot_card_type type);
bool app_slot_card_from_string(const char *s, app_slot_card_type *out_type);
/* Last Mockingboard selection wins and clears any earlier Mockingboard. */
bool app_options_set_slot_card(app_options *options, int slot, app_slot_card_type type);
/* Remove media mounts whose controller is no longer installed. */
void app_options_reconcile_slot_cards(app_options *options);
/* Parse sNdN (e.g. s6d0) → slot/unit. Case-insensitive. */
bool app_options_parse_slot_unit_key(const char *key, int *out_slot, int *out_unit);
/* Sync convenience s6d0/s6d1/s7d0/s5d0 buffers from diskii/smartport lists. */
void app_options_sync_convenience_paths(app_options *options);
/* Apply convenience buffers into mount lists (replaces those four targets). */
bool app_options_apply_convenience_paths(app_options *options);

void app_options_init(app_options *options);
bool app_options_load_startup(app_options *options, int argc, char **argv);
bool app_options_save_shutdown(const app_options *options);
/* Re-reads the named INI and rewrites the [browse] folders plus the current
   Disk II/SmartPort media paths, leaving other settings intact. Silent no-op
   (returns true) when there is no writable INI target. */
bool app_options_save_paths_only(const app_options *options);
bool app_options_copy(app_options *dest, const app_options *src);
/* Replace dest Disk II / SmartPort mounts (and Disk II queues) from src.
   Slot cards and other Configure fields are left alone. Used because live
   media is owned by Misc -> Machine, not the Configure dialog snapshot. */
bool app_options_replace_media_mounts(app_options *dest, const app_options *src);
bool app_options_apply_ini_file(app_options *options, const char *path);
bool app_options_set_string(char **target, const char *value);
bool app_options_path_relative_to_ini(
    const app_options *options,
    const char *path,
    char *out,
    size_t out_size);
/* Inverse of app_options_path_relative_to_ini: resolves a (possibly relative)
   path against the INI file's directory into an absolute path. */
bool app_options_path_absolute_from_ini(
    const app_options *options,
    const char *path,
    char *out,
    size_t out_size);
bool app_options_symbol_files_absolute(
    const app_options *options,
    char *out,
    size_t out_size);
void app_options_destroy(app_options *options);

/* Disk slot helpers used by callers that manage live mount state. */
bool app_disk_slot_set(app_disk_slot *slot, const char *path);
void app_disk_slot_clear(app_disk_slot *slot);
bool app_disk_slot_copy(app_disk_slot *dest, const app_disk_slot *src);
/* Append path at end of queue without changing current (unless empty). */
bool app_disk_slot_append(app_disk_slot *slot, const char *path);
/* Queue index is slot * 2 + drive (s1d0..s7d1 fit in 16 entries). */
app_disk_slot *app_options_diskii_queue(app_options *options, int slot, int drive);
const app_disk_slot *app_options_diskii_queue_const(
    const app_options *options, int slot, int drive);
bool app_options_diskii_append_path(
    app_options *options, int slot, int drive, const char *path);
bool app_options_diskii_eject_current(app_options *options, int slot, int drive);
bool app_options_smartport_set_path(
    app_options *options, int slot, int device, const char *path);
void app_options_smartport_clear_path(app_options *options, int slot, int device);
/* Rebuild all per-slot/per-drive queues from the ordered diskii[] mounts. */
void app_options_rebuild_diskii_queues(app_options *options);

/*
 * Remove the current disk from the queue and advance to the next one
 * (round-robin).  Returns the path that should now be mounted, or NULL if the
 * queue is now empty.  The returned pointer is into slot->paths and is only
 * valid until the next mutation of slot.
 */
const char *app_disk_slot_eject_current(app_disk_slot *slot);

/*
 * Insert path into the queue immediately after the current disk.  If the queue
 * was empty the disk becomes the only entry (current=0).  Returns false on
 * allocation failure.
 */
bool app_disk_slot_add_after_current(app_disk_slot *slot, const char *path);

/*
 * Set current to index without modifying the queue.  Returns the path at that
 * index, or NULL if index is out of range.  The returned pointer is into
 * slot->paths.
 */
const char *app_disk_slot_select(app_disk_slot *slot, int index);
bool app_disk_slot_current_writable(const app_disk_slot *slot);
bool app_disk_slot_set_current_writable(app_disk_slot *slot, bool writable);
