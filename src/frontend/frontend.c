#include "frontend.h"

#include "frontend_input.h"
#include "forensics_view.h"
#include "help_view.h"
#include "memory_search.h"
#include "nuklear_config.h"
#include "nuklear_sdl.h"

#include "debugger_layout.h"
#include "mono_font_data.h"
#include "crt_renderer.h"
#include "disk_led_data.h"
#include "disasm_6502.h"
#include "disasm_pc_lock.h"
#include "platform_fs.h"
#include "runtime.h"
#include "runtime_history.h"
#include "runtime_inspector.h"
#include "softswitch.h"
#include "symbol_table.h"

#include "stb_image.h"

#include "a2m_log.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#define A2M_STAT_ISREG(mode) (((mode) & _S_IFREG) != 0)
#else
#define A2M_STAT_ISREG(mode) S_ISREG(mode)
#endif

enum {
    FRONTEND_DEBUGGER_INTENT_CAPACITY = 32,
    FRONTEND_DISK_LED_SIZE = 16,
    /* Host-time LED hold after each activity event (ms). Independent of pause. */
    FRONTEND_DISK_LED_HOLD_MS = 300,
    /* Fixed-point scale for fractional pixel aspect ratio math. */
    FRONTEND_DISPLAY_ASPECT_FIXED = 256,
    /* Framebuffer is 560-wide (double-res host pixels). True-aspect uses PAR
       1/2 so presentation matches the historical ~280×192 square-pixel shape. */
    FRONTEND_DISPLAY_PAR_FIXED = FRONTEND_DISPLAY_ASPECT_FIXED / 2,
    FRONTEND_CRT_RENDER_SCALE = 2,
    FRONTEND_DISPLAY_MATTE_R = 17,
    FRONTEND_DISPLAY_MATTE_G = 22,
    FRONTEND_DISPLAY_MATTE_B = 28
};

#define FRONTEND_DISPLAY_MATTE_ARGB UINT32_C(0xff11161c)

/* Alias Apple display size (macros from display_frame.h — validated there). */
enum {
    FRONTEND_DISPLAY_WIDTH = DISPLAY_FRAME_WIDTH,
    FRONTEND_DISPLAY_HEIGHT = DISPLAY_FRAME_HEIGHT
};

typedef enum frontend_register_field {
    FRONTEND_REGISTER_FIELD_NONE = 0,
    FRONTEND_REGISTER_FIELD_PC,
    FRONTEND_REGISTER_FIELD_SP,
    FRONTEND_REGISTER_FIELD_A,
    FRONTEND_REGISTER_FIELD_X,
    FRONTEND_REGISTER_FIELD_Y,
    FRONTEND_REGISTER_FIELD_STATUS_N,
    FRONTEND_REGISTER_FIELD_STATUS_V,
    FRONTEND_REGISTER_FIELD_STATUS_UNUSED,
    FRONTEND_REGISTER_FIELD_STATUS_B,
    FRONTEND_REGISTER_FIELD_STATUS_D,
    FRONTEND_REGISTER_FIELD_STATUS_I,
    FRONTEND_REGISTER_FIELD_STATUS_Z,
    FRONTEND_REGISTER_FIELD_STATUS_C
} frontend_register_field;

typedef struct frontend_register_view_state {
    frontend_register_field active_field;
    char pc[5];
    char sp[3];
    char a[3];
    char x[3];
    char y[3];
    char flags[8][2];
} frontend_register_view_state;

typedef enum frontend_memory_edit_field {
    FRONTEND_MEMORY_EDIT_HEX = 0,
    FRONTEND_MEMORY_EDIT_ASCII,
    FRONTEND_MEMORY_EDIT_ADDRESS
} frontend_memory_edit_field;

typedef struct frontend_memory_view_state {
    uint16_t view_address;
    uint16_t cursor_address;
    runtime_memory_mode mode;
    frontend_memory_edit_field edit_field;
    uint8_t active_nibble;
    uint8_t active_address_digit;
    uint8_t address_digits[4];
    uint16_t requested_address;
    uint16_t requested_length;
    runtime_memory_mode requested_mode;
    uint8_t columns;
    uint8_t rows;
    uint8_t color_slot;
    float cached_y_top;
    float cached_y_bottom;
    bool initialized;
    bool request_pending;
    bool scrollbar_dragging;
    float scrollbar_grab_offset;
    /* true: strip/set bit7 for ASCII column + typing (Apple text). Default on. */
    bool apple_ascii;
} frontend_memory_view_state;

typedef struct frontend_memory_search_state {
    bool open;
    bool just_opened;
    bool ignore_case;
    bool has_pattern;
    memory_search_mode mode;
    char query[MEMORY_SEARCH_QUERY_MAX + 1];
    memory_search_pattern pattern;
    uint16_t last_found_address;
    char status[96];
} frontend_memory_search_state;

typedef struct frontend_context_popup_state {
    bool open;
    bool just_opened;
    bool scroll;
    bool group_open;
    struct nk_rect rect;
    struct nk_rect screen_rect;
} frontend_context_popup_state;

enum {
    MEMORY_VIEW_MAX = 16
};

static const struct {
    struct nk_color text;
    struct nk_color bg;
} memory_view_colors[MEMORY_VIEW_MAX] = {
    { { 0xFF, 0xFF, 0xFF, 0xFF }, { 0x00, 0x00, 0x00, 0xFF } },
    { { 0xFF, 0xFF, 0xFF, 0xFF }, { 0x2F, 0x4F, 0x4F, 0xFF } },
    { { 0xFF, 0xFF, 0xFF, 0xFF }, { 0x00, 0x1F, 0x3F, 0xFF } },
    { { 0x00, 0x00, 0x00, 0xFF }, { 0xA9, 0xCC, 0xE3, 0xFF } },
    { { 0xFF, 0xFF, 0xFF, 0xFF }, { 0x2E, 0x8B, 0x57, 0xFF } },
    { { 0x00, 0x00, 0x00, 0xFF }, { 0xFF, 0xFF, 0xE0, 0xFF } },
    { { 0x00, 0x00, 0x00, 0xFF }, { 0xFF, 0xD1, 0xDC, 0xFF } },
    { { 0xFF, 0xFF, 0xFF, 0xFF }, { 0xFF, 0x8C, 0x00, 0xFF } },
    { { 0x00, 0x00, 0x00, 0xFF }, { 0xE0, 0xFF, 0xFF, 0xFF } },
    { { 0xFF, 0xFF, 0xFF, 0xFF }, { 0x5D, 0x3F, 0xD3, 0xFF } },
    { { 0x00, 0x00, 0x00, 0xFF }, { 0x90, 0xEE, 0x90, 0xFF } },
    { { 0xFF, 0xFF, 0xFF, 0xFF }, { 0x80, 0x00, 0x00, 0xFF } },
    { { 0x00, 0x00, 0x00, 0xFF }, { 0xAD, 0xD8, 0xE6, 0xFF } },
    { { 0x00, 0x00, 0x00, 0xFF }, { 0xF0, 0x80, 0x80, 0xFF } },
    { { 0xFF, 0xFF, 0xFF, 0xFF }, { 0x8B, 0x00, 0x00, 0xFF } },
    { { 0x00, 0x00, 0x00, 0xFF }, { 0xFF, 0xFF, 0xFF, 0xFF } }
};

enum {
    FRONTEND_DISASM_MAX_ROWS       = DISASM_PC_LOCK_MAX_ROWS,
    FRONTEND_DISASM_FETCH_BYTES    = RUNTIME_MEMORY_SNAPSHOT_MAX,
    FRONTEND_DISASM_CENTER_LOOKBACK = 32
};

typedef disasm_pc_lock_line frontend_disassembly_line;
typedef disasm_pc_lock_cache frontend_disasm_cache;

enum { FRONTEND_DISASM_MODE_CACHE_COUNT = 6 };

typedef struct frontend_disassembly_view_state {
    uint16_t top_address;
    uint16_t cursor_address;
    uint16_t cursor_prev_address;
    uint16_t pc_lock_address;
    uint16_t requested_address;
    uint16_t requested_length;
    runtime_memory_mode mode;
    runtime_memory_mode requested_mode;
    uint8_t rows;
    uint8_t cursor_row;
    uint8_t cursor_length;
    uint8_t active_address_digit;
    uint16_t last_pc;
    frontend_runtime_state last_runtime_state;
    bool initialized;
    bool request_pending;
    bool active;
    bool address_entry;
    bool follow_pc;
    bool pc_lock_active;
    bool has_last_pc;
    bool has_user_cursor;
    bool has_snapshot;
    bool scrollbar_dragging;
    float scrollbar_grab_offset;
    uint64_t cache_generation;
    runtime_memory_snapshot snapshot;
    frontend_disassembly_line lines[FRONTEND_DISASM_MAX_ROWS];
    frontend_disasm_cache mem_cache[FRONTEND_DISASM_MODE_CACHE_COUNT];
} frontend_disassembly_view_state;

static uint64_t frontend_disassembly_write_history_at(
    const frontend_debug_state *debug_state,
    const frontend_disassembly_view_state *view,
    uint16_t address);
static uint64_t frontend_memory_write_history_at(
    const frontend_debug_state *debug_state,
    const frontend_memory_view_state *view,
    uint16_t address);
static void frontend_context_menu_label(struct nk_context *ctx, const char *label);
static void frontend_context_menu_separator(struct nk_context *ctx);
static void frontend_context_menu_heading(struct nk_context *ctx, const char *label);
static bool frontend_context_menu_item(struct nk_context *ctx, const char *label);
static bool frontend_context_menu_mode_item(
    struct nk_context *ctx,
    bool active,
    const char *label);
static bool frontend_context_menu_access(
    struct nk_context *ctx,
    uint64_t write_history,
    uint16_t *out_address);
static void frontend_context_popup_open(
    frontend *ui,
    frontend_context_popup_state *popup,
    float width,
    float desired_height);
static bool frontend_context_popup_begin(
    frontend *ui,
    frontend_context_popup_state *popup,
    const char *title);
static void frontend_context_popup_end(
    frontend *ui,
    frontend_context_popup_state *popup,
    bool close_popup);
static void frontend_disassembly_navigate_to_address(frontend *ui, uint16_t address);
static void frontend_memory_split_view(frontend *ui, int view_index, bool aligned);
static void frontend_memory_split_at_address(frontend *ui, uint16_t address);
static void frontend_memory_join_view(frontend *ui, int view_index);

typedef enum frontend_misc_tab {
    FRONTEND_MISC_TAB_PROGRAMS = 0,
    FRONTEND_MISC_TAB_DEBUGGER,
    FRONTEND_MISC_TAB_BREAKPOINTS,
    FRONTEND_MISC_TAB_HARDWARE,
    FRONTEND_MISC_TAB_ASSEMBLER,
    FRONTEND_MISC_TAB_INSPECTOR
} frontend_misc_tab;

typedef struct frontend_misc_view_state {
    frontend_misc_tab active_tab;
    bool initialized;
    bool active;
    bool display_override_enabled;
    uint32_t display_override_flags;
    int inspector_slider; /* sample-indexed; maximum min(N-1, 1000) */
    bool inspector_thumb_down;
    bool inspector_land_pending;
    uint64_t inspector_preview_picture_id;
    uint64_t inspector_preview_sample_id;
    uint64_t inspector_preview_ordinal;
    bool inspector_preview_keep_current;
} frontend_misc_view_state;

typedef enum frontend_active_view {
    FRONTEND_ACTIVE_VIEW_NONE = 0,
    FRONTEND_ACTIVE_VIEW_MACHINE,
    FRONTEND_ACTIVE_VIEW_DISASSEMBLY,
    FRONTEND_ACTIVE_VIEW_MISC,
    FRONTEND_ACTIVE_VIEW_MEMORY
} frontend_active_view;

typedef enum frontend_config_tab {
    FRONTEND_CONFIG_TAB_MACHINE = 0,
    FRONTEND_CONFIG_TAB_EMULATOR,
    FRONTEND_CONFIG_TAB_PATHS
} frontend_config_tab;

typedef enum frontend_ini_prompt_state {
    FRONTEND_INI_PROMPT_NONE = 0,
    FRONTEND_INI_PROMPT_EXISTING
} frontend_ini_prompt_state;

typedef struct frontend_config_dialog_state {
    bool open;
    bool initialized;
    frontend_config_tab active_tab;
    frontend_ini_prompt_state prompt;
    app_options original;
    app_options edited;
    char previous_ini_path[1024];
    char error[160];
    int pending_browse_slot; /* Paths tab: slot whose folder a [...] pick targets */
} frontend_config_dialog_state;

typedef enum frontend_breakpoint_dialog_mode {
    FRONTEND_BREAKPOINT_DIALOG_CREATE = 0,
    FRONTEND_BREAKPOINT_DIALOG_EDIT
} frontend_breakpoint_dialog_mode;

typedef struct frontend_breakpoint_dialog_state {
    bool open;
    frontend_breakpoint_dialog_mode mode;
    uint32_t id;
    bool enabled;
    bool execute;
    bool read;
    bool write;
    bool range;
    int ram_mapping;
    int c100_mapping;
    int d000_mapping;
    bool use_counter;
    bool action_break;
    bool action_fast;
    bool action_slow;
    bool action_tron;
    bool action_troff;
    bool action_type;
    bool action_swap;
    char start_address[5];
    char end_address[5];
    char initial_count[11];
    char reset_count[11];
    char tron_path_buf[RUNTIME_BREAKPOINT_TRON_PATH_MAX];
    char swap_slot_buf[2];
    char swap_param_buf[16];
    char type_text_buf[RUNTIME_BREAKPOINT_TYPE_TEXT_MAX];
    char error[96];
} frontend_breakpoint_dialog_state;

/* ---- Symbol lookup dialog ---- */

enum {
    SYMBOL_LOOKUP_ENTRY_MAX  = 4096,
    SYMBOL_LOOKUP_SEARCH_MAX = 128,
    SYMBOL_LOOKUP_COL_MAX    = 15
};

typedef enum frontend_symbol_lookup_sort_col {
    SYMBOL_LOOKUP_SORT_ADDR   = 0,
    SYMBOL_LOOKUP_SORT_SCOPE  = 1,
    SYMBOL_LOOKUP_SORT_LABEL  = 2,
    SYMBOL_LOOKUP_SORT_SOURCE = 3
} frontend_symbol_lookup_sort_col;

typedef struct frontend_symbol_lookup_entry {
    uint16_t address;
    char     scope [SYMBOL_LOOKUP_COL_MAX + 1];
    char     label [SYMBOL_LOOKUP_COL_MAX + 1];
    char     source[SYMBOL_LOOKUP_COL_MAX + 1];
} frontend_symbol_lookup_entry;

typedef struct frontend_symbol_lookup_state {
    bool                            open;
    bool                            from_memory;
    char                            search[SYMBOL_LOOKUP_SEARCH_MAX];
    frontend_symbol_lookup_entry    entries[SYMBOL_LOOKUP_ENTRY_MAX];
    int                             entry_count;
    int                             filtered[SYMBOL_LOOKUP_ENTRY_MAX];
    int                             filtered_count;
    frontend_symbol_lookup_sort_col sort_col;
    bool                            sort_asc;
    int                             selected;
    bool                            table_has_kb_focus;
    bool                            scroll_to_selected;
    bool                            just_opened;
} frontend_symbol_lookup_state;

/* ---- File browser dialog ---- */

typedef struct frontend_file_browser_state {
    bool open;
    frontend_debugger_intent_type purpose;
    char title[64];
    bool save_mode;
    /* "" = no filter; else comma-separated list without dots, e.g. "nib,dsk,woz". */
    char filter_extension[64];
    char default_extension[16];  /* save_mode only; single ext auto-appended if missing */
    uint8_t disk_device;         /* pass-through context for the two disk purposes */
    uint8_t disk_slot;
    runtime_slot_card_type disk_card_type;
    char current_dir[1024];
    char filename[PLATFORM_FS_NAME_MAX + 8];
    platform_fs_listing listing;
    int filtered[PLATFORM_FS_MAX_ENTRIES];
    int filtered_count;
    int selected; /* index into filtered[], -1 = none */
    bool confirm_overwrite;
    char error[160];
    /* Type-ahead incremental search (active when no edit field has focus). */
    char typeahead[PLATFORM_FS_NAME_MAX];
    int typeahead_len;
    uint64_t typeahead_last_ms; /* SDL tick of the last accepted keystroke */
    bool scroll_to_selected;    /* reveal selection, anchored ~1/4 down (type-ahead) */
    bool scroll_ensure_visible; /* reveal selection with minimal scroll (key nav) */
    float list_visible_h;       /* measured inner height of the list group, in px */
    int browse_slot;            /* frontend_browse_slot for this session, -1 = none */
    bool pick_dir;              /* folder-select mode: commit the current directory */
    /* SmartPort Insert: also offer Use This Folder beside Open (HostFS). */
    bool allow_folder_mount;
} frontend_file_browser_state;

struct frontend {
    platform_window *window;
    struct nk_context *ctx;
    SDL_Renderer *renderer;
    struct nk_font *help_font;
    SDL_Texture *display_texture;
    SDL_Texture *crt_texture;
    uint32_t *display_pixels;
    uint32_t *crt_pixels;
    bool crt_texture_valid;
    SDL_Texture *led_green_texture;
    SDL_Texture *led_red_texture;
    /* Disk LEDs: green = Disk II motor-on (level); red = write activity hold. */
    bool show_disk_leds;
    bool true_aspect;
    bool crt_smoothing;
    frontend_crt_effects crt_effects;
    bool video_display_preview_valid;
    bool video_display_preview_colour;
    app_mono_mode video_display_preview_mono;
    uint32_t disk_led_seen_write_seq[2];
    uint64_t disk_led_write_until_ms;
    display_frame current_frame;
    bool has_frame;
    debugger_layout layout;
    debugger_layout_limits limits;
    frontend_active_view active_view;
    bool machine_input_active;
    bool input_focus_initialized;
    frontend_register_view_state registers;
    frontend_memory_view_state memory_views[MEMORY_VIEW_MAX];
    frontend_memory_search_state memory_search;
    int memory_view_count;
    int memory_active_view_index;
    int memory_context_menu_view_index;
    uint16_t memory_context_menu_address;
    frontend_context_popup_state memory_context_popup;
    frontend_context_popup_state disassembly_context_popup;
    bool memory_color_slot_used[MEMORY_VIEW_MAX];
    frontend_disassembly_view_state disassembly;
    frontend_misc_view_state misc;
    struct {
        bool open;
        bool unmount_cartridge;
        bool resume_running;
    } reset_prompt;
    frontend_config_dialog_state config_dialog;
    frontend_breakpoint_dialog_state breakpoint_dialog;
    frontend_assembler_state assembler;
    frontend_load_bin_dialog_state load_bin_dialog;
    frontend_save_bin_dialog_state save_bin_dialog;
    frontend_help_state help;
    frontend_forensics_state forensics;
    frontend_symbol_lookup_state symbol_lookup;
    frontend_file_browser_state file_browser;
    /* Remembered default folder per browse slot (session memory; main.c bridges
       these to the INI). Empty string means "unset" -> fall back to cwd. */
    char browse_dirs[FRONTEND_BROWSE_SLOT_COUNT][1024];
    symbol_resolver symbols;
    symbol_table *symbol_table;
    app_disk_slot disk_queue[2]; /* mirrors options->disk_slots[0]/[1] (d0/d1) */
    frontend_debugger_intent intents[FRONTEND_DEBUGGER_INTENT_CAPACITY];
    size_t intent_read;
    size_t intent_write;
    bool cancel_register_edit_requested;
    SDL_KeyboardEvent pending_memory_key;
    bool has_pending_memory_key;
    SDL_KeyboardEvent pending_disassembly_key;
    bool has_pending_disassembly_key;
    bool debug_memory_request_pending;
    uint64_t debug_memory_seen_generation;
    bool debug_memory_request_write_history;
    struct nk_rect memory_scrollbar_bounds;
    bool has_memory_scrollbar_bounds;
    struct nk_rect disassembly_scrollbar_bounds;
    bool has_disassembly_scrollbar_bounds;
};

static const nk_flags pane_flags = NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_NO_SCROLLBAR;

static SDL_Texture *frontend_load_png_texture(
    SDL_Renderer *renderer,
    const unsigned char *png,
    unsigned int png_len)
{
    int width = 0;
    int height = 0;
    int components = 0;
    unsigned char *pixels;
    SDL_Texture *texture;

    if (renderer == NULL || png == NULL || png_len == 0u) {
        return NULL;
    }

    pixels = stbi_load_from_memory(
        png,
        (int)png_len,
        &width,
        &height,
        &components,
        4);
    if (pixels == NULL || width <= 0 || height <= 0) {
        return NULL;
    }

    texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGBA32,
        SDL_TEXTUREACCESS_STATIC,
        width,
        height);
    if (texture == NULL) {
        stbi_image_free(pixels);
        return NULL;
    }

    if (SDL_UpdateTexture(texture, NULL, pixels, width * 4) != 0) {
        SDL_DestroyTexture(texture);
        stbi_image_free(pixels);
        return NULL;
    }

    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    stbi_image_free(pixels);
    return texture;
}

void frontend_clear_disk_activity_leds(frontend *ui)
{
    if (ui == NULL) {
        return;
    }
    ui->disk_led_seen_write_seq[0] = 0;
    ui->disk_led_seen_write_seq[1] = 0;
    ui->disk_led_write_until_ms = 0;
}

/* Green LED follows Disk II motor soft-switch (drive enable / spindle on).
   Real Disk II drive LEDs track drive enable, which is motor+select; we show
   motor-on for any attached slot. Red remains a sticky write-activity hold
   when write events are wired. */
static bool frontend_disk_motor_active(const frontend_debug_state *debug_state)
{
    return debug_state != NULL && debug_state->disk_motor_mask != 0u;
}

/* Small clickable power LED using the same PNG assets as corner activity LEDs.
   Returns true when the LED was clicked. Draws green when powered, red when off. */
static bool frontend_draw_power_led_button(frontend *ui, bool powered)
{
    struct nk_context *ctx;
    struct nk_rect cell;
    struct nk_rect led;
    struct nk_command_buffer *canvas;
    SDL_Texture *tex;
    int tw = 0;
    int th = 0;
    float size;
    enum nk_widget_layout_states state;
    bool clicked = false;

    if (ui == NULL || ui->ctx == NULL) {
        return false;
    }
    ctx = ui->ctx;
    state = nk_widget(&cell, ctx);
    if (state == NK_WIDGET_INVALID) {
        return false;
    }

    tex = powered ? ui->led_green_texture : ui->led_red_texture;
    size = (float)FRONTEND_DISK_LED_SIZE;
    if (size > cell.h - 2.0f) {
        size = cell.h - 2.0f;
    }
    if (size > cell.w - 2.0f) {
        size = cell.w - 2.0f;
    }
    if (size < 8.0f) {
        size = 8.0f;
    }
    led = nk_rect(
        cell.x + (cell.w - size) * 0.5f,
        cell.y + (cell.h - size) * 0.5f,
        size,
        size);

    canvas = nk_window_get_canvas(ctx);
    if (canvas != NULL && tex != NULL &&
        SDL_QueryTexture(tex, NULL, NULL, &tw, &th) == 0 && tw > 0 && th > 0) {
        struct nk_image image = nk_subimage_handle(
            nk_handle_ptr(tex),
            (nk_ushort)tw,
            (nk_ushort)th,
            nk_rect(0.0f, 0.0f, (float)tw, (float)th));
        nk_draw_image(canvas, led, &image, nk_rgba(255, 255, 255, 255));
    } else if (canvas != NULL) {
        /* Fallback if textures failed to load. */
        nk_fill_circle(
            canvas,
            led,
            powered ? nk_rgb(40, 200, 70) : nk_rgb(200, 45, 45));
    }

    if (state == NK_WIDGET_VALID &&
        nk_input_has_mouse_click_in_rect(&ctx->input, NK_BUTTON_LEFT, cell) &&
        nk_input_is_mouse_released(&ctx->input, NK_BUTTON_LEFT)) {
        clicked = true;
    }
    return clicked;
}

static void frontend_draw_disk_activity_leds(
    frontend *ui,
    int width,
    int height,
    const frontend_debug_state *debug_state)
{
    bool motor_active = false;
    bool write_active = false;
    uint64_t now;
    SDL_Rect dest;

    if (ui == NULL || ui->renderer == NULL || !ui->show_disk_leds) {
        return;
    }
    if (width < FRONTEND_DISK_LED_SIZE || height < FRONTEND_DISK_LED_SIZE) {
        return;
    }

    now = SDL_GetTicks64();
    /* Green: level — stays lit while any Disk II motor soft-switch is on. */
    motor_active = frontend_disk_motor_active(debug_state);
    /* Red: host-time hold (write activity when wired); expires while paused. */
    write_active = (ui->disk_led_write_until_ms > now);
    /* Prefer exclusive colours when both overlap (write wins for red). */
    if (write_active) {
        motor_active = false;
    }

    if (write_active && ui->led_red_texture != NULL) {
        dest.x = width - (FRONTEND_DISK_LED_SIZE * 2);
        dest.y = height - FRONTEND_DISK_LED_SIZE;
        dest.w = FRONTEND_DISK_LED_SIZE;
        dest.h = FRONTEND_DISK_LED_SIZE;
        SDL_RenderCopy(ui->renderer, ui->led_red_texture, NULL, &dest);
    }

    if (motor_active && ui->led_green_texture != NULL) {
        dest.x = width - FRONTEND_DISK_LED_SIZE;
        dest.y = height - FRONTEND_DISK_LED_SIZE;
        dest.w = FRONTEND_DISK_LED_SIZE;
        dest.h = FRONTEND_DISK_LED_SIZE;
        SDL_RenderCopy(ui->renderer, ui->led_green_texture, NULL, &dest);
    }
}

static bool frontend_any_dialog_open(const frontend *ui)
{
    return ui->config_dialog.open
        || ui->breakpoint_dialog.open
        || ui->load_bin_dialog.open
        || ui->save_bin_dialog.open
        || ui->assembler.error_dialog_open
        || ui->memory_search.open
        || ui->symbol_lookup.open
        || ui->file_browser.open;
}

static void frontend_set_active_view(frontend *ui, frontend_active_view view)
{
    if (ui == NULL) {
        return;
    }

    ui->active_view = view;
    ui->machine_input_active = view == FRONTEND_ACTIVE_VIEW_MACHINE;
    ui->disassembly.active = view == FRONTEND_ACTIVE_VIEW_DISASSEMBLY;
    ui->misc.active = view == FRONTEND_ACTIVE_VIEW_MISC;
}

static frontend_active_view frontend_next_active_view(frontend_active_view view, bool reverse)
{
    static const frontend_active_view cycle[] = {
        FRONTEND_ACTIVE_VIEW_MACHINE,
        FRONTEND_ACTIVE_VIEW_DISASSEMBLY,
        FRONTEND_ACTIVE_VIEW_MISC,
        FRONTEND_ACTIVE_VIEW_MEMORY
    };
    size_t count = sizeof(cycle) / sizeof(cycle[0]);
    size_t i;

    for (i = 0; i < count; ++i) {
        if (cycle[i] == view) {
            return reverse ? cycle[(i + count - 1u) % count] : cycle[(i + 1u) % count];
        }
    }

    return reverse ? cycle[count - 1u] : cycle[0];
}

static void frontend_draw_view_border(struct nk_context *ctx, struct nk_color color, float inset, float thickness)
{
    struct nk_command_buffer *canvas;
    struct nk_rect content;

    if (ctx == NULL) {
        return;
    }

    canvas = nk_window_get_canvas(ctx);
    content = nk_window_get_content_region(ctx);
    if (content.w <= inset * 2.0f || content.h <= inset * 2.0f) {
        return;
    }

    nk_stroke_rect(
        canvas,
        nk_rect(content.x + inset, content.y + inset, content.w - inset * 2.0f, content.h - inset * 2.0f),
        0.0f,
        thickness,
        color);
}

static void frontend_draw_active_view_border(struct nk_context *ctx)
{
    frontend_draw_view_border(ctx, nk_rgb(188, 198, 190), 1.0f, 2.0f);
}

static bool frontend_memory_mode_is_drive(runtime_memory_mode mode)
{
    (void)mode;
    return false; /* Apple areas are all host-side views; all editable when paused */
}

static bool frontend_memory_mode_is_editable(runtime_memory_mode mode)
{
    (void)mode;
    return true;
}

static const char *frontend_memory_mode_label(runtime_memory_mode mode)
{
    switch (mode) {
    case RUNTIME_MEMORY_MODE_MAIN:
        return "Main";
    case RUNTIME_MEMORY_MODE_AUX:
        return "Aux";
    case RUNTIME_MEMORY_MODE_LC1:
        return "LC1";
    case RUNTIME_MEMORY_MODE_LC2:
        return "LC2";
    case RUNTIME_MEMORY_MODE_ROM:
        return "ROM";
    case RUNTIME_MEMORY_MODE_MAP:
    default:
        return "Map";
    }
}

/* Opt+M / cycle: Map → Main → Aux → LC1 → LC2 → ROM → Map */
static runtime_memory_mode frontend_memory_next_mode(runtime_memory_mode mode)
{
    switch (mode) {
    case RUNTIME_MEMORY_MODE_MAP:
        return RUNTIME_MEMORY_MODE_MAIN;
    case RUNTIME_MEMORY_MODE_MAIN:
        return RUNTIME_MEMORY_MODE_AUX;
    case RUNTIME_MEMORY_MODE_AUX:
        return RUNTIME_MEMORY_MODE_LC1;
    case RUNTIME_MEMORY_MODE_LC1:
        return RUNTIME_MEMORY_MODE_LC2;
    case RUNTIME_MEMORY_MODE_LC2:
        return RUNTIME_MEMORY_MODE_ROM;
    case RUNTIME_MEMORY_MODE_ROM:
    default:
        return RUNTIME_MEMORY_MODE_MAP;
    }
}

static struct nk_color frontend_memory_mode_border_color(runtime_memory_mode mode)
{
    switch (mode) {
    case RUNTIME_MEMORY_MODE_MAIN:
        return nk_rgb(60, 120, 200);
    case RUNTIME_MEMORY_MODE_AUX:
        return nk_rgb(80, 160, 120);
    case RUNTIME_MEMORY_MODE_LC1:
        return nk_rgb(160, 100, 180);
    case RUNTIME_MEMORY_MODE_LC2:
        return nk_rgb(180, 80, 140);
    case RUNTIME_MEMORY_MODE_ROM:
        return nk_rgb(200, 130, 40);
    case RUNTIME_MEMORY_MODE_MAP:
    default:
        return nk_rgb(188, 198, 190);
    }
}

static void frontend_draw_memory_mode_border(struct nk_context *ctx, runtime_memory_mode mode, bool view_active)
{
    if (mode == RUNTIME_MEMORY_MODE_MAP) {
        return;
    }

    frontend_draw_view_border(
        ctx,
        frontend_memory_mode_border_color(mode),
        view_active ? 4.0f : 1.0f,
        2.0f);
}

/* Shared Source menu items for memory + disasm context menus. */
static bool frontend_context_menu_source_modes(
    frontend *ui,
    runtime_memory_mode *mode_io,
    bool *close_popup)
{
    static const runtime_memory_mode modes[] = {
        RUNTIME_MEMORY_MODE_MAP,
        RUNTIME_MEMORY_MODE_MAIN,
        RUNTIME_MEMORY_MODE_AUX,
        RUNTIME_MEMORY_MODE_LC1,
        RUNTIME_MEMORY_MODE_LC2,
        RUNTIME_MEMORY_MODE_ROM
    };
    size_t i;
    bool changed = false;

    frontend_context_menu_heading(ui->ctx, "Source");
    for (i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
        if (frontend_context_menu_mode_item(
                ui->ctx,
                *mode_io == modes[i],
                frontend_memory_mode_label(modes[i]))) {
            *mode_io = modes[i];
            *close_popup = true;
            changed = true;
        }
    }
    return changed;
}

static SDL_Rect frontend_fit_rect(int area_x, int area_y, int area_w, int area_h, int source_w, int source_h)
{
    SDL_Rect out = {area_x, area_y, 0, 0};
    int width_from_height;
    int height_from_width;

    if (area_w <= 0 || area_h <= 0 || source_w <= 0 || source_h <= 0) {
        return out;
    }

    height_from_width = area_w * source_h / source_w;
    if (height_from_width <= area_h) {
        out.w = area_w;
        out.h = height_from_width;
    } else {
        width_from_height = area_h * source_w / source_h;
        out.w = width_from_height;
        out.h = area_h;
    }

    out.x = area_x + (area_w - out.w) / 2;
    out.y = area_y + (area_h - out.h) / 2;
    return out;
}

static struct nk_rect frontend_fit_nk_rect(
    struct nk_rect area,
    uint32_t source_w,
    uint32_t source_h)
{
    SDL_Rect fit = frontend_fit_rect(
        (int)area.x,
        (int)area.y,
        (int)area.w,
        (int)area.h,
        (int)source_w,
        (int)source_h);

    return nk_rect((float)fit.x, (float)fit.y, (float)fit.w, (float)fit.h);
}

/* Apple product path: always the full active display, never a VIC border crop. */
static int frontend_display_crop_x_for_frame(const display_frame *frame)
{
    (void)frame;
    return 0;
}

static int frontend_display_crop_y_for_frame(const display_frame *frame)
{
    (void)frame;
    return 0;
}

static int frontend_display_crop_w_for_frame(const display_frame *frame)
{
    if (frame != NULL && display_frame_is_valid(frame)) {
        return (int)frame->width;
    }
    return FRONTEND_DISPLAY_WIDTH;
}

static int frontend_display_crop_h_for_frame(const display_frame *frame)
{
    if (frame != NULL && display_frame_is_valid(frame)) {
        return (int)frame->height;
    }
    return FRONTEND_DISPLAY_HEIGHT;
}

/* Host pixel aspect: double-res ARGB pixels are half-width in true-aspect math. */
static float frontend_display_par_for_frame(const display_frame *frame)
{
    (void)frame;
    return (float)FRONTEND_DISPLAY_PAR_FIXED / (float)FRONTEND_DISPLAY_ASPECT_FIXED;
}

/* Source dimensions for fit/snap, in FRONTEND_DISPLAY_ASPECT_FIXED units. */
static void frontend_display_fit_source(const frontend *ui, int *out_w, int *out_h)
{
    const display_frame *frame = (ui != NULL && ui->has_frame) ? &ui->current_frame : NULL;

    *out_h = frontend_display_crop_h_for_frame(frame) * FRONTEND_DISPLAY_ASPECT_FIXED;
    *out_w = (int)lroundf((float)frontend_display_crop_w_for_frame(frame) *
        (float)FRONTEND_DISPLAY_ASPECT_FIXED *
        frontend_display_par_for_frame(frame));
}

/* True Aspect Ratio off means "don't correct anything, just fill the view" - the
   picture stretches to whatever the user made the pane. That is a deliberate
   choice, not an approximation of hardware: the viewer decides how they want to
   look at the machine, and the aspect snap is one click away if they want the true
   geometry back. Do not substitute a square-pixel aspect here; the display never
   maps 1:1 to screen pixels anyway, so that would just be a second wrong shape
   with no one asking for it. */
static bool frontend_display_fills_view(const frontend *ui)
{
    return ui != NULL && !ui->true_aspect;
}

static float frontend_display_aspect(const frontend *ui)
{
    int source_w = 0;
    int source_h = 0;

    frontend_display_fit_source(ui, &source_w, &source_h);
    return (float)source_w / (float)source_h;
}

static bool frontend_crt_effects_enabled(const frontend *ui)
{
    return ui != NULL && (ui->crt_effects.scanlines || ui->crt_effects.curvature);
}

/* Smoothing is pure presentation - a texture filter, not a pass. It needs no CPU
   work at all, so it deliberately does NOT gate frontend_crt_effects_enabled().
   Scanlines and curvature force it on because they are drawn through crt_texture,
   which must be filtered: their output carries a 1-pixel pattern at the render
   buffer's Nyquist limit, and point-sampling that to an arbitrary pane size beats
   into moire. So "smooth" is implied by those two rather than combinable with
   them - crisp scanlines are not reachable without generating them at output
   resolution. */
static bool frontend_display_smoothed(const frontend *ui)
{
    return ui != NULL && (ui->crt_smoothing || frontend_crt_effects_enabled(ui));
}

/* The unfiltered path draws display_texture, whose scale mode SDL defaults to
   Nearest. Smoothing without scanlines or curvature is therefore just this one
   flag - no crt_process, no second texture, no per-frame cost. */
static void frontend_apply_display_filter(frontend *ui)
{
    if (ui == NULL || ui->display_texture == NULL) {
        return;
    }
    SDL_SetTextureScaleMode(
        ui->display_texture,
        frontend_display_smoothed(ui) ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
}

static SDL_Texture *frontend_display_texture_for_render(const frontend *ui)
{
    if (frontend_crt_effects_enabled(ui) && ui->crt_texture != NULL &&
        ui->crt_texture_valid) {
        return ui->crt_texture;
    }
    return ui != NULL ? ui->display_texture : NULL;
}

/* Pitch of display_pixels rows (tight ARGB pack at machine width). */
static int frontend_display_pixels_pitch(const frontend *ui)
{
    if (ui != NULL && display_frame_is_valid(&ui->current_frame)) {
        return (int)ui->current_frame.width;
    }
    return FRONTEND_DISPLAY_WIDTH;
}

/* Height of display_pixels / CRT intermediate buffer. */
static int frontend_display_pixels_height(const frontend *ui)
{
    if (ui != NULL && display_frame_is_valid(&ui->current_frame)) {
        return (int)ui->current_frame.height;
    }
    return FRONTEND_DISPLAY_HEIGHT;
}

static bool frontend_update_crt_texture(frontend *ui)
{
    int crop_y;
    int pitch_w;
    int frame_h;
    int tex_w;
    int tex_h;
    size_t nbytes;

    if (ui == NULL || !ui->has_frame || !frontend_crt_effects_enabled(ui)) {
        return true;
    }
    ui->crt_texture_valid = false;

    pitch_w = frontend_display_pixels_pitch(ui);
    frame_h = frontend_display_pixels_height(ui);
    tex_w = pitch_w * FRONTEND_CRT_RENDER_SCALE;
    tex_h = frame_h * FRONTEND_CRT_RENDER_SCALE;
    nbytes = (size_t)tex_w * (size_t)tex_h * sizeof(*ui->crt_pixels);

    if (ui->crt_texture != NULL) {
        int cur_w = 0;
        int cur_h = 0;
        if (SDL_QueryTexture(ui->crt_texture, NULL, NULL, &cur_w, &cur_h) != 0 ||
            cur_w != tex_w || cur_h != tex_h) {
            SDL_DestroyTexture(ui->crt_texture);
            ui->crt_texture = NULL;
            free(ui->crt_pixels);
            ui->crt_pixels = NULL;
        }
    }

    if (ui->crt_pixels == NULL) {
        ui->crt_pixels = malloc(nbytes);
        if (ui->crt_pixels == NULL) {
            return false;
        }
    }
    if (ui->crt_texture == NULL) {
        ui->crt_texture = SDL_CreateTexture(
            ui->renderer,
            SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_STREAMING,
            tex_w,
            tex_h);
        if (ui->crt_texture == NULL) {
            log_error("SDL_CreateTexture for CRT output failed: %s", SDL_GetError());
            return false;
        }
        SDL_SetTextureBlendMode(ui->crt_texture, SDL_BLENDMODE_NONE);
    }
    /* Always Linear, never Nearest. Scanlines are a 1-pixel alternating pattern
       baked into this FRONTEND_CRT_RENDER_SCALE buffer - i.e. sitting right at
       its Nyquist limit - and the buffer is then presented at whatever
       non-integer scale the pane happens to be. Nearest-sampling that beats
       against the output grid and shreds the scanlines into uneven moiré bands;
       Linear averages the rows each output pixel straddles and reproduces them
       evenly. Keying this off curvature (as it once did) meant curvature was
       secretly the only thing enabling the filter, so scanlines alone looked
       broken. Nothing here affects the crisp path: with no CRT effects the
       renderer draws display_texture instead, which keeps SDL's default
       Nearest. */
    SDL_SetTextureScaleMode(ui->crt_texture, SDL_ScaleModeLinear);

    crop_y = frontend_display_crop_y_for_frame(&ui->current_frame);
    frontend_crt_process(
        ui->display_pixels,
        ui->crt_pixels,
        pitch_w,
        frame_h,
        frontend_display_crop_x_for_frame(&ui->current_frame),
        crop_y,
        frontend_display_crop_w_for_frame(&ui->current_frame),
        frontend_display_crop_h_for_frame(&ui->current_frame),
        FRONTEND_CRT_RENDER_SCALE,
        FRONTEND_DISPLAY_MATTE_ARGB,
        &ui->crt_effects);
    if (SDL_UpdateTexture(ui->crt_texture, NULL, ui->crt_pixels,
            tex_w * (int)sizeof(*ui->crt_pixels)) != 0) {
        log_error("SDL_UpdateTexture for CRT output failed: %s", SDL_GetError());
        return false;
    }
    ui->crt_texture_valid = true;
    return true;
}

static void frontend_preview_crt_options(frontend *ui, const app_options *options)
{
    bool effects_changed;

    if (ui == NULL || options == NULL) {
        return;
    }

    effects_changed =
        ui->crt_effects.scanlines != options->crt_scanlines ||
        ui->crt_effects.scanline_strength != options->crt_scanline_strength ||
        ui->crt_effects.curvature != options->crt_curvature ||
        ui->crt_effects.curvature_amount != options->crt_curvature_amount;
    ui->true_aspect = options->true_aspect;
    ui->crt_smoothing = options->crt_smoothing;
    ui->crt_effects.scanlines = options->crt_scanlines;
    ui->crt_effects.scanline_strength = options->crt_scanline_strength;
    ui->crt_effects.curvature = options->crt_curvature;
    ui->crt_effects.curvature_amount = options->crt_curvature_amount;
    ui->layout.display_aspect = frontend_display_aspect(ui);
    frontend_apply_display_filter(ui);

    if (effects_changed && !frontend_update_crt_texture(ui)) {
        log_warn("frontend: could not refresh CRT output");
    }
}

static bool frontend_point_in_rect(float x, float y, struct nk_rect rect)
{
    return x >= rect.x && x < rect.x + rect.w &&
        y >= rect.y && y < rect.y + rect.h;
}

static bool frontend_click_in_any_dialog(const frontend *ui, float x, float y)
{
    struct nk_window *win;

    if (ui->config_dialog.open) {
        win = nk_window_find(ui->ctx, "Configure");
        if (win && frontend_point_in_rect(x, y, win->bounds)) {
            return true;
        }
    }
    if (ui->breakpoint_dialog.open) {
        win = nk_window_find(ui->ctx, "Breakpoint Editor");
        if (win && frontend_point_in_rect(x, y, win->bounds)) {
            return true;
        }
    }
    if (ui->load_bin_dialog.open) {
        win = nk_window_find(ui->ctx, "Load");
        if (win && frontend_point_in_rect(x, y, win->bounds)) {
            return true;
        }
    }
    if (ui->save_bin_dialog.open) {
        win = nk_window_find(ui->ctx, "Save");
        if (win && frontend_point_in_rect(x, y, win->bounds)) {
            return true;
        }
    }
    if (ui->assembler.error_dialog_open) {
        win = nk_window_find(
            ui->ctx,
            ui->assembler.error_dialog_is_notice ?
                "Assembly Adjustments" : "Assembly Errors");
        if (win && frontend_point_in_rect(x, y, win->bounds)) {
            return true;
        }
    }
    if (ui->memory_search.open) {
        win = nk_window_find(ui->ctx, "Find Memory");
        if (win && frontend_point_in_rect(x, y, win->bounds)) {
            return true;
        }
    }
    if (ui->symbol_lookup.open) {
        win = nk_window_find(ui->ctx, "Symbol Lookup");
        if (win && frontend_point_in_rect(x, y, win->bounds)) {
            return true;
        }
    }
    if (ui->file_browser.open) {
        win = nk_window_find(ui->ctx, "File Browser");
        if (win && frontend_point_in_rect(x, y, win->bounds)) {
            return true;
        }
    }
    return false;
}

const char *frontend_runtime_state_name(frontend_runtime_state state)
{
    switch (state) {
        case FRONTEND_RUNTIME_STATE_RUNNING:
            return "RUNNING";
        case FRONTEND_RUNTIME_STATE_PAUSED:
            return "PAUSED";
        case FRONTEND_RUNTIME_STATE_ERROR:
            return "ERROR";
        case FRONTEND_RUNTIME_STATE_UNKNOWN:
        default:
            return "UNKNOWN";
    }
}

const char *frontend_stop_reason_name(runtime_stop_reason reason)
{
    switch (reason) {
        case RUNTIME_STOP_REASON_RESET:
            return "reset";
        case RUNTIME_STOP_REASON_PAUSE_COMMAND:
            return "pause";
        case RUNTIME_STOP_REASON_STEP:
            return "step";
        case RUNTIME_STOP_REASON_RUN_COMPLETE:
            return "run complete";
        case RUNTIME_STOP_REASON_BREAKPOINT:
            return "breakpoint";
        case RUNTIME_STOP_REASON_BRK:
            return "BRK";
        case RUNTIME_STOP_REASON_ERROR:
            return "error";
        case RUNTIME_STOP_REASON_NONE:
        default:
            return "none";
    }
}

static void frontend_push_debugger_intent(
    frontend *ui,
    frontend_debugger_intent_type type,
    uint16_t value);

static bool frontend_push_assemble_run_intent(
    frontend *ui,
    const char *path,
    uint16_t address,
    uint16_t run_address,
    bool auto_run,
    bool mli_launch,
    bool reset_first,
    bool rearm_oneshots);

static void frontend_draw_assembler_error_dialog(frontend *ui, int width, int height);
static void frontend_draw_symbol_lookup(frontend *ui, int width, int height);
static void frontend_open_symbol_lookup(frontend *ui, bool from_memory);
static void frontend_symbol_lookup_commit(frontend *ui);
static void frontend_draw_file_browser(frontend *ui, int width, int height);
static bool frontend_push_load_bin_execute_intent(
    frontend *ui,
    const char *path,
    uint16_t address,
    frontend_machine_file_kind kind,
    apple2_binary_format format,
    bool reset_first,
    bool run_after_load);
static bool frontend_commit_load_bin_dialog(frontend *ui);

static void frontend_push_breakpoint_id_intent(
    frontend *ui,
    frontend_debugger_intent_type type,
    uint32_t id,
    bool enabled);

static void frontend_push_breakpoint_definition_intent(
    frontend *ui,
    frontend_debugger_intent_type type,
    uint32_t id,
    const runtime_breakpoint_definition *definition);

static const runtime_breakpoint_snapshot_entry *frontend_find_execute_breakpoint(
    const frontend_debug_state *debug_state,
    uint16_t address)
{
    uint16_t i;

    if (debug_state == NULL || !debug_state->has_breakpoints) {
        return NULL;
    }

    for (i = 0; i < debug_state->breakpoints.count; ++i) {
        const runtime_breakpoint_snapshot_entry *entry = &debug_state->breakpoints.entries[i];
        if ((entry->access & RUNTIME_BREAKPOINT_ACCESS_EXECUTE) != 0 && entry->address == address) {
            return entry;
        }
    }

    return NULL;
}

static void frontend_push_video_display_intent(
    frontend *ui,
    bool colour,
    app_mono_mode mono_mode)
{
    size_t next;

    if (ui == NULL) {
        return;
    }
    next = (ui->intent_write + 1u) % FRONTEND_DEBUGGER_INTENT_CAPACITY;
    if (next == ui->intent_read) {
        return;
    }
    memset(&ui->intents[ui->intent_write], 0, sizeof(ui->intents[ui->intent_write]));
    ui->intents[ui->intent_write].type =
        FRONTEND_DEBUGGER_INTENT_SET_VIDEO_DISPLAY;
    ui->intents[ui->intent_write].enabled = colour;
    ui->intents[ui->intent_write].value = (uint16_t)mono_mode;
    ui->intent_write = next;
}

static void frontend_preview_video_display(frontend *ui, const app_options *options)
{
    if (ui == NULL || options == NULL) {
        return;
    }
    if (ui->video_display_preview_valid &&
        ui->video_display_preview_colour == options->colour_display &&
        ui->video_display_preview_mono == options->mono_mode) {
        return;
    }
    ui->video_display_preview_valid = true;
    ui->video_display_preview_colour = options->colour_display;
    ui->video_display_preview_mono = options->mono_mode;
    frontend_push_video_display_intent(
        ui, options->colour_display, options->mono_mode);
}

static void frontend_push_display_override_intent(frontend *ui)
{
    size_t next;

    if (ui == NULL) {
        return;
    }
    next = (ui->intent_write + 1u) % FRONTEND_DEBUGGER_INTENT_CAPACITY;
    if (next == ui->intent_read) {
        return;
    }
    memset(&ui->intents[ui->intent_write], 0, sizeof(ui->intents[ui->intent_write]));
    ui->intents[ui->intent_write].type =
        FRONTEND_DEBUGGER_INTENT_SET_DISPLAY_OVERRIDE;
    ui->intents[ui->intent_write].enabled = ui->misc.display_override_enabled;
    ui->intents[ui->intent_write].display_override_flags =
        ui->misc.display_override_flags;
    ui->intent_write = next;
}

static void frontend_toggle_execute_breakpoint_at_cursor(
    frontend *ui,
    const frontend_debug_state *debug_state)
{
    const runtime_breakpoint_snapshot_entry *entry;
    uint16_t address;

    if (ui == NULL || !ui->disassembly.has_user_cursor) {
        return;
    }

    address = ui->disassembly.cursor_address;
    entry = frontend_find_execute_breakpoint(debug_state, address);
    if (entry != NULL) {
        frontend_push_breakpoint_id_intent(
            ui,
            FRONTEND_DEBUGGER_INTENT_BREAKPOINT_CLEAR,
            entry->id,
            false);
        return;
    }

    frontend_push_debugger_intent(ui, FRONTEND_DEBUGGER_INTENT_BREAKPOINT_SET_EXECUTE, address);
}

static bool frontend_parse_hex16_text(const char *text, uint16_t *out)
{
    char *end;
    unsigned long value;

    if (text == NULL || *text == '\0') {
        return false;
    }

    value = strtoul(text, &end, 16);
    if (end == text || *end != '\0' || value > 0xfffful) {
        return false;
    }

    *out = (uint16_t)value;
    return true;
}

static bool frontend_parse_u32_text(const char *text, uint32_t *out)
{
    char *end;
    unsigned long value;

    if (text == NULL || *text == '\0' || *text == '-') {
        return false;
    }

    value = strtoul(text, &end, 10);
    if (end == text || *end != '\0' || value > 0xfffffffful) {
        return false;
    }

    *out = (uint32_t)value;
    return true;
}

static void frontend_open_breakpoint_dialog_default(frontend *ui)
{
    frontend_breakpoint_dialog_state *dialog;

    if (ui == NULL) {
        return;
    }

    dialog = &ui->breakpoint_dialog;
    memset(dialog, 0, sizeof(*dialog));
    dialog->open = true;
    dialog->mode = FRONTEND_BREAKPOINT_DIALOG_CREATE;
    dialog->enabled = true;
    dialog->execute = true;
    dialog->action_break = true;
    snprintf(dialog->start_address, sizeof(dialog->start_address), "%04X", ui->disassembly.cursor_address);
    snprintf(dialog->end_address, sizeof(dialog->end_address), "%04X", ui->disassembly.cursor_address);
    snprintf(dialog->initial_count, sizeof(dialog->initial_count), "0");
    snprintf(dialog->reset_count, sizeof(dialog->reset_count), "1");
    snprintf(dialog->swap_slot_buf, sizeof(dialog->swap_slot_buf), "6");
}

static void frontend_open_breakpoint_dialog_from_entry(
    frontend *ui,
    const runtime_breakpoint_snapshot_entry *entry,
    bool duplicate)
{
    frontend_breakpoint_dialog_state *dialog;

    if (ui == NULL || entry == NULL) {
        return;
    }

    dialog = &ui->breakpoint_dialog;
    memset(dialog, 0, sizeof(*dialog));
    dialog->open = true;
    dialog->mode = duplicate ? FRONTEND_BREAKPOINT_DIALOG_CREATE : FRONTEND_BREAKPOINT_DIALOG_EDIT;
    dialog->id = duplicate ? 0 : entry->id;
    dialog->enabled = entry->enabled != 0;
    dialog->execute = (entry->access & RUNTIME_BREAKPOINT_ACCESS_EXECUTE) != 0;
    dialog->read = (entry->access & RUNTIME_BREAKPOINT_ACCESS_READ) != 0;
    dialog->write = (entry->access & RUNTIME_BREAKPOINT_ACCESS_WRITE) != 0;
    dialog->range = entry->has_end_address != 0;
    dialog->ram_mapping = vf_get_ram(entry->mapping);
    dialog->c100_mapping = vf_get_c100(entry->mapping);
    dialog->d000_mapping = vf_get_d000(entry->mapping);
    dialog->use_counter = entry->use_counter != 0;
    dialog->action_break = (entry->actions & RUNTIME_BREAKPOINT_ACTION_BREAK) != 0;
    dialog->action_fast = (entry->actions & RUNTIME_BREAKPOINT_ACTION_FAST) != 0;
    dialog->action_slow = (entry->actions & RUNTIME_BREAKPOINT_ACTION_SLOW) != 0;
    dialog->action_tron = (entry->actions & RUNTIME_BREAKPOINT_ACTION_TRON) != 0;
    dialog->action_troff = (entry->actions & RUNTIME_BREAKPOINT_ACTION_TROFF) != 0;
    dialog->action_type = (entry->actions & RUNTIME_BREAKPOINT_ACTION_TYPE) != 0;
    dialog->action_swap = (entry->actions & RUNTIME_BREAKPOINT_ACTION_SWAP) != 0;
    snprintf(dialog->start_address, sizeof(dialog->start_address), "%04X", entry->start_address);
    snprintf(dialog->end_address, sizeof(dialog->end_address), "%04X", entry->end_address);
    snprintf(dialog->initial_count, sizeof(dialog->initial_count), "%u", entry->initial_count);
    snprintf(dialog->reset_count, sizeof(dialog->reset_count), "%u", entry->reset_count);
    snprintf(dialog->tron_path_buf, sizeof(dialog->tron_path_buf), "%s", entry->tron_path);
    snprintf(dialog->type_text_buf, sizeof(dialog->type_text_buf), "%s", entry->type_text);
    snprintf(dialog->swap_slot_buf, sizeof(dialog->swap_slot_buf), "%u", entry->swap_slot);
    if (entry->swap_param == 0) {
        dialog->swap_param_buf[0] = '\0';
    } else if (entry->swap_relative) {
        snprintf(dialog->swap_param_buf, sizeof(dialog->swap_param_buf), "%+d", entry->swap_param);
    } else {
        snprintf(dialog->swap_param_buf, sizeof(dialog->swap_param_buf), "%d", entry->swap_param);
    }
}

static bool frontend_breakpoint_dialog_build_definition(
    frontend_breakpoint_dialog_state *dialog,
    runtime_breakpoint_definition *definition)
{
    uint16_t start;
    uint16_t end;

    memset(definition, 0, sizeof(*definition));
    if (!frontend_parse_hex16_text(dialog->start_address, &start)) {
        snprintf(dialog->error, sizeof(dialog->error), "Invalid start address");
        return false;
    }
    if (dialog->range && !frontend_parse_hex16_text(dialog->end_address, &end)) {
        snprintf(dialog->error, sizeof(dialog->error), "Invalid end address");
        return false;
    }
    if (dialog->range && end < start) {
        /* Swap so C800..C7FE does not become a wrap-the-map range. */
        uint16_t tmp = start;
        start = end;
        end = tmp;
    }
    if (!dialog->execute && !dialog->read && !dialog->write) {
        snprintf(dialog->error, sizeof(dialog->error), "Select at least one access type");
        return false;
    }
    if (!dialog->action_break && !dialog->action_fast && !dialog->action_slow &&
        !dialog->action_tron && !dialog->action_troff && !dialog->action_type && !dialog->action_swap) {
        snprintf(dialog->error, sizeof(dialog->error), "Select at least one action");
        return false;
    }

    definition->enabled = dialog->enabled ? 1u : 0u;
    definition->start_address = start;
    definition->end_address = dialog->range ? end : start;
    definition->has_end_address = dialog->range ? 1u : 0u;
    vf_set_ram(&definition->mapping, (a2sel_48k)dialog->ram_mapping);
    vf_set_c100(&definition->mapping, (a2sel_c100)dialog->c100_mapping);
    vf_set_d000(&definition->mapping, (a2sel_d000)dialog->d000_mapping);
    if (dialog->execute) {
        definition->access |= RUNTIME_BREAKPOINT_ACCESS_EXECUTE;
    }
    if (dialog->read) {
        definition->access |= RUNTIME_BREAKPOINT_ACCESS_READ;
    }
    if (dialog->write) {
        definition->access |= RUNTIME_BREAKPOINT_ACCESS_WRITE;
    }
    if (dialog->action_break) {
        definition->actions |= RUNTIME_BREAKPOINT_ACTION_BREAK;
    }
    if (dialog->action_fast) {
        definition->actions |= RUNTIME_BREAKPOINT_ACTION_FAST;
    }
    if (dialog->action_slow) {
        definition->actions |= RUNTIME_BREAKPOINT_ACTION_SLOW;
    }
    if (dialog->action_tron) {
        definition->actions |= RUNTIME_BREAKPOINT_ACTION_TRON;
        snprintf(definition->tron_path, sizeof(definition->tron_path), "%s", dialog->tron_path_buf);
    }
    if (dialog->action_troff) {
        definition->actions |= RUNTIME_BREAKPOINT_ACTION_TROFF;
    }
    if (dialog->action_type) {
        definition->actions |= RUNTIME_BREAKPOINT_ACTION_TYPE;
        snprintf(definition->type_text, sizeof(definition->type_text), "%s", dialog->type_text_buf);
    }
    if (dialog->action_swap) {
        uint32_t slot;
        if (!frontend_parse_u32_text(dialog->swap_slot_buf, &slot) || slot > 7u) {
            snprintf(dialog->error, sizeof(dialog->error), "Invalid swap slot (0..7)");
            return false;
        }
        definition->swap_slot = (uint8_t)slot;
        if (dialog->swap_param_buf[0] != '\0') {
            int32_t param = 0;
            uint8_t relative = 0;
            const char *p = dialog->swap_param_buf;
            bool neg = false;
            if (*p == '+') { relative = 1; p++; }
            else if (*p == '-') { relative = 1; neg = true; p++; }
            if (*p != '\0') {
                char *end;
                unsigned long v = strtoul(p, &end, 10);
                if (end != p && *end == '\0') {
                    param = neg ? -(int32_t)v : (int32_t)v;
                }
            }
            definition->swap_param = param;
            definition->swap_relative = relative;
        }
        definition->actions |= RUNTIME_BREAKPOINT_ACTION_SWAP;
    }

    if (dialog->use_counter) {
        definition->use_counter = 1;
        if (!frontend_parse_u32_text(dialog->initial_count, &definition->initial_count) ||
            !frontend_parse_u32_text(dialog->reset_count, &definition->reset_count)) {
            snprintf(dialog->error, sizeof(dialog->error), "Invalid counter value");
            return false;
        }
    }

    dialog->error[0] = '\0';
    return true;
}

static void frontend_checkbox_bool(struct nk_context *ctx, const char *label, bool *value)
{
    nk_bool active = *value ? nk_true : nk_false;
    nk_checkbox_label(ctx, label, &active);
    *value = active != 0;
}

static bool frontend_file_exists(const char *path)
{
    struct stat st;

    return path != NULL && path[0] != '\0' && stat(path, &st) == 0 && A2M_STAT_ISREG(st.st_mode);
}

static bool frontend_string_equal(const char *a, const char *b)
{
    if (a == NULL) {
        a = "";
    }
    if (b == NULL) {
        b = "";
    }
    return strcmp(a, b) == 0;
}

static void frontend_copy_text(char *out, size_t out_size, const char *value)
{
    if (out == NULL || out_size == 0) {
        return;
    }
    snprintf(out, out_size, "%s", value != NULL ? value : "");
}

static bool frontend_config_reserve_string(char **target, size_t capacity)
{
    char *buffer;

    if (target == NULL || capacity == 0) {
        return false;
    }

    buffer = calloc(1, capacity);
    if (buffer == NULL) {
        return false;
    }

    if (*target != NULL) {
        snprintf(buffer, capacity, "%s", *target);
    }
    free(*target);
    *target = buffer;
    return true;
}

static bool frontend_config_prepare_edit_buffers(frontend_config_dialog_state *dialog)
{
    return
        frontend_config_reserve_string(&dialog->edited.ini_path, 1024) &&
        frontend_config_reserve_string(&dialog->edited.video_standard, 16) &&
        frontend_config_reserve_string(&dialog->edited.turbo_multipliers, 256) &&
        frontend_config_reserve_string(&dialog->edited.symbol_files, 1024) &&
        frontend_config_reserve_string(&dialog->edited.keyboard_joystick_layout, 16);
}

static void frontend_config_dialog_reset(frontend_config_dialog_state *dialog)
{
    app_options_destroy(&dialog->original);
    app_options_destroy(&dialog->edited);
    memset(dialog, 0, sizeof(*dialog));
}

static bool frontend_config_dialog_open(frontend *ui)
{
    if (ui == NULL) {
        return false;
    }

    ui->config_dialog.open = true;
    ui->config_dialog.active_tab = FRONTEND_CONFIG_TAB_MACHINE;
    ui->config_dialog.error[0] = '\0';
    return true;
}

static void frontend_config_dialog_cancel(frontend *ui)
{
    if (ui == NULL) {
        return;
    }
    frontend_preview_crt_options(ui, &ui->config_dialog.original);
    frontend_preview_video_display(ui, &ui->config_dialog.original);
    ui->config_dialog.open = false;
}

static bool frontend_push_config_intent(
    frontend *ui,
    frontend_debugger_intent_type type,
    const app_options *options,
    const frontend_config_apply_result *result)
{
    size_t next;

    if (ui == NULL || options == NULL || result == NULL ||
        (type != FRONTEND_DEBUGGER_INTENT_CONFIG_APPLY &&
         type != FRONTEND_DEBUGGER_INTENT_SAVE_INI_NOW)) {
        return false;
    }

    next = (ui->intent_write + 1u) % FRONTEND_DEBUGGER_INTENT_CAPACITY;
    if (next == ui->intent_read) {
        return false;
    }

    memset(&ui->intents[ui->intent_write], 0, sizeof(ui->intents[ui->intent_write]));
    ui->intents[ui->intent_write].type = type;
    ui->intents[ui->intent_write].config_result = *result;
    if (!app_options_copy(&ui->intents[ui->intent_write].config, options)) {
        ui->intents[ui->intent_write].type = FRONTEND_DEBUGGER_INTENT_NONE;
        return false;
    }
    ui->intent_write = next;
    return true;
}

static bool frontend_push_config_apply_intent(
    frontend *ui,
    const app_options *options,
    const frontend_config_apply_result *result)
{
    return frontend_push_config_intent(ui, FRONTEND_DEBUGGER_INTENT_CONFIG_APPLY, options, result);
}

static bool frontend_push_save_ini_now_intent(
    frontend *ui,
    const app_options *options,
    const frontend_config_apply_result *result)
{
    return frontend_push_config_intent(ui, FRONTEND_DEBUGGER_INTENT_SAVE_INI_NOW, options, result);
}

static bool frontend_push_assemble_run_intent(
    frontend *ui,
    const char *path,
    uint16_t address,
    uint16_t run_address,
    bool auto_run,
    bool mli_launch,
    bool reset_first,
    bool rearm_oneshots)
{
    size_t next;

    if (ui == NULL || path == NULL) {
        return false;
    }

    next = (ui->intent_write + 1u) % FRONTEND_DEBUGGER_INTENT_CAPACITY;
    if (next == ui->intent_read) {
        return false;
    }

    memset(&ui->intents[ui->intent_write], 0, sizeof(ui->intents[ui->intent_write]));
    ui->intents[ui->intent_write].type = FRONTEND_DEBUGGER_INTENT_ASSEMBLE_RUN;
    snprintf(
        ui->intents[ui->intent_write].assemble_path,
        sizeof(ui->intents[ui->intent_write].assemble_path),
        "%s", path);
    ui->intents[ui->intent_write].assemble_address = address;
    ui->intents[ui->intent_write].assemble_run_address = run_address;
    ui->intents[ui->intent_write].assemble_auto_run = auto_run;
    ui->intents[ui->intent_write].assemble_mli_launch = mli_launch;
    ui->intents[ui->intent_write].assemble_reset_first = reset_first;
    ui->intents[ui->intent_write].assemble_rearm_oneshots = rearm_oneshots;
    ui->intent_write = next;
    return true;
}

bool frontend_trigger_assembler(frontend *ui)
{
    frontend_assembler_state *asm_state;
    uint16_t assemble_address;

    if (ui == NULL) {
        return false;
    }

    asm_state = &ui->assembler;
    if (!asm_state->initialized) {
        snprintf(asm_state->address_buf, sizeof(asm_state->address_buf), "8000");
        snprintf(asm_state->run_address_buf, sizeof(asm_state->run_address_buf), "8000");
        asm_state->use_address = true;
        asm_state->reset_first = true;
        asm_state->mli_launch = false;
        asm_state->initialized = true;
    }

    if (asm_state->file_path[0] == '\0') {
        return false;
    }

    assemble_address = asm_state->use_address
        ? (uint16_t)strtoul(asm_state->address_buf, NULL, 16)
        : 0u;
    return frontend_push_assemble_run_intent(
        ui,
        asm_state->file_path,
        assemble_address,
        (uint16_t)strtoul(asm_state->run_address_buf, NULL, 16),
        asm_state->auto_run,
        asm_state->mli_launch,
        asm_state->reset_first,
        asm_state->rearm_oneshots);
}

static bool frontend_push_simple_intent(frontend *ui, frontend_debugger_intent_type type)
{
    size_t next;

    if (ui == NULL || type == FRONTEND_DEBUGGER_INTENT_NONE) {
        return false;
    }

    next = (ui->intent_write + 1u) % FRONTEND_DEBUGGER_INTENT_CAPACITY;
    if (next == ui->intent_read) {
        return false;
    }

    memset(&ui->intents[ui->intent_write], 0, sizeof(ui->intents[ui->intent_write]));
    ui->intents[ui->intent_write].type = type;
    ui->intent_write = next;
    return true;
}

static frontend_debugger_intent_type frontend_history_intent_type(
    frontend_history_verb verb)
{
    switch (verb) {
    case FRONTEND_HISTORY_VERB_FIND:
        return FRONTEND_DEBUGGER_INTENT_HISTORY_FIND;
    case FRONTEND_HISTORY_VERB_NEXT:
        return FRONTEND_DEBUGGER_INTENT_HISTORY_NEXT;
    case FRONTEND_HISTORY_VERB_READ:
        return FRONTEND_DEBUGGER_INTENT_HISTORY_READ;
    case FRONTEND_HISTORY_VERB_INFO:
        return FRONTEND_DEBUGGER_INTENT_HISTORY_INFO;
    case FRONTEND_HISTORY_VERB_CLOSE:
        return FRONTEND_DEBUGGER_INTENT_HISTORY_CLOSE;
    default:
        return FRONTEND_DEBUGGER_INTENT_NONE;
    }
}

static bool frontend_push_history_intent(
    frontend *ui,
    const frontend_forensics_query_parsed *parsed)
{
    size_t next;
    frontend_debugger_intent *slot;
    frontend_history_verb verb;
    frontend_debugger_intent_type type;

    if (ui == NULL || parsed == NULL || !parsed->ok || parsed->empty) {
        return false;
    }
    verb = (frontend_history_verb)parsed->verb_code;
    type = frontend_history_intent_type(verb);
    if (type == FRONTEND_DEBUGGER_INTENT_NONE) {
        return false;
    }
    next = (ui->intent_write + 1u) % FRONTEND_DEBUGGER_INTENT_CAPACITY;
    if (next == ui->intent_read) {
        return false;
    }
    slot = &ui->intents[ui->intent_write];
    memset(slot, 0, sizeof(*slot));
    slot->type = type;
    slot->history_verb = verb;
    slot->history_query = parsed->query;
    slot->history_from_kind = parsed->from_kind;
    slot->history_from_id = parsed->from_id;
    slot->history_limit = parsed->limit;
    slot->history_read_id = parsed->read_id;
    slot->history_read_epoch = parsed->read_epoch;
    slot->history_before = parsed->before;
    slot->history_after = parsed->after;
    snprintf(slot->history_label, sizeof(slot->history_label), "%s", parsed->label);
    ui->intent_write = next;
    return true;
}

static void frontend_forensics_flush_query_submit(frontend *ui)
{
    frontend_forensics_query_parsed parsed;

    if (ui == NULL || !ui->forensics.request_submit) {
        return;
    }
    ui->forensics.request_submit = false;
    if (ui->forensics.query[0] == '\0') {
        return;
    }
    if (!forensics_view_parse_query(
            ui->forensics.query, ui->forensics.last_cursor, &parsed)) {
        forensics_view_set_status(
            &ui->forensics,
            parsed.error[0] != '\0' ? parsed.error : "bad-args");
        return;
    }
    if (parsed.empty) {
        return;
    }
    forensics_view_query_history_push(&ui->forensics, ui->forensics.query);
    if (!frontend_push_history_intent(ui, &parsed)) {
        forensics_view_set_status(&ui->forensics, "history-request-active");
        return;
    }
    forensics_view_set_status(&ui->forensics, "querying...");
    ui->forensics.query[0] = '\0';
    ui->forensics.query_history_index = 0u;
}



static bool frontend_push_machine_file_browse_intent(
    frontend *ui,
    frontend_debugger_intent_type type,
    frontend_machine_file_kind kind,
    apple2_binary_format format)
{
    size_t next;

    if (ui == NULL || (type != FRONTEND_DEBUGGER_INTENT_LOAD_BIN_BROWSE &&
                       type != FRONTEND_DEBUGGER_INTENT_SAVE_BIN_BROWSE)) return false;
    next = (ui->intent_write + 1u) % FRONTEND_DEBUGGER_INTENT_CAPACITY;
    if (next == ui->intent_read) return false;
    memset(&ui->intents[ui->intent_write], 0, sizeof(ui->intents[ui->intent_write]));
    ui->intents[ui->intent_write].type = type;
    if (type == FRONTEND_DEBUGGER_INTENT_LOAD_BIN_BROWSE) {
        ui->intents[ui->intent_write].load_file_kind = (uint8_t)kind;
        ui->intents[ui->intent_write].load_bin_format = format;
    } else {
        ui->intents[ui->intent_write].save_file_kind = (uint8_t)kind;
        ui->intents[ui->intent_write].save_bin_format = format;
    }
    ui->intent_write = next;
    return true;
}

static bool frontend_push_file_browser_result_intent(
    frontend *ui,
    frontend_debugger_intent_type purpose,
    const char *path,
    uint8_t disk_device)
{
    size_t next;

    if (ui == NULL || path == NULL) {
        return false;
    }

    next = (ui->intent_write + 1u) % FRONTEND_DEBUGGER_INTENT_CAPACITY;
    if (next == ui->intent_read) {
        return false;
    }

    memset(&ui->intents[ui->intent_write], 0, sizeof(ui->intents[ui->intent_write]));
    ui->intents[ui->intent_write].type = FRONTEND_DEBUGGER_INTENT_FILE_BROWSER_RESULT;
    ui->intents[ui->intent_write].file_browser_purpose = purpose;
    snprintf(
        ui->intents[ui->intent_write].file_browser_path,
        sizeof(ui->intents[ui->intent_write].file_browser_path),
        "%s", path);
    ui->intents[ui->intent_write].disk_device = disk_device;
    ui->intents[ui->intent_write].disk_slot = ui->file_browser.disk_slot;
    ui->intents[ui->intent_write].disk_card_type = ui->file_browser.disk_card_type;
    ui->intent_write = next;
    return true;
}

static bool frontend_push_media_intent(
    frontend *ui,
    frontend_debugger_intent_type type,
    uint8_t slot,
    uint8_t device,
    runtime_slot_card_type card_type)
{
    size_t next;
    if (ui == NULL || type == FRONTEND_DEBUGGER_INTENT_NONE ||
        slot < 1u || slot > 7u || device > 1u) {
        return false;
    }
    next = (ui->intent_write + 1u) % FRONTEND_DEBUGGER_INTENT_CAPACITY;
    if (next == ui->intent_read) {
        return false;
    }
    memset(&ui->intents[ui->intent_write], 0, sizeof(ui->intents[ui->intent_write]));
    ui->intents[ui->intent_write].type = type;
    ui->intents[ui->intent_write].disk_slot = slot;
    ui->intents[ui->intent_write].disk_device = device;
    ui->intents[ui->intent_write].disk_card_type = card_type;
    ui->intent_write = next;
    return true;
}

static const char *path_basename(const char *path)
{
    const char *base = path;
    const char *p;

    if (path == NULL) {
        return "";
    }
    for (p = path; *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }
    return *base != '\0' ? base : path;
}

static nk_flags frontend_edit_replace(
    struct nk_context *ctx,
    nk_flags flags,
    char *buffer,
    int max,
    nk_plugin_filter filter)
{
    nk_flags result = nk_edit_string_zero_terminated(
        ctx,
        (flags & ~(nk_flags)NK_EDIT_ALWAYS_INSERT_MODE) | NK_EDIT_SIG_ENTER,
        buffer, max, filter);
    if (result & NK_EDIT_ACTIVE) {
        ctx->current->edit.mode = NK_TEXT_EDIT_MODE_REPLACE;
    }
    if (result & NK_EDIT_COMMITED) {
        nk_edit_unfocus(ctx);
    }
    return result;
}

static void frontend_draw_breakpoint_editor(frontend *ui, int width, int height)
{
    frontend_breakpoint_dialog_state *dialog;
    runtime_breakpoint_definition definition;
    struct nk_context *ctx;
    struct nk_rect bounds;
    nk_flags edit_flags = NK_EDIT_FIELD;

    if (ui == NULL || !ui->breakpoint_dialog.open || ui->ctx == NULL) {
        return;
    }

    ctx = ui->ctx;
    dialog = &ui->breakpoint_dialog;
    bounds = nk_rect((float)(width - 430) * 0.5f, (float)(height - 580) * 0.5f, 430.0f, 580.0f);
    if (bounds.x < 8.0f) {
        bounds.x = 8.0f;
    }
    if (bounds.y < 8.0f) {
        bounds.y = 8.0f;
    }

    if (nk_begin(
            ctx,
            "Breakpoint Editor",
            bounds,
            NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_CLOSABLE)) {
        if (!nk_window_is_closed(ctx, "Breakpoint Editor")) {
            nk_layout_row_dynamic(ctx, 20.0f, 1);
            frontend_checkbox_bool(ctx, "Enabled", &dialog->enabled);

            nk_layout_row_dynamic(ctx, 18.0f, 1);
            nk_label(ctx, "Access", NK_TEXT_LEFT);
            nk_layout_row_dynamic(ctx, 20.0f, 3);
            frontend_checkbox_bool(ctx, "Execute", &dialog->execute);
            frontend_checkbox_bool(ctx, "Read", &dialog->read);
            frontend_checkbox_bool(ctx, "Write", &dialog->write);

            nk_layout_row_dynamic(ctx, 18.0f, 1);
            nk_label(ctx, "Address", NK_TEXT_LEFT);
            nk_layout_row_begin(ctx, NK_DYNAMIC, 22.0f, 4);
            nk_layout_row_push(ctx, 0.20f);
            nk_label(ctx, "Start", NK_TEXT_LEFT);
            nk_layout_row_push(ctx, 0.25f);
            frontend_edit_replace(ctx, edit_flags, dialog->start_address, sizeof(dialog->start_address), nk_filter_hex);
            nk_layout_row_push(ctx, 0.20f);
            frontend_checkbox_bool(ctx, "Range", &dialog->range);
            nk_layout_row_push(ctx, 0.25f);
            if (!dialog->range) {
                edit_flags |= NK_EDIT_READ_ONLY;
            }
            frontend_edit_replace(ctx, edit_flags, dialog->end_address, sizeof(dialog->end_address), nk_filter_hex);
            nk_layout_row_end(ctx);
            edit_flags = NK_EDIT_FIELD;

            nk_layout_row_dynamic(ctx, 18.0f, 1);
            nk_label(ctx, "Mapping", NK_TEXT_LEFT);
            nk_layout_row_begin(ctx, NK_DYNAMIC, 20.0f, 4);
            nk_layout_row_push(ctx, 0.20f);
            nk_label(ctx, "RAM", NK_TEXT_LEFT);
            nk_layout_row_push(ctx, 0.26f);
            if (nk_option_label(ctx, "Map", dialog->ram_mapping == A2SEL48K_MAPPED)) {
                dialog->ram_mapping = A2SEL48K_MAPPED;
            }
            nk_layout_row_push(ctx, 0.27f);
            if (nk_option_label(ctx, "Main", dialog->ram_mapping == A2SEL48K_MAIN)) {
                dialog->ram_mapping = A2SEL48K_MAIN;
            }
            nk_layout_row_push(ctx, 0.27f);
            if (nk_option_label(ctx, "Aux", dialog->ram_mapping == A2SEL48K_AUX)) {
                dialog->ram_mapping = A2SEL48K_AUX;
            }
            nk_layout_row_end(ctx);

            nk_layout_row_begin(ctx, NK_DYNAMIC, 20.0f, 3);
            nk_layout_row_push(ctx, 0.20f);
            nk_label(ctx, "C100", NK_TEXT_LEFT);
            nk_layout_row_push(ctx, 0.40f);
            if (nk_option_label(ctx, "Map", dialog->c100_mapping == A2SELC100_MAPPED)) {
                dialog->c100_mapping = A2SELC100_MAPPED;
            }
            nk_layout_row_push(ctx, 0.40f);
            if (nk_option_label(ctx, "ROM", dialog->c100_mapping == A2SELC100_ROM)) {
                dialog->c100_mapping = A2SELC100_ROM;
            }
            nk_layout_row_end(ctx);

            nk_layout_row_begin(ctx, NK_DYNAMIC, 20.0f, 5);
            nk_layout_row_push(ctx, 0.20f);
            nk_label(ctx, "D000", NK_TEXT_LEFT);
            nk_layout_row_push(ctx, 0.20f);
            if (nk_option_label(ctx, "Map", dialog->d000_mapping == A2SELD000_MAPPED)) {
                dialog->d000_mapping = A2SELD000_MAPPED;
            }
            nk_layout_row_push(ctx, 0.20f);
            if (nk_option_label(ctx, "LC1", dialog->d000_mapping == A2SELD000_LC_B1)) {
                dialog->d000_mapping = A2SELD000_LC_B1;
            }
            nk_layout_row_push(ctx, 0.20f);
            if (nk_option_label(ctx, "LC2", dialog->d000_mapping == A2SELD000_LC_B2)) {
                dialog->d000_mapping = A2SELD000_LC_B2;
            }
            nk_layout_row_push(ctx, 0.20f);
            if (nk_option_label(ctx, "ROM", dialog->d000_mapping == A2SELD000_ROM)) {
                dialog->d000_mapping = A2SELD000_ROM;
            }
            nk_layout_row_end(ctx);

            nk_layout_row_dynamic(ctx, 18.0f, 1);
            nk_label(ctx, "Counter", NK_TEXT_LEFT);
            nk_layout_row_begin(ctx, NK_DYNAMIC, 22.0f, 5);
            nk_layout_row_push(ctx, 0.28f);
            frontend_checkbox_bool(ctx, "Use Counter", &dialog->use_counter);
            nk_layout_row_push(ctx, 0.16f);
            nk_label(ctx, "Initial", NK_TEXT_LEFT);
            nk_layout_row_push(ctx, 0.20f);
            frontend_edit_replace(
                ctx,
                dialog->use_counter ? (nk_flags)NK_EDIT_FIELD : ((nk_flags)NK_EDIT_FIELD | NK_EDIT_READ_ONLY),
                dialog->initial_count,
                sizeof(dialog->initial_count),
                nk_filter_decimal);
            nk_layout_row_push(ctx, 0.14f);
            nk_label(ctx, "Repeat", NK_TEXT_LEFT);
            nk_layout_row_push(ctx, 0.20f);
            frontend_edit_replace(
                ctx,
                dialog->use_counter ? (nk_flags)NK_EDIT_FIELD : ((nk_flags)NK_EDIT_FIELD | NK_EDIT_READ_ONLY),
                dialog->reset_count,
                sizeof(dialog->reset_count),
                nk_filter_decimal);
            nk_layout_row_end(ctx);

            nk_layout_row_dynamic(ctx, 18.0f, 1);
            nk_label(ctx, "Actions", NK_TEXT_LEFT);
            nk_layout_row_dynamic(ctx, 20.0f, 3);
            frontend_checkbox_bool(ctx, "Break", &dialog->action_break);
            frontend_checkbox_bool(ctx, "Fast", &dialog->action_fast);
            frontend_checkbox_bool(ctx, "Slow", &dialog->action_slow);

            /* Troff — no parameter */
            nk_layout_row_dynamic(ctx, 20.0f, 1);
            {
                bool prev_troff = dialog->action_troff;
                frontend_checkbox_bool(ctx, "Troff", &dialog->action_troff);
                if (dialog->action_troff && !prev_troff) {
                    dialog->action_tron = false;
                }
            }

            /* Tron — optional trace file path */
            nk_layout_row_begin(ctx, NK_DYNAMIC, 22.0f, 2);
            nk_layout_row_push(ctx, 0.20f);
            {
                bool prev_tron = dialog->action_tron;
                frontend_checkbox_bool(ctx, "Tron", &dialog->action_tron);
                if (dialog->action_tron && !prev_tron) {
                    dialog->action_troff = false;
                }
            }
            nk_layout_row_push(ctx, 0.80f);
            frontend_edit_replace(
                ctx,
                dialog->action_tron ? (nk_flags)NK_EDIT_FIELD : ((nk_flags)NK_EDIT_FIELD | NK_EDIT_READ_ONLY),
                dialog->tron_path_buf,
                sizeof(dialog->tron_path_buf),
                nk_filter_default);
            nk_layout_row_end(ctx);

            /* Swap — controller slot and disk queue parameter (+N/-N/N). */
            nk_layout_row_begin(ctx, NK_DYNAMIC, 22.0f, 3);
            nk_layout_row_push(ctx, 0.20f);
            frontend_checkbox_bool(ctx, "Swap", &dialog->action_swap);
            nk_layout_row_push(ctx, 0.12f);
            frontend_edit_replace(
                ctx,
                dialog->action_swap ? (nk_flags)NK_EDIT_FIELD : ((nk_flags)NK_EDIT_FIELD | NK_EDIT_READ_ONLY),
                dialog->swap_slot_buf,
                sizeof(dialog->swap_slot_buf),
                nk_filter_decimal);
            nk_layout_row_push(ctx, 0.68f);
            frontend_edit_replace(
                ctx,
                dialog->action_swap ? (nk_flags)NK_EDIT_FIELD : ((nk_flags)NK_EDIT_FIELD | NK_EDIT_READ_ONLY),
                dialog->swap_param_buf,
                sizeof(dialog->swap_param_buf),
                nk_filter_default);
            nk_layout_row_end(ctx);

            /* Type — text to inject */
            nk_layout_row_begin(ctx, NK_DYNAMIC, 22.0f, 2);
            nk_layout_row_push(ctx, 0.20f);
            frontend_checkbox_bool(ctx, "Type", &dialog->action_type);
            nk_layout_row_push(ctx, 0.80f);
            frontend_edit_replace(
                ctx,
                dialog->action_type ? (nk_flags)NK_EDIT_FIELD : ((nk_flags)NK_EDIT_FIELD | NK_EDIT_READ_ONLY),
                dialog->type_text_buf,
                sizeof(dialog->type_text_buf),
                nk_filter_default);
            nk_layout_row_end(ctx);

            if (dialog->error[0] != '\0') {
                nk_layout_row_dynamic(ctx, 18.0f, 1);
                nk_label_colored(ctx, dialog->error, NK_TEXT_LEFT, nk_rgb(255, 128, 118));
            } else {
                nk_layout_row_dynamic(ctx, 8.0f, 1);
                nk_spacing(ctx, 1);
            }

            nk_layout_row_dynamic(ctx, 24.0f, 2);
            if (nk_button_label(ctx, "Cancel")) {
                dialog->open = false;
            }
            if (nk_button_label(ctx, "Apply") &&
                frontend_breakpoint_dialog_build_definition(dialog, &definition)) {
                if (dialog->mode == FRONTEND_BREAKPOINT_DIALOG_EDIT) {
                    frontend_push_breakpoint_definition_intent(
                        ui,
                        FRONTEND_DEBUGGER_INTENT_BREAKPOINT_UPDATE,
                        dialog->id,
                        &definition);
                } else {
                    frontend_push_breakpoint_definition_intent(
                        ui,
                        FRONTEND_DEBUGGER_INTENT_BREAKPOINT_CREATE,
                        0,
                        &definition);
                }
                dialog->open = false;
            }
        } else {
            dialog->open = false;
        }
    } else if (nk_window_is_closed(ctx, "Breakpoint Editor")) {
        dialog->open = false;
    }
    nk_end(ctx);
}

static bool frontend_config_validate(frontend_config_dialog_state *dialog)
{
    if (dialog->edited.scroll_wheel_lines < 1 || dialog->edited.scroll_wheel_lines > 100) {
        snprintf(dialog->error, sizeof(dialog->error), "Scroll wheel speed must be 1..100");
        return false;
    }
    if (dialog->edited.crt_scanline_strength < 0 ||
        dialog->edited.crt_scanline_strength > 100) {
        snprintf(dialog->error, sizeof(dialog->error), "Scanline strength must be 0..100");
        return false;
    }
    if (dialog->edited.crt_curvature_amount < 0 ||
        dialog->edited.crt_curvature_amount > 100) {
        snprintf(dialog->error, sizeof(dialog->error), "CRT curvature must be 0..100");
        return false;
    }
    if (dialog->edited.ini_path == NULL || dialog->edited.ini_path[0] == '\0') {
        snprintf(dialog->error, sizeof(dialog->error), "INI file path is required");
        return false;
    }
    {
        runtime_config turbo_check;

        runtime_config_init(&turbo_check);
        if (dialog->edited.turbo_multipliers == NULL ||
            dialog->edited.turbo_multipliers[0] == '\0' ||
            !runtime_config_set_turbo_csv(
                &turbo_check, dialog->edited.turbo_multipliers)) {
            snprintf(
                dialog->error,
                sizeof(dialog->error),
                "Turbo list must be MHz values and/or max (e.g. 1,4,8,max)");
            return false;
        }
    }
    dialog->error[0] = '\0';
    return true;
}

static bool frontend_config_standard_changed(const app_options *a, const app_options *b)
{
    return !frontend_string_equal(a->video_standard, b->video_standard);
}

static bool frontend_config_symbols_changed(const app_options *a, const app_options *b)
{
    return !frontend_string_equal(a->symbol_files, b->symbol_files);
}

static bool frontend_config_roms_changed(const app_options *a, const app_options *b)
{
    return a->apple_model != b->apple_model ||
        memcmp(a->slot_cards, b->slot_cards, sizeof(a->slot_cards)) != 0;
}

static bool frontend_config_machine_changed(const app_options *a, const app_options *b)
{
    return a->apple_model != b->apple_model ||
        memcmp(a->slot_cards, b->slot_cards, sizeof(a->slot_cards)) != 0;
}

static void frontend_config_commit_ini_path(frontend_config_dialog_state *dialog)
{
    (void)dialog;
    /* Path commit is handled by the existing-INI parse prompt; no side effects. */
}

static void frontend_draw_config_tab_button(frontend *ui, frontend_config_tab tab, const char *label)
{
    struct nk_style_button saved_button = ui->ctx->style.button;

    if (ui->config_dialog.active_tab == tab) {
        ui->ctx->style.button.normal = nk_style_item_color(nk_rgb(49, 78, 94));
        ui->ctx->style.button.hover = nk_style_item_color(nk_rgb(58, 93, 112));
        ui->ctx->style.button.text_normal = nk_rgb(226, 246, 255);
    }

    if (nk_button_label(ui->ctx, label)) {
        ui->config_dialog.active_tab = tab;
    }

    ui->ctx->style.button = saved_button;
}

static void frontend_draw_config_machine_tab(frontend *ui, frontend_config_dialog_state *dialog, struct nk_context *ctx)
{
    static const char *const model_items[] = { "//e Enhanced", "][+" };
    static const char *const slot_items[] = {
        "Empty", "Disk II", "SmartPort", "Mockingboard"
    };
    static const char *const joystick_stick_items[] = { "Off", "Stick 1", "Stick 2" };
    static const char *const joystick_layout_items[] = { "Numpad", "WASD" };
    int selected;
    int next;
    int slot;

    (void)ui;

    if (dialog->edited.turbo_multipliers == NULL) {
        app_options_set_string(&dialog->edited.turbo_multipliers, "1,max");
    }
    if (dialog->edited.keyboard_joystick_layout == NULL) {
        app_options_set_string(&dialog->edited.keyboard_joystick_layout, "numpad");
    }

    nk_layout_row_begin(ctx, NK_DYNAMIC, 22.0f, 2);
    nk_layout_row_push(ctx, 0.30f);
    nk_label(ctx, "Model", NK_TEXT_LEFT);
    nk_layout_row_push(ctx, 0.70f);
    selected = dialog->edited.apple_model == 1 ? 1 : 0;
    next = nk_combo(ctx, model_items, 2, selected, 18, nk_vec2(180.0f, 100.0f));
    if (next != selected) {
        dialog->edited.apple_model = next;
    }
    nk_layout_row_end(ctx);

    for (slot = 1; slot <= 7; ++slot) {
        char label[16];
        nk_layout_row_begin(ctx, NK_DYNAMIC, 22.0f, 2);
        nk_layout_row_push(ctx, 0.30f);
        snprintf(label, sizeof(label), "Slot %d", slot);
        nk_label(ctx, label, NK_TEXT_LEFT);
        nk_layout_row_push(ctx, 0.70f);
        selected = (int)dialog->edited.slot_cards[slot];
        if (selected < APP_SLOT_CARD_EMPTY || selected > APP_SLOT_CARD_MOCKINGBOARD) {
            selected = APP_SLOT_CARD_EMPTY;
        }
        next = nk_combo(ctx, slot_items, 4, selected, 18, nk_vec2(180.0f, 120.0f));
        if (next != selected) {
            (void)app_options_set_slot_card(
                &dialog->edited, slot, (app_slot_card_type)next);
        }
        nk_layout_row_end(ctx);
    }

    nk_layout_row_dynamic(ctx, 10.0f, 1);
    nk_spacing(ctx, 1);

    /* Keyboard fake stick: stick assignment + layout (numpad needs a keypad;
       WASD is for laptops). Live-applied on OK via main host loop. */
    nk_layout_row_begin(ctx, NK_DYNAMIC, 22.0f, 3);
    nk_layout_row_push(ctx, 0.30f);
    nk_label(ctx, "Keyboard Joystick", NK_TEXT_LEFT);
    nk_layout_row_push(ctx, 0.35f);
    selected = dialog->edited.keyboard_joystick_port;
    if (selected < 0 || selected > 2) {
        selected = 0;
    }
    next = nk_combo(ctx, joystick_stick_items, 3, selected, 18, nk_vec2(120.0f, 120.0f));
    if (next != selected) {
        dialog->edited.keyboard_joystick_port = next;
    }
    nk_layout_row_push(ctx, 0.35f);
    selected = frontend_string_equal(dialog->edited.keyboard_joystick_layout, "wasd") ? 1 : 0;
    next = nk_combo(ctx, joystick_layout_items, 2, selected, 18, nk_vec2(120.0f, 100.0f));
    if (next != selected) {
        char *p;
        app_options_set_string(
            &dialog->edited.keyboard_joystick_layout, joystick_layout_items[next]);
        for (p = dialog->edited.keyboard_joystick_layout; p != NULL && *p != '\0'; ++p) {
            if (*p >= 'A' && *p <= 'Z') {
                *p = (char)(*p - 'A' + 'a');
            }
        }
    }
    nk_layout_row_end(ctx);

    /* While stick is on: Option=fire0 / Space=fire1 by default (//e OA role).
       Swap puts Space on BUTN0 for WASD ergonomics. Off when stick is Off. */
    nk_layout_row_dynamic(ctx, 22.0f, 1);
    frontend_checkbox_bool(
        ctx,
        "Swap fire keys (Space = button 0, Option = button 1)",
        &dialog->edited.keyboard_joystick_swap_buttons);

    nk_layout_row_begin(ctx, NK_DYNAMIC, 22.0f, 2);
    nk_layout_row_push(ctx, 0.30f);
    nk_label(ctx, "Turbo (e.g. 1,4,max)", NK_TEXT_LEFT);
    nk_layout_row_push(ctx, 0.70f);
    frontend_edit_replace(ctx, NK_EDIT_FIELD, dialog->edited.turbo_multipliers, 256, nk_filter_default);
    nk_layout_row_end(ctx);

    nk_layout_row_dynamic(ctx, 22.0f, 1);
    frontend_checkbox_bool(
        ctx,
        "History off on max (pause CPU flight recorder only)",
        &dialog->edited.history_off_on_max);

    nk_layout_row_dynamic(ctx, 22.0f, 1);
    frontend_checkbox_bool(ctx, "Pause on BRK", &dialog->edited.pause_on_brk);
    nk_layout_row_dynamic(ctx, 22.0f, 1);
    frontend_checkbox_bool(ctx, "Show disk LEDs", &dialog->edited.show_disk_leds);
}

static void frontend_draw_config_emulator_tab(frontend *ui, frontend_config_dialog_state *dialog, struct nk_context *ctx)
{
    /* Same 3-column split as the CRT sliders so the scroll-wheel property,
       Mono radio, and phosphor combo share one left edge. Two-column rows
       subtract one fewer spacer and would miss that line. */
    const float label_w = 0.42f;
    const float control_w = 0.43f;
    const float trail_w = 0.15f;

    if (dialog->edited.symbol_files == NULL) {
        app_options_set_string(&dialog->edited.symbol_files, "");
    }

    nk_layout_row_begin(ctx, NK_DYNAMIC, 22.0f, 3);
    nk_layout_row_push(ctx, label_w);
    nk_label(ctx, "Scroll Wheel Speed", NK_TEXT_LEFT);
    nk_layout_row_push(ctx, control_w);
    nk_property_int(ctx, "#L", 1, &dialog->edited.scroll_wheel_lines, 100, 1, 4.0f);
    nk_layout_row_push(ctx, trail_w);
    nk_spacing(ctx, 1);
    nk_layout_row_end(ctx);

    nk_layout_row_dynamic(ctx, 22.0f, 1);
    frontend_checkbox_bool(ctx, "Original DEL behaviour", &dialog->edited.original_del);

    nk_layout_row_begin(ctx, NK_DYNAMIC, 22.0f, 3);
    nk_layout_row_push(ctx, 0.30f);
    nk_label(ctx, "Symbol Files", NK_TEXT_LEFT);
    nk_layout_row_push(ctx, 0.55f);
    nk_spacing(ctx, 1);
    nk_layout_row_push(ctx, 0.15f);
    if (nk_button_label(ctx, "Add")) {
        frontend_push_simple_intent(ui, FRONTEND_DEBUGGER_INTENT_CONFIG_PICK_SYMBOL_DIALOG);
    }
    nk_layout_row_end(ctx);

    nk_layout_row_dynamic(ctx, 22.0f, 1);
    frontend_edit_replace(ctx, NK_EDIT_FIELD, dialog->edited.symbol_files, 1024, nk_filter_default);

    nk_layout_row_dynamic(ctx, 10.0f, 1);
    nk_spacing(ctx, 1);
    nk_layout_row_dynamic(ctx, 18.0f, 1);
    nk_label(ctx, "CRT display", NK_TEXT_LEFT);

    nk_layout_row_begin(ctx, NK_DYNAMIC, 22.0f, 3);
    nk_layout_row_push(ctx, label_w);
    if (nk_option_label(ctx, "Colour", dialog->edited.colour_display)) {
        dialog->edited.colour_display = true;
    }
    nk_layout_row_push(ctx, control_w);
    if (nk_option_label(ctx, "Mono", !dialog->edited.colour_display)) {
        dialog->edited.colour_display = false;
    }
    nk_layout_row_push(ctx, trail_w);
    nk_spacing(ctx, 1);
    nk_layout_row_end(ctx);

    {
        static const char *mono_items[] = { "White", "Green", "Amber" };
        int selected = (int)dialog->edited.mono_mode;
        int next;

        if (selected < 0 || selected > 2) {
            selected = 0;
        }
        nk_layout_row_begin(ctx, NK_DYNAMIC, 22.0f, 3);
        nk_layout_row_push(ctx, label_w);
        nk_label(ctx, "Mono Mode", NK_TEXT_LEFT);
        nk_layout_row_push(ctx, control_w);
        next = nk_combo(ctx, mono_items, 3, selected, 18, nk_vec2(180.0f, 120.0f));
        if (next != selected) {
            dialog->edited.mono_mode = (app_mono_mode)next;
        }
        nk_layout_row_push(ctx, trail_w);
        nk_spacing(ctx, 1);
        nk_layout_row_end(ctx);
    }

    nk_layout_row_dynamic(ctx, 22.0f, 1);
    frontend_checkbox_bool(ctx, "True Aspect Ratio", &dialog->edited.true_aspect);

    /* Scanlines and curvature are drawn through a filtered texture, so they force
       smoothing on. Show that as ticked-and-disabled rather than leaving a box
       unticked while the picture is plainly smoothed. The stored value is left
       alone, so unticking them restores whatever the user last chose. */
    {
        bool forced = dialog->edited.crt_scanlines || dialog->edited.crt_curvature;
        bool shown = dialog->edited.crt_smoothing || forced;

        if (forced) {
            nk_widget_disable_begin(ctx);
        }
        frontend_checkbox_bool(ctx, "CRT Smoothing", &shown);
        if (forced) {
            nk_widget_disable_end(ctx);
        } else {
            dialog->edited.crt_smoothing = shown;
        }
    }

    nk_layout_row_begin(ctx, NK_DYNAMIC, 22.0f, 3);
    nk_layout_row_push(ctx, label_w);
    frontend_checkbox_bool(ctx, "CRT Scanlines", &dialog->edited.crt_scanlines);
    if (!dialog->edited.crt_scanlines) nk_widget_disable_begin(ctx);
    nk_layout_row_push(ctx, control_w);
    /* Minimum 1%: the checkbox is what turns the effect off, so a 0% setting
       would just make an enabled effect look broken. */
    dialog->edited.crt_scanline_strength = nk_slide_int(
        ctx, 1, dialog->edited.crt_scanline_strength, 100, 1);
    nk_layout_row_push(ctx, trail_w);
    nk_labelf(ctx, NK_TEXT_RIGHT, "%d%%", dialog->edited.crt_scanline_strength);
    if (!dialog->edited.crt_scanlines) nk_widget_disable_end(ctx);
    nk_layout_row_end(ctx);

    nk_layout_row_begin(ctx, NK_DYNAMIC, 22.0f, 3);
    nk_layout_row_push(ctx, label_w);
    frontend_checkbox_bool(ctx, "CRT Curvature", &dialog->edited.crt_curvature);
    if (!dialog->edited.crt_curvature) nk_widget_disable_begin(ctx);
    nk_layout_row_push(ctx, control_w);
    dialog->edited.crt_curvature_amount = nk_slide_int(
        ctx, 1, dialog->edited.crt_curvature_amount, 100, 1);
    nk_layout_row_push(ctx, trail_w);
    nk_labelf(ctx, NK_TEXT_RIGHT, "%d%%", dialog->edited.crt_curvature_amount);
    if (!dialog->edited.crt_curvature) nk_widget_disable_end(ctx);
    nk_layout_row_end(ctx);

    /* CRT presentation is a transactional live preview: edited values drive the
       frontend immediately, while Cancel restores dialog->original. */
    frontend_preview_crt_options(ui, &dialog->edited);
    frontend_preview_video_display(ui, &dialog->edited);
}

/* Per-purpose default browse folders. They bind to the live ui->browse_dirs so
   an edit applies on the next browse immediately. Media mounts belong to the
   installed devices shown in Misc -> Machine, not to this configuration tab. */
static void frontend_draw_config_paths_tab(frontend *ui, frontend_config_dialog_state *dialog, struct nk_context *ctx)
{
    static const char *const labels[FRONTEND_BROWSE_SLOT_COUNT] = {
        "Assembler", "Floppy", "SmartPort", "Binary", "Basic", "Snapshot"
    };
    int i;

    (void)dialog;

    nk_layout_row_dynamic(ctx, 18.0f, 1);
    nk_label(ctx, "Default browse paths", NK_TEXT_LEFT);

    for (i = 0; i < FRONTEND_BROWSE_SLOT_COUNT; ++i) {
        nk_layout_row_begin(ctx, NK_DYNAMIC, 22.0f, 3);
        nk_layout_row_push(ctx, 0.20f);
        nk_label(ctx, labels[i], NK_TEXT_LEFT);
        nk_layout_row_push(ctx, 0.66f);
        frontend_edit_replace(ctx, NK_EDIT_FIELD,
            ui->browse_dirs[i], (int)sizeof(ui->browse_dirs[i]), nk_filter_default);
        nk_layout_row_push(ctx, 0.14f);
        if (nk_button_label(ctx, "...")) {
            ui->config_dialog.pending_browse_slot = i;
            frontend_push_simple_intent(ui, FRONTEND_DEBUGGER_INTENT_CONFIG_PICK_PATH_DIALOG);
        }
        nk_layout_row_end(ctx);

        if (i == FRONTEND_BROWSE_SLOT_SNAPSHOT) {
            nk_layout_row_dynamic(ctx, 16.0f, 1);
            nk_label_colored(ctx, "(snapshot also serves as the quicksave folder)",
                NK_TEXT_LEFT, nk_rgb(150, 170, 180));
        }
    }

    nk_layout_row_dynamic(ctx, 8.0f, 1);
    nk_spacing(ctx, 1);
    nk_layout_row_dynamic(ctx, 24.0f, 1);
    if (nk_button_label(ctx, "Save Paths Only")) {
        frontend_push_simple_intent(ui, FRONTEND_DEBUGGER_INTENT_SAVE_PATHS_ONLY);
    }
}

static void frontend_draw_config_existing_ini_prompt(frontend *ui, frontend_config_dialog_state *dialog, struct nk_context *ctx)
{
    if (dialog->prompt != FRONTEND_INI_PROMPT_EXISTING) {
        return;
    }

    if (nk_popup_begin(ctx, NK_POPUP_STATIC, "Parse selected INI file now?", NK_WINDOW_BORDER, nk_rect(220, 160, 360, 130))) {
        nk_layout_row_dynamic(ctx, 20.0f, 1);
        nk_label(ctx, "Parse selected INI file now?", NK_TEXT_LEFT);
        nk_layout_row_dynamic(ctx, 24.0f, 3);
        if (nk_button_label(ctx, "Yes")) {
            app_options parsed;
            memset(&parsed, 0, sizeof(parsed));
            if (app_options_copy(&parsed, &dialog->edited) &&
                app_options_apply_ini_file(&parsed, dialog->edited.ini_path)) {
                char path[1024];
                frontend_copy_text(path, sizeof(path), dialog->edited.ini_path);
                app_options_destroy(&dialog->edited);
                dialog->edited = parsed;
                app_options_set_string(&dialog->edited.ini_path, path);
                frontend_config_prepare_edit_buffers(dialog);
            } else {
                snprintf(dialog->error, sizeof(dialog->error), "Could not parse selected INI");
                app_options_destroy(&parsed);
            }
            dialog->prompt = FRONTEND_INI_PROMPT_NONE;
            nk_popup_close(ctx);
        }
        if (nk_button_label(ctx, "No")) {
            dialog->prompt = FRONTEND_INI_PROMPT_NONE;
            nk_popup_close(ctx);
        }
        if (nk_button_label(ctx, "Cancel")) {
            app_options_set_string(&dialog->edited.ini_path, dialog->previous_ini_path);
            frontend_config_reserve_string(&dialog->edited.ini_path, 1024);
            dialog->prompt = FRONTEND_INI_PROMPT_NONE;
            nk_popup_close(ctx);
        }
        nk_popup_end(ctx);
    }
    (void)ui;
}

static void frontend_draw_config_dialog(frontend *ui, int width, int height)
{
    frontend_config_dialog_state *dialog;
    struct nk_context *ctx;
    struct nk_rect bounds;

    if (ui == NULL || !ui->config_dialog.open || ui->ctx == NULL) {
        return;
    }

    ctx = ui->ctx;
    dialog = &ui->config_dialog;
    bounds = nk_rect((float)(width - 560) * 0.5f, (float)(height - 500) * 0.5f, 560.0f, 500.0f);
    if (bounds.x < 8.0f) {
        bounds.x = 8.0f;
    }
    if (bounds.y < 8.0f) {
        bounds.y = 8.0f;
    }

    if (nk_begin(ctx, "Configure", bounds, NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_CLOSABLE)) {
        if (!nk_window_is_closed(ctx, "Configure")) {
            nk_layout_row_dynamic(ctx, 24.0f, 3);
            frontend_draw_config_tab_button(ui, FRONTEND_CONFIG_TAB_MACHINE, "Machine");
            frontend_draw_config_tab_button(ui, FRONTEND_CONFIG_TAB_EMULATOR, "Emulator");
            frontend_draw_config_tab_button(ui, FRONTEND_CONFIG_TAB_PATHS, "Paths");

            /* Grow the tab body to fill the space above the bottom controls (INI
               row, message line, OK/Cancel) so no dead space is left and the tab
               content is not clipped. */
            {
                struct nk_rect cregion = nk_window_get_content_region(ctx);
                float sp = ctx->style.window.spacing.y;
                float pad = ctx->style.window.padding.y;
                float other = 24.0f /*tabs*/ + 24.0f /*INI*/ + 20.0f /*save once*/
                            + 18.0f /*message*/ + 24.0f /*OK/Cancel*/;
                float group_h = cregion.h - other - 6.0f * sp - 2.0f * pad;
                if (group_h < 120.0f) group_h = 120.0f;
                nk_layout_row_dynamic(ctx, group_h, 1);
            }
            if (nk_group_begin(ctx, "config-tab-body", NK_WINDOW_BORDER)) {
                if (dialog->active_tab == FRONTEND_CONFIG_TAB_MACHINE) {
                    frontend_draw_config_machine_tab(ui, dialog, ctx);
                } else if (dialog->active_tab == FRONTEND_CONFIG_TAB_EMULATOR) {
                    frontend_draw_config_emulator_tab(ui, dialog, ctx);
                } else {
                    frontend_draw_config_paths_tab(ui, dialog, ctx);
                }
                nk_group_end(ctx);
            }

            nk_layout_row_begin(ctx, NK_DYNAMIC, 24.0f, 3);
            nk_layout_row_push(ctx, 0.18f);
            nk_label(ctx, "INI File", NK_TEXT_LEFT);
            nk_layout_row_push(ctx, 0.68f);
            if (dialog->edited.ini_path == NULL) {
                app_options_set_string(&dialog->edited.ini_path, "");
            }
            if (dialog->prompt == FRONTEND_INI_PROMPT_NONE) {
                frontend_copy_text(dialog->previous_ini_path, sizeof(dialog->previous_ini_path), dialog->edited.ini_path);
            }
            {
                nk_flags ini_result = frontend_edit_replace(
                    ctx,
                    (nk_flags)NK_EDIT_FIELD | NK_EDIT_SIG_ENTER,
                    dialog->edited.ini_path,
                    1024,
                    nk_filter_default);
                if (ini_result & NK_EDIT_COMMITED) {
                    frontend_config_commit_ini_path(dialog);
                    if (frontend_file_exists(dialog->edited.ini_path)) {
                        dialog->prompt = FRONTEND_INI_PROMPT_EXISTING;
                    }
                    nk_edit_unfocus(ctx);
                }
            }
            nk_layout_row_push(ctx, 0.14f);
            if (nk_button_label(ctx, "...")) {
                frontend_copy_text(dialog->previous_ini_path, sizeof(dialog->previous_ini_path), dialog->edited.ini_path);
                frontend_push_simple_intent(ui, FRONTEND_DEBUGGER_INTENT_CONFIG_PICK_INI_DIALOG);
            }
            nk_layout_row_end(ctx);

            nk_layout_row_dynamic(ctx, 20.0f, 1);
            if (dialog->edited.no_save_ini) {
                nk_label(ctx, "Saving disabled", NK_TEXT_LEFT);
            } else {
                frontend_checkbox_bool(ctx, "Auto-save INI on Quit", &dialog->edited.remember);
            }

            if (dialog->error[0] != '\0') {
                nk_layout_row_dynamic(ctx, 18.0f, 1);
                nk_label_colored(ctx, dialog->error, NK_TEXT_LEFT, nk_rgb(255, 128, 118));
            } else {
                nk_layout_row_dynamic(ctx, 8.0f, 1);
                nk_spacing(ctx, 1);
            }

            nk_layout_row_dynamic(ctx, 24.0f, 3);
            if (nk_button_label(ctx, "Save INI now") && frontend_config_validate(dialog)) {
                bool roms_changed = frontend_config_roms_changed(&dialog->original, &dialog->edited);
                bool machine_changed = frontend_config_machine_changed(
                    &dialog->original, &dialog->edited);
                frontend_config_apply_result result = {
                    .accepted = true,
                    .needs_reboot = frontend_config_standard_changed(&dialog->original, &dialog->edited) ||
                        roms_changed,
                    .machine_changed = machine_changed,
                    .symbols_changed = frontend_config_symbols_changed(&dialog->original, &dialog->edited),
                    .roms_changed = roms_changed,
                };
                app_options_reconcile_slot_cards(&dialog->edited);
                /* Apply edited options then write the INI; keep the dialog open. */
                if (frontend_push_save_ini_now_intent(ui, &dialog->edited, &result)) {
                    dialog->error[0] = '\0';
                }
            }
            if (nk_button_label(ctx, "OK") && frontend_config_validate(dialog)) {
                bool roms_changed = frontend_config_roms_changed(&dialog->original, &dialog->edited);
                bool machine_changed = frontend_config_machine_changed(
                    &dialog->original, &dialog->edited);
                frontend_config_apply_result result = {
                    .accepted = true,
                    .needs_reboot = frontend_config_standard_changed(&dialog->original, &dialog->edited) ||
                        roms_changed,
                    .machine_changed = machine_changed,
                    .symbols_changed = frontend_config_symbols_changed(&dialog->original, &dialog->edited),
                    .roms_changed = roms_changed,
                };
                app_options_reconcile_slot_cards(&dialog->edited);
                if (frontend_push_config_apply_intent(ui, &dialog->edited, &result)) {
                    dialog->open = false;
                }
            }
            if (nk_button_label(ctx, "Cancel")) {
                frontend_config_dialog_cancel(ui);
            }
            frontend_draw_config_existing_ini_prompt(ui, dialog, ctx);
        } else {
            frontend_config_dialog_cancel(ui);
        }
    } else if (nk_window_is_closed(ctx, "Configure")) {
        frontend_config_dialog_cancel(ui);
    }
    nk_end(ctx);
}

static void frontend_push_debugger_intent(
    frontend *ui,
    frontend_debugger_intent_type type,
    uint16_t value)
{
    size_t next;

    if (ui == NULL || type == FRONTEND_DEBUGGER_INTENT_NONE) {
        return;
    }

    next = (ui->intent_write + 1u) % FRONTEND_DEBUGGER_INTENT_CAPACITY;
    if (next == ui->intent_read) {
        return;
    }

    memset(&ui->intents[ui->intent_write], 0, sizeof(ui->intents[ui->intent_write]));
    ui->intents[ui->intent_write].type = type;
    ui->intents[ui->intent_write].value = value;
    /* SET_EXECUTE and some register paths use address; keep both in sync. */
    ui->intents[ui->intent_write].address = value;
    ui->intent_write = next;
}

static void frontend_push_breakpoint_id_intent(
    frontend *ui,
    frontend_debugger_intent_type type,
    uint32_t id,
    bool enabled)
{
    size_t next;

    if (ui == NULL || type == FRONTEND_DEBUGGER_INTENT_NONE) {
        return;
    }

    next = (ui->intent_write + 1u) % FRONTEND_DEBUGGER_INTENT_CAPACITY;
    if (next == ui->intent_read) {
        return;
    }

    memset(&ui->intents[ui->intent_write], 0, sizeof(ui->intents[ui->intent_write]));
    ui->intents[ui->intent_write].type = type;
    ui->intents[ui->intent_write].id = id;
    ui->intents[ui->intent_write].enabled = enabled;
    ui->intent_write = next;
}

static void frontend_push_breakpoint_definition_intent(
    frontend *ui,
    frontend_debugger_intent_type type,
    uint32_t id,
    const runtime_breakpoint_definition *definition)
{
    size_t next;

    if (ui == NULL || definition == NULL || type == FRONTEND_DEBUGGER_INTENT_NONE) {
        return;
    }

    next = (ui->intent_write + 1u) % FRONTEND_DEBUGGER_INTENT_CAPACITY;
    if (next == ui->intent_read) {
        return;
    }

    ui->intents[ui->intent_write].type = type;
    ui->intents[ui->intent_write].id = id;
    ui->intents[ui->intent_write].breakpoint = *definition;
    ui->intent_write = next;
}

static void frontend_push_memory_request(
    frontend *ui,
    uint16_t address,
    uint16_t length,
    runtime_memory_mode mode)
{
    size_t next;

    if (ui == NULL || length == 0) {
        return;
    }

    next = (ui->intent_write + 1u) % FRONTEND_DEBUGGER_INTENT_CAPACITY;
    if (next == ui->intent_read) {
        return;
    }

    ui->intents[ui->intent_write].type = FRONTEND_DEBUGGER_INTENT_REQUEST_MEMORY;
    ui->intents[ui->intent_write].address = address;
    ui->intents[ui->intent_write].length = length;
    ui->intents[ui->intent_write].value = 0;
    ui->intents[ui->intent_write].memory_mode = mode;
    ui->intent_write = next;
}

static void frontend_push_memory_view_request(
    frontend *ui,
    uint16_t address,
    uint16_t length,
    runtime_memory_mode mode)
{
    size_t next;

    if (ui == NULL || length == 0) {
        return;
    }

    next = (ui->intent_write + 1u) % FRONTEND_DEBUGGER_INTENT_CAPACITY;
    if (next == ui->intent_read) {
        return;
    }

    ui->intents[ui->intent_write].type = FRONTEND_DEBUGGER_INTENT_REQUEST_MEMORY_VIEW;
    ui->intents[ui->intent_write].address = address;
    ui->intents[ui->intent_write].length = length;
    ui->intents[ui->intent_write].value = 0;
    ui->intents[ui->intent_write].memory_mode = mode;
    ui->intent_write = next;
}

static void frontend_push_debug_memory_request(frontend *ui, bool include_write_history)
{
    size_t next;

    if (ui == NULL) {
        return;
    }

    next = (ui->intent_write + 1u) % FRONTEND_DEBUGGER_INTENT_CAPACITY;
    if (next == ui->intent_read) {
        return;
    }

    ui->intents[ui->intent_write].type = FRONTEND_DEBUGGER_INTENT_REQUEST_DEBUG_MEMORY;
    ui->intents[ui->intent_write].include_write_history = include_write_history;
    ui->intent_write = next;
    ui->debug_memory_request_pending = true;
    ui->debug_memory_request_write_history = include_write_history;
}

static void frontend_push_memory_write_byte(
    frontend *ui,
    uint16_t address,
    uint8_t value,
    runtime_memory_mode mode)
{
    size_t next;

    if (ui == NULL) {
        return;
    }

    next = (ui->intent_write + 1u) % FRONTEND_DEBUGGER_INTENT_CAPACITY;
    if (next == ui->intent_read) {
        return;
    }

    ui->intents[ui->intent_write].type = FRONTEND_DEBUGGER_INTENT_MEMORY_WRITE_BYTE;
    ui->intents[ui->intent_write].address = address;
    ui->intents[ui->intent_write].length = 1;
    ui->intents[ui->intent_write].value = value;
    ui->intents[ui->intent_write].memory_mode = mode;
    ui->intent_write = next;
}

static void frontend_format_register_buffers(
    frontend_register_view_state *state,
    const runtime_cpu_snapshot *cpu,
    frontend_register_field except)
{
    static const uint8_t flag_bits[8] = {7, 6, 5, 4, 3, 2, 1, 0};
    size_t i;

    if (state == NULL || cpu == NULL) {
        return;
    }

    if (except != FRONTEND_REGISTER_FIELD_PC) {
        snprintf(state->pc, sizeof(state->pc), "%04X", cpu->pc);
    }
    if (except != FRONTEND_REGISTER_FIELD_SP) {
        snprintf(state->sp, sizeof(state->sp), "%02X", cpu->sp);
    }
    if (except != FRONTEND_REGISTER_FIELD_A) {
        snprintf(state->a, sizeof(state->a), "%02X", cpu->a);
    }
    if (except != FRONTEND_REGISTER_FIELD_X) {
        snprintf(state->x, sizeof(state->x), "%02X", cpu->x);
    }
    if (except != FRONTEND_REGISTER_FIELD_Y) {
        snprintf(state->y, sizeof(state->y), "%02X", cpu->y);
    }

    for (i = 0; i < 8; ++i) {
        frontend_register_field field = (frontend_register_field)(
            FRONTEND_REGISTER_FIELD_STATUS_N + (int)i);

        if (except == field) {
            continue;
        }

        state->flags[i][0] = (cpu->p & (uint8_t)(1u << flag_bits[i])) ? '1' : '0';
        state->flags[i][1] = '\0';
    }
}

static int frontend_hex_digit_value(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

static float frontend_memory_char_width(frontend *ui);

static bool frontend_parse_hex(const char *text, size_t max_digits, uint16_t *out)
{
    uint16_t value = 0;
    size_t i;
    size_t length;

    if (text == NULL || out == NULL) {
        return false;
    }

    length = strlen(text);
    if (length == 0 || length > max_digits) {
        return false;
    }

    for (i = 0; i < length; ++i) {
        int digit = frontend_hex_digit_value(text[i]);

        if (digit < 0) {
            return false;
        }

        value = (uint16_t)((value << 4) | (uint16_t)digit);
    }

    *out = value;
    return true;
}

static bool frontend_parse_flag(const char *text, uint8_t *out)
{
    if (text == NULL || out == NULL || text[0] == '\0' || text[1] != '\0') {
        return false;
    }
    if (text[0] != '0' && text[0] != '1') {
        return false;
    }

    *out = (uint8_t)(text[0] - '0');
    return true;
}

static void frontend_commit_register_edit(
    frontend *ui,
    frontend_register_field field,
    const frontend_debug_state *debug_state)
{
    static const uint8_t flag_bits[8] = {7, 6, 5, 4, 3, 2, 1, 0};
    frontend_register_view_state *state;
    uint16_t value;
    uint8_t flag_value;

    if (ui == NULL || debug_state == NULL || !debug_state->has_cpu) {
        return;
    }

    state = &ui->registers;
    if (debug_state->runtime_state != FRONTEND_RUNTIME_STATE_PAUSED ||
        debug_state->inspecting) {
        frontend_format_register_buffers(state, &debug_state->cpu, FRONTEND_REGISTER_FIELD_NONE);
        return;
    }

    switch (field) {
        case FRONTEND_REGISTER_FIELD_PC:
            if (frontend_parse_hex(state->pc, 4, &value)) {
                frontend_push_debugger_intent(ui, FRONTEND_DEBUGGER_INTENT_REGISTER_SET_PC, value);
                return;
            }
            break;

        case FRONTEND_REGISTER_FIELD_SP:
            if (frontend_parse_hex(state->sp, 2, &value)) {
                frontend_push_debugger_intent(ui, FRONTEND_DEBUGGER_INTENT_REGISTER_SET_SP, value);
                return;
            }
            break;

        case FRONTEND_REGISTER_FIELD_A:
            if (frontend_parse_hex(state->a, 2, &value)) {
                frontend_push_debugger_intent(ui, FRONTEND_DEBUGGER_INTENT_REGISTER_SET_A, value);
                return;
            }
            break;

        case FRONTEND_REGISTER_FIELD_X:
            if (frontend_parse_hex(state->x, 2, &value)) {
                frontend_push_debugger_intent(ui, FRONTEND_DEBUGGER_INTENT_REGISTER_SET_X, value);
                return;
            }
            break;

        case FRONTEND_REGISTER_FIELD_Y:
            if (frontend_parse_hex(state->y, 2, &value)) {
                frontend_push_debugger_intent(ui, FRONTEND_DEBUGGER_INTENT_REGISTER_SET_Y, value);
                return;
            }
            break;

        case FRONTEND_REGISTER_FIELD_STATUS_N:
        case FRONTEND_REGISTER_FIELD_STATUS_V:
        case FRONTEND_REGISTER_FIELD_STATUS_UNUSED:
        case FRONTEND_REGISTER_FIELD_STATUS_B:
        case FRONTEND_REGISTER_FIELD_STATUS_D:
        case FRONTEND_REGISTER_FIELD_STATUS_I:
        case FRONTEND_REGISTER_FIELD_STATUS_Z:
        case FRONTEND_REGISTER_FIELD_STATUS_C: {
            size_t flag_index = (size_t)(field - FRONTEND_REGISTER_FIELD_STATUS_N);

            if (frontend_parse_flag(state->flags[flag_index], &flag_value)) {
                uint8_t mask = (uint8_t)(1u << flag_bits[flag_index]);
                uint8_t status = (uint8_t)(debug_state->cpu.p & (uint8_t)~mask);

                if (flag_value != 0) {
                    status = (uint8_t)(status | mask);
                }
                frontend_push_debugger_intent(ui, FRONTEND_DEBUGGER_INTENT_REGISTER_SET_STATUS, status);
                return;
            }
            break;
        }

        case FRONTEND_REGISTER_FIELD_NONE:
        default:
            break;
    }

    frontend_format_register_buffers(state, &debug_state->cpu, FRONTEND_REGISTER_FIELD_NONE);
}

static uint32_t frontend_display_probe_flags(
    const frontend *ui,
    const frontend_debug_state *debug_state)
{
    const uint32_t mask = A2S_COL80 | A2S_ALTCHARSET | A2S_TEXT |
        A2S_MIXED | A2S_PAGE2 | A2S_HIRES | A2S_DHIRES;
    uint32_t flags;

    if (debug_state == NULL || !debug_state->has_apple_flags) {
        return 0u;
    }
    flags = debug_state->apple_state_flags;
    if (ui != NULL && ui->misc.display_override_enabled) {
        flags = (flags & ~mask) | (ui->misc.display_override_flags & mask);
    }
    return flags;
}

static void frontend_draw_display_placeholder(
    frontend *ui,
    struct nk_rect bounds,
    const frontend_debug_state *debug_state)
{
    /* Status band inside content (padding zeroed). Outer bounds.h-52 left the
       text below the window clip once title + default padding ate the slack. */
    enum { DISPLAY_STATUS_BAND_H = 24 };

    if (nk_begin(ui->ctx, "Machine Display", bounds, pane_flags)) {
        struct nk_style_window saved_window_style = ui->ctx->style.window;
        struct nk_rect content;
        struct nk_rect canvas_bounds;
        struct nk_rect status_bounds;
        struct nk_command_buffer *canvas;
        struct nk_rect image_bounds;
        bool have_image_bounds = false;
        char status[80];
        int status_len = 0;
        float image_h;

        ui->ctx->style.window.padding = nk_vec2(0.0f, 0.0f);
        ui->ctx->style.window.spacing = nk_vec2(0.0f, 0.0f);
        ui->ctx->style.window.group_padding = nk_vec2(0.0f, 0.0f);

        content = nk_window_get_content_region(ui->ctx);
        image_h = content.h - (float)DISPLAY_STATUS_BAND_H;
        if (image_h < 1.0f) {
            image_h = 1.0f;
        }

        nk_layout_row_dynamic(ui->ctx, image_h, 1);
        canvas_bounds = nk_widget_bounds(ui->ctx);
        canvas = nk_window_get_canvas(ui->ctx);
        nk_fill_rect(canvas, canvas_bounds, 0.0f,
            nk_rgb(FRONTEND_DISPLAY_MATTE_R,
                FRONTEND_DISPLAY_MATTE_G,
                FRONTEND_DISPLAY_MATTE_B));

        if (ui->has_frame && ui->display_texture != NULL) {
            SDL_Texture *render_texture = frontend_display_texture_for_render(ui);
            int texture_scale = render_texture == ui->crt_texture ?
                FRONTEND_CRT_RENDER_SCALE : 1;
            int tex_w = 0;
            int tex_h = 0;
            int fit_w = 0;
            int fit_h = 0;

            /* Nuklear UVs are region/tex_size — always query the real texture
               size so UV math cannot silently crop the Apple 560×192 frame. */
            if (render_texture != NULL &&
                SDL_QueryTexture(render_texture, NULL, NULL, &tex_w, &tex_h) == 0 &&
                tex_w > 0 && tex_h > 0) {
                struct nk_image image = nk_subimage_handle(
                    nk_handle_ptr(render_texture),
                    (nk_ushort)tex_w,
                    (nk_ushort)tex_h,
                    nk_rect(
                        (float)(frontend_display_crop_x_for_frame(&ui->current_frame) *
                            texture_scale),
                        (float)(frontend_display_crop_y_for_frame(&ui->current_frame) *
                            texture_scale),
                        (float)(frontend_display_crop_w_for_frame(&ui->current_frame) *
                            texture_scale),
                        (float)(frontend_display_crop_h_for_frame(&ui->current_frame) *
                            texture_scale)));

                if (frontend_display_fills_view(ui)) {
                    image_bounds = canvas_bounds;
                } else {
                    frontend_display_fit_source(ui, &fit_w, &fit_h);
                    image_bounds = frontend_fit_nk_rect(
                        canvas_bounds, (uint32_t)fit_w, (uint32_t)fit_h);
                }
                have_image_bounds = true;

                nk_draw_image(canvas, image_bounds, &image, nk_rgba(255, 255, 255, 255));
                if (!(render_texture == ui->crt_texture && ui->crt_effects.curvature)) {
                    nk_stroke_rect(canvas, image_bounds, 0.0f, 1.0f, nk_rgb(75, 94, 112));
                }
            }
        } else {
            nk_stroke_rect(canvas, canvas_bounds, 0.0f, 1.0f, nk_rgb(75, 94, 112));
            nk_draw_text(
                canvas,
                nk_rect(canvas_bounds.x + 14.0f, canvas_bounds.y + 14.0f, canvas_bounds.w - 28.0f, 20.0f),
                "waiting for frame",
                17,
                ui->ctx->style.font,
                nk_rgb(17, 22, 28),
                nk_rgb(196, 214, 228));
        }

        /* Soft-switch-locked pixel address in the reserved bottom band.
           Active only while paused (or inspecting) and hovering the picture. */
        if (have_image_bounds &&
            debug_state != NULL &&
            debug_state->has_apple_flags &&
            (debug_state->runtime_state == FRONTEND_RUNTIME_STATE_PAUSED ||
             debug_state->inspecting) &&
            !frontend_any_dialog_open(ui)) {
            uint16_t px = 0;
            uint16_t py = 0;
            int crop_w = frontend_display_crop_w_for_frame(&ui->current_frame);
            int crop_h = frontend_display_crop_h_for_frame(&ui->current_frame);
            const frontend_crt_effects *crt =
                (ui->crt_effects.curvature) ? &ui->crt_effects : NULL;

            if (frontend_crt_mouse_to_pixel(
                    ui->ctx->input.mouse.pos.x,
                    ui->ctx->input.mouse.pos.y,
                    image_bounds.x,
                    image_bounds.y,
                    image_bounds.w,
                    image_bounds.h,
                    crop_w,
                    crop_h,
                    crt,
                    &px,
                    &py)) {
                apple2_video_pixel_addr addr;
                uint32_t flags = frontend_display_probe_flags(ui, debug_state);

                if (apple2_video_pixel_address(flags, px, py, &addr)) {
                    if (addr.from_aux) {
                        status_len = snprintf(
                            status,
                            sizeof(status),
                            "bank: aux $%04X ofs: $%04X adr: $%X",
                            (unsigned)addr.bank_base,
                            (unsigned)addr.offset,
                            (unsigned)addr.host_addr);
                    } else {
                        status_len = snprintf(
                            status,
                            sizeof(status),
                            "bank: $%04X ofs: $%04X adr: $%04X",
                            (unsigned)addr.bank_base,
                            (unsigned)addr.offset,
                            (unsigned)addr.host_addr);
                    }
                }
            }
        }

        nk_layout_row_dynamic(ui->ctx, (float)DISPLAY_STATUS_BAND_H, 1);
        status_bounds = nk_widget_bounds(ui->ctx);
        if (status_len > 0) {
            float text_y = status_bounds.y +
                (status_bounds.h - ui->ctx->style.font->height) * 0.5f;
            nk_draw_text(
                canvas,
                nk_rect(
                    status_bounds.x + 8.0f,
                    text_y,
                    status_bounds.w - 16.0f,
                    ui->ctx->style.font->height),
                status,
                status_len,
                ui->ctx->style.font,
                nk_rgb(FRONTEND_DISPLAY_MATTE_R,
                    FRONTEND_DISPLAY_MATTE_G,
                    FRONTEND_DISPLAY_MATTE_B),
                nk_rgb(196, 214, 228));
        }

        if (!frontend_any_dialog_open(ui) && ui->active_view == FRONTEND_ACTIVE_VIEW_MACHINE) {
            frontend_draw_active_view_border(ui->ctx);
        }
        ui->ctx->style.window = saved_window_style;
    }
    nk_end(ui->ctx);
}

static void frontend_draw_register_edit(
    frontend *ui,
    frontend_register_field field,
    char *buffer,
    int max,
    nk_plugin_filter filter,
    const frontend_debug_state *debug_state,
    bool editable)
{
    /* NK_EDIT_FIELD includes NK_EDIT_ALWAYS_INSERT_MODE which forces insert
       mode every frame. Use the component flags directly so we can stay in
       replace (overwrite) mode. */
    nk_flags edit_flags = NK_EDIT_SELECTABLE | NK_EDIT_CLIPBOARD | NK_EDIT_SIG_ENTER;
    nk_flags result;

    if (!editable) {
        edit_flags |= NK_EDIT_READ_ONLY;
    }

    result = nk_edit_string_zero_terminated(ui->ctx, edit_flags, buffer, max, filter);
    if ((result & NK_EDIT_ACTIVE) != 0 && editable) {
        /* Keep the widget in replace (overwrite) mode so the user can type a
           new value directly without deleting the old one first. */
        ui->ctx->current->edit.mode = NK_TEXT_EDIT_MODE_REPLACE;
    }
    if ((result & NK_EDIT_ACTIVATED) != 0 && editable) {
        ui->registers.active_field = field;
    }
    if ((result & NK_EDIT_COMMITED) != 0) {
        frontend_commit_register_edit(ui, field, debug_state);
        ui->registers.active_field = FRONTEND_REGISTER_FIELD_NONE;
        /* NK_EDIT_SIG_ENTER signals the commit but does not deactivate the
           widget; call unfocus so the field loses the cursor immediately. */
        nk_edit_unfocus(ui->ctx);
    }
}

static void frontend_draw_register_pair(
    frontend *ui,
    const char *label,
    frontend_register_field field,
    char *buffer,
    int max,
    float label_w,
    float edit_w,
    const frontend_debug_state *debug_state,
    bool editable)
{
    nk_layout_row_push(ui->ctx, label_w);
    nk_label(ui->ctx, label, NK_TEXT_LEFT);
    nk_layout_row_push(ui->ctx, edit_w);
    frontend_draw_register_edit(ui, field, buffer, max, nk_filter_hex, debug_state, editable);
}

static void frontend_draw_flag_pair(
    frontend *ui,
    const char *label,
    size_t index,
    const frontend_debug_state *debug_state,
    bool editable)
{
    nk_layout_row_push(ui->ctx, 0.055f);
    nk_label(ui->ctx, label, NK_TEXT_LEFT);
    nk_layout_row_push(ui->ctx, 0.070f);
    frontend_draw_register_edit(
        ui,
        (frontend_register_field)(FRONTEND_REGISTER_FIELD_STATUS_N + (int)index),
        ui->registers.flags[index],
        (int)sizeof(ui->registers.flags[index]),
        nk_filter_binary,
        debug_state,
        editable);
}

static void frontend_draw_registers(
    frontend *ui,
    struct nk_rect bounds,
    const frontend_debug_state *debug_state)
{
    bool editable;

    if (debug_state == NULL) {
        return;
    }

    editable = debug_state->has_cpu &&
        debug_state->runtime_state == FRONTEND_RUNTIME_STATE_PAUSED &&
        !debug_state->inspecting;

    if (!editable) {
        ui->registers.active_field = FRONTEND_REGISTER_FIELD_NONE;
    }

    if (debug_state->has_cpu) {
        frontend_format_register_buffers(
            &ui->registers,
            &debug_state->cpu,
            ui->registers.active_field);
    }

    if (ui->cancel_register_edit_requested) {
        if (debug_state->has_cpu) {
            frontend_format_register_buffers(
                &ui->registers,
                &debug_state->cpu,
                FRONTEND_REGISTER_FIELD_NONE);
        }
        ui->registers.active_field = FRONTEND_REGISTER_FIELD_NONE;
        ui->cancel_register_edit_requested = false;
    }

    if (nk_begin(ui->ctx, "CPU", bounds, pane_flags)) {
        if (!debug_state->has_cpu) {
            nk_layout_row_dynamic(ui->ctx, 20.0f, 1);
            nk_label(ui->ctx, frontend_runtime_state_name(debug_state->runtime_state), NK_TEXT_LEFT);
            nk_label(ui->ctx, "PC ----  SP --  A --  X --  Y --", NK_TEXT_LEFT);
            nk_label(ui->ctx, "N -  V -  - -  B -  D -  I -  Z -  C -", NK_TEXT_LEFT);
        } else {
            nk_layout_row_begin(ui->ctx, NK_DYNAMIC, 22.0f, 10);
            frontend_draw_register_pair(
                ui,
                "PC",
                FRONTEND_REGISTER_FIELD_PC,
                ui->registers.pc,
                (int)sizeof(ui->registers.pc),
                0.07f,
                0.19f,
                debug_state,
                editable);
            frontend_draw_register_pair(
                ui,
                "SP",
                FRONTEND_REGISTER_FIELD_SP,
                ui->registers.sp,
                (int)sizeof(ui->registers.sp),
                0.07f,
                0.13f,
                debug_state,
                editable);
            frontend_draw_register_pair(
                ui,
                "A",
                FRONTEND_REGISTER_FIELD_A,
                ui->registers.a,
                (int)sizeof(ui->registers.a),
                0.05f,
                0.12f,
                debug_state,
                editable);
            frontend_draw_register_pair(
                ui,
                "X",
                FRONTEND_REGISTER_FIELD_X,
                ui->registers.x,
                (int)sizeof(ui->registers.x),
                0.05f,
                0.12f,
                debug_state,
                editable);
            frontend_draw_register_pair(
                ui,
                "Y",
                FRONTEND_REGISTER_FIELD_Y,
                ui->registers.y,
                (int)sizeof(ui->registers.y),
                0.05f,
                0.12f,
                debug_state,
                editable);
            nk_layout_row_end(ui->ctx);

            nk_layout_row_begin(ui->ctx, NK_DYNAMIC, 22.0f, 16);
            frontend_draw_flag_pair(ui, "N", 0, debug_state, editable);
            frontend_draw_flag_pair(ui, "V", 1, debug_state, editable);
            frontend_draw_flag_pair(ui, "-", 2, debug_state, editable);
            frontend_draw_flag_pair(ui, "B", 3, debug_state, editable);
            frontend_draw_flag_pair(ui, "D", 4, debug_state, editable);
            frontend_draw_flag_pair(ui, "I", 5, debug_state, editable);
            frontend_draw_flag_pair(ui, "Z", 6, debug_state, editable);
            frontend_draw_flag_pair(ui, "C", 7, debug_state, editable);
            nk_layout_row_end(ui->ctx);
        }
    }
    nk_end(ui->ctx);
}

static frontend_disasm_cache *frontend_disassembly_active_cache(
    frontend_disassembly_view_state *view)
{
    int idx = (int)view->mode;
    if (idx < 0 || idx >= FRONTEND_DISASM_MODE_CACHE_COUNT) {
        idx = 0;
    }
    return &view->mem_cache[idx];
}

static const frontend_disasm_cache *frontend_disassembly_active_cache_ro(
    const frontend_disassembly_view_state *view)
{
    int idx = (int)view->mode;
    if (idx < 0 || idx >= FRONTEND_DISASM_MODE_CACHE_COUNT) {
        idx = 0;
    }
    return &view->mem_cache[idx];
}

static void frontend_disasm_cache_invalidate_range(
    frontend_disasm_cache *cache,
    uint16_t address,
    uint16_t length)
{
    uint16_t i;
    for (i = 0; i < length; ++i) {
        cache->valid[(uint16_t)(address + i)] = false;
    }
}

static void frontend_disasm_cache_merge(
    frontend_disasm_cache *cache,
    const runtime_memory_snapshot *snap)
{
    uint16_t i;
    for (i = 0; i < snap->length; ++i) {
        uint16_t addr = (uint16_t)(snap->address + i);
        cache->bytes[addr] = snap->bytes[i];
        cache->valid[addr] = true;
    }
}

static const uint8_t *frontend_debug_memory_source(
    const runtime_debug_memory_snapshot *snapshot,
    runtime_memory_mode mode)
{
    if (snapshot == NULL) {
        return NULL;
    }
    switch (mode) {
    case RUNTIME_MEMORY_MODE_MAIN:
        return snapshot->ram;
    case RUNTIME_MEMORY_MODE_ROM:
        return snapshot->rom;
    case RUNTIME_MEMORY_MODE_AUX:
        return snapshot->aux;
    case RUNTIME_MEMORY_MODE_LC1:
        return snapshot->lc1;
    case RUNTIME_MEMORY_MODE_LC2:
        return snapshot->lc2;
    case RUNTIME_MEMORY_MODE_MAP:
    default:
        return snapshot->map;
    }
}

static const uint8_t *frontend_debug_memory_valid_source(
    const runtime_debug_memory_snapshot *snapshot,
    runtime_memory_mode mode)
{
    if (snapshot == NULL) {
        return NULL;
    }
    switch (mode) {
    case RUNTIME_MEMORY_MODE_AUX:
        return snapshot->aux_valid;
    case RUNTIME_MEMORY_MODE_LC1:
        return snapshot->lc1_valid;
    case RUNTIME_MEMORY_MODE_LC2:
        return snapshot->lc2_valid;
    default:
        return NULL;
    }
}

static void frontend_memory_search_open(frontend *ui)
{
    frontend_memory_view_state *memory;

    if (ui == NULL || ui->memory_view_count <= 0) {
        return;
    }
    memory = &ui->memory_views[ui->memory_active_view_index];
    ui->memory_search.open = true;
    ui->memory_search.just_opened = true;
    ui->memory_search.last_found_address = memory->cursor_address;
    ui->memory_search.status[0] = '\0';
}

static bool frontend_memory_search_run(
    frontend *ui,
    const frontend_debug_state *debug_state,
    bool reverse)
{
    frontend_memory_view_state *memory;
    const uint8_t *bytes;
    const uint8_t *valid;
    uint16_t found;

    if (ui == NULL || ui->memory_view_count <= 0 || !ui->memory_search.has_pattern) {
        return false;
    }
    if (debug_state == NULL || !debug_state->has_debug_memory) {
        snprintf(ui->memory_search.status, sizeof(ui->memory_search.status),
            "Memory snapshot unavailable");
        return false;
    }

    memory = &ui->memory_views[ui->memory_active_view_index];
    bytes = frontend_debug_memory_source(&debug_state->debug_memory, memory->mode);
    valid = frontend_debug_memory_valid_source(&debug_state->debug_memory, memory->mode);
    if (!memory_search_find(
            bytes,
            valid,
            &ui->memory_search.pattern,
            ui->memory_search.last_found_address,
            reverse,
            &found)) {
        snprintf(ui->memory_search.status, sizeof(ui->memory_search.status), "Not found");
        return false;
    }

    ui->memory_search.last_found_address = found;
    snprintf(ui->memory_search.status, sizeof(ui->memory_search.status),
        "Found: %04X", found);
    memory->cursor_address = found;
    memory->view_address = found;
    memory->edit_field = FRONTEND_MEMORY_EDIT_HEX;
    memory->active_nibble = 0u;
    memory->request_pending = false;
    return true;
}

static void frontend_disasm_cache_replace(
    frontend_disasm_cache *cache,
    const uint8_t *bytes)
{
    if (cache == NULL || bytes == NULL) {
        return;
    }
    memcpy(cache->bytes, bytes, MACHINE_ADDRESS_SPACE);
    memset(cache->valid, 1, sizeof(cache->valid));
}

static bool frontend_disasm_cache_range_valid(
    const frontend_disasm_cache *cache,
    uint16_t address,
    uint16_t length)
{
    uint16_t i;
    for (i = 0; i < length; ++i) {
        if (!cache->valid[(uint16_t)(address + i)]) {
            return false;
        }
    }
    return true;
}

static void frontend_disassembly_decode(frontend *ui)
{
    frontend_disassembly_view_state *view = &ui->disassembly;
    const frontend_disasm_cache *cache = frontend_disassembly_active_cache_ro(view);
    uint16_t address = view->top_address;
    uint8_t row;

    for (row = 0; row < view->rows && row < FRONTEND_DISASM_MAX_ROWS; ++row) {
        uint8_t fetched[3];
        size_t available = disasm_pc_lock_fetch(cache, address, fetched);

        view->lines[row].base = disasm_6502_decode_line(
            address,
            available > 0u ? fetched : NULL,
            available,
            &ui->symbols);
        view->lines[row].is_provisional = (available == 0u);
        address = (uint16_t)(address + view->lines[row].base.length);
    }
}

static int frontend_disassembly_find_row(
    const frontend_disassembly_view_state *view,
    uint16_t address)
{
    uint8_t row;

    if (view == NULL) {
        return -1;
    }

    for (row = 0; row < view->rows && row < FRONTEND_DISASM_MAX_ROWS; ++row) {
        if (view->lines[row].base.address == address) {
            return (int)row;
        }
    }

    return -1;
}

static void frontend_disassembly_set_user_cursor(
    frontend_disassembly_view_state *view,
    uint16_t address,
    uint8_t row,
    uint8_t length)
{
    if (view == NULL) {
        return;
    }

    view->has_user_cursor = true;
    view->cursor_address = address;
    view->cursor_row = row;
    view->cursor_length = length == 0 ? 1 : length;
    if (row > 0 &&
        row < view->rows &&
        view->lines[row].base.address == address) {
        view->cursor_prev_address = view->lines[row - 1u].base.address;
    } else {
        view->cursor_prev_address = (uint16_t)(address - 1u);
    }
}

static uint16_t frontend_disassembly_center_top(uint16_t address, uint8_t rows)
{
    uint8_t back = rows > 0 ? rows / 2 : 0;
    uint8_t i;
    uint16_t top = address;

    if (back > FRONTEND_DISASM_CENTER_LOOKBACK) {
        back = FRONTEND_DISASM_CENTER_LOOKBACK;
    }

    for (i = 0; i < back; ++i) {
        top = (uint16_t)(top - 1u);
    }

    return top;
}

static bool frontend_disassembly_previous_address(
    const frontend_disassembly_view_state *view,
    uint16_t address,
    uint16_t *out_previous)
{
    const frontend_disasm_cache *cache;
    int back;
    uint16_t invalid_candidate = 0;
    bool has_invalid_candidate = false;

    if (view == NULL || out_previous == NULL) {
        return false;
    }

    cache = frontend_disassembly_active_cache_ro(view);

    for (back = 3; back >= 1; --back) {
        uint16_t candidate = (uint16_t)(address - back);
        uint8_t opcode;
        uint8_t length;

        if (!cache->valid[candidate]) {
            continue;
        }

        opcode = cache->bytes[candidate];
        length = disasm_6502_instruction_length(opcode);
        if (length == (uint8_t)back &&
            cache->valid[(uint16_t)(candidate + length - 1u)]) {
            if (disasm_6502_opcode_is_valid(opcode)) {
                *out_previous = candidate;
                return true;
            }
            if (!has_invalid_candidate) {
                invalid_candidate = candidate;
                has_invalid_candidate = true;
            }
        }
    }

    if (has_invalid_candidate) {
        *out_previous = invalid_candidate;
        return true;
    }

    if (cache->valid[(uint16_t)(address - 1u)]) {
        *out_previous = (uint16_t)(address - 1u);
        return true;
    }

    return false;
}

static bool frontend_disassembly_pc_locked_top(
    const frontend_disassembly_view_state *view,
    uint16_t pc,
    uint16_t *out_top)
{
    uint8_t target_row;
    uint8_t i;
    uint16_t address;

    if (view == NULL || out_top == NULL || view->rows == 0) {
        return false;
    }

    target_row = view->rows / 2;
    address = pc;
    for (i = 0; i < target_row; ++i) {
        uint16_t previous;

        if (!frontend_disassembly_previous_address(view, address, &previous)) {
            previous = (uint16_t)(address - 1u);
        }
        address = previous;
    }

    *out_top = address;
    return true;
}

static bool frontend_disassembly_cache_covers(
    const frontend_disassembly_view_state *view,
    uint16_t address,
    uint16_t length)
{
    if (view == NULL || length == 0) {
        return false;
    }
    return frontend_disasm_cache_range_valid(
        frontend_disassembly_active_cache_ro(view), address, length);
}

static uint16_t frontend_disassembly_visible_decode_length(const frontend_disassembly_view_state *view)
{
    uint16_t length;

    if (view == NULL) {
        return 16;
    }

    length = (uint16_t)((uint16_t)view->rows * 3u + 16u);
    if (length > FRONTEND_DISASM_FETCH_BYTES) {
        return FRONTEND_DISASM_FETCH_BYTES;
    }
    return length;
}

static void frontend_disassembly_request_if_needed(frontend *ui, const frontend_debug_state *debug_state)
{
    frontend_disassembly_view_state *view = &ui->disassembly;
    bool want_history = ui->memory_context_popup.open || ui->disassembly_context_popup.open;

    if (debug_state != NULL && debug_state->has_debug_memory) {
        uint64_t generation = debug_state->debug_memory.generation;
        if (generation != view->cache_generation) {
            frontend_disasm_cache_replace(
                &view->mem_cache[RUNTIME_MEMORY_MODE_MAP],
                debug_state->debug_memory.map);
            frontend_disasm_cache_replace(
                &view->mem_cache[RUNTIME_MEMORY_MODE_MAIN],
                debug_state->debug_memory.ram);
            frontend_disasm_cache_replace(
                &view->mem_cache[RUNTIME_MEMORY_MODE_ROM],
                debug_state->debug_memory.rom);
            frontend_disasm_cache_replace(
                &view->mem_cache[RUNTIME_MEMORY_MODE_AUX],
                debug_state->debug_memory.aux);
            frontend_disasm_cache_replace(
                &view->mem_cache[RUNTIME_MEMORY_MODE_LC1],
                debug_state->debug_memory.lc1);
            frontend_disasm_cache_replace(
                &view->mem_cache[RUNTIME_MEMORY_MODE_LC2],
                debug_state->debug_memory.lc2);
            view->cache_generation = generation;
            ui->debug_memory_seen_generation = generation;
            ui->debug_memory_request_pending = false;
        }
        if (debug_state->debug_memory.has_write_history) {
            ui->debug_memory_request_write_history = false;
        }
    }

    if (ui->debug_memory_request_pending &&
        want_history &&
        !ui->debug_memory_request_write_history) {
        ui->debug_memory_request_pending = false;
    }

    if (!ui->debug_memory_request_pending) {
        frontend_push_debug_memory_request(ui, want_history);
    }
}

static bool frontend_disassembly_is_pc_locked(const frontend_disassembly_view_state *view)
{
    return view != NULL && view->pc_lock_active;
}

static void frontend_disassembly_force_refresh_pc(frontend_disassembly_view_state *view)
{
    if (view != NULL) {
        view->request_pending = false;
    }
}

static void frontend_disassembly_build_pc_locked_lines(
    frontend *ui,
    uint16_t pc,
    uint8_t rows)
{
    frontend_disassembly_view_state *view = &ui->disassembly;

    disasm_pc_lock_build(
        frontend_disassembly_active_cache_ro(view),
        &ui->symbols,
        pc,
        rows,
        view->lines,
        &view->top_address);
}

static void frontend_disassembly_follow_pc(frontend *ui, const frontend_debug_state *debug_state)
{
    frontend_disassembly_view_state *view = &ui->disassembly;

    if (debug_state == NULL || !debug_state->has_cpu) {
        return;
    }

    view->pc_lock_address = debug_state->cpu.pc;
    if (!frontend_disassembly_pc_locked_top(view, debug_state->cpu.pc, &view->top_address)) {
        view->top_address = frontend_disassembly_center_top(debug_state->cpu.pc, view->rows);
    }
    view->address_entry = false;
    view->follow_pc = true;
    view->pc_lock_active = true;
    view->has_user_cursor = false;
    view->request_pending = false;
}

static void frontend_disassembly_center_pc(frontend *ui, const frontend_debug_state *debug_state)
{
    frontend_disassembly_view_state *view = &ui->disassembly;

    if (debug_state == NULL || !debug_state->has_cpu) {
        return;
    }

    view->pc_lock_address = debug_state->cpu.pc;
    if (!frontend_disassembly_pc_locked_top(view, debug_state->cpu.pc, &view->top_address)) {
        view->top_address = frontend_disassembly_center_top(debug_state->cpu.pc, view->rows);
    }
    view->pc_lock_active = true;
    view->request_pending = false;
}

static void frontend_disassembly_center_cursor(frontend *ui)
{
    frontend_disassembly_view_state *view = &ui->disassembly;

    if (!view->has_user_cursor) {
        return;
    }

    view->top_address = frontend_disassembly_center_top(view->cursor_address, view->rows);
    view->cursor_row = view->rows / 2;
    view->request_pending = false;
}

static void frontend_disassembly_ensure_user_cursor(
    frontend *ui,
    const frontend_debug_state *debug_state)
{
    frontend_disassembly_view_state *view = &ui->disassembly;
    int pc_row;

    if (view->has_user_cursor) {
        return;
    }

    if (debug_state != NULL && debug_state->has_cpu) {
        pc_row = frontend_disassembly_find_row(view, debug_state->cpu.pc);
        if (pc_row >= 0) {
            frontend_disassembly_set_user_cursor(
                view,
                debug_state->cpu.pc,
                (uint8_t)pc_row,
                view->lines[pc_row].base.length);
        } else {
            frontend_disassembly_set_user_cursor(
                view,
                debug_state->cpu.pc,
                view->rows / 2,
                1);
        }
        return;
    }

    frontend_disassembly_set_user_cursor(view, view->top_address, 0, 1);
}

static void frontend_disassembly_scroll_to_top(frontend *ui, uint16_t address)
{
    ui->disassembly.top_address = address;
    frontend_disassembly_set_user_cursor(&ui->disassembly, address, 0, 1);
    ui->disassembly.request_pending = false;
    ui->disassembly.follow_pc = false;
    ui->disassembly.pc_lock_active = false;
}

static void frontend_disassembly_navigate_to_address(frontend *ui, uint16_t address)
{
    frontend_disassembly_view_state *view;

    if (ui == NULL) {
        return;
    }

    view = &ui->disassembly;
    view->top_address = frontend_disassembly_center_top(address, view->rows);
    frontend_disassembly_set_user_cursor(view, address, view->rows / 2u, 1);
    view->request_pending = false;
    view->follow_pc = false;
    view->pc_lock_active = false;
    view->address_entry = false;
    frontend_set_active_view(ui, FRONTEND_ACTIVE_VIEW_DISASSEMBLY);
}

static void frontend_disassembly_commit_goto_address(frontend_disassembly_view_state *view)
{
    view->pc_lock_address = view->cursor_address;
    view->pc_lock_active = true;
    view->follow_pc = false;
    view->request_pending = false;
}

static void frontend_disassembly_apply_address_digit(frontend *ui, int digit)
{
    frontend_disassembly_view_state *view = &ui->disassembly;
    int shift;
    uint16_t mask;

    if (digit < 0 || digit > 15) {
        return;
    }

    shift = (3 - view->active_address_digit) * 4;
    mask = (uint16_t)(0x0fu << shift);
    view->cursor_address = (uint16_t)((view->cursor_address & (uint16_t)~mask) |
        (uint16_t)((uint16_t)digit << shift));
    view->top_address = frontend_disassembly_center_top(view->cursor_address, view->rows);
    view->has_user_cursor = true;
    view->cursor_row = view->rows / 2u;
    view->cursor_length = 1;
    view->follow_pc = false;
    view->pc_lock_active = false;

    if (view->active_address_digit >= 3) {
        view->address_entry = false;
        view->active_address_digit = 0;
        frontend_disassembly_commit_goto_address(view);
    } else {
        view->active_address_digit++;
    }
}

static void frontend_disassembly_handle_key(
    frontend *ui,
    const frontend_debug_state *debug_state,
    const SDL_KeyboardEvent *key)
{
    frontend_disassembly_view_state *view;
    SDL_Keycode sym;
    SDL_Keymod mod;
    bool alt, shift;
    int row;

    if (ui == NULL || key == NULL || key->type != SDL_KEYDOWN) {
        return;
    }

    view = &ui->disassembly;
    if (debug_state != NULL && debug_state->runtime_state == FRONTEND_RUNTIME_STATE_RUNNING) {
        return;
    }

    sym = key->keysym.sym;
    mod = key->keysym.mod;
    alt = (mod & KMOD_ALT) != 0;
    shift = (mod & KMOD_SHIFT) != 0;

    if (alt && sym == SDLK_b) {
        frontend_disassembly_ensure_user_cursor(ui, debug_state);
        frontend_toggle_execute_breakpoint_at_cursor(ui, debug_state);
        return;
    }

    if (sym == SDLK_F9 || sym == SDLK_F10 || sym == SDLK_F11 || sym == SDLK_F12) {
        return;
    }

    if (alt && !shift && sym == SDLK_a) {
        frontend_disassembly_ensure_user_cursor(ui, debug_state);
        view->address_entry = !view->address_entry;
        view->active_address_digit = 0;
        view->follow_pc = false;
        view->pc_lock_active = false;
        return;
    }

    if (alt && sym == SDLK_s) {
        frontend_open_symbol_lookup(ui, false);
        return;
    }

    if (alt && !shift && sym == SDLK_m) {
        if (view->mode == RUNTIME_MEMORY_MODE_CPU_MAP) {
            view->mode = RUNTIME_MEMORY_MODE_ROM;
        } else if (view->mode == RUNTIME_MEMORY_MODE_ROM) {
            view->mode = RUNTIME_MEMORY_MODE_RAM;
        } else {
            view->mode = RUNTIME_MEMORY_MODE_CPU_MAP;
        }
        view->request_pending = false;
        return;
    }

    if (view->address_entry) {
        if (sym == SDLK_RETURN) {
            view->address_entry = false;
            frontend_disassembly_commit_goto_address(view);
            return;
        }
        if (sym == SDLK_HOME) {
            view->active_address_digit = 0;
            return;
        }
        if (sym == SDLK_END) {
            view->active_address_digit = 3;
            return;
        }
        if (sym == SDLK_LEFT) {
            if (view->active_address_digit > 0) {
                view->active_address_digit--;
            }
            return;
        }
        if (sym == SDLK_RIGHT) {
            if (view->active_address_digit >= 3) {
                view->address_entry = false;
                view->active_address_digit = 0;
            } else {
                view->active_address_digit++;
            }
            return;
        }
        frontend_disassembly_apply_address_digit(ui, frontend_hex_digit_value((char)sym));
        return;
    }

    frontend_disassembly_ensure_user_cursor(ui, debug_state);
    row = frontend_disassembly_find_row(view, view->cursor_address);

    if (sym == SDLK_PAGEUP) {
        uint16_t old_top = view->lines[0].base.address;
        view->top_address = (uint16_t)(old_top - (uint16_t)(view->rows > 0 ? view->rows - 1u : 0u));
        frontend_disassembly_set_user_cursor(
            view,
            old_top,
            view->rows > 0 ? view->rows - 1u : 0,
            view->lines[0].base.length);
        view->request_pending = false;
        view->follow_pc = false;
        view->pc_lock_active = false;
        return;
    }

    if (sym == SDLK_PAGEDOWN) {
        uint8_t last = view->rows > 0 ? view->rows - 1u : 0;
        view->top_address = view->lines[last].base.address;
        frontend_disassembly_set_user_cursor(view, view->top_address, 0, view->lines[last].base.length);
        view->request_pending = false;
        view->follow_pc = false;
        view->pc_lock_active = false;
        return;
    }

    if (sym == SDLK_HOME) {
        if (alt) {
            frontend_disassembly_scroll_to_top(ui, 0x0000);
        } else if (view->rows > 0) {
            frontend_disassembly_set_user_cursor(
                view, view->lines[0].base.address, 0, view->lines[0].base.length);
        }
        return;
    }

    if (sym == SDLK_END) {
        if (alt) {
            view->top_address = 0xffff;
            frontend_disassembly_set_user_cursor(
                view,
                0xffff,
                view->rows > 0 ? view->rows - 1u : 0,
                1);
            view->request_pending = false;
            view->follow_pc = false;
            view->pc_lock_active = false;
        } else if (view->rows > 0) {
            uint8_t last = view->rows - 1u;
            frontend_disassembly_set_user_cursor(
                view,
                view->lines[last].base.address,
                last,
                view->lines[last].base.length);
        }
        return;
    }

    if (sym == SDLK_UP) {
        if (row > 0) {
            frontend_disassembly_set_user_cursor(
                view,
                view->lines[row - 1].base.address,
                (uint8_t)(row - 1),
                view->lines[row - 1].base.length);
        } else if (row < 0) {
            frontend_disassembly_set_user_cursor(view, view->cursor_prev_address, view->rows / 2, 1);
            frontend_disassembly_center_cursor(ui);
        } else {
            uint16_t previous = view->cursor_prev_address;
            view->top_address = previous;
            frontend_disassembly_set_user_cursor(view, previous, 0, 1);
            view->request_pending = false;
        }
        view->follow_pc = false;
        view->pc_lock_active = false;
        return;
    }

    if (sym == SDLK_DOWN) {
        if (row >= 0 && row + 1 < view->rows) {
            frontend_disassembly_set_user_cursor(
                view,
                view->lines[row + 1].base.address,
                (uint8_t)(row + 1),
                view->lines[row + 1].base.length);
        } else if (row < 0) {
            uint16_t next = (uint16_t)(view->cursor_address + view->cursor_length);
            frontend_disassembly_set_user_cursor(view, next, view->rows / 2, 1);
            frontend_disassembly_center_cursor(ui);
        } else if (view->rows > 0) {
            view->top_address = view->rows > 1 ? view->lines[1].base.address :
                (uint16_t)(view->top_address + 1u);
            frontend_disassembly_set_user_cursor(
                view,
                view->lines[view->rows - 1u].base.address,
                view->rows - 1u,
                view->lines[view->rows - 1u].base.length);
            view->request_pending = false;
        }
        view->follow_pc = false;
        view->pc_lock_active = false;
        return;
    }

    if (sym == SDLK_LEFT) {
        if (alt && debug_state != NULL &&
            debug_state->runtime_state == FRONTEND_RUNTIME_STATE_PAUSED) {
            if (debug_state->inspecting) {
                /* Time travel: Opt+Left is unbound. */
            } else {
                frontend_push_debugger_intent(
                    ui,
                    FRONTEND_DEBUGGER_INTENT_REGISTER_SET_PC,
                    view->cursor_address);
            }
        } else if (row < 0) {
            frontend_disassembly_center_cursor(ui);
        }
        return;
    }

    if (sym == SDLK_RIGHT) {
        frontend_disassembly_follow_pc(ui, debug_state);
        return;
    }
}

static char frontend_disassembly_line_char_at(const char *line, int index)
{
    size_t length;

    if (line == NULL || index < 0) {
        return ' ';
    }

    length = strlen(line);
    if ((size_t)index >= length) {
        return ' ';
    }

    return line[index];
}

static void frontend_disassembly_draw_address_cursor(
    frontend *ui,
    struct nk_rect row_bounds,
    const char *line)
{
    struct nk_command_buffer *canvas;
    float char_w;
    struct nk_rect cursor_rect;
    char text[2];
    int text_col;

    if (ui == NULL || line == NULL || !ui->disassembly.active || !ui->disassembly.address_entry) {
        return;
    }

    text_col = 3 + ui->disassembly.active_address_digit;
    char_w = frontend_memory_char_width(ui);
    cursor_rect = nk_rect(
        row_bounds.x + char_w * (float)text_col,
        row_bounds.y + 1.0f,
        char_w,
        row_bounds.h - 2.0f);
    text[0] = frontend_disassembly_line_char_at(line, text_col);
    text[1] = '\0';

    canvas = nk_window_get_canvas(ui->ctx);
    nk_fill_rect(canvas, cursor_rect, 0.0f, nk_rgb(255, 244, 120));
    nk_draw_text(
        canvas,
        cursor_rect,
        text,
        1,
        ui->ctx->style.font,
        nk_rgb(255, 244, 120),
        nk_rgb(20, 24, 28));
}

static void frontend_disassembly_handle_mouse_row(
    frontend *ui,
    const frontend_debug_state *debug_state,
    struct nk_rect row_bounds,
    uint8_t row)
{
    float char_w;
    float rel_x;
    int text_col;

    if (!nk_input_is_mouse_pressed(&ui->ctx->input, NK_BUTTON_LEFT) ||
        !nk_input_is_mouse_hovering_rect(&ui->ctx->input, row_bounds)) {
        return;
    }

    if (debug_state != NULL && debug_state->runtime_state == FRONTEND_RUNTIME_STATE_RUNNING) {
        frontend_set_active_view(ui, FRONTEND_ACTIVE_VIEW_DISASSEMBLY);
        return;
    }

    frontend_set_active_view(ui, FRONTEND_ACTIVE_VIEW_DISASSEMBLY);
    frontend_disassembly_set_user_cursor(
        &ui->disassembly,
        ui->disassembly.lines[row].base.address,
        row,
        ui->disassembly.lines[row].base.length);

    char_w = frontend_memory_char_width(ui);
    rel_x = ui->ctx->input.mouse.pos.x - row_bounds.x;
    if (rel_x < 0.0f) {
        rel_x = 0.0f;
    }
    text_col = (int)(rel_x / char_w);
    if (text_col >= 3 && text_col <= 6) {
        ui->disassembly.address_entry = true;
        ui->disassembly.active_address_digit = (uint8_t)(text_col - 3);
    } else {
        ui->disassembly.address_entry = false;
    }
}

static void frontend_disassembly_draw_scrollbar(
    frontend *ui,
    const frontend_debug_state *debug_state,
    struct nk_rect bounds)
{
    frontend_disassembly_view_state *view;
    struct nk_command_buffer *canvas;
    struct nk_rect track;
    struct nk_rect thumb;
    const struct nk_mouse *mouse;
    float visible_fraction;
    float thumb_h;
    float thumb_y;
    float usable_h;

    if (ui == NULL || bounds.h <= 0.0f) {
        return;
    }

    ui->disassembly_scrollbar_bounds = bounds;
    ui->has_disassembly_scrollbar_bounds = true;

    view = &ui->disassembly;
    canvas = nk_window_get_canvas(ui->ctx);
    mouse = &ui->ctx->input.mouse;
    track = nk_rect(bounds.x + 2.0f, bounds.y + 2.0f, bounds.w - 4.0f, bounds.h - 4.0f);

    visible_fraction = ((float)view->rows * 2.0f) / 65536.0f;
    thumb_h = track.h * visible_fraction;
    if (thumb_h < 16.0f) {
        thumb_h = 16.0f;
    }
    if (thumb_h > track.h) {
        thumb_h = track.h;
    }

    usable_h = track.h - thumb_h;
    thumb_y = track.y + usable_h * ((float)view->top_address / 65535.0f);
    thumb = nk_rect(track.x + 2.0f, thumb_y, track.w - 4.0f, thumb_h);

    if (nk_input_is_mouse_hovering_rect(&ui->ctx->input, thumb) &&
        nk_input_is_mouse_down(&ui->ctx->input, NK_BUTTON_LEFT) &&
        !view->scrollbar_dragging) {
        frontend_set_active_view(ui, FRONTEND_ACTIVE_VIEW_DISASSEMBLY);
        view->scrollbar_dragging = true;
        view->scrollbar_grab_offset = mouse->pos.y - thumb.y;
    }

    if (!nk_input_is_mouse_down(&ui->ctx->input, NK_BUTTON_LEFT)) {
        view->scrollbar_dragging = false;
    }

    if (view->scrollbar_dragging) {
        float y = mouse->pos.y - view->scrollbar_grab_offset;
        float relative;

        if (y < track.y) {
            y = track.y;
        }
        if (y > track.y + usable_h) {
            y = track.y + usable_h;
        }

        relative = usable_h > 0.0f ? (y - track.y) / usable_h : 0.0f;
        if (debug_state == NULL || debug_state->runtime_state != FRONTEND_RUNTIME_STATE_RUNNING) {
            view->top_address = (uint16_t)(relative * 65535.0f);
            frontend_disassembly_set_user_cursor(view, view->top_address, 0, 1);
            view->follow_pc = false;
            view->pc_lock_active = false;
            view->request_pending = false;
        }
    } else if (nk_input_is_mouse_hovering_rect(&ui->ctx->input, track) &&
        nk_input_is_mouse_pressed(&ui->ctx->input, NK_BUTTON_LEFT) &&
        !nk_input_is_mouse_hovering_rect(&ui->ctx->input, thumb)) {
        frontend_set_active_view(ui, FRONTEND_ACTIVE_VIEW_DISASSEMBLY);
        if (debug_state == NULL || debug_state->runtime_state != FRONTEND_RUNTIME_STATE_RUNNING) {
            if (mouse->pos.y < thumb.y) {
                view->top_address = (uint16_t)(view->top_address - (uint16_t)(view->rows * 2u));
            } else {
                view->top_address = (uint16_t)(view->top_address + (uint16_t)(view->rows * 2u));
            }
            frontend_disassembly_set_user_cursor(view, view->top_address, 0, 1);
            view->follow_pc = false;
            view->pc_lock_active = false;
            view->request_pending = false;
        }
    }

    nk_fill_rect(canvas, track, 0.0f, nk_rgb(35, 41, 47));
    nk_fill_rect(
        canvas,
        thumb,
        2.0f,
        nk_input_is_mouse_hovering_rect(&ui->ctx->input, thumb) || view->scrollbar_dragging ?
            nk_rgb(160, 174, 186) :
            nk_rgb(103, 124, 139));
}

/*
 * Effective-address / value annotation for the trailing disassembly column.
 * `show` gates whether the column is rendered at all; `has_value` distinguishes
 * a data access (which carries the current byte at `address`) from a control-flow
 * target (address only). Register/pointer-derived targets are only meaningful
 * while the machine is paused, so the caller must supply a paused snapshot.
 */
typedef struct frontend_disasm_target {
    bool show;
    bool has_value;
    uint16_t address;
    uint8_t value;
} frontend_disasm_target;

static bool frontend_disasm_map_get(const frontend *ui, uint16_t addr, uint8_t *out)
{
    const frontend_disasm_cache *cache =
        &ui->disassembly.mem_cache[RUNTIME_MEMORY_MODE_CPU_MAP];
    if (!cache->valid[addr]) {
        return false;
    }
    *out = cache->bytes[addr];
    return true;
}

static bool frontend_disasm_has_label(const frontend *ui, uint16_t addr)
{
    char label[32];
    if (ui->symbols.address_to_label == NULL) {
        return false;
    }
    return ui->symbols.address_to_label(
        ui->symbols.userdata, addr, label, sizeof(label)) == SYMBOL_LOOKUP_FOUND;
}

static frontend_disasm_target frontend_disassembly_compute_target(
    const frontend *ui,
    const frontend_debug_state *debug_state,
    const disasm_6502_line *line)
{
    frontend_disasm_target result = {false, false, 0, 0};
    disasm_6502_mode mode;
    uint8_t opcode;
    uint8_t x;
    uint8_t y;
    uint8_t b1;
    uint16_t op16;
    uint8_t lo;
    uint8_t hi;

    if (ui == NULL || debug_state == NULL || line == NULL) {
        return result;
    }
    /* Registers and zero-page pointers are only a coherent snapshot while paused. */
    if (!debug_state->has_cpu ||
        debug_state->runtime_state == FRONTEND_RUNTIME_STATE_RUNNING) {
        return result;
    }
    if (line->forced_byte || line->length == 0) {
        return result;
    }

    opcode = line->bytes[0];
    if (!disasm_6502_opcode_is_valid(opcode)) {
        return result;
    }

    mode = disasm_6502_opcode_mode(opcode);
    x = debug_state->cpu.x;
    y = debug_state->cpu.y;
    b1 = line->bytes[1];
    op16 = (uint16_t)(line->bytes[1] | ((uint16_t)line->bytes[2] << 8));

    switch (mode) {
        case DISASM_MODE_IMP:
        case DISASM_MODE_ACC:
        case DISASM_MODE_IMM:
        case DISASM_MODE_ZP:
            /* No memory target, or a plain literal address that is already visible. */
            return result;
        case DISASM_MODE_ZPX:
            result.address = (uint8_t)(b1 + x); /* zero-page wrap */
            result.has_value = true;
            result.show = true;
            break;
        case DISASM_MODE_ZPY:
            result.address = (uint8_t)(b1 + y); /* zero-page wrap */
            result.has_value = true;
            result.show = true;
            break;
        case DISASM_MODE_ABSX:
            result.address = (uint16_t)(op16 + x);
            result.has_value = true;
            result.show = true;
            break;
        case DISASM_MODE_ABSY:
            result.address = (uint16_t)(op16 + y);
            result.has_value = true;
            result.show = true;
            break;
        case DISASM_MODE_INDX:
            if (!frontend_disasm_map_get(ui, (uint8_t)(b1 + x), &lo) ||
                !frontend_disasm_map_get(ui, (uint8_t)(b1 + x + 1u), &hi)) {
                return result;
            }
            result.address = (uint16_t)(lo | ((uint16_t)hi << 8));
            result.has_value = true;
            result.show = true;
            break;
        case DISASM_MODE_INDY:
            if (!frontend_disasm_map_get(ui, b1, &lo) ||
                !frontend_disasm_map_get(ui, (uint8_t)(b1 + 1u), &hi)) {
                return result;
            }
            result.address = (uint16_t)((uint16_t)(lo | ((uint16_t)hi << 8)) + y);
            result.has_value = true;
            result.show = true;
            break;
        case DISASM_MODE_IND: {
            /* JMP (ind): replicate the 6502 page-boundary wrap bug on the high byte. */
            uint16_t hi_addr = (uint16_t)((op16 & 0xFF00u) | (uint16_t)((op16 + 1u) & 0x00FFu));
            if (!frontend_disasm_map_get(ui, op16, &lo) ||
                !frontend_disasm_map_get(ui, hi_addr, &hi)) {
                return result;
            }
            result.address = (uint16_t)(lo | ((uint16_t)hi << 8));
            result.show = true; /* control-flow target, no value */
            break;
        }
        case DISASM_MODE_ABS:
            /* Direct address: only annotate when the operand was rendered as a label. */
            result.address = op16;
            result.has_value = (opcode != 0x4Cu && opcode != 0x20u); /* not JMP/JSR */
            result.show = frontend_disasm_has_label(ui, op16);
            break;
        case DISASM_MODE_REL:
            result.address = (uint16_t)(line->address + 2u + (int8_t)b1);
            result.show = frontend_disasm_has_label(ui, result.address);
            break;
    }

    if (result.show && result.has_value) {
        if (!frontend_disasm_map_get(ui, result.address, &result.value)) {
            result.has_value = false;
        }
    }
    return result;
}

static void frontend_draw_disassembly_view(
    frontend *ui,
    struct nk_rect bounds,
    const frontend_debug_state *debug_state)
{
    frontend_disassembly_view_state *view = &ui->disassembly;
    struct nk_style_window saved_window_style;
    uint8_t row;
    uint8_t rows;
    float row_h;
    float scrollbar_w = 24.0f;
    float scrollbar_margin = 8.0f;
    float content_h;

    if (!view->initialized) {
        view->mode = RUNTIME_MEMORY_MODE_CPU_MAP;
        view->follow_pc = true;
        view->initialized = true;
        if (debug_state != NULL && debug_state->has_cpu) {
            view->top_address = debug_state->cpu.pc;
        }
    }

    row_h = ui->ctx->style.font != NULL ? ui->ctx->style.font->height : 13.0f;
    content_h = bounds.h - 28.0f;
    rows = (uint8_t)((content_h > row_h) ? (content_h / row_h) : 1);
    if (rows > 1) {
        rows--;
    }
    if (rows == 0) {
        rows = 1;
    }
    if (rows > FRONTEND_DISASM_MAX_ROWS) {
        rows = FRONTEND_DISASM_MAX_ROWS;
    }
    view->rows = rows;

    if (debug_state != NULL && debug_state->has_cpu) {
        bool pc_changed = !view->has_last_pc || view->last_pc != debug_state->cpu.pc;

        if (debug_state->runtime_state == FRONTEND_RUNTIME_STATE_RUNNING) {
            frontend_disassembly_follow_pc(ui, debug_state);
        } else if (pc_changed) {
            frontend_disassembly_center_pc(ui, debug_state);
        } else if (view->follow_pc) {
            int pc_row = frontend_disassembly_find_row(view, debug_state->cpu.pc);
            if (pc_row < 0 || pc_row != rows / 2) {
                frontend_disassembly_center_pc(ui, debug_state);
            }
        }

        view->last_pc = debug_state->cpu.pc;
        view->has_last_pc = true;
    }
    if (debug_state != NULL) {
        view->last_runtime_state = debug_state->runtime_state;
    }

    if (!frontend_any_dialog_open(ui) &&
        nk_input_is_mouse_hovering_rect(&ui->ctx->input, bounds) &&
        ui->ctx->input.mouse.scroll_delta.y != 0.0f) {
        if (debug_state == NULL || debug_state->runtime_state != FRONTEND_RUNTIME_STATE_RUNNING) {
            int32_t lines = ui->ctx->input.mouse.scroll_delta.y > 0.0f ? -3 : 3;
            view->top_address = (uint16_t)(view->top_address + lines);
            view->request_pending = false;
            view->follow_pc = false;
            view->pc_lock_active = false;
            frontend_disassembly_set_user_cursor(view, view->top_address, 0, 1);
        }
    }

    if (ui->has_pending_disassembly_key) {
        if (view->active) {
            frontend_disassembly_handle_key(ui, debug_state, &ui->pending_disassembly_key);
        }
        ui->has_pending_disassembly_key = false;
    }

    /* Force cache refresh on RUNNING→PAUSED transition or CPU PC change while paused */
    if (debug_state != NULL && debug_state->has_cpu && view->pc_lock_active && view->follow_pc) {
        bool was_running = view->last_runtime_state == FRONTEND_RUNTIME_STATE_RUNNING;
        bool now_paused = debug_state->runtime_state != FRONTEND_RUNTIME_STATE_RUNNING;
        bool cpu_pc_changed = !view->has_last_pc || view->last_pc != debug_state->cpu.pc;
        if ((was_running && now_paused) || (now_paused && cpu_pc_changed)) {
            frontend_disassembly_force_refresh_pc(view);
        }
    }

    frontend_disassembly_request_if_needed(ui, debug_state);
    if (frontend_disassembly_is_pc_locked(view)) {
        frontend_disassembly_build_pc_locked_lines(ui, view->pc_lock_address, view->rows);
    } else {
        frontend_disassembly_decode(ui);
    }

    if (nk_begin(ui->ctx, "Disassembly", bounds, pane_flags)) {
        saved_window_style = ui->ctx->style.window;
        ui->ctx->style.window.padding = nk_vec2(0.0f, 0.0f);
        ui->ctx->style.window.spacing = nk_vec2(0.0f, 0.0f);
        ui->ctx->style.window.group_padding = nk_vec2(0.0f, 0.0f);

        nk_layout_row_begin(ui->ctx, NK_STATIC, row_h * (float)rows, 3);
        nk_layout_row_push(ui->ctx, bounds.w - scrollbar_w - scrollbar_margin);
        if (nk_group_begin(ui->ctx, "disassembly-rows", NK_WINDOW_NO_SCROLLBAR)) {
            for (row = 0; row < rows; ++row) {
                frontend_disassembly_line *line = &view->lines[row];
                char bytes[16];
                char address_label[32] = "";
                char rendered[128];
                struct nk_rect row_bounds;
                const runtime_breakpoint_snapshot_entry *breakpoint =
                    frontend_find_execute_breakpoint(debug_state, line->base.address);
                bool is_pc = debug_state != NULL && debug_state->has_cpu &&
                    line->base.address == debug_state->cpu.pc;
                bool is_cursor = view->has_user_cursor &&
                    line->base.address == view->cursor_address && !is_pc;
                bool is_breakpoint = breakpoint != NULL;
                bool is_enabled_breakpoint = is_breakpoint && breakpoint->enabled != 0;
                struct nk_style_selectable saved_selectable = ui->ctx->style.selectable;
                nk_bool selected = is_cursor ? nk_true : nk_false;

                if (line->is_provisional) {
                    snprintf(bytes, sizeof(bytes), "??      ");
                } else if (line->base.length == 2) {
                    snprintf(bytes, sizeof(bytes), "%02X %02X   ",
                        line->base.bytes[0], line->base.bytes[1]);
                } else if (line->base.length >= 3) {
                    snprintf(bytes, sizeof(bytes), "%02X %02X %02X",
                        line->base.bytes[0], line->base.bytes[1], line->base.bytes[2]);
                } else {
                    snprintf(bytes, sizeof(bytes), "%02X      ", line->base.bytes[0]);
                }

                if (ui->symbols.address_to_label != NULL) {
                    (void)ui->symbols.address_to_label(
                        ui->symbols.userdata,
                        line->base.address,
                        address_label,
                        sizeof(address_label));
                }
                address_label[15] = '\0';

                {
                    char target[24] = "";
                    frontend_disasm_target tgt =
                        frontend_disassembly_compute_target(ui, debug_state, &line->base);
                    if (tgt.show) {
                        if (tgt.has_value) {
                            snprintf(target, sizeof(target), " [$%04X:%02X]",
                                tgt.address, tgt.value);
                        } else {
                            snprintf(target, sizeof(target), " [$%04X]", tgt.address);
                        }
                    }
                    snprintf(rendered, sizeof(rendered), "%c%c %04X %-15s %-8s %-20s%s",
                        is_pc ? '>' : ' ',
                        is_breakpoint ? (is_enabled_breakpoint ? 'X' : 'x') : ' ',
                        line->base.address,
                        address_label,
                        bytes,
                        line->base.text,
                        target);
                }

                if (line->is_provisional) {
                    ui->ctx->style.selectable.normal = nk_style_item_color(nk_rgb(20, 24, 28));
                    ui->ctx->style.selectable.hover = nk_style_item_color(nk_rgb(25, 29, 33));
                    ui->ctx->style.selectable.normal_active = nk_style_item_color(nk_rgb(30, 60, 74));
                    ui->ctx->style.selectable.text_normal = nk_rgb(72, 80, 88);
                } else if (line->base.forced_byte) {
                    ui->ctx->style.selectable.normal = nk_style_item_color(nk_rgb(30, 34, 38));
                    ui->ctx->style.selectable.hover = nk_style_item_color(nk_rgb(39, 45, 51));
                    ui->ctx->style.selectable.normal_active = nk_style_item_color(nk_rgb(49, 78, 94));
                    ui->ctx->style.selectable.text_normal = nk_rgb(125, 136, 145);
                }
                if (is_pc) {
                    ui->ctx->style.selectable.normal = nk_style_item_color(nk_rgb(83, 73, 24));
                    ui->ctx->style.selectable.text_normal = nk_rgb(255, 244, 120);
                }
                if (is_breakpoint && !is_pc) {
                    ui->ctx->style.selectable.text_normal = is_enabled_breakpoint ?
                        nk_rgb(255, 151, 122) :
                        nk_rgb(169, 126, 202);
                }
                if (is_cursor) {
                    ui->ctx->style.selectable.normal_active = nk_style_item_color(nk_rgb(21, 91, 116));
                    ui->ctx->style.selectable.text_normal_active = nk_rgb(226, 246, 255);
                    ui->ctx->style.selectable.text_hover_active = nk_rgb(226, 246, 255);
                    ui->ctx->style.selectable.text_pressed_active = nk_rgb(226, 246, 255);
                }

                nk_layout_row_dynamic(ui->ctx, row_h, 1);
                row_bounds = nk_widget_bounds(ui->ctx);
                frontend_disassembly_handle_mouse_row(ui, debug_state, row_bounds, row);
                if (nk_selectable_label(ui->ctx, rendered, NK_TEXT_LEFT, &selected)) {
                    if (debug_state == NULL || debug_state->runtime_state != FRONTEND_RUNTIME_STATE_RUNNING) {
                        frontend_set_active_view(ui, FRONTEND_ACTIVE_VIEW_DISASSEMBLY);
                        frontend_disassembly_set_user_cursor(
                            view, line->base.address, row, line->base.length);
                        view->address_entry = false;
                        view->follow_pc = false;
                        view->pc_lock_active = false;
                    }
                }
                frontend_disassembly_draw_address_cursor(ui, row_bounds, rendered);
                ui->ctx->style.selectable = saved_selectable;
            }
            nk_group_end(ui->ctx);
        }
        nk_layout_row_push(ui->ctx, scrollbar_w);
        if (nk_group_begin(ui->ctx, "disassembly-scrollbar", NK_WINDOW_NO_SCROLLBAR)) {
            struct nk_rect scrollbar_bounds = nk_window_get_content_region(ui->ctx);
            frontend_disassembly_draw_scrollbar(ui, debug_state, scrollbar_bounds);
            nk_group_end(ui->ctx);
        }
        nk_layout_row_push(ui->ctx, scrollbar_margin);
        nk_spacing(ui->ctx, 1);
        nk_layout_row_end(ui->ctx);

        if (nk_input_is_mouse_click_in_rect(&ui->ctx->input, NK_BUTTON_RIGHT, bounds)) {
            frontend_context_popup_open(
                ui,
                &ui->disassembly_context_popup,
                120.0f,
                100.0f);
        }

        if (frontend_context_popup_begin(
                ui,
                &ui->disassembly_context_popup,
                "dasm-context-menu")) {
            bool close_popup = false;

            if (frontend_context_menu_source_modes(ui, &view->mode, &close_popup)) {
                view->request_pending = false;
            }
            frontend_context_popup_end(ui, &ui->disassembly_context_popup, close_popup);
        }

        if (!frontend_any_dialog_open(ui) && ui->active_view == FRONTEND_ACTIVE_VIEW_DISASSEMBLY) {
            frontend_draw_active_view_border(ui->ctx);
        }
        frontend_draw_memory_mode_border(
            ui->ctx,
            view->mode,
            !frontend_any_dialog_open(ui) && ui->active_view == FRONTEND_ACTIVE_VIEW_DISASSEMBLY);

        ui->ctx->style.window = saved_window_style;
    }
    nk_end(ui->ctx);
}

static uint16_t frontend_memory_visible_count(const frontend_memory_view_state *memory)
{
    return (uint16_t)((uint16_t)memory->columns * (uint16_t)memory->rows);
}

static bool frontend_memory_cursor_visible(const frontend_memory_view_state *memory)
{
    uint16_t offset = (uint16_t)(memory->cursor_address - memory->view_address);
    return offset < frontend_memory_visible_count(memory);
}

static void frontend_memory_recenter_cursor(frontend_memory_view_state *memory)
{
    uint16_t visible = frontend_memory_visible_count(memory);
    uint16_t offset;
    uint16_t col_in_view;

    if (visible == 0 || memory->columns == 0 || frontend_memory_cursor_visible(memory)) {
        return;
    }

    /* offset = cursor_address - view_address in uint16; wraps when cursor is above view.
       col_in_view uses that same wrapping modulo to preserve the view's column alignment
       regardless of whether view_address is row-aligned. */
    offset = (uint16_t)(memory->cursor_address - memory->view_address);
    col_in_view = (uint16_t)(offset % memory->columns);

    if (offset >= 0x8000u) {
        /* cursor above view: scroll up so cursor lands on row 0, same column */
        memory->view_address = (uint16_t)(memory->cursor_address - col_in_view);
    } else {
        /* cursor below view: scroll down so cursor lands on last row, same column */
        memory->view_address = (uint16_t)(memory->cursor_address - col_in_view -
            (uint16_t)((memory->rows - 1u) * memory->columns));
    }
}

static int frontend_memory_snapshot_index(
    const runtime_memory_snapshot *snapshot,
    uint16_t address)
{
    uint16_t offset;

    if (snapshot == NULL) {
        return -1;
    }

    offset = (uint16_t)(address - snapshot->address);
    if (offset >= snapshot->length) {
        return -1;
    }

    return (int)offset;
}

static uint64_t frontend_memory_snapshot_write_history_at(
    const runtime_memory_snapshot *snapshot,
    uint16_t address)
{
    int index = frontend_memory_snapshot_index(snapshot, address);

    return index >= 0 ? snapshot->write_history[index] : 0;
}

static uint64_t frontend_disassembly_write_history_at(
    const frontend_debug_state *debug_state,
    const frontend_disassembly_view_state *view,
    uint16_t address)
{
    (void)view;
    if (debug_state == NULL ||
        !debug_state->has_debug_memory ||
        !debug_state->debug_memory.has_write_history) {
        return 0;
    }
    return debug_state->debug_memory.write_history[address];
}

static uint64_t frontend_memory_write_history_at(
    const frontend_debug_state *debug_state,
    const frontend_memory_view_state *view,
    uint16_t address)
{
    if (debug_state == NULL ||
        view == NULL ||
        frontend_memory_mode_is_drive(view->mode) ||
        !debug_state->has_debug_memory ||
        !debug_state->debug_memory.has_write_history) {
        return 0;
    }
    return debug_state->debug_memory.write_history[address];
}

static void frontend_context_menu_label(struct nk_context *ctx, const char *label)
{
    nk_layout_row_dynamic(ctx, 22.0f, 1);
    nk_label(ctx, label, NK_TEXT_LEFT);
}

static void frontend_context_menu_separator(struct nk_context *ctx)
{
    struct nk_rect bounds;

    nk_layout_row_dynamic(ctx, 5.0f, 1);
    if (nk_widget(&bounds, ctx) != NK_WIDGET_INVALID) {
        struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
        float y = bounds.y + bounds.h * 0.5f;
        nk_stroke_line(
            canvas,
            bounds.x,
            y,
            bounds.x + bounds.w,
            y,
            1.0f,
            nk_rgb(90, 101, 110));
    }
}

static void frontend_context_menu_heading(struct nk_context *ctx, const char *label)
{
    frontend_context_menu_label(ctx, label);
    frontend_context_menu_separator(ctx);
}

static bool frontend_context_menu_item(struct nk_context *ctx, const char *label)
{
    nk_bool selected = nk_false;

    nk_layout_row_dynamic(ctx, 22.0f, 1);
    return nk_selectable_label(ctx, label, NK_TEXT_LEFT, &selected) != 0;
}

static bool frontend_context_menu_mode_item(
    struct nk_context *ctx,
    bool active,
    const char *label)
{
    char item[16];
    nk_bool selected = nk_false;

    snprintf(item, sizeof(item), "%c %s", active ? '*' : ' ', label);
    nk_layout_row_dynamic(ctx, 22.0f, 1);
    return nk_selectable_label(ctx, item, NK_TEXT_LEFT, &selected) != 0;
}

static bool frontend_context_menu_access(
    struct nk_context *ctx,
    uint64_t write_history,
    uint16_t *out_address)
{
    int lane;
    bool selected = false;

    frontend_context_menu_heading(ctx, "Access");
    for (lane = 3; lane >= 0; lane--) {
        char item[5];
        unsigned shift = (unsigned)lane * 16u;
        uint16_t address = (uint16_t)((write_history >> shift) & 0xffffu);

        snprintf(item, sizeof(item), "%04X", (unsigned)address);
        if (frontend_context_menu_item(ctx, item)) {
            if (out_address != NULL) {
                *out_address = address;
            }
            selected = true;
        }
    }
    return selected;
}

static void frontend_context_popup_open(
    frontend *ui,
    frontend_context_popup_state *popup,
    float width,
    float desired_height)
{
    struct nk_rect origin;
    struct nk_rect viewport;
    struct nk_vec2 pos;
    float height;
    float x;
    float y;
    float max_height;
    int window_w = 0;
    int window_h = 0;

    if (ui == NULL || ui->ctx == NULL || popup == NULL) {
        return;
    }

    origin = nk_window_get_content_region(ui->ctx);
    platform_window_get_size(ui->window, &window_w, &window_h);
    if (window_w > 16 && window_h > 16) {
        viewport = nk_rect(4.0f, 4.0f, (float)window_w - 8.0f, (float)window_h - 8.0f);
    } else {
        viewport = origin;
    }

    pos = ui->ctx->input.mouse.buttons[NK_BUTTON_RIGHT].clicked_pos;
    max_height = viewport.h;
    if (max_height < 60.0f) {
        max_height = viewport.h > 0.0f ? viewport.h : 60.0f;
    }
    height = desired_height > max_height ? max_height : desired_height;
    if (height < 60.0f) {
        height = 60.0f;
    }

    x = pos.x;
    y = pos.y;
    if (x + width > viewport.x + viewport.w) {
        x = viewport.x + viewport.w - width;
    }
    if (x < viewport.x) {
        x = viewport.x;
    }
    if (y + height > viewport.y + viewport.h) {
        y = viewport.y + viewport.h - height;
    }
    if (y < viewport.y) {
        y = viewport.y;
    }

    popup->open = true;
    popup->just_opened = true;
    popup->scroll = height < desired_height;
    popup->group_open = false;
    popup->rect = nk_rect(x - origin.x, y - origin.y, width, height);
    popup->screen_rect = nk_rect(x, y, width, height);
}

static bool frontend_context_popup_begin(
    frontend *ui,
    frontend_context_popup_state *popup,
    const char *title)
{
    const struct nk_input *input;
    bool click_outside;

    if (ui == NULL || ui->ctx == NULL || popup == NULL || !popup->open) {
        return false;
    }

    input = &ui->ctx->input;
    click_outside =
        (nk_input_is_mouse_pressed(input, NK_BUTTON_LEFT) ||
         nk_input_is_mouse_pressed(input, NK_BUTTON_RIGHT)) &&
        !nk_input_is_mouse_hovering_rect(input, popup->screen_rect);
    if (!popup->just_opened && click_outside) {
        popup->open = false;
        return false;
    }
    popup->just_opened = false;

    if (!nk_popup_begin(ui->ctx, NK_POPUP_STATIC, title, NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR, popup->rect)) {
        popup->open = false;
        return false;
    }

    if (popup->scroll) {
        nk_layout_row_dynamic(ui->ctx, popup->rect.h - 8.0f, 1);
        popup->group_open = nk_group_begin(ui->ctx, title, 0) ? true : false;
        if (!popup->group_open) {
            return true;
        }
    }
    return true;
}

static void frontend_context_popup_end(
    frontend *ui,
    frontend_context_popup_state *popup,
    bool close_popup)
{
    if (ui == NULL || ui->ctx == NULL || popup == NULL) {
        return;
    }

    if (popup->scroll && popup->group_open) {
        nk_group_end(ui->ctx);
        popup->group_open = false;
    }
    if (close_popup) {
        popup->open = false;
        nk_popup_close(ui->ctx);
    }
    nk_popup_end(ui->ctx);
}

static uint8_t frontend_memory_view_byte_at(
    const frontend_debug_state *debug_state,
    const frontend_memory_view_state *view,
    uint16_t address)
{
    const uint8_t *bytes;

    if (debug_state == NULL || view == NULL || !debug_state->has_debug_memory) {
        return 0;
    }

    bytes = frontend_debug_memory_source(&debug_state->debug_memory, view->mode);
    return bytes != NULL ? bytes[address] : 0;
}

static bool frontend_memory_view_byte_available(
    const frontend_debug_state *debug_state,
    const frontend_memory_view_state *view,
    uint16_t address)
{
    const uint8_t *valid;

    if (debug_state == NULL || view == NULL || !debug_state->has_debug_memory) {
        return false;
    }

    valid = frontend_debug_memory_valid_source(&debug_state->debug_memory, view->mode);
    return valid == NULL || valid[address] != 0;
}

static char frontend_memory_ascii(uint8_t value, bool apple_ascii)
{
    uint8_t shown = apple_ascii ? (uint8_t)(value & 0x7fu) : value;

    if (shown >= 32u && shown <= 126u) {
        return (char)shown;
    }

    return '.';
}

static uint8_t frontend_memory_ascii_store_byte(uint8_t value, bool apple_ascii)
{
    if (apple_ascii) {
        return (uint8_t)(value | 0x80u);
    }
    return value;
}

static void frontend_memory_request_if_needed(frontend *ui, const frontend_debug_state *debug_state)
{
    int v;
    bool want_history;

    if (ui == NULL) {
        return;
    }

    want_history = ui->memory_context_popup.open || ui->disassembly_context_popup.open;

    if (debug_state != NULL && debug_state->has_debug_memory) {
        if (debug_state->debug_memory.generation != ui->debug_memory_seen_generation) {
            ui->debug_memory_seen_generation = debug_state->debug_memory.generation;
            ui->debug_memory_request_pending = false;
        }
        if (debug_state->debug_memory.has_write_history) {
            ui->debug_memory_request_write_history = false;
        }
    }

    if (ui->debug_memory_request_pending &&
        want_history &&
        !ui->debug_memory_request_write_history) {
        ui->debug_memory_request_pending = false;
    }

    if (!ui->debug_memory_request_pending) {
        frontend_push_debug_memory_request(ui, want_history);
    }

    for (v = 0; v < ui->memory_view_count; v++) {
        frontend_memory_view_state *memory = &ui->memory_views[v];
        uint16_t length = frontend_memory_visible_count(memory);

        if (length == 0) {
            continue;
        }

        memory->requested_address = memory->view_address;
        memory->requested_length = length;
        memory->requested_mode = memory->mode;
        memory->request_pending = false;
    }
}

static void frontend_memory_move_cursor(frontend *ui, int32_t delta)
{
    frontend_memory_view_state *memory = &ui->memory_views[ui->memory_active_view_index];

    memory->cursor_address = (uint16_t)(memory->cursor_address + delta);
    frontend_memory_recenter_cursor(memory);
    memory->request_pending = false;
}

static void frontend_memory_write_byte(
    frontend *ui,
    const frontend_debug_state *debug_state,
    uint16_t address,
    uint8_t value)
{
    frontend_memory_view_state *memory = &ui->memory_views[ui->memory_active_view_index];

    if (debug_state == NULL ||
        debug_state->runtime_state != FRONTEND_RUNTIME_STATE_PAUSED ||
        debug_state->inspecting) {
        return;
    }
    if (!frontend_memory_mode_is_editable(memory->mode) ||
        !frontend_memory_view_byte_available(debug_state, memory, address)) {
        return;
    }

    frontend_push_memory_write_byte(ui, address, value, memory->mode);
    memory->request_pending = false;
    ui->debug_memory_request_pending = false;
}

static void frontend_memory_apply_hex_digit(
    frontend *ui,
    const frontend_debug_state *debug_state,
    int digit)
{
    frontend_memory_view_state *memory = &ui->memory_views[ui->memory_active_view_index];
    uint8_t old_value;
    uint8_t new_value;

    if (digit < 0 || digit > 15) {
        return;
    }
    if (!frontend_memory_mode_is_editable(memory->mode) ||
        !frontend_memory_view_byte_available(debug_state, memory, memory->cursor_address)) {
        return;
    }

    old_value = frontend_memory_view_byte_at(debug_state, memory, memory->cursor_address);
    if (memory->active_nibble == 0) {
        new_value = (uint8_t)((old_value & 0x0fu) | (uint8_t)(digit << 4));
        frontend_memory_write_byte(ui, debug_state, memory->cursor_address, new_value);
        memory->active_nibble = 1;
    } else {
        new_value = (uint8_t)((old_value & 0xf0u) | (uint8_t)digit);
        frontend_memory_write_byte(ui, debug_state, memory->cursor_address, new_value);
        memory->active_nibble = 0;
        frontend_memory_move_cursor(ui, 1);
    }
}

static void frontend_memory_apply_address_digit(frontend *ui, int digit)
{
    frontend_memory_view_state *memory = &ui->memory_views[ui->memory_active_view_index];
    int shift;
    uint16_t mask;

    if (digit < 0 || digit > 15) {
        return;
    }

    shift = (3 - memory->active_address_digit) * 4;
    mask = (uint16_t)(0x0fu << shift);
    memory->view_address = (uint16_t)((memory->view_address & (uint16_t)~mask) |
        (uint16_t)((uint16_t)digit << shift));
    memory->cursor_address = memory->view_address;

    if (memory->active_address_digit >= 3) {
        memory->edit_field = FRONTEND_MEMORY_EDIT_HEX;
        memory->active_address_digit = 0;
    } else {
        memory->active_address_digit++;
    }
    memory->request_pending = false;
}

static float frontend_memory_char_width(frontend *ui)
{
    const struct nk_user_font *font;

    if (ui == NULL || ui->ctx == NULL) {
        return 8.0f;
    }

    font = ui->ctx->style.font;
    if (font == NULL || font->width == NULL) {
        return 8.0f;
    }

    return font->width(font->userdata, font->height, "0", 1);
}

static int frontend_memory_cursor_text_col(const frontend_memory_view_state *memory)
{
    uint16_t offset;
    uint8_t col;

    if (memory == NULL || memory->columns == 0) {
        return 0;
    }

    if (memory->edit_field == FRONTEND_MEMORY_EDIT_ADDRESS) {
        return memory->active_address_digit;
    }

    offset = (uint16_t)(memory->cursor_address - memory->view_address);
    col = (uint8_t)(offset % memory->columns);
    if (memory->edit_field == FRONTEND_MEMORY_EDIT_ASCII) {
        return 5 + (int)memory->columns * 3 + col;
    }

    return 5 + (int)col * 3 + memory->active_nibble;
}

static char frontend_memory_line_char_at(const char *line, int index)
{
    size_t length;

    if (line == NULL || index < 0) {
        return ' ';
    }

    length = strlen(line);
    if ((size_t)index >= length) {
        return ' ';
    }

    return line[index];
}

static void frontend_memory_draw_cursor(
    frontend *ui,
    const frontend_debug_state *debug_state,
    frontend_memory_view_state *memory,
    struct nk_rect row_bounds,
    const char *line,
    uint16_t row_addr)
{
    struct nk_command_buffer *canvas;
    uint16_t row_offset;
    int text_col;
    float char_w;
    struct nk_rect cursor_rect;
    char text[2];

    if (ui == NULL ||
        line == NULL ||
        ui->active_view != FRONTEND_ACTIVE_VIEW_MEMORY ||
        debug_state == NULL ||
        debug_state->runtime_state != FRONTEND_RUNTIME_STATE_PAUSED) {
        return;
    }
    row_offset = (uint16_t)(memory->cursor_address - row_addr);
    if (memory->edit_field == FRONTEND_MEMORY_EDIT_ADDRESS &&
        (row_offset >= memory->columns || memory->cursor_address != row_addr)) {
        return;
    }

    if (memory->edit_field != FRONTEND_MEMORY_EDIT_ADDRESS && row_offset >= memory->columns) {
        return;
    }

    text_col = frontend_memory_cursor_text_col(memory);
    char_w = frontend_memory_char_width(ui);
    cursor_rect = nk_rect(
        row_bounds.x + char_w * (float)text_col,
        row_bounds.y + 1.0f,
        char_w,
        row_bounds.h - 2.0f);
    text[0] = frontend_memory_line_char_at(line, text_col);
    text[1] = '\0';

    canvas = nk_window_get_canvas(ui->ctx);
    nk_fill_rect(canvas, cursor_rect, 0.0f, nk_rgb(255, 244, 120));
    nk_draw_text(
        canvas,
        cursor_rect,
        text,
        1,
        ui->ctx->style.font,
        nk_rgb(255, 244, 120),
        nk_rgb(20, 24, 28));
}

static void frontend_memory_draw_scrollbar(
    frontend *ui,
    struct nk_rect bounds,
    uint16_t visible_count)
{
    frontend_memory_view_state *memory;
    struct nk_command_buffer *canvas;
    float visible_fraction;
    float thumb_h;
    float thumb_y;
    float usable_h;
    struct nk_rect track;
    struct nk_rect thumb;
    const struct nk_mouse *mouse;

    if (ui == NULL || bounds.h <= 0.0f) {
        return;
    }

    ui->memory_scrollbar_bounds = bounds;
    ui->has_memory_scrollbar_bounds = true;

    memory = &ui->memory_views[ui->memory_active_view_index];
    canvas = nk_window_get_canvas(ui->ctx);
    mouse = &ui->ctx->input.mouse;
    track = nk_rect(bounds.x + 2.0f, bounds.y + 2.0f, bounds.w - 4.0f, bounds.h - 4.0f);

    visible_fraction = (float)visible_count / 65536.0f;
    thumb_h = track.h * visible_fraction;
    if (thumb_h < 16.0f) {
        thumb_h = 16.0f;
    }
    if (thumb_h > track.h) {
        thumb_h = track.h;
    }

    usable_h = track.h - thumb_h;
    thumb_y = track.y + usable_h * ((float)memory->view_address / 65535.0f);
    thumb = nk_rect(track.x + 2.0f, thumb_y, track.w - 4.0f, thumb_h);

    if (nk_input_is_mouse_hovering_rect(&ui->ctx->input, thumb) &&
        nk_input_is_mouse_down(&ui->ctx->input, NK_BUTTON_LEFT) &&
        !memory->scrollbar_dragging) {
        frontend_set_active_view(ui, FRONTEND_ACTIVE_VIEW_MEMORY);
        memory->scrollbar_dragging = true;
        memory->scrollbar_grab_offset = mouse->pos.y - thumb.y;
    }

    if (!nk_input_is_mouse_down(&ui->ctx->input, NK_BUTTON_LEFT)) {
        memory->scrollbar_dragging = false;
    }

    if (memory->scrollbar_dragging) {
        float y = mouse->pos.y - memory->scrollbar_grab_offset;
        float relative;

        if (y < track.y) {
            y = track.y;
        }
        if (y > track.y + usable_h) {
            y = track.y + usable_h;
        }

        relative = usable_h > 0.0f ? (y - track.y) / usable_h : 0.0f;
        memory->view_address = (uint16_t)(relative * 65535.0f);
        memory->request_pending = false;
    } else if (nk_input_is_mouse_hovering_rect(&ui->ctx->input, track) &&
        nk_input_is_mouse_pressed(&ui->ctx->input, NK_BUTTON_LEFT) &&
        !nk_input_is_mouse_hovering_rect(&ui->ctx->input, thumb)) {
        frontend_set_active_view(ui, FRONTEND_ACTIVE_VIEW_MEMORY);
        if (mouse->pos.y < thumb.y) {
            memory->view_address = (uint16_t)(memory->view_address - visible_count);
        } else {
            memory->view_address = (uint16_t)(memory->view_address + visible_count);
        }
        memory->request_pending = false;
    }

    nk_fill_rect(canvas, track, 0.0f, nk_rgb(35, 41, 47));
    nk_fill_rect(
        canvas,
        thumb,
        2.0f,
        nk_input_is_mouse_hovering_rect(&ui->ctx->input, thumb) || memory->scrollbar_dragging ?
            nk_rgb(160, 174, 186) :
            nk_rgb(103, 124, 139));
}

static void frontend_memory_draw_status_footer(
    frontend *ui,
    const frontend_debug_state *debug_state,
    struct nk_rect content,
    float footer_h)
{
    struct nk_command_buffer *canvas;
    struct nk_rect footer;
    struct nk_rect field_rect;
    struct nk_rect address_rect;
    struct nk_rect edit_rect;
    const char *field;
    const char *status_text;
    char address[32];
    char edit_status[48];
    frontend_memory_view_state *memory = &ui->memory_views[ui->memory_active_view_index];
    bool can_edit;

    if (ui == NULL || ui->ctx == NULL || content.w <= 8.0f || content.h <= footer_h) {
        return;
    }

    field = memory->edit_field == FRONTEND_MEMORY_EDIT_ASCII ? "ASCII" :
        (memory->edit_field == FRONTEND_MEMORY_EDIT_ADDRESS ? "Address" : "Hex");
    can_edit = debug_state != NULL &&
        debug_state->runtime_state == FRONTEND_RUNTIME_STATE_PAUSED &&
        !debug_state->inspecting &&
        frontend_memory_mode_is_editable(memory->mode);
    snprintf(
        edit_status,
        sizeof(edit_status),
        "%s / %s",
        can_edit ? "editable" : "read-only",
        memory->apple_ascii ? "hi-bit on" : "hi-bit off");
    status_text = ui->memory_search.status[0] != '\0' ? ui->memory_search.status : edit_status;
    snprintf(address, sizeof(address), "Address: %04X", memory->cursor_address);

    canvas = nk_window_get_canvas(ui->ctx);
    footer = nk_rect(content.x + 4.0f, content.y + content.h - footer_h, content.w - 8.0f, footer_h);
    field_rect = nk_rect(footer.x, footer.y + 2.0f, footer.w * 0.25f, footer.h - 4.0f);
    address_rect = nk_rect(footer.x + footer.w * 0.25f, footer.y + 2.0f, footer.w * 0.40f, footer.h - 4.0f);
    edit_rect = nk_rect(footer.x + footer.w * 0.65f, footer.y + 2.0f, footer.w * 0.35f, footer.h - 4.0f);

    if (ui->memory_search.status[0] == '\0' &&
        nk_input_is_mouse_click_in_rect(&ui->ctx->input, NK_BUTTON_LEFT, edit_rect)) {
        memory->apple_ascii = !memory->apple_ascii;
    }

    nk_draw_text(
        canvas,
        field_rect,
        field,
        (int)strlen(field),
        ui->ctx->style.font,
        nk_rgb(30, 34, 38),
        nk_rgb(196, 214, 228));
    nk_draw_text(
        canvas,
        address_rect,
        address,
        (int)strlen(address),
        ui->ctx->style.font,
        nk_rgb(30, 34, 38),
        nk_rgb(196, 214, 228));
    nk_draw_text(
        canvas,
        edit_rect,
        status_text,
        (int)strlen(status_text),
        ui->ctx->style.font,
        nk_rgb(30, 34, 38),
        nk_rgb(196, 214, 228));
}

static bool frontend_memory_row_address_at(
    const frontend_memory_view_state *memory,
    struct nk_rect row_bounds,
    uint16_t row_addr,
    float mouse_x,
    float char_w,
    uint16_t *out_address,
    frontend_memory_edit_field *out_field,
    uint8_t *out_nibble,
    uint8_t *out_address_digit)
{
    float rel_x;
    int text_col;
    int hex_start = 5;
    int ascii_start = hex_start + memory->columns * 3;
    int hex_end = hex_start + memory->columns * 3;

    if (memory == NULL || out_address == NULL || char_w <= 0.0f) {
        return false;
    }

    rel_x = mouse_x - row_bounds.x;
    if (rel_x < 0.0f) {
        rel_x = 0.0f;
    }
    text_col = (int)(rel_x / char_w);

    if (text_col < 4) {
        *out_address = row_addr;
        if (out_field != NULL) {
            *out_field = FRONTEND_MEMORY_EDIT_ADDRESS;
        }
        if (out_address_digit != NULL) {
            *out_address_digit = (uint8_t)text_col;
        }
        return true;
    }

    if (text_col >= hex_start && text_col < hex_end) {
        int cell = (text_col - hex_start) / 3;
        int cell_col = (text_col - hex_start) % 3;

        if (cell >= 0 && cell < memory->columns) {
            *out_address = (uint16_t)(row_addr + cell);
            if (out_field != NULL) {
                *out_field = FRONTEND_MEMORY_EDIT_HEX;
            }
            if (out_nibble != NULL) {
                *out_nibble = (uint8_t)(cell_col == 1 ? 1 : 0);
            }
            return true;
        }
        return false;
    }

    if (text_col >= ascii_start && text_col < ascii_start + memory->columns) {
        int cell = text_col - ascii_start;

        *out_address = (uint16_t)(row_addr + cell);
        if (out_field != NULL) {
            *out_field = FRONTEND_MEMORY_EDIT_ASCII;
        }
        return true;
    }

    return false;
}

static void frontend_memory_handle_mouse_row(
    frontend *ui,
    int view_index,
    struct nk_rect row_bounds,
    uint16_t row_addr)
{
    frontend_memory_view_state *memory = &ui->memory_views[view_index];
    uint16_t address;
    frontend_memory_edit_field field;
    uint8_t nibble = 0;
    uint8_t address_digit = 0;
    bool left_click;
    bool right_click;

    if (!nk_input_is_mouse_hovering_rect(&ui->ctx->input, row_bounds)) {
        return;
    }

    left_click = nk_input_is_mouse_pressed(&ui->ctx->input, NK_BUTTON_LEFT) != 0;
    right_click = nk_input_is_mouse_pressed(&ui->ctx->input, NK_BUTTON_RIGHT) != 0;
    if (!left_click && !right_click) {
        return;
    }

    if (!frontend_memory_row_address_at(
            memory,
            row_bounds,
            row_addr,
            left_click ? ui->ctx->input.mouse.pos.x :
                ui->ctx->input.mouse.buttons[NK_BUTTON_RIGHT].clicked_pos.x,
            frontend_memory_char_width(ui),
            &address,
            &field,
            &nibble,
            &address_digit)) {
        return;
    }

    frontend_set_active_view(ui, FRONTEND_ACTIVE_VIEW_MEMORY);
    ui->memory_active_view_index = view_index;
    memory->edit_field = field;
    memory->cursor_address = address;
    if (field == FRONTEND_MEMORY_EDIT_HEX) {
        memory->active_nibble = nibble;
    } else if (field == FRONTEND_MEMORY_EDIT_ADDRESS) {
        memory->active_address_digit = address_digit;
    }

    if (right_click) {
        ui->memory_context_menu_view_index = view_index;
        ui->memory_context_menu_address = address;
    }
}

static int frontend_memory_total_rows(const frontend *ui)
{
    int i, total = 0;
    for (i = 0; i < ui->memory_view_count; i++) {
        total += ui->memory_views[i].rows;
    }
    return total;
}

static void frontend_memory_redistribute_rows(frontend *ui, int new_total)
{
    int i, old_total, delta, best;
    int shares[MEMORY_VIEW_MAX];
    int assigned;
    bool was_visible[MEMORY_VIEW_MAX];

    if (ui->memory_view_count <= 0 || new_total < 0) {
        return;
    }

    old_total = frontend_memory_total_rows(ui);
    if (old_total == new_total) {
        return;
    }

    for (i = 0; i < ui->memory_view_count; i++) {
        was_visible[i] = frontend_memory_cursor_visible(&ui->memory_views[i]);
    }

    /* Compute proportional share for each view (floor division) */
    assigned = 0;
    for (i = 0; i < ui->memory_view_count; i++) {
        shares[i] = (old_total > 0) ? (int)((long)ui->memory_views[i].rows * new_total / old_total) : (new_total / ui->memory_view_count);
        if (shares[i] < 1) {
            shares[i] = 1;
        }
        assigned += shares[i];
    }

    delta = new_total - assigned;

    /* Distribute remainder: add to smallest, remove from largest */
    while (delta > 0) {
        best = -1;
        for (i = 0; i < ui->memory_view_count; i++) {
            if (best == -1 || shares[i] < shares[best]) {
                best = i;
            }
        }
        if (best < 0) break;
        shares[best]++;
        delta--;
    }
    while (delta < 0) {
        best = -1;
        for (i = 0; i < ui->memory_view_count; i++) {
            if (shares[i] > 1 && (best == -1 || shares[i] > shares[best])) {
                best = i;
            }
        }
        if (best < 0) break;
        shares[best]--;
        delta++;
    }

    for (i = 0; i < ui->memory_view_count; i++) {
        ui->memory_views[i].rows = (uint8_t)(shares[i] > 255 ? 255 : shares[i]);
    }

    for (i = 0; i < ui->memory_view_count; i++) {
        if (was_visible[i]) {
            frontend_memory_recenter_cursor(&ui->memory_views[i]);
        }
    }
}

static void frontend_memory_split_view(frontend *ui, int view_index, bool aligned)
{
    frontend_memory_view_state *av;
    frontend_memory_view_state *nv;
    uint16_t split_addr;
    int new_count, new_view_rows, total, slot, i;

    if (ui->memory_view_count >= MEMORY_VIEW_MAX ||
        view_index < 0 ||
        view_index >= ui->memory_view_count) {
        return;
    }

    ui->memory_active_view_index = view_index;
    av = &ui->memory_views[view_index];
    split_addr = aligned
        ? (uint16_t)(av->cursor_address & ~(uint16_t)0x0f)
        : av->cursor_address;

    new_count = ui->memory_view_count + 1;
    total = frontend_memory_total_rows(ui);
    new_view_rows = total / new_count;
    if (new_view_rows < 1) {
        new_view_rows = 1;
    }

    /* Take new_view_rows from existing views proportionally */
    {
        int taken = 0, shares[MEMORY_VIEW_MAX], best, remaining;
        bool was_visible[MEMORY_VIEW_MAX];
        for (i = 0; i < ui->memory_view_count; i++) {
            was_visible[i] = frontend_memory_cursor_visible(&ui->memory_views[i]);
        }
        for (i = 0; i < ui->memory_view_count; i++) {
            shares[i] = (total > 0) ? (int)((long)ui->memory_views[i].rows * new_view_rows / total) : 0;
            taken += shares[i];
        }
        remaining = new_view_rows - taken;
        while (remaining > 0) {
            best = -1;
            for (i = 0; i < ui->memory_view_count; i++) {
                if (ui->memory_views[i].rows - shares[i] > 1 &&
                    (best == -1 || ui->memory_views[i].rows - shares[i] > ui->memory_views[best].rows - shares[best])) {
                    best = i;
                }
            }
            if (best < 0) break;
            shares[best]++;
            remaining--;
        }
        for (i = 0; i < ui->memory_view_count; i++) {
            int nr = ui->memory_views[i].rows - shares[i];
            ui->memory_views[i].rows = (uint8_t)(nr < 1 ? 1 : nr);
        }
        for (i = 0; i < ui->memory_view_count; i++) {
            if (was_visible[i]) {
                frontend_memory_recenter_cursor(&ui->memory_views[i]);
            }
        }
    }

    /* Find lowest free color slot */
    slot = 0;
    for (i = 0; i < MEMORY_VIEW_MAX; i++) {
        if (!ui->memory_color_slot_used[i]) {
            slot = i;
            break;
        }
    }
    ui->memory_color_slot_used[slot] = true;

    /* Shift views after the clicked/active index down to make room */
    for (i = ui->memory_view_count; i > view_index + 1; i--) {
        ui->memory_views[i] = ui->memory_views[i - 1];
    }

    /* Initialize new view */
    nv = &ui->memory_views[view_index + 1];
    memset(nv, 0, sizeof(*nv));
    nv->view_address = split_addr;
    nv->cursor_address = split_addr;
    nv->mode = av->mode;
    nv->columns = 16;
    nv->rows = (uint8_t)(new_view_rows > 255 ? 255 : new_view_rows);
    nv->color_slot = (uint8_t)slot;
    nv->edit_field = FRONTEND_MEMORY_EDIT_HEX;
    nv->apple_ascii = av->apple_ascii;
    nv->initialized = true;

    ui->memory_view_count = new_count;
    ui->memory_active_view_index = view_index + 1;
}

static void frontend_memory_split(frontend *ui, bool aligned)
{
    frontend_memory_split_view(ui, ui->memory_active_view_index, aligned);
}

static void frontend_memory_split_at_address(frontend *ui, uint16_t address)
{
    frontend_memory_view_state *memory;

    if (ui == NULL || ui->memory_view_count <= 0) {
        return;
    }

    memory = &ui->memory_views[ui->memory_active_view_index];
    memory->cursor_address = address;
    memory->view_address = address;
    memory->request_pending = false;
    frontend_memory_split_view(ui, ui->memory_active_view_index, false);
}

static void frontend_memory_join_view(frontend *ui, int view_index)
{
    int active, i, give_rows, rem_count, best, total_remain;
    int shares[MEMORY_VIEW_MAX];

    if (ui->memory_view_count <= 1 ||
        view_index < 0 ||
        view_index >= ui->memory_view_count) {
        return;
    }

    active = view_index;
    ui->memory_active_view_index = view_index;
    give_rows = ui->memory_views[active].rows;
    ui->memory_color_slot_used[ui->memory_views[active].color_slot] = false;

    rem_count = ui->memory_view_count - 1;
    total_remain = 0;
    for (i = 0; i < ui->memory_view_count; i++) {
        if (i != active) {
            total_remain += ui->memory_views[i].rows;
        }
    }

    /* Distribute give_rows proportionally to remaining views (smallest first for extras) */
    {
        int given = 0, ri = 0;
        for (i = 0; i < ui->memory_view_count; i++) {
            if (i == active) continue;
            shares[ri] = (total_remain > 0)
                ? (int)((long)ui->memory_views[i].rows * give_rows / total_remain)
                : (give_rows / rem_count);
            given += shares[ri++];
        }
        {
            int remaining = give_rows - given;
            while (remaining > 0) {
                best = -1;
                for (i = 0; i < rem_count; i++) {
                    if (best == -1 || shares[i] < shares[best]) best = i;
                }
                if (best < 0) break;
                shares[best]++;
                remaining--;
            }
        }
        ri = 0;
        for (i = 0; i < ui->memory_view_count; i++) {
            if (i == active) continue;
            int nr = ui->memory_views[i].rows + shares[ri++];
            ui->memory_views[i].rows = (uint8_t)(nr > 255 ? 255 : nr);
        }
    }

    /* Remove view from array */
    for (i = active; i < ui->memory_view_count - 1; i++) {
        ui->memory_views[i] = ui->memory_views[i + 1];
    }
    ui->memory_view_count--;

    /* Activate next view below if exists, else previous */
    if (active < ui->memory_view_count) {
        ui->memory_active_view_index = active;
    } else {
        ui->memory_active_view_index = ui->memory_view_count - 1;
    }
}

static void frontend_memory_dissolve(frontend *ui)
{
    frontend_memory_join_view(ui, ui->memory_active_view_index);
}

static void frontend_memory_handle_key(
    frontend *ui,
    const frontend_debug_state *debug_state,
    const SDL_KeyboardEvent *key)
{
    frontend_memory_view_state *memory;
    SDL_Keycode sym;
    SDL_Keymod mod;
    bool alt, shift;

    if (ui == NULL || key == NULL || key->type != SDL_KEYDOWN) {
        return;
    }

    sym = key->keysym.sym;
    mod = key->keysym.mod;
    alt = (mod & KMOD_ALT) != 0;
    shift = (mod & KMOD_SHIFT) != 0;
    memory = &ui->memory_views[ui->memory_active_view_index];

    if (sym == SDLK_F9 || sym == SDLK_F10 || sym == SDLK_F11 || sym == SDLK_F12) {
        return;
    }

    if (alt && !shift && sym == SDLK_f) {
        if (debug_state == NULL ||
            debug_state->runtime_state != FRONTEND_RUNTIME_STATE_RUNNING) {
            frontend_memory_search_open(ui);
        }
        return;
    }

    if (alt && sym == SDLK_g) {
        if (debug_state == NULL ||
            debug_state->runtime_state != FRONTEND_RUNTIME_STATE_RUNNING) {
            (void)frontend_memory_search_run(ui, debug_state, shift);
        }
        return;
    }

    /* Virtual view navigation */
    if (alt && sym == SDLK_UP && memory->edit_field != FRONTEND_MEMORY_EDIT_ADDRESS) {
        if (ui->memory_active_view_index > 0) {
            ui->memory_active_view_index--;
        }
        return;
    }

    if (alt && sym == SDLK_DOWN && memory->edit_field != FRONTEND_MEMORY_EDIT_ADDRESS) {
        if (ui->memory_active_view_index < ui->memory_view_count - 1) {
            ui->memory_active_view_index++;
        }
        /* Refresh pointer after index change */
        memory = &ui->memory_views[ui->memory_active_view_index];
        return;
    }

    /* Split / dissolve */
    if (alt && sym == SDLK_v) {
        frontend_memory_split(ui, !shift);
        return;
    }

    if (alt && sym == SDLK_j) {
        frontend_memory_dissolve(ui);
        return;
    }

    if (alt && !shift && sym == SDLK_a) {
        if (memory->edit_field == FRONTEND_MEMORY_EDIT_ADDRESS) {
            memory->edit_field = FRONTEND_MEMORY_EDIT_HEX;
        } else {
            uint16_t offset = (uint16_t)(memory->cursor_address - memory->view_address);
            uint16_t row = (uint16_t)(offset / memory->columns);

            memory->edit_field = FRONTEND_MEMORY_EDIT_ADDRESS;
            memory->cursor_address = (uint16_t)(memory->view_address + row * memory->columns);
        }
        memory->active_address_digit = 0;
        return;
    }

    if (alt && sym == SDLK_x) {
        memory->edit_field = memory->edit_field == FRONTEND_MEMORY_EDIT_ASCII ?
            FRONTEND_MEMORY_EDIT_HEX :
            FRONTEND_MEMORY_EDIT_ASCII;
        return;
    }

    if (alt && !shift && sym == SDLK_m) {
        memory->mode = frontend_memory_next_mode(memory->mode);
        memory->request_pending = false;
        return;
    }

    if (alt && sym == SDLK_s) {
        frontend_open_symbol_lookup(ui, true);
        return;
    }

    if (sym == SDLK_PAGEUP && memory->edit_field != FRONTEND_MEMORY_EDIT_ADDRESS) {
        memory->view_address = (uint16_t)(memory->view_address - frontend_memory_visible_count(memory));
        memory->request_pending = false;
        return;
    }

    if (sym == SDLK_PAGEDOWN && memory->edit_field != FRONTEND_MEMORY_EDIT_ADDRESS) {
        memory->view_address = (uint16_t)(memory->view_address + frontend_memory_visible_count(memory));
        memory->request_pending = false;
        return;
    }

    if (sym == SDLK_HOME) {
        if (memory->edit_field == FRONTEND_MEMORY_EDIT_ADDRESS) {
            memory->active_address_digit = 0;
        } else if (alt) {
            memory->cursor_address = memory->view_address;
        } else {
            uint16_t offset = (uint16_t)(memory->cursor_address - memory->view_address);
            uint16_t row = (uint16_t)(offset / memory->columns);
            memory->cursor_address = (uint16_t)(memory->view_address + row * memory->columns);
        }
        return;
    }

    if (sym == SDLK_END) {
        if (memory->edit_field == FRONTEND_MEMORY_EDIT_ADDRESS) {
            memory->active_address_digit = 3;
        } else if (alt) {
            memory->cursor_address = (uint16_t)(memory->view_address +
                frontend_memory_visible_count(memory) - 1u);
        } else {
            uint16_t offset = (uint16_t)(memory->cursor_address - memory->view_address);
            uint16_t row = (uint16_t)(offset / memory->columns);
            memory->cursor_address = (uint16_t)(memory->view_address + row * memory->columns +
                memory->columns - 1u);
        }
        return;
    }

    if (sym == SDLK_UP && memory->edit_field != FRONTEND_MEMORY_EDIT_ADDRESS) {
        frontend_memory_move_cursor(ui, -(int32_t)memory->columns);
        return;
    }

    if (sym == SDLK_DOWN && memory->edit_field != FRONTEND_MEMORY_EDIT_ADDRESS) {
        frontend_memory_move_cursor(ui, memory->columns);
        return;
    }

    if (sym == SDLK_LEFT) {
        if (memory->edit_field == FRONTEND_MEMORY_EDIT_ADDRESS) {
            if (memory->active_address_digit > 0) {
                memory->active_address_digit--;
            }
        } else if (memory->edit_field == FRONTEND_MEMORY_EDIT_ASCII) {
            frontend_memory_move_cursor(ui, -1);
        } else if (memory->active_nibble == 0) {
            frontend_memory_move_cursor(ui, -1);
            memory->active_nibble = 1;
        } else {
            memory->active_nibble = 0;
        }
        return;
    }

    if (sym == SDLK_RIGHT) {
        if (memory->edit_field == FRONTEND_MEMORY_EDIT_ADDRESS) {
            if (memory->active_address_digit >= 3) {
                memory->edit_field = FRONTEND_MEMORY_EDIT_HEX;
                memory->active_address_digit = 0;
            } else {
                memory->active_address_digit++;
            }
        } else if (memory->edit_field == FRONTEND_MEMORY_EDIT_ASCII) {
            frontend_memory_move_cursor(ui, 1);
        } else if (memory->active_nibble == 0) {
            memory->active_nibble = 1;
        } else {
            memory->active_nibble = 0;
            frontend_memory_move_cursor(ui, 1);
        }
        return;
    }

    if (memory->edit_field == FRONTEND_MEMORY_EDIT_ASCII) {
        if (sym == SDLK_RETURN) {
            if (!frontend_memory_mode_is_editable(memory->mode) ||
                !frontend_memory_view_byte_available(debug_state, memory, memory->cursor_address)) {
                return;
            }
            frontend_memory_write_byte(
                ui,
                debug_state,
                memory->cursor_address,
                frontend_memory_ascii_store_byte(0x0du, memory->apple_ascii));
            frontend_memory_move_cursor(ui, 1);
            return;
        }
        if (sym == SDLK_BACKSPACE) {
            if (!frontend_memory_mode_is_editable(memory->mode) ||
                !frontend_memory_view_byte_available(debug_state, memory, memory->cursor_address)) {
                return;
            }
            frontend_memory_write_byte(
                ui,
                debug_state,
                memory->cursor_address,
                frontend_memory_ascii_store_byte(0x08u, memory->apple_ascii));
            frontend_memory_move_cursor(ui, 1);
            return;
        }
        if (sym >= 32 && sym <= 126) {
            uint8_t byte = (uint8_t)sym;
            if (!frontend_memory_mode_is_editable(memory->mode) ||
                !frontend_memory_view_byte_available(debug_state, memory, memory->cursor_address)) {
                return;
            }
            if (sym >= 'a' && sym <= 'z') {
                bool caps = (mod & KMOD_CAPS) != 0;
                if (shift ^ caps) {
                    byte = (uint8_t)(sym - ('a' - 'A'));
                }
            }
            frontend_memory_write_byte(
                ui,
                debug_state,
                memory->cursor_address,
                frontend_memory_ascii_store_byte(byte, memory->apple_ascii));
            frontend_memory_move_cursor(ui, 1);
            return;
        }
    } else {
        int digit = frontend_hex_digit_value((char)sym);
        if (memory->edit_field == FRONTEND_MEMORY_EDIT_ADDRESS) {
            frontend_memory_apply_address_digit(ui, digit);
        } else {
            frontend_memory_apply_hex_digit(ui, debug_state, digit);
        }
    }
}

static void frontend_draw_memory(frontend *ui, struct nk_rect bounds, const frontend_debug_state *debug_state)
{
    float row_h;
    float footer_h;
    float scrollbar_w = 24.0f;
    float scrollbar_margin = 8.0f;
    int total_rows, max_rows_per_view;
    struct nk_style_window saved_window_style;
    frontend_memory_view_state *active_mem;
    const struct nk_user_font *font;
    int v;

    /* One-time initialization of the first virtual view */
    if (ui->memory_view_count == 0) {
        ui->memory_views[0].view_address = 0x0000;
        ui->memory_views[0].cursor_address = 0x0000;
        ui->memory_views[0].mode = RUNTIME_MEMORY_MODE_CPU_MAP;
        ui->memory_views[0].edit_field = FRONTEND_MEMORY_EDIT_HEX;
        ui->memory_views[0].columns = 16;
        ui->memory_views[0].color_slot = 0;
        ui->memory_views[0].apple_ascii = true;
        ui->memory_views[0].initialized = true;
        ui->memory_view_count = 1;
        ui->memory_active_view_index = 0;
        ui->memory_color_slot_used[0] = true;
    }

    active_mem = &ui->memory_views[ui->memory_active_view_index];
    font = ui->ctx->style.font;
    row_h = (font != NULL) ? font->height : 13.0f;
    footer_h = 22.0f;

    /* Compute total available rows */
    max_rows_per_view = RUNTIME_MEMORY_SNAPSHOT_MAX / 16;
    total_rows = (bounds.h > footer_h + 28.0f)
        ? (int)((bounds.h - footer_h - 28.0f) / row_h)
        : 1;
    if (total_rows > 1) {
        total_rows--;
    }
    if (total_rows < 1) {
        total_rows = 1;
    }

    /* Cap per-view rows and detect resize */
    {
        int current_sum = frontend_memory_total_rows(ui);
        /* Cap each view's rows to the per-view maximum */
        for (v = 0; v < ui->memory_view_count; v++) {
            if (ui->memory_views[v].rows > (uint8_t)max_rows_per_view) {
                ui->memory_views[v].rows = (uint8_t)max_rows_per_view;
            }
            if (ui->memory_views[v].rows < 1) {
                ui->memory_views[v].rows = 1;
            }
        }
        current_sum = frontend_memory_total_rows(ui);
        if (current_sum != total_rows) {
            frontend_memory_redistribute_rows(ui, total_rows);
        }
    }

    if (ui->has_pending_memory_key) {
        if (ui->active_view == FRONTEND_ACTIVE_VIEW_MEMORY) {
            frontend_memory_handle_key(ui, debug_state, &ui->pending_memory_key);
        }
        ui->has_pending_memory_key = false;
    }

    frontend_memory_request_if_needed(ui, debug_state);

    if (nk_begin(ui->ctx, "Memory", bounds, pane_flags)) {
        struct nk_command_buffer *canvas;
        bool any_dialog = frontend_any_dialog_open(ui);
        bool mem_active = !any_dialog && ui->active_view == FRONTEND_ACTIVE_VIEW_MEMORY;
        float rows_x = 0.0f, rows_w = 0.0f;
        bool have_rows_x = false;
        struct nk_color sep_color = nk_rgb(80, 92, 104);

        saved_window_style = ui->ctx->style.window;
        ui->ctx->style.window.padding = nk_vec2(0.0f, 0.0f);
        ui->ctx->style.window.spacing = nk_vec2(0.0f, 0.0f);
        ui->ctx->style.window.group_padding = nk_vec2(0.0f, 0.0f);

        nk_layout_row_begin(ui->ctx, NK_STATIC, row_h * (float)total_rows, 3);
        nk_layout_row_push(ui->ctx, bounds.w - scrollbar_w - scrollbar_margin);

        if (nk_group_begin(ui->ctx, "memory-rows", NK_WINDOW_NO_SCROLLBAR)) {
            float text_x_offset = frontend_memory_char_width(ui) * 0.5f;
            canvas = nk_window_get_canvas(ui->ctx);

            for (v = 0; v < ui->memory_view_count; v++) {
                frontend_memory_view_state *mv = &ui->memory_views[v];
                struct nk_color text_c = memory_view_colors[mv->color_slot].text;
                struct nk_color bg_c = memory_view_colors[mv->color_slot].bg;
                bool is_active_v = (v == ui->memory_active_view_index);
                uint8_t row;
                int next_vis;

                if (mv->rows == 0) {
                    mv->cached_y_top = 0.0f;
                    mv->cached_y_bottom = 0.0f;
                    continue;
                }

                for (row = 0; row < mv->rows; row++) {
                    char line[96];
                    char *lp = line;
                    size_t remaining = sizeof(line);
                    uint8_t col;
                    uint16_t row_addr = (uint16_t)(mv->view_address + (uint16_t)row * mv->columns);
                    int written = snprintf(lp, remaining, "%04X:", row_addr);
                    struct nk_rect rb;

                    lp += written; remaining -= (size_t)written;
                    for (col = 0; col < mv->columns; col++) {
                        uint16_t addr = (uint16_t)(row_addr + col);
                        if (frontend_memory_view_byte_available(debug_state, mv, addr)) {
                            uint8_t val = frontend_memory_view_byte_at(debug_state, mv, addr);
                            written = snprintf(lp, remaining, "%02X ", val);
                        } else {
                            written = snprintf(lp, remaining, "-- ");
                        }
                        lp += written; remaining -= (size_t)written;
                    }
                    for (col = 0; col < mv->columns; col++) {
                        uint16_t addr = (uint16_t)(row_addr + col);
                        if (frontend_memory_view_byte_available(debug_state, mv, addr)) {
                            uint8_t val = frontend_memory_view_byte_at(debug_state, mv, addr);
                            written = snprintf(
                                lp,
                                remaining,
                                "%c",
                                frontend_memory_ascii(val, mv->apple_ascii));
                        } else {
                            written = snprintf(lp, remaining, " ");
                        }
                        lp += written; remaining -= (size_t)written;
                    }

                    nk_layout_row_dynamic(ui->ctx, row_h, 1);
                    /* nk_widget advances the layout cursor AND returns bounds */
                    if (nk_widget(&rb, ui->ctx) != NK_WIDGET_INVALID) {
                        struct nk_rect text_rb = nk_rect(rb.x + text_x_offset, rb.y, rb.w - text_x_offset, rb.h);
                        if (!have_rows_x) {
                            rows_x = rb.x;
                            rows_w = rb.w;
                            have_rows_x = true;
                        }
                        if (row == 0) {
                            mv->cached_y_top = rb.y;
                        }
                        mv->cached_y_bottom = rb.y + rb.h;

                        /* Draw colored background then text via canvas */
                        nk_fill_rect(canvas, rb, 0.0f, bg_c);
                        nk_draw_text(canvas, text_rb, line, (int)(lp - line), font, bg_c, text_c);

                        frontend_memory_handle_mouse_row(ui, v, text_rb, row_addr);

                        if (is_active_v) {
                            frontend_memory_draw_cursor(ui, debug_state, mv, text_rb, line, row_addr);
                        }
                    }
                }

                /* Scroll wheel: route to hovered view after rows are known */
                if (!any_dialog && ui->ctx->input.mouse.scroll_delta.y != 0.0f) {
                    float mx = ui->ctx->input.mouse.pos.x;
                    float my = ui->ctx->input.mouse.pos.y;
                    if (mx >= rows_x && mx < rows_x + rows_w &&
                        my >= mv->cached_y_top && my < mv->cached_y_bottom) {
                        int32_t lines = ui->ctx->input.mouse.scroll_delta.y > 0.0f ? -3 : 3;
                        mv->view_address = (uint16_t)(mv->view_address + lines * mv->columns);
                        mv->request_pending = false;
                    }
                }

                /* Draw separator after this view if a next visible view exists */
                next_vis = -1;
                {
                    int nv2;
                    for (nv2 = v + 1; nv2 < ui->memory_view_count; nv2++) {
                        if (ui->memory_views[nv2].rows > 0) {
                            next_vis = nv2;
                            break;
                        }
                    }
                }
                if (next_vis >= 0) {
                    struct nk_rect sep = nk_rect(rows_x, mv->cached_y_bottom - 1.0f, rows_w, 1.0f);
                    nk_fill_rect(canvas, sep, 0.0f, sep_color);
                }
            }

            nk_group_end(ui->ctx);
        }

        nk_layout_row_push(ui->ctx, scrollbar_w);
        if (nk_group_begin(ui->ctx, "memory-scrollbar", NK_WINDOW_NO_SCROLLBAR)) {
            struct nk_rect scrollbar_bounds = nk_window_get_content_region(ui->ctx);
            frontend_memory_draw_scrollbar(ui, scrollbar_bounds, frontend_memory_visible_count(active_mem));
            nk_group_end(ui->ctx);
        }
        nk_layout_row_push(ui->ctx, scrollbar_margin);
        nk_spacing(ui->ctx, 1);
        nk_layout_row_end(ui->ctx);

        frontend_memory_draw_status_footer(
            ui,
            debug_state,
            nk_window_get_content_region(ui->ctx),
            footer_h);

        /* On right-click, record which virtual view was under the cursor */
        if (nk_input_is_mouse_click_in_rect(&ui->ctx->input, NK_BUTTON_RIGHT, bounds)) {
            float click_y = ui->ctx->input.mouse.buttons[NK_BUTTON_RIGHT].clicked_pos.y;
            bool stopped = debug_state == NULL ||
                debug_state->runtime_state != FRONTEND_RUNTIME_STATE_RUNNING;
            bool can_join = ui->memory_view_count > 1;
            ui->memory_context_menu_view_index = ui->memory_active_view_index;
            ui->memory_context_menu_address =
                ui->memory_views[ui->memory_active_view_index].cursor_address;
            for (v = 0; v < ui->memory_view_count; v++) {
                if (ui->memory_views[v].rows > 0 &&
                    click_y >= ui->memory_views[v].cached_y_top &&
                    click_y < ui->memory_views[v].cached_y_bottom) {
                    ui->memory_context_menu_view_index = v;
                    ui->memory_context_menu_address = ui->memory_views[v].cursor_address;
                    break;
                }
            }
            frontend_context_popup_open(
                ui,
                &ui->memory_context_popup,
                120.0f,
                stopped ?
                    (can_join ? 417.0f : 395.0f) :
                    (can_join ? 297.0f : 275.0f));
        }

        /* Context menu applies to the virtual view that was right-clicked */
        if (frontend_context_popup_begin(
                ui,
                &ui->memory_context_popup,
                "memory-context-menu")) {
            bool close_popup = false;
            bool stopped = debug_state == NULL ||
                debug_state->runtime_state != FRONTEND_RUNTIME_STATE_RUNNING;
            int ctx_idx = (ui->memory_context_menu_view_index >= 0 &&
                           ui->memory_context_menu_view_index < ui->memory_view_count)
                          ? ui->memory_context_menu_view_index : ui->memory_active_view_index;
            frontend_memory_view_state *ctx_view = &ui->memory_views[ctx_idx];
            uint64_t write_history =
                frontend_memory_write_history_at(debug_state, ctx_view, ui->memory_context_menu_address);

            if (frontend_context_menu_source_modes(ui, &ctx_view->mode, &close_popup)) {
                ctx_view->request_pending = false;
            }
            frontend_context_menu_separator(ui->ctx);
            frontend_context_menu_heading(ui->ctx, "ASCII");
            if (frontend_context_menu_mode_item(ui->ctx, ctx_view->apple_ascii, "hi-bit on")) {
                ctx_view->apple_ascii = true;
                close_popup = true;
            }
            if (frontend_context_menu_mode_item(ui->ctx, !ctx_view->apple_ascii, "hi-bit off")) {
                ctx_view->apple_ascii = false;
                close_popup = true;
            }
            frontend_context_menu_separator(ui->ctx);
            frontend_context_menu_heading(ui->ctx, "View");
            if (frontend_context_menu_item(ui->ctx, "Split")) {
                frontend_memory_split_view(ui, ctx_idx, false);
                close_popup = true;
            }
            if (ui->memory_view_count > 1 &&
                frontend_context_menu_item(ui->ctx, "Join")) {
                frontend_memory_join_view(ui, ctx_idx);
                close_popup = true;
            }
            if (stopped) {
                uint16_t selected_address;

                frontend_context_menu_separator(ui->ctx);
                if (frontend_context_menu_access(ui->ctx, write_history, &selected_address)) {
                    frontend_disassembly_navigate_to_address(ui, selected_address);
                    close_popup = true;
                }
            }
            frontend_context_popup_end(ui, &ui->memory_context_popup, close_popup);
        }

        /* Active-panel selection border wraps the whole memory panel */
        if (mem_active) {
            frontend_draw_active_view_border(ui->ctx);
        }

        /* Per-virtual-view RAM/ROM borders, inset to avoid overlap at shared edges */
        if (have_rows_x) {
            canvas = nk_window_get_canvas(ui->ctx);
            for (v = 0; v < ui->memory_view_count; v++) {
                frontend_memory_view_state *mv = &ui->memory_views[v];
                struct nk_color border_color;
                float thickness;
                struct nk_rect br;
                float inset_top, inset_bot;

                if (mv->rows == 0 || mv->mode == RUNTIME_MEMORY_MODE_CPU_MAP) {
                    continue;
                }

                border_color = frontend_memory_mode_border_color(mv->mode);
                thickness = (mem_active && v == ui->memory_active_view_index) ? 4.0f : 1.0f;
                inset_top = (v > 0) ? 1.0f : 2.0f;
                inset_bot = (v < ui->memory_view_count - 1) ? 1.0f : 2.0f;

                br = nk_rect(
                    rows_x + thickness * 0.5f,
                    mv->cached_y_top + inset_top,
                    rows_w - thickness,
                    (mv->cached_y_bottom - inset_bot) - (mv->cached_y_top + inset_top));

                if (br.h > 0.0f) {
                    nk_stroke_rect(canvas, br, 0.0f, thickness, border_color);
                }
            }
        }

        ui->ctx->style.window = saved_window_style;
    }
    nk_end(ui->ctx);
}

static const char *frontend_disk_label(const frontend_debug_state *debug_state, uint8_t device)
{
    size_t index;
    const runtime_disk_status_snapshot *status;

    if (debug_state == NULL || device < 8 || device > 9) {
        return "No disk";
    }
    index = (size_t)(device - 8u);
    if (!debug_state->has_disk_status[index]) {
        return "No disk";
    }

    status = &debug_state->disk_status[index];
    if (!status->mounted) {
        return status->last_result != 0 ? "Mount failed" : "No disk";
    }
    if (status->disk_title[0] != '\0') {
        return status->disk_title;
    }
    if (status->display_name[0] != '\0') {
        return status->display_name;
    }
    return "Mounted";
}

static bool frontend_push_reset_intent(frontend *ui, bool resume_running)
{
    size_t next;

    if (ui == NULL) {
        return false;
    }

    next = (ui->intent_write + 1u) % FRONTEND_DEBUGGER_INTENT_CAPACITY;
    if (next == ui->intent_read) {
        return false;
    }

    memset(&ui->intents[ui->intent_write], 0, sizeof(ui->intents[ui->intent_write]));
    ui->intents[ui->intent_write].type = FRONTEND_DEBUGGER_INTENT_MACHINE_RESET;
    ui->intents[ui->intent_write].machine_reset_resume_running = resume_running;
    ui->intent_write = next;
    return true;
}

static void frontend_draw_reset_prompt(frontend *ui, struct nk_context *ctx)
{
    /* Apple product has no cartridge-unmount prompt; any stale open is closed. */
    (void)ctx;
    if (ui != NULL) {
        ui->reset_prompt.open = false;
    }
}

static const char *frontend_slot_card_label(runtime_slot_card_type type)
{
    switch (type) {
    case RUNTIME_SLOT_CARD_DISKII: return "Disk II";
    case RUNTIME_SLOT_CARD_SMARTPORT: return "SmartPort";
    case RUNTIME_SLOT_CARD_MOCKINGBOARD: return "Mockingboard";
    case RUNTIME_SLOT_CARD_EMPTY:
    default: return "Empty";
    }
}

static void frontend_draw_machine_slots(
    frontend *ui,
    const frontend_debug_state *debug_state)
{
    struct nk_context *ctx = ui->ctx;
    int slot;

    if (debug_state == NULL || !debug_state->has_apple_flags) {
        nk_layout_row_dynamic(ctx, 18.0f, 1);
        nk_label(ctx, "Waiting for machine configuration", NK_TEXT_LEFT);
        return;
    }

    for (slot = 1; slot <= 7; ++slot) {
        const runtime_slot_snapshot *slot_state = &debug_state->slots[slot];
        char label[64];
        int device;

        if (slot_state->card_type == RUNTIME_SLOT_CARD_EMPTY) {
            continue;
        }
        snprintf(label, sizeof(label), "Slot %d: %s", slot,
            frontend_slot_card_label(slot_state->card_type));
        nk_layout_row_dynamic(ctx, 18.0f, 1);
        nk_label(ctx, label, NK_TEXT_LEFT);

        if (slot_state->card_type != RUNTIME_SLOT_CARD_DISKII &&
            slot_state->card_type != RUNTIME_SLOT_CARD_SMARTPORT) {
            continue;
        }
        for (device = 0; device < 2; ++device) {
            const runtime_media_device_snapshot *media = &slot_state->devices[device];
            const bool show_swap = slot_state->card_type == RUNTIME_SLOT_CARD_DISKII &&
                media->queue_count > 1u;
            char device_label[8];
            char swap_label[32];

            snprintf(device_label, sizeof(device_label), "%d.%d", slot, device);
            nk_layout_row_begin(ctx, NK_DYNAMIC, 22.0f, show_swap ? 5 : 4);
            nk_layout_row_push(ctx, 0.09f);
            if (device == 0) {
                if (nk_button_label(ctx, device_label)) {
                    (void)frontend_push_media_intent(
                        ui, FRONTEND_DEBUGGER_INTENT_BOOT_SLOT,
                        (uint8_t)slot, (uint8_t)device, slot_state->card_type);
                }
            } else {
                nk_label(ctx, device_label, NK_TEXT_CENTERED);
            }
            nk_layout_row_push(ctx, 0.10f);
            if (nk_button_label(ctx, "Eject")) {
                (void)frontend_push_media_intent(
                    ui, FRONTEND_DEBUGGER_INTENT_MEDIA_EJECT,
                    (uint8_t)slot, (uint8_t)device, slot_state->card_type);
            }
            nk_layout_row_push(ctx, 0.11f);
            if (nk_button_label(ctx, "Insert")) {
                (void)frontend_push_media_intent(
                    ui, FRONTEND_DEBUGGER_INTENT_MEDIA_INSERT_DIALOG,
                    (uint8_t)slot, (uint8_t)device, slot_state->card_type);
            }
            if (show_swap) {
                snprintf(swap_label, sizeof(swap_label), "Swap (%02u/%02u)",
                    (unsigned)(media->queue_index + 1u),
                    (unsigned)media->queue_count);
                nk_layout_row_push(ctx, 0.20f);
                if (nk_button_label(ctx, swap_label)) {
                    (void)frontend_push_media_intent(
                        ui, FRONTEND_DEBUGGER_INTENT_MEDIA_SWAP,
                        (uint8_t)slot, (uint8_t)device, slot_state->card_type);
                }
                nk_layout_row_push(ctx, 0.50f);
            } else {
                nk_layout_row_push(ctx, 0.70f);
            }
            nk_label(ctx, media->display_name, NK_TEXT_LEFT);
            nk_layout_row_end(ctx);
        }
    }
}

static void frontend_draw_misc_programs(frontend *ui, const frontend_debug_state *debug_state)
{
    struct nk_context *ctx;

    if (ui == NULL || ui->ctx == NULL) {
        return;
    }

    ctx = ui->ctx;

    frontend_draw_machine_slots(ui, debug_state);

    nk_layout_row_dynamic(ctx, 18.0f, 1);
    nk_label(ctx, "Machine files", NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx, 24.0f, 2);
    if (nk_button_label(ctx, "Load...")) {
        frontend_load_bin_dialog_state *dlg = &ui->load_bin_dialog;
        if (!dlg->initialized) {
            memset(dlg, 0, sizeof(*dlg));
            dlg->kind = FRONTEND_MACHINE_FILE_AUTO;
            dlg->binary_format = APPLE2_BINARY_FORMAT_AUTO;
            snprintf(dlg->address_buf, sizeof(dlg->address_buf), "0800");
            dlg->initialized = true;
        }
        dlg->open = true;
    }
    if (nk_button_label(ctx, "Save...")) {
        frontend_save_bin_dialog_state *dlg = &ui->save_bin_dialog;
        if (!dlg->initialized) {
            memset(dlg, 0, sizeof(*dlg));
            dlg->kind = FRONTEND_MACHINE_FILE_SNAPSHOT;
            dlg->binary_format = APPLE2_BINARY_FORMAT_NAPS;
            snprintf(dlg->start_address_buf, sizeof(dlg->start_address_buf), "0800");
            snprintf(dlg->end_address_buf, sizeof(dlg->end_address_buf), "08FF");
            dlg->initialized = true;
        }
        dlg->open = true;
    }

    nk_layout_row_dynamic(ctx, 18.0f, 1);
    nk_label(ctx, "Emulator", NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx, 24.0f, 1);
    if (nk_button_label(ctx, "Configure...")) {
        frontend_config_dialog_open(ui);
    }
    nk_layout_row_dynamic(ctx, 24.0f, 1);
    if (nk_button_label(ctx, "Reset")) {
        frontend_push_reset_intent(
            ui,
            debug_state != NULL &&
                debug_state->runtime_state == FRONTEND_RUNTIME_STATE_RUNNING);
    }
}

static void frontend_draw_misc_debugger(frontend *ui, const frontend_debug_state *debug_state)
{
    struct nk_context *ctx;

    if (ui == NULL || ui->ctx == NULL) {
        return;
    }

    ctx = ui->ctx;
    nk_layout_row_dynamic(ctx, 18.0f, 1);
    nk_label(ctx, "Debug Status", NK_TEXT_LEFT);
    {
        char status[96];
        {
            char turbo_label[32];
            runtime_turbo_format_label(
                debug_state != NULL ? debug_state->active_turbo_multiplier : RUNTIME_TURBO_MHZ_1,
                turbo_label,
                sizeof(turbo_label));
            snprintf(
                status,
                sizeof(status),
                "State: %s  Turbo: %s  PC: %04X  Stop: %s",
                debug_state != NULL ? frontend_runtime_state_name(debug_state->runtime_state) : "UNKNOWN",
                turbo_label,
                debug_state != NULL && debug_state->has_cpu ? debug_state->cpu.pc : 0,
                debug_state != NULL ? frontend_stop_reason_name(debug_state->stop_reason) : "none");
        }
        nk_label(ctx, status, NK_TEXT_LEFT);
    }
    {
        char status[128];
        snprintf(
            status,
            sizeof(status),
            "CPU cycles: %llu  Machine: %llu",
            (unsigned long long)(debug_state != NULL && debug_state->has_cpu ? debug_state->cpu.cycles : 0),
            (unsigned long long)(debug_state != NULL ? debug_state->machine_cycle : 0));
        nk_label(ctx, status, NK_TEXT_LEFT);
    }
    {
        char status[96];
        snprintf(
            status,
            sizeof(status),
            "Frame: %llu  Frame cycle: %llu  Dropped: %llu",
            (unsigned long long)(debug_state != NULL ? debug_state->frame_number : 0),
            (unsigned long long)(debug_state != NULL ? debug_state->frame_cycle : 0),
            (unsigned long long)(debug_state != NULL ? debug_state->dropped_frames : 0));
        nk_label(ctx, status, NK_TEXT_LEFT);
    }
    {
        char status[48];
        snprintf(
            status,
            sizeof(status),
            "Step - CPU: %llu  Machine: %llu",
            (unsigned long long)(debug_state != NULL
                ? debug_state->cpu.cycles - debug_state->step_cpu_cycle_start
                : 0),
            (unsigned long long)(debug_state != NULL
                ? debug_state->machine_cycle - debug_state->step_cycle_start
                : 0));
        nk_label(ctx, status, NK_TEXT_LEFT);
    }

    nk_layout_row_dynamic(ctx, 8.0f, 1);
    nk_spacing(ctx, 1);
    nk_layout_row_dynamic(ctx, 18.0f, 1);
    nk_label(ctx, "Call Stack", NK_TEXT_LEFT);

    frontend_push_simple_intent(ui, FRONTEND_DEBUGGER_INTENT_REQUEST_CALL_STACK);

    if (debug_state == NULL || !debug_state->has_call_stack ||
        debug_state->call_stack.count == 0) {
        nk_layout_row_dynamic(ctx, 15.0f, 1);
        nk_label(ctx, "No stack entries", NK_TEXT_LEFT);
    } else {
        uint8_t cs_i;
        for (cs_i = 0; cs_i < debug_state->call_stack.count; cs_i++) {
            const runtime_call_stack_entry *cs_entry = &debug_state->call_stack.entries[cs_i];
            frontend_disassembly_view_state *view = &ui->disassembly;
            char jsr_label[8];
            char dest_label[28];
            char sym[17];
            nk_bool selected;

            snprintf(jsr_label, sizeof(jsr_label), "%04X", cs_entry->jsr_address);

            sym[0] = '\0';
            if (ui->symbols.address_to_label != NULL) {
                (void)ui->symbols.address_to_label(
                    ui->symbols.userdata,
                    cs_entry->dest_address,
                    sym,
                    sizeof(sym));
                sym[15] = '\0';
            }
            if (sym[0] != '\0') {
                snprintf(dest_label, sizeof(dest_label), "JSR %s", sym);
            } else {
                snprintf(dest_label, sizeof(dest_label), "JSR %04X", cs_entry->dest_address);
            }

            nk_layout_row_begin(ctx, NK_DYNAMIC, 15.0f, 2);

            nk_layout_row_push(ctx, 0.12f);
            selected = nk_false;
            if (nk_selectable_label(ctx, jsr_label, NK_TEXT_LEFT, &selected) && selected) {
                view->cursor_address = cs_entry->jsr_address;
                view->top_address = frontend_disassembly_center_top(cs_entry->jsr_address, view->rows);
                view->cursor_row = view->rows / 2u;
                view->has_user_cursor = true;
                view->request_pending = false;
                view->follow_pc = false;
                view->pc_lock_active = false;
            }

            nk_layout_row_push(ctx, 0.88f);
            selected = nk_false;
            if (nk_selectable_label(ctx, dest_label, NK_TEXT_LEFT, &selected) && selected) {
                view->cursor_address = cs_entry->dest_address;
                view->top_address = frontend_disassembly_center_top(cs_entry->dest_address, view->rows);
                view->cursor_row = view->rows / 2u;
                view->has_user_cursor = true;
                view->request_pending = false;
                view->follow_pc = false;
                view->pc_lock_active = false;
            }

            nk_layout_row_end(ctx);
        }
    }
}

static bool frontend_nk_action_button(
    struct nk_context *ctx,
    const char *label,
    bool enabled)
{
    if (!enabled) {
        nk_label(ctx, label, NK_TEXT_CENTERED);
        return false;
    }
    return nk_button_label(ctx, label) != 0;
}

static void frontend_draw_misc_breakpoints(frontend *ui, const frontend_debug_state *debug_state)
{
    struct nk_context *ctx;
    uint16_t i;
    uint16_t count;

    if (ui == NULL || ui->ctx == NULL) {
        return;
    }

    ctx = ui->ctx;
    count = debug_state != NULL && debug_state->has_breakpoints ?
        debug_state->breakpoints.count : 0;

    nk_layout_row_dynamic(ctx, 18.0f, 1);
    nk_label(ctx, "Breakpoints", NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx, 24.0f, 1);
    if (nk_button_label(ctx, "New")) {
        frontend_open_breakpoint_dialog_default(ui);
    }

    if (count == 0) {
        nk_layout_row_dynamic(ctx, 18.0f, 1);
        nk_label(ctx, "No breakpoints set", NK_TEXT_LEFT);
    }

    for (i = 0; i < count; ++i) {
        const runtime_breakpoint_snapshot_entry *entry =
            &debug_state->breakpoints.entries[i];
        struct nk_style_button saved_button = ctx->style.button;
        char label[96];
        char access[4];
        size_t access_len = 0;

        if (entry->enabled == 0) {
            ctx->style.button.text_normal = nk_rgb(180, 142, 210);
            ctx->style.button.normal = nk_style_item_color(nk_rgb(40, 34, 48));
        }

        if ((entry->access & RUNTIME_BREAKPOINT_ACCESS_EXECUTE) != 0) {
            access[access_len++] = 'X';
        }
        if ((entry->access & RUNTIME_BREAKPOINT_ACCESS_READ) != 0) {
            access[access_len++] = 'R';
        }
        if ((entry->access & RUNTIME_BREAKPOINT_ACCESS_WRITE) != 0) {
            access[access_len++] = 'W';
        }
        access[access_len] = '\0';

        if (entry->use_counter != 0) {
            if (entry->has_end_address) {
                snprintf(
                    label,
                    sizeof(label),
                    "%s [%04X-%04X] (%u:%u)",
                    access,
                    entry->start_address,
                    entry->end_address,
                    entry->current_hits,
                    entry->counter);
            } else {
                snprintf(
                    label,
                    sizeof(label),
                    "%s [%04X] (%u:%u)",
                    access,
                    entry->start_address,
                    entry->current_hits,
                    entry->counter);
            }
        } else if (entry->has_end_address) {
            snprintf(label, sizeof(label), "%s [%04X-%04X] (%u)", access, entry->start_address, entry->end_address, entry->current_hits);
        } else {
            snprintf(label, sizeof(label), "%s [%04X] (%u)", access, entry->start_address, entry->current_hits);
        }
        nk_layout_row_begin(ctx, NK_DYNAMIC, 24.0f, 6);
        nk_layout_row_push(ctx, 0.28f);
        nk_label_colored(
            ctx,
            label,
            NK_TEXT_LEFT,
            entry->enabled != 0 ? nk_rgb(232, 235, 238) : nk_rgb(180, 142, 210));
        nk_layout_row_push(ctx, 0.13f);
        if (frontend_nk_action_button(ctx, "Edit", true)) {
            frontend_open_breakpoint_dialog_from_entry(ui, entry, false);
        }
        nk_layout_row_push(ctx, 0.16f);
        if (frontend_nk_action_button(ctx, "Duplicate", true)) {
            frontend_open_breakpoint_dialog_from_entry(ui, entry, true);
        }
        nk_layout_row_push(ctx, 0.14f);
        if (frontend_nk_action_button(
                ctx,
                entry->enabled != 0 ? "Disable" : "Enable",
                true)) {
            frontend_push_breakpoint_id_intent(
                ui,
                FRONTEND_DEBUGGER_INTENT_BREAKPOINT_SET_ENABLED,
                entry->id,
                entry->enabled == 0);
        }
        nk_layout_row_push(ctx, 0.15f);
        if (nk_button_label(ctx, "View PC")) {
            frontend_disassembly_scroll_to_top(ui, frontend_disassembly_center_top(entry->start_address, ui->disassembly.rows));
            frontend_disassembly_set_user_cursor(&ui->disassembly, entry->start_address, ui->disassembly.rows / 2, 1);
        }
        nk_layout_row_push(ctx, 0.14f);
        if (frontend_nk_action_button(ctx, "Clear", true)) {
            frontend_push_breakpoint_id_intent(
                ui,
                FRONTEND_DEBUGGER_INTENT_BREAKPOINT_CLEAR,
                entry->id,
                false);
        }
        nk_layout_row_end(ctx);
        ctx->style.button = saved_button;
    }

    if (count >= 2) {
        nk_layout_row_dynamic(ctx, 24.0f, 1);
        if (frontend_nk_action_button(ctx, "Clear All", true)) {
            frontend_push_debugger_intent(
                ui,
                FRONTEND_DEBUGGER_INTENT_BREAKPOINT_CLEAR_ALL,
                0);
        }
    }
}


static void frontend_hardware_label_value(
    struct nk_context *ctx,
    const char *label,
    const char *value)
{
    nk_layout_row_begin(ctx, NK_STATIC, 18.0f, 2);
    nk_layout_row_push(ctx, 150.0f);
    nk_label(ctx, label, NK_TEXT_LEFT);
    nk_layout_row_push(ctx, 660.0f);
    nk_label(ctx, value, NK_TEXT_LEFT);
    nk_layout_row_end(ctx);
}

static void frontend_draw_apple_actual_flag(
    struct nk_context *ctx,
    const char *address,
    const char *name,
    uint32_t flags,
    uint32_t bit)
{
    nk_layout_row_begin(ctx, NK_DYNAMIC, 18.0f, 3);
    nk_layout_row_push(ctx, 0.16f);
    nk_label(ctx, address, NK_TEXT_LEFT);
    nk_layout_row_push(ctx, 0.28f);
    nk_label(ctx, name, NK_TEXT_LEFT);
    nk_layout_row_push(ctx, 0.56f);
    nk_label(ctx, (flags & bit) != 0 ? "On" : "Off", NK_TEXT_LEFT);
    nk_layout_row_end(ctx);
}

static void frontend_draw_apple_display_flag(
    frontend *ui,
    struct nk_context *ctx,
    const char *address,
    const char *name,
    uint32_t actual_flags,
    uint32_t bit)
{
    nk_bool override_on =
        (ui->misc.display_override_flags & bit) != 0u ? nk_true : nk_false;
    nk_bool previous = override_on;

    nk_layout_row_begin(ctx, NK_DYNAMIC, 18.0f, 4);
    nk_layout_row_push(ctx, 0.16f);
    nk_label(ctx, address, NK_TEXT_LEFT);
    nk_layout_row_push(ctx, 0.28f);
    nk_label(ctx, name, NK_TEXT_LEFT);
    nk_layout_row_push(ctx, 0.16f);
    nk_label(ctx, (actual_flags & bit) != 0u ? "On" : "Off", NK_TEXT_LEFT);
    nk_layout_row_push(ctx, 0.40f);
    if (!ui->misc.display_override_enabled) {
        nk_widget_disable_begin(ctx);
    }
    nk_checkbox_label(ctx, "", &override_on);
    if (!ui->misc.display_override_enabled) {
        nk_widget_disable_end(ctx);
    }
    nk_layout_row_end(ctx);

    if (ui->misc.display_override_enabled && override_on != previous) {
        if (override_on) {
            ui->misc.display_override_flags |= bit;
        } else {
            ui->misc.display_override_flags &= ~bit;
        }
        frontend_push_display_override_intent(ui);
    }
}

static void frontend_draw_misc_hardware(frontend *ui, const frontend_debug_state *debug_state)
{
    struct nk_context *ctx;
    uint32_t f = 0;
    char value[96];

    if (ui == NULL || ui->ctx == NULL) {
        return;
    }

    ctx = ui->ctx;
    if (debug_state != NULL && debug_state->has_apple_flags) {
        f = debug_state->apple_state_flags;
    }

    if (debug_state == NULL || !debug_state->has_apple_flags) {
        nk_layout_row_dynamic(ctx, 18.0f, 1);
        nk_label(ctx, "No Apple state yet (wait for machine snapshot)", NK_TEXT_LEFT);
        return;
    }

    snprintf(
        value,
        sizeof(value),
        "%s",
        debug_state->apple_model == 0 ? "//e Enhanced" : "][+");
    frontend_hardware_label_value(ctx, "Model", value);

    if (!ui->misc.display_override_enabled) {
        ui->misc.display_override_flags = f &
            (A2S_COL80 | A2S_ALTCHARSET | A2S_TEXT | A2S_MIXED |
             A2S_PAGE2 | A2S_HIRES | A2S_DHIRES);
    }

    nk_layout_row_dynamic(ctx, 8.0f, 1);
    nk_spacing(ctx, 1);
    {
        nk_bool enabled = ui->misc.display_override_enabled ? nk_true : nk_false;
        nk_bool previous = enabled;
        nk_layout_row_begin(ctx, NK_DYNAMIC, 18.0f, 4);
        nk_layout_row_push(ctx, 0.16f);
        nk_label(ctx, "Display", NK_TEXT_LEFT);
        nk_layout_row_push(ctx, 0.28f);
        nk_label(ctx, "", NK_TEXT_LEFT);
        nk_layout_row_push(ctx, 0.16f);
        nk_label(ctx, "Actual", NK_TEXT_LEFT);
        nk_layout_row_push(ctx, 0.40f);
        nk_checkbox_label(ctx, "Override", &enabled);
        nk_layout_row_end(ctx);
        if (enabled != previous) {
            ui->misc.display_override_enabled = enabled != 0;
            if (!ui->misc.display_override_enabled) {
                ui->misc.display_override_flags = f &
                    (A2S_COL80 | A2S_ALTCHARSET | A2S_TEXT | A2S_MIXED |
                     A2S_PAGE2 | A2S_HIRES | A2S_DHIRES);
            }
            frontend_push_display_override_intent(ui);
        }
    }
    frontend_draw_apple_display_flag(ui, ctx, "$C00D", "80COL", f, A2S_COL80);
    frontend_draw_apple_display_flag(ui, ctx, "$C00F", "ALTCHAR", f, A2S_ALTCHARSET);
    frontend_draw_apple_display_flag(ui, ctx, "$C051", "TEXT", f, A2S_TEXT);
    frontend_draw_apple_display_flag(ui, ctx, "$C053", "MIXED", f, A2S_MIXED);
    frontend_draw_apple_display_flag(ui, ctx, "$C055", "PAGE2", f, A2S_PAGE2);
    frontend_draw_apple_display_flag(ui, ctx, "$C057", "HIRES", f, A2S_HIRES);
    frontend_draw_apple_display_flag(ui, ctx, "$C05E", "DHIRES", f, A2S_DHIRES);

    nk_layout_row_dynamic(ctx, 8.0f, 1);
    nk_spacing(ctx, 1);
    nk_layout_row_dynamic(ctx, 16.0f, 1);
    nk_label(ctx, "Banking", NK_TEXT_LEFT);
    frontend_draw_apple_actual_flag(ctx, "$C001", "80STORE", f, A2S_80STORE);
    frontend_draw_apple_actual_flag(ctx, "$C003", "RAMRD", f, A2S_RAMRD);
    frontend_draw_apple_actual_flag(ctx, "$C005", "RAMWRT", f, A2S_RAMWRT);
    frontend_draw_apple_actual_flag(ctx, "$C009", "ALTZP", f, A2S_ALTZP);
    frontend_draw_apple_actual_flag(ctx, "$C08X", "LC_READ", f, A2S_LC_READ);
    frontend_draw_apple_actual_flag(ctx, "$C08X", "LC_WRITE", f, A2S_LC_WRITE);
    frontend_draw_apple_actual_flag(ctx, "$C08X", "LC_BANK2", f, A2S_LC_BANK2);
    frontend_draw_apple_actual_flag(
        ctx, "$C007", "CXROM", f, A2S_CXSLOTROM_MB_ENABLE);
    frontend_draw_apple_actual_flag(
        ctx, "$C00B", "C3ROM off", f, A2S_SLOT3ROM_MB_DISABLE);
}

static void frontend_draw_misc_assembler(frontend *ui)
{
    struct nk_context *ctx;
    frontend_assembler_state *asm_state;
    static const nk_flags edit_flags = (nk_flags)NK_EDIT_FIELD | NK_EDIT_SELECTABLE | NK_EDIT_CLIPBOARD;

    if (ui == NULL || ui->ctx == NULL) {
        return;
    }

    ctx = ui->ctx;
    asm_state = &ui->assembler;

    if (!asm_state->initialized) {
        snprintf(asm_state->address_buf, sizeof(asm_state->address_buf), "8000");
        snprintf(asm_state->run_address_buf, sizeof(asm_state->run_address_buf), "8000");
        asm_state->use_address = true;
        asm_state->run_address_user_edited = false;
        asm_state->auto_run = false;
        asm_state->mli_launch = false;
        asm_state->reset_first = true;
        asm_state->rearm_oneshots = false;
        asm_state->initialized = true;
    }

    /* File Name row */
    nk_layout_row_begin(ctx, NK_DYNAMIC, 24.0f, 3);
    nk_layout_row_push(ctx, 0.22f);
    nk_label(ctx, "File Name", NK_TEXT_LEFT);
    nk_layout_row_push(ctx, 0.56f);
    frontend_edit_replace(ctx, edit_flags, asm_state->file_path, (int)sizeof(asm_state->file_path), nk_filter_default);
    nk_layout_row_push(ctx, 0.22f);
    if (nk_button_label(ctx, "Browse...")) {
        frontend_push_simple_intent(ui, FRONTEND_DEBUGGER_INTENT_ASSEMBLE_BROWSE);
    }
    nk_layout_row_end(ctx);

    /* [x] Assemble at [address] */
    nk_layout_row_begin(ctx, NK_DYNAMIC, 24.0f, 2);
    nk_layout_row_push(ctx, 0.50f);
    frontend_checkbox_bool(ctx, "Assemble at", &asm_state->use_address);
    if (!asm_state->use_address) {
        nk_widget_disable_begin(ctx);
    }
    nk_layout_row_push(ctx, 0.50f);
    {
        nk_flags addr_flags = asm_state->use_address
            ? edit_flags
            : (edit_flags | (nk_flags)NK_EDIT_READ_ONLY);
        if (frontend_edit_replace(
                ctx,
                addr_flags,
                asm_state->address_buf,
                (int)sizeof(asm_state->address_buf),
                nk_filter_hex) & NK_EDIT_DEACTIVATED) {
            /* Keep run_address in sync if user hasn't edited it independently */
            if (!asm_state->run_address_user_edited) {
                snprintf(
                    asm_state->run_address_buf,
                    sizeof(asm_state->run_address_buf),
                    "%s",
                    asm_state->address_buf);
            }
        }
    }
    if (!asm_state->use_address) {
        nk_widget_disable_end(ctx);
    }
    nk_layout_row_end(ctx);

    /* [x] Auto-run at [address] */
    nk_layout_row_begin(ctx, NK_DYNAMIC, 24.0f, 2);
    nk_layout_row_push(ctx, 0.50f);
    frontend_checkbox_bool(ctx, "Auto-run at", &asm_state->auto_run);
    if (!asm_state->auto_run) {
        asm_state->mli_launch = false;
        nk_widget_disable_begin(ctx);
    }
    nk_layout_row_push(ctx, 0.50f);
    {
        nk_flags run_flags = asm_state->auto_run
            ? edit_flags
            : (edit_flags | (nk_flags)NK_EDIT_READ_ONLY);
        if (frontend_edit_replace(
                ctx,
                run_flags,
                asm_state->run_address_buf,
                (int)sizeof(asm_state->run_address_buf),
                nk_filter_hex) & NK_EDIT_ACTIVE) {
            asm_state->run_address_user_edited = true;
        }
    }
    if (!asm_state->auto_run) {
        nk_widget_disable_end(ctx);
    }
    nk_layout_row_end(ctx);

    nk_layout_row_dynamic(ctx, 24.0f, 1);
    if (!asm_state->auto_run) {
        nk_widget_disable_begin(ctx);
    }
    {
        bool prev_mli = asm_state->mli_launch;
        frontend_checkbox_bool(ctx, "MLI launch", &asm_state->mli_launch);
        if (asm_state->mli_launch && !prev_mli) {
            asm_state->reset_first = false;
        }
    }
    if (!asm_state->auto_run) {
        nk_widget_disable_end(ctx);
    }

    nk_layout_row_dynamic(ctx, 24.0f, 1);
    {
        bool prev_reset = asm_state->reset_first;
        frontend_checkbox_bool(ctx, "Reset machine", &asm_state->reset_first);
        if (asm_state->reset_first && !prev_reset) {
            asm_state->mli_launch = false;
        }
    }

    nk_layout_row_dynamic(ctx, 24.0f, 1);
    frontend_checkbox_bool(ctx, "Rearm one-shots", &asm_state->rearm_oneshots);

    /* Assemble button */
    nk_layout_row_dynamic(ctx, 28.0f, 1);
    if (nk_button_label(ctx, "Assemble")) {
        if (asm_state->file_path[0] != '\0') {
            uint16_t assemble_address = asm_state->use_address
                ? (uint16_t)strtoul(asm_state->address_buf, NULL, 16)
                : 0u;
            uint16_t run_address = (uint16_t)strtoul(asm_state->run_address_buf, NULL, 16);
            frontend_push_assemble_run_intent(
                ui,
                asm_state->file_path,
                assemble_address,
                run_address,
                asm_state->auto_run,
                asm_state->mli_launch,
                asm_state->reset_first,
                asm_state->rearm_oneshots);
        }
    }
}

static void frontend_push_inspector_intent_ex(
    frontend *ui,
    frontend_debugger_intent_type type,
    bool enabled,
    uint64_t cycle,
    bool coalesce_land)
{
    size_t next;
    size_t i;

    if (ui == NULL) {
        return;
    }
    if (coalesce_land &&
        (type == FRONTEND_DEBUGGER_INTENT_INSPECTOR_LAND ||
         type == FRONTEND_DEBUGGER_INTENT_INSPECTOR_LAND_TO_CYCLE)) {
        i = ui->intent_read;
        while (i != ui->intent_write) {
            if (ui->intents[i].type == FRONTEND_DEBUGGER_INTENT_INSPECTOR_LAND ||
                ui->intents[i].type ==
                    FRONTEND_DEBUGGER_INTENT_INSPECTOR_LAND_TO_CYCLE) {
                /* Latest land wins; keep the requested land flavor. */
                ui->intents[i].type = type;
                ui->intents[i].inspector_cycle = cycle;
                return;
            }
            i = (i + 1u) % FRONTEND_DEBUGGER_INTENT_CAPACITY;
        }
    }
    next = (ui->intent_write + 1u) % FRONTEND_DEBUGGER_INTENT_CAPACITY;
    if (next == ui->intent_read) {
        return;
    }
    memset(&ui->intents[ui->intent_write], 0, sizeof(ui->intents[ui->intent_write]));
    ui->intents[ui->intent_write].type = type;
    ui->intents[ui->intent_write].enabled = enabled;
    ui->intents[ui->intent_write].inspector_cycle = cycle;
    ui->intent_write = next;
}

static void frontend_push_inspector_intent(
    frontend *ui,
    frontend_debugger_intent_type type,
    bool enabled,
    uint64_t cycle)
{
    frontend_push_inspector_intent_ex(ui, type, enabled, cycle, true);
}

static void frontend_forensics_land_context_from_debug(
    const frontend_debug_state *debug,
    frontend_forensics_land_context *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (debug == NULL) {
        return;
    }
    out->inspecting = debug->inspecting;
    out->window_valid = debug->inspector_window_valid;
    out->can_enter = debug->inspector_enabled && debug->inspector_window_valid;
    out->focus_cycle = debug->inspector_focus_cycle;
    out->oldest_cycle = debug->inspector_oldest_cycle;
    out->newest_cycle = debug->inspector_newest_cycle;
}

static void frontend_forensics_flush_land(frontend *ui)
{
    uint64_t cycle;
    bool need_enter;
    bool exact;
    frontend_debugger_intent_type land_type;

    if (ui == NULL || !ui->forensics.request_land) {
        return;
    }
    cycle = ui->forensics.pending_land_cycle;
    need_enter = ui->forensics.request_land_enter;
    exact = ui->forensics.request_land_exact;
    land_type = exact ? FRONTEND_DEBUGGER_INTENT_INSPECTOR_LAND_TO_CYCLE :
                        FRONTEND_DEBUGGER_INTENT_INSPECTOR_LAND;
    ui->forensics.request_land = false;
    ui->forensics.request_land_enter = false;
    ui->forensics.request_land_exact = false;
    if (need_enter) {
        /* ENTER then land must stay ordered; do not coalesce over a prior land. */
        frontend_push_inspector_intent_ex(
            ui, FRONTEND_DEBUGGER_INTENT_INSPECTOR_ENTER, false, 0u, false);
        frontend_push_inspector_intent_ex(ui, land_type, false, cycle, false);
    } else {
        frontend_push_inspector_intent(ui, land_type, false, cycle);
    }
    ui->forensics.land_requested_cycle = cycle;
    ui->forensics.land_awaiting_focus = true;
    ui->forensics.land_awaiting_exact = exact;
}

static void frontend_format_inspector_window_reason(
    const frontend_debug_state *debug,
    char *out,
    size_t out_size)
{
    unsigned slot;
    unsigned drive;
    const char *kind;

    if (out == NULL || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (debug == NULL || !debug->inspector_window_valid) {
        return;
    }
    slot = (unsigned)((debug->inspector_window_start_arg1 >> 8) & 0xffu);
    drive = (unsigned)(debug->inspector_window_start_arg1 & 0xffu) + 1u;
    kind = runtime_inspector_window_start_name(
        (runtime_history_media_change_kind)debug->inspector_window_start_kind);
    if (debug->inspector_window_start_kind ==
        RUNTIME_HISTORY_MEDIA_CHANGE_GUEST_WRITE) {
        snprintf(
            out,
            out_size,
            "history starts here: disk write, s%ud%u @ cycle %llu",
            slot,
            drive,
            (unsigned long long)debug->inspector_oldest_cycle);
    } else if (debug->inspector_window_start_kind ==
        RUNTIME_HISTORY_MEDIA_CHANGE_HOST_DIRECTORY) {
        snprintf(
            out,
            out_size,
            "history starts here: host folder change, s%ud%u @ cycle %llu",
            slot,
            drive,
            (unsigned long long)debug->inspector_oldest_cycle);
    } else if (debug->inspector_oldest_cycle > 0u) {
        snprintf(
            out,
            out_size,
            "history starts here: %s @ cycle %llu",
            kind,
            (unsigned long long)debug->inspector_oldest_cycle);
    }
}

static uint64_t frontend_inspector_slider_to_ordinal(uint64_t count, int tick)
{
    uint64_t width;
    uint64_t maximum;
    uint64_t q;
    uint64_t r;

    if (count <= 1u) return 0u;
    width = count - 1u;
    maximum = width < 1000u ? width : 1000u;
    if (tick <= 0) return 0u;
    if ((uint64_t)tick >= maximum) return width;
    q = width / maximum;
    r = width % maximum;
    return (uint64_t)tick * q + ((uint64_t)tick * r) / maximum;
}

static int frontend_inspector_ordinal_to_slider(uint64_t count, uint64_t ordinal)
{
    uint64_t maximum;
    uint64_t tick;
    uint64_t best_tick = 0u;
    uint64_t best_distance = UINT64_MAX;

    if (count <= 1u) return 0;
    if (ordinal >= count) ordinal = count - 1u;
    maximum = count - 1u < 1000u ? count - 1u : 1000u;
    for (tick = 0u; tick <= maximum; tick++) {
        uint64_t mapped = frontend_inspector_slider_to_ordinal(count, (int)tick);
        uint64_t distance = mapped > ordinal ? mapped - ordinal : ordinal - mapped;
        if (distance <= best_distance) {
            best_distance = distance;
            best_tick = tick;
        }
    }
    return (int)best_tick;
}

static void frontend_draw_inspector_window_summary(
    struct nk_context *ctx,
    const frontend_debug_state *debug)
{
    char line[64];
    uint64_t oldest;
    uint64_t live;
    uint64_t span;
    uint64_t cps;
    uint64_t secs;

    if (ctx == NULL || debug == NULL || !debug->inspector_window_valid) {
        return;
    }
    oldest = debug->inspector_oldest_cycle;
    live = debug->inspector_newest_cycle;
    span = live >= oldest ? live - oldest : 0u;
    cps = (uint64_t)APPLE2_VIDEO_CYCLES_PER_FRAME * 60u;
    secs = (span + cps / 2u) / cps;

    nk_layout_row_begin(ctx, NK_DYNAMIC, 18.0f, 2);
    nk_layout_row_push(ctx, 0.48f);
    nk_label(ctx, "History start cycle", NK_TEXT_LEFT);
    nk_layout_row_push(ctx, 0.52f);
    snprintf(line, sizeof(line), "%llu", (unsigned long long)oldest);
    nk_label(ctx, line, NK_TEXT_LEFT);
    nk_layout_row_end(ctx);

    nk_layout_row_begin(ctx, NK_DYNAMIC, 18.0f, 2);
    nk_layout_row_push(ctx, 0.48f);
    nk_label(ctx, "Live cycle", NK_TEXT_LEFT);
    nk_layout_row_push(ctx, 0.52f);
    snprintf(line, sizeof(line), "%llu", (unsigned long long)live);
    nk_label(ctx, line, NK_TEXT_LEFT);
    nk_layout_row_end(ctx);

    nk_layout_row_begin(ctx, NK_DYNAMIC, 18.0f, 2);
    nk_layout_row_push(ctx, 0.48f);
    nk_label(ctx, "Duration", NK_TEXT_LEFT);
    nk_layout_row_push(ctx, 0.52f);
    snprintf(line, sizeof(line), "~%llu seconds", (unsigned long long)secs);
    nk_label(ctx, line, NK_TEXT_LEFT);
    nk_layout_row_end(ctx);
}

static void frontend_draw_misc_inspector(
    frontend *ui,
    const frontend_debug_state *debug)
{
    struct nk_context *ctx;
    char line[192];
    nk_bool rec;
    bool inspecting;
    bool can_enter;
    bool preview_active;
    int slider;
    uint64_t catalog_count;

    if (ui == NULL || ui->ctx == NULL) {
        return;
    }
    ctx = ui->ctx;
    inspecting = debug != NULL && debug->inspecting;
    catalog_count = debug != NULL ? debug->inspector_catalog.count : 0u;
    can_enter = debug != NULL && debug->inspector_enabled && debug->inspector_window_valid;

    if (!inspecting) {
        ui->misc.inspector_thumb_down = false;
        ui->misc.inspector_land_pending = false;
        ui->misc.inspector_preview_picture_id = 0u;
        ui->misc.inspector_preview_sample_id = 0u;
        ui->misc.inspector_preview_ordinal = 0u;
        ui->misc.inspector_preview_keep_current = false;
    } else if (ui->misc.inspector_land_pending &&
               debug->inspector_focus_is_sample &&
               debug->inspector_focus_id ==
                   ui->misc.inspector_preview_sample_id) {
        ui->misc.inspector_land_pending = false;
    }
    preview_active = ui->misc.inspector_thumb_down ||
        ui->misc.inspector_land_pending;

    if (!inspecting) {
        rec = (debug != NULL && debug->inspector_enabled) ? nk_true : nk_false;
        nk_layout_row_dynamic(ctx, 22.0f, 1);
        if (nk_checkbox_label(ctx, "Record", &rec)) {
            bool want = rec != nk_false;
            bool have = debug != NULL && debug->inspector_enabled;
            if (want != have &&
                debug != NULL) {
                frontend_push_inspector_intent(
                    ui, FRONTEND_DEBUGGER_INTENT_INSPECTOR_SET_ENABLED, want, 0u);
            }
        }
    }

    nk_layout_row_dynamic(ctx, 24.0f, 1);
    if (nk_button_label(ctx, "Forensics...")) {
        /* Inspector lives in the debugger; entry surface is debugger. */
        frontend_open_forensics(ui, true, false);
    }

    if (debug == NULL || !debug->inspector_enabled) {
        return;
    }

    if (!inspecting) {
        if (can_enter) {
            nk_layout_row_dynamic(ctx, 24.0f, 1);
            if (nk_button_label(ctx, "Inspect")) {
                frontend_push_inspector_intent(
                    ui, FRONTEND_DEBUGGER_INTENT_INSPECTOR_ENTER, false, 0u);
            }
            frontend_draw_inspector_window_summary(ctx, debug);
        } else if (!debug->inspector_window_valid) {
            nk_layout_row_dynamic(ctx, 36.0f, 1);
            nk_label_wrap(ctx, "No Inspector snapshots yet.");
        }
        return;
    }

    nk_layout_row_dynamic(ctx, 24.0f, 1);
    if (nk_button_label(ctx, "Leave Inspector")) {
        frontend_push_inspector_intent(
            ui, FRONTEND_DEBUGGER_INTENT_INSPECTOR_LEAVE, false, 0u);
    }

    slider = ui->misc.inspector_slider;
    {
        bool down = nk_input_is_mouse_down(&ctx->input, NK_BUTTON_LEFT);
        bool can_previous = false;
        bool can_next = false;
        bool thumb = ui->misc.inspector_thumb_down;
        bool busy = thumb || ui->misc.inspector_land_pending;
        int slider_max = catalog_count > 1u ?
            (int)((catalog_count - 1u) < 1000u ? catalog_count - 1u : 1000u) : 0;

        if (debug->inspector_focus_is_sample) {
            can_previous = debug->inspector_focus_ordinal > 0u;
            can_next = debug->inspector_focus_ordinal + 1u < catalog_count;
        } else {
            uint64_t i;
            for (i = 0u; i < catalog_count; i++) {
                uint64_t cycle =
                    debug->inspector_catalog.samples[i].snapshot_cycle;
                if (cycle < debug->inspector_focus_cycle) can_previous = true;
                if (cycle > debug->inspector_focus_cycle) can_next = true;
            }
        }

        if (!busy) {
            uint64_t focus_ordinal = debug->inspector_focus_ordinal;
            if (!debug->inspector_focus_is_sample) {
                uint64_t i;
                focus_ordinal = 0u;
                for (i = 0u; i < catalog_count; i++) {
                    if (debug->inspector_catalog.samples[i].snapshot_cycle <=
                        debug->inspector_focus_cycle) {
                        focus_ordinal = i;
                    } else {
                        break;
                    }
                }
            }
            slider = frontend_inspector_ordinal_to_slider(
                catalog_count, focus_ordinal);
        }

        nk_layout_row_begin(ctx, NK_DYNAMIC, 22.0f, 3);
        nk_layout_row_push(ctx, 0.08f);
        if (frontend_nk_action_button(
                ctx, "-", !busy && can_previous)) {
            frontend_push_inspector_intent(
                ui, FRONTEND_DEBUGGER_INTENT_INSPECTOR_SAMPLE_STEP, false, 0u);
        }
        nk_layout_row_push(ctx, 0.84f);
        {
            /* Peek this column before the slider consumes it. After
               nk_slider_int, nk_widget_bounds is the [+] slot. */
            struct nk_rect bounds = nk_widget_bounds(ctx);
            nk_bool moved;
            if (slider_max == 0) {
                nk_widget_disable_begin(ctx);
            }
            moved = nk_slider_int(ctx, 0, &slider, slider_max, 1);
            if (slider_max == 0) {
                nk_widget_disable_end(ctx);
            }
            bool hovered =
                nk_input_is_mouse_hovering_rect(&ctx->input, bounds);

            if (moved && down) {
                ui->misc.inspector_thumb_down = true;
            }
            if (ui->misc.inspector_thumb_down) {
                uint64_t ordinal = frontend_inspector_slider_to_ordinal(
                    catalog_count, slider);
                uint64_t sample_id = 0u;
                if (ordinal < catalog_count) {
                    sample_id = debug->inspector_catalog.samples[ordinal].sample_id;
                    ui->misc.inspector_preview_picture_id =
                        debug->inspector_catalog.samples[ordinal].picture_id;
                    ui->misc.inspector_preview_sample_id = sample_id;
                    ui->misc.inspector_preview_ordinal = ordinal;
                    ui->misc.inspector_preview_keep_current =
                        ordinal + 1u == catalog_count;
                }
                if (!down) {
                    frontend_push_inspector_intent(
                        ui,
                        FRONTEND_DEBUGGER_INTENT_INSPECTOR_LAND_SAMPLE,
                        false,
                        sample_id);
                    ui->misc.inspector_thumb_down = false;
                    ui->misc.inspector_land_pending = sample_id != 0u;
                }
            } else if ((hovered || moved) && down) {
                uint64_t ordinal = frontend_inspector_slider_to_ordinal(
                    catalog_count, slider);
                ui->misc.inspector_thumb_down = true;
                ui->misc.inspector_preview_picture_id = ordinal < catalog_count ?
                    debug->inspector_catalog.samples[ordinal].picture_id : 0u;
                ui->misc.inspector_preview_sample_id = ordinal < catalog_count ?
                    debug->inspector_catalog.samples[ordinal].sample_id : 0u;
                ui->misc.inspector_preview_ordinal = ordinal;
                ui->misc.inspector_preview_keep_current =
                    ordinal + 1u == catalog_count;
            }
        }
        nk_layout_row_push(ctx, 0.08f);
        if (frontend_nk_action_button(ctx, "+", !busy && can_next)) {
            frontend_push_inspector_intent(
                ui, FRONTEND_DEBUGGER_INTENT_INSPECTOR_SAMPLE_STEP, true, 0u);
        }
        nk_layout_row_end(ctx);
        ui->misc.inspector_slider = slider;
    }

    nk_layout_row_begin(ctx, NK_DYNAMIC, 18.0f, 2);
    nk_layout_row_push(ctx, 0.48f);
    nk_label(ctx, "Snapshot:", NK_TEXT_LEFT);
    nk_layout_row_push(ctx, 0.52f);
    if (preview_active && catalog_count > 0u) {
        uint64_t preview_number = ui->misc.inspector_preview_ordinal + 1u;
        if (debug->inspector_focus_is_sample) {
            snprintf(
                line, sizeof(line), "%llu of %llu/%llu",
                (unsigned long long)(debug->inspector_focus_ordinal + 1u),
                (unsigned long long)preview_number,
                (unsigned long long)catalog_count);
        } else {
            snprintf(
                line, sizeof(line), "Exact to %llu/%llu",
                (unsigned long long)preview_number,
                (unsigned long long)catalog_count);
        }
    } else if (debug->inspector_focus_is_sample) {
        snprintf(
            line, sizeof(line), "%llu of %llu",
            (unsigned long long)(catalog_count > 0u ?
                debug->inspector_focus_ordinal + 1u : 0u),
            (unsigned long long)catalog_count);
    } else {
        snprintf(line, sizeof(line), "Exact focus");
    }
    nk_label(ctx, line, NK_TEXT_LEFT);
    nk_layout_row_end(ctx);

    nk_layout_row_begin(ctx, NK_DYNAMIC, 18.0f, 2);
    nk_layout_row_push(ctx, 0.48f);
    nk_label(ctx, "Current cycle:", NK_TEXT_LEFT);
    nk_layout_row_push(ctx, 0.52f);
    snprintf(
        line,
        sizeof(line),
        "%llu",
        (unsigned long long)debug->inspector_focus_cycle);
    nk_label(ctx, line, NK_TEXT_LEFT);
    nk_layout_row_end(ctx);
    if (debug->inspector_focus_is_sample &&
        debug->inspector_focus_ordinal < catalog_count) {
        nk_layout_row_begin(ctx, NK_DYNAMIC, 18.0f, 2);
        nk_layout_row_push(ctx, 0.48f);
        nk_label(ctx, "Frame cycle:", NK_TEXT_LEFT);
        nk_layout_row_push(ctx, 0.52f);
        snprintf(
            line, sizeof(line), "%llu",
            (unsigned long long)debug->inspector_catalog
                .samples[debug->inspector_focus_ordinal].frame_cycle);
        nk_label(ctx, line, NK_TEXT_LEFT);
        nk_layout_row_end(ctx);
    }
    frontend_draw_inspector_window_summary(ctx, debug);
}

static void frontend_draw_misc_tab_button(
    frontend *ui,
    frontend_misc_tab tab,
    const char *label)
{
    struct nk_style_button saved_button = ui->ctx->style.button;

    if (ui->misc.active_tab == tab) {
        ui->ctx->style.button.normal = nk_style_item_color(nk_rgb(49, 78, 94));
        ui->ctx->style.button.hover = nk_style_item_color(nk_rgb(58, 93, 112));
        ui->ctx->style.button.text_normal = nk_rgb(226, 246, 255);
    }

    if (nk_button_label(ui->ctx, label)) {
        ui->misc.active_tab = tab;
    }

    ui->ctx->style.button = saved_button;
}

static void frontend_draw_misc(frontend *ui, struct nk_rect bounds, const frontend_debug_state *debug_state)
{
    struct nk_context *ctx;
    float tab_h = 22.0f;
    float content_h;

    if (ui == NULL || ui->ctx == NULL) {
        return;
    }

    if (!ui->misc.initialized) {
        ui->misc.active_tab = FRONTEND_MISC_TAB_PROGRAMS;
        ui->misc.initialized = true;
    }

    ctx = ui->ctx;
    if (nk_begin(ctx, "Misc", bounds, NK_WINDOW_BORDER | NK_WINDOW_TITLE)) {
        nk_layout_row_dynamic(ctx, tab_h, 3);
        frontend_draw_misc_tab_button(ui, FRONTEND_MISC_TAB_PROGRAMS, "Machine");
        frontend_draw_misc_tab_button(ui, FRONTEND_MISC_TAB_DEBUGGER, "Debugger");
        frontend_draw_misc_tab_button(ui, FRONTEND_MISC_TAB_BREAKPOINTS, "Breakpoints");
        nk_layout_row_dynamic(ctx, tab_h, 3);
        frontend_draw_misc_tab_button(ui, FRONTEND_MISC_TAB_HARDWARE, "Hardware");
        frontend_draw_misc_tab_button(ui, FRONTEND_MISC_TAB_ASSEMBLER, "Assembler");
        frontend_draw_misc_tab_button(ui, FRONTEND_MISC_TAB_INSPECTOR, "Inspector");

        /* 55.0f is the original one-row chrome (title, pad, borders, reserved
           window scrollbar). Each extra tab row also costs window.spacing.y,
           which Nuklear adds to every row; omitting it made the group overflow
           and drew a second scrollbar outside every tab. */
        content_h = bounds.h - (tab_h * 2.0f) - 55.0f - ctx->style.window.spacing.y;
        if (content_h < 24.0f) {
            content_h = 24.0f;
        }

        nk_layout_row_dynamic(ctx, content_h, 1);
        if (nk_group_begin(ctx, "misc-tab-content", NK_WINDOW_BORDER)) {
            switch (ui->misc.active_tab) {
                case FRONTEND_MISC_TAB_PROGRAMS:
                    frontend_draw_misc_programs(ui, debug_state);
                    break;

                case FRONTEND_MISC_TAB_DEBUGGER:
                    frontend_draw_misc_debugger(ui, debug_state);
                    break;

                case FRONTEND_MISC_TAB_BREAKPOINTS:
                    frontend_draw_misc_breakpoints(ui, debug_state);
                    break;

                case FRONTEND_MISC_TAB_HARDWARE:
                    frontend_draw_misc_hardware(ui, debug_state);
                    break;

                case FRONTEND_MISC_TAB_INSPECTOR:
                    frontend_draw_misc_inspector(ui, debug_state);
                    break;

                case FRONTEND_MISC_TAB_ASSEMBLER:
                default:
                    frontend_draw_misc_assembler(ui);
                    break;
            }
            nk_group_end(ctx);
        }

        /* Drawn at window level (not inside the group) so the popup is not
           clipped by the tab-content group bounds. */
        frontend_draw_reset_prompt(ui, ctx);

        if (!frontend_any_dialog_open(ui) && ui->active_view == FRONTEND_ACTIVE_VIEW_MISC) {
            frontend_draw_active_view_border(ctx);
        }
    }
    nk_end(ctx);
}

static void frontend_draw_splitter(struct nk_context *ctx, const char *name, struct nk_rect bounds, int active)
{
    struct nk_style_window saved;
    struct nk_color color = active ? nk_rgb(114, 164, 204) : nk_rgb(74, 88, 100);

    saved = ctx->style.window;
    ctx->style.window.fixed_background = nk_style_item_color(color);
    ctx->style.window.border = 0.0f;
    ctx->style.window.rounding = 0.0f;
    ctx->style.window.padding = nk_vec2(0.0f, 0.0f);

    if (nk_begin(ctx, name, bounds, NK_WINDOW_NO_SCROLLBAR)) {
    }
    nk_end(ctx);

    ctx->style.window = saved;
}

static void frontend_draw_corner_handle(struct nk_context *ctx, struct nk_rect bounds, int active)
{
    struct nk_color color = active ? nk_rgb(176, 214, 241) : nk_rgb(105, 126, 143);
    struct nk_style_window saved;

    saved = ctx->style.window;
    ctx->style.window.fixed_background = nk_style_item_color(nk_rgba(0, 0, 0, 0));
    ctx->style.window.border = 0.0f;
    ctx->style.window.rounding = 0.0f;
    ctx->style.window.padding = nk_vec2(0.0f, 0.0f);

    if (nk_begin(ctx, "display-corner", bounds, NK_WINDOW_NO_SCROLLBAR)) {
        struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
        float x = bounds.x + bounds.w - 2.0f;
        float y = bounds.y + bounds.h - 2.0f;

        nk_stroke_line(canvas, x - 13.0f, y, x, y - 13.0f, 2.0f, color);
        nk_stroke_line(canvas, x - 8.0f, y, x, y - 8.0f, 2.0f, color);
        nk_stroke_line(canvas, x - 3.0f, y, x, y - 3.0f, 2.0f, color);
    }
    nk_end(ctx);

    ctx->style.window = saved;
}

frontend *frontend_create(platform_window *window)
{
    frontend *ui;
    struct nk_font_atlas *atlas;
    struct nk_font *font;
    SDL_Window *sdl_window;
    SDL_Renderer *sdl_renderer;

    if (window == NULL) {
        return NULL;
    }

    sdl_window = platform_window_get_sdl_window(window);
    sdl_renderer = platform_window_get_sdl_renderer(window);
    if (sdl_window == NULL || sdl_renderer == NULL) {
        return NULL;
    }

    ui = calloc(1, sizeof(*ui));
    if (ui == NULL) {
        return NULL;
    }

    ui->window = window;
    ui->renderer = sdl_renderer;
    ui->ctx = nk_sdl_init(sdl_window, sdl_renderer);
    if (ui->ctx == NULL) {
        free(ui);
        return NULL;
    }

    nk_sdl_font_stash_begin(&atlas);
    font = nk_font_atlas_add_default(atlas, 13.0f, NULL);
    ui->help_font = nk_font_atlas_add_from_memory(
        atlas,
        (void *)mono_font_data,
        (nk_size)mono_font_data_len,
        10.0f,
        NULL);
    nk_sdl_font_stash_end();
    if (font != NULL) {
        nk_style_set_font(ui->ctx, &font->handle);
    }
    symbol_resolver_null(&ui->symbols);

    ui->limits.registers_h_px = 88;
    ui->limits.min_display_w_px = 220;
    ui->limits.min_right_w_px = 220;
    ui->limits.min_disassembly_h_px = 120;
    ui->limits.min_bottom_h_px = 150;
    ui->limits.min_memory_w_px = 260;
    ui->limits.min_misc_w_px = 180;
    ui->limits.gutter_px = 5;
    ui->limits.corner_px = 22;
    debugger_layout_init(&ui->layout);
    help_view_init(&ui->help);
    forensics_view_init(&ui->forensics);
    ui->show_disk_leds = true; /* matches app_options default until set_config_state */

    ui->led_green_texture = frontend_load_png_texture(
        sdl_renderer,
        led_green,
        led_green_len);
    ui->led_red_texture = frontend_load_png_texture(
        sdl_renderer,
        led_red,
        led_red_len);
    if (ui->led_green_texture == NULL || ui->led_red_texture == NULL) {
        log_warn("frontend: failed to load disk activity LED textures");
    }

    return ui;
}

void frontend_set_layout_state(frontend *ui, const frontend_layout_state *state)
{
    if (ui == NULL || state == NULL) {
        return;
    }

    if (state->split_display_right > 0.0f) {
        ui->layout.split_display_right = state->split_display_right;
    }
    if (state->split_top_bottom > 0.0f) {
        ui->layout.split_top_bottom = state->split_top_bottom;
    }
    if (state->split_memory_misc > 0.0f) {
        ui->layout.split_memory_misc = state->split_memory_misc;
    }
}

void frontend_get_layout_state(frontend *ui, frontend_layout_state *out_state)
{
    if (ui == NULL || out_state == NULL) {
        return;
    }

    out_state->split_display_right = ui->layout.split_display_right;
    out_state->split_top_bottom = ui->layout.split_top_bottom;
    out_state->split_memory_misc = ui->layout.split_memory_misc;
}

void frontend_debug_min_window_size(const frontend *ui, int *out_min_w, int *out_min_h)
{
    int min_w = 0;
    int min_h = 0;

    if (ui != NULL) {
        min_w = ui->limits.min_display_w_px + ui->limits.min_right_w_px;
        min_h = ui->limits.registers_h_px + ui->limits.min_disassembly_h_px + ui->limits.min_bottom_h_px;
    }
    if (out_min_w != NULL) {
        *out_min_w = min_w;
    }
    if (out_min_h != NULL) {
        *out_min_h = min_h;
    }
}

void frontend_set_config_state(frontend *ui, const app_options *options)
{
    bool keep_open;
    frontend_config_tab keep_tab;

    if (ui == NULL || options == NULL) {
        return;
    }

    keep_open = ui->config_dialog.open;
    keep_tab = ui->config_dialog.active_tab;
    ui->show_disk_leds = options->show_disk_leds;
    frontend_preview_crt_options(ui, options);
    ui->video_display_preview_valid = true;
    ui->video_display_preview_colour = options->colour_display;
    ui->video_display_preview_mono = options->mono_mode;

    frontend_config_dialog_reset(&ui->config_dialog);
    if (!app_options_copy(&ui->config_dialog.original, options) ||
        !app_options_copy(&ui->config_dialog.edited, options) ||
        !frontend_config_prepare_edit_buffers(&ui->config_dialog)) {
        frontend_config_dialog_reset(&ui->config_dialog);
        return;
    }

    ui->config_dialog.initialized = true;
    ui->config_dialog.open = keep_open;
    if (keep_open) {
        ui->config_dialog.active_tab = keep_tab;
    }
}

void frontend_sync_config_media_mounts(frontend *ui, const app_options *options)
{
    if (ui == NULL || options == NULL || !ui->config_dialog.initialized) {
        return;
    }
    (void)app_options_replace_media_mounts(&ui->config_dialog.original, options);
    (void)app_options_replace_media_mounts(&ui->config_dialog.edited, options);
}

bool frontend_config_dialog_is_open(const frontend *ui)
{
    return ui != NULL && ui->config_dialog.open;
}

bool frontend_config_toggle_colour_preview(frontend *ui)
{
    if (ui == NULL || !ui->config_dialog.open || !ui->config_dialog.initialized) {
        return false;
    }
    ui->config_dialog.edited.colour_display =
        !ui->config_dialog.edited.colour_display;
    frontend_preview_video_display(ui, &ui->config_dialog.edited);
    return true;
}

void frontend_set_disk_queue(frontend *ui, uint8_t device, const app_disk_slot *slot)
{
    int index;

    if (ui == NULL || slot == NULL) {
        return;
    }
    if (device > 1u) {
        return;
    }
    index = (int)device;

    app_disk_slot_copy(&ui->disk_queue[index], slot);
}

bool frontend_apply_selected_ini(frontend *ui, const app_options *options)
{
    frontend_config_dialog_state *dialog;

    if (ui == NULL || options == NULL) {
        return false;
    }

    dialog = &ui->config_dialog;
    if (!dialog->initialized) {
        return false;
    }

    app_options_destroy(&dialog->edited);
    if (!app_options_copy(&dialog->edited, options) ||
        !frontend_config_prepare_edit_buffers(dialog)) {
        frontend_config_dialog_reset(dialog);
        return false;
    }

    frontend_config_commit_ini_path(dialog);
    if (frontend_file_exists(dialog->edited.ini_path)) {
        dialog->prompt = FRONTEND_INI_PROMPT_EXISTING;
    }
    return true;
}

void frontend_append_symbol_file(frontend *ui, const char *path)
{
    frontend_config_dialog_state *dialog;
    char display_path[1024];
    char merged[1024];

    if (ui == NULL || path == NULL || path[0] == '\0') {
        return;
    }

    dialog = &ui->config_dialog;
    if (!dialog->initialized) {
        return;
    }

    if (!app_options_path_relative_to_ini(&dialog->edited, path, display_path, sizeof(display_path))) {
        snprintf(display_path, sizeof(display_path), "%s", path);
    }

    if (dialog->edited.symbol_files != NULL && dialog->edited.symbol_files[0] != '\0') {
        /* symbol_files is a heap buffer GCC can't size-track; the trailing
         * snprintf() below re-truncates to the same 1024 limit regardless,
         * so a truncated merge here is harmless. */
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif
        snprintf(merged, sizeof(merged), "%s,%s", dialog->edited.symbol_files, display_path);
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
    } else {
        snprintf(merged, sizeof(merged), "%s", display_path);
    }
    frontend_config_reserve_string(&dialog->edited.symbol_files, 1024);
    snprintf(dialog->edited.symbol_files, 1024, "%s", merged);
}

void frontend_set_assembler_path(frontend *ui, const char *path)
{
    char display_path[1024];

    if (ui == NULL || path == NULL) {
        return;
    }

    if (ui->config_dialog.initialized &&
        app_options_path_relative_to_ini(
            &ui->config_dialog.edited, path, display_path, sizeof(display_path))) {
        snprintf(ui->assembler.file_path, sizeof(ui->assembler.file_path), "%s", display_path);
    } else {
        snprintf(ui->assembler.file_path, sizeof(ui->assembler.file_path), "%s", path);
    }
}

void frontend_show_assembler_errors(frontend *ui, const char *errors)
{
    if (ui == NULL) {
        return;
    }

    snprintf(ui->assembler.error_text, sizeof(ui->assembler.error_text), "%s",
        errors != NULL ? errors : "");
    ui->assembler.error_scroll_x = 0;
    ui->assembler.error_scroll_y = 0;
    ui->assembler.error_dialog_is_notice = false;
    ui->assembler.error_dialog_open = true;
    ui->misc.active_tab = FRONTEND_MISC_TAB_ASSEMBLER;
}

void frontend_show_assembler_notice(frontend *ui, const char *notice)
{
    if (ui == NULL) {
        return;
    }

    snprintf(ui->assembler.error_text, sizeof(ui->assembler.error_text), "%s",
        notice != NULL ? notice : "");
    ui->assembler.error_scroll_x = 0;
    ui->assembler.error_scroll_y = 0;
    ui->assembler.error_dialog_is_notice = true;
    ui->assembler.error_dialog_open = true;
    ui->misc.active_tab = FRONTEND_MISC_TAB_ASSEMBLER;
}

void frontend_set_load_bin_path(frontend *ui, const char *path)
{
    frontend_load_bin_dialog_state *dlg;
    FILE *file;
    long file_size;
    uint8_t *bytes;
    uint16_t raw_address = 0u;
    apple2_binary_view binary;
    char error[96] = {0};

    if (ui == NULL || path == NULL) {
        return;
    }
    dlg = &ui->load_bin_dialog;
    snprintf(dlg->path, sizeof(dlg->path), "%s", path);
    dlg->detected[0] = '\0';
    dlg->has_naps_type = false;
    dlg->naps_type = 0u;
    dlg->decode_ok = false;
    {
        uint8_t naps_type = 0u;
        uint16_t naps_aux = 0u;
        if (apple2_naps_parse_path(path, &naps_type, &naps_aux)) {
            dlg->has_naps_type = true;
            dlg->naps_type = naps_type;
        }
    }
    if (dlg->kind == FRONTEND_MACHINE_FILE_SNAPSHOT) {
        snprintf(dlg->detected, sizeof(dlg->detected), "Selected: machine snapshot");
        return;
    }
    if (dlg->kind == FRONTEND_MACHINE_FILE_APPLESOFT_TEXT) {
        snprintf(dlg->detected, sizeof(dlg->detected), "Selected: Applesoft text listing");
        return;
    }
    {
        const char *dot = strrchr(path, '.');
        if (dlg->kind == FRONTEND_MACHINE_FILE_AUTO && dot != NULL &&
            SDL_strcasecmp(dot + 1, "a2state") == 0) {
            snprintf(dlg->detected, sizeof(dlg->detected), "Detected: machine snapshot");
            return;
        }
    }
    (void)frontend_parse_hex16_text(dlg->address_buf, &raw_address);
    file = fopen(path, "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0 ||
        (file_size = ftell(file)) < 0 || fseek(file, 0, SEEK_SET) != 0) {
        if (file != NULL) fclose(file);
        return;
    }
    bytes = (uint8_t *)malloc((size_t)file_size == 0u ? 1u : (size_t)file_size);
    if (bytes == NULL || ((size_t)file_size > 0u &&
        fread(bytes, 1, (size_t)file_size, file) != (size_t)file_size)) {
        free(bytes);
        fclose(file);
        return;
    }
    fclose(file);
    if (apple2_binary_decode(
            path, bytes, (size_t)file_size, dlg->binary_format, raw_address,
            &binary, error, sizeof(error))) {
        const char *name;
        if (binary.format == APPLE2_BINARY_FORMAT_APPLESINGLE) {
            name = binary.has_prodos_type && binary.prodos_type == 0xffu ?
                "AppleSingle SYS" : "AppleSingle BIN";
        } else if (binary.format == APPLE2_BINARY_FORMAT_NAPS) {
            name = binary.has_prodos_type && binary.prodos_type == 0xffu ?
                "NAPS SYS" : "NAPS BIN";
        } else if (binary.format == APPLE2_BINARY_FORMAT_LEGACY_DOS) {
            name = "legacy DOS binary";
        } else {
            name = "raw binary";
        }
        snprintf(dlg->detected, sizeof(dlg->detected),
            "Detected: %s at $%04X (%u bytes)", name,
            (unsigned)binary.load_address, (unsigned)binary.size);
        dlg->decode_ok = true;
    } else {
        snprintf(dlg->detected, sizeof(dlg->detected), "%s", error);
    }
    free(bytes);
}

void frontend_set_save_bin_path(frontend *ui, const char *path)
{
    if (ui == NULL || path == NULL) {
        return;
    }

    snprintf(ui->save_bin_dialog.path, sizeof(ui->save_bin_dialog.path), "%s", path);
}

void frontend_set_assembler_options(frontend *ui, const frontend_assembler_options *opts)
{
    frontend_assembler_state *s;
    char display_path[1024];

    if (ui == NULL || opts == NULL) {
        return;
    }

    s = &ui->assembler;
    if (opts->file[0] != '\0') {
        if (ui->config_dialog.initialized &&
            app_options_path_relative_to_ini(
                &ui->config_dialog.edited, opts->file, display_path, sizeof(display_path))) {
            snprintf(s->file_path, sizeof(s->file_path), "%s", display_path);
        } else {
            snprintf(s->file_path, sizeof(s->file_path), "%s", opts->file);
        }
    }
    snprintf(s->address_buf, sizeof(s->address_buf), "%s",
             opts->address[0] != '\0' ? opts->address : "8000");
    snprintf(s->run_address_buf, sizeof(s->run_address_buf), "%s",
             opts->run_address[0] != '\0' ? opts->run_address : "8000");
    s->use_address = opts->use_address;
    s->run_address_user_edited = (opts->run_address[0] != '\0');
    s->auto_run = opts->auto_run;
    s->mli_launch = opts->mli_launch;
    s->reset_first = opts->reset_first;
    s->rearm_oneshots = opts->rearm_oneshots;
    if (!s->auto_run) {
        s->mli_launch = false;
    }
    if (s->mli_launch) {
        s->reset_first = false;
    }
    s->initialized = true;
}

void frontend_get_assembler_options(frontend *ui, frontend_assembler_options *out)
{
    const frontend_assembler_state *s;

    if (ui == NULL || out == NULL) {
        return;
    }

    s = &ui->assembler;
    snprintf(out->file, sizeof(out->file), "%s", s->file_path);
    snprintf(out->address, sizeof(out->address), "%s", s->address_buf);
    snprintf(out->run_address, sizeof(out->run_address), "%s", s->run_address_buf);
    out->use_address = s->use_address;
    out->auto_run = s->auto_run;
    out->mli_launch = s->mli_launch;
    out->reset_first = s->reset_first;
    out->rearm_oneshots = s->rearm_oneshots;
}

void frontend_invalidate_disassembly_cache(frontend *ui)
{
    int i;

    if (ui == NULL) {
        return;
    }

    for (i = 0; i < 3; ++i) {
        memset(ui->disassembly.mem_cache[i].valid, 0,
               sizeof(ui->disassembly.mem_cache[i].valid));
    }
    ui->disassembly.request_pending = false;
}

void frontend_update_symbols(frontend *ui, const runtime_symbol_snapshot *snapshot)
{
    size_t i;

    if (ui == NULL || snapshot == NULL) {
        return;
    }

    if (ui->symbol_table == NULL) {
        ui->symbol_table = symbol_table_create();
    }
    if (ui->symbol_table == NULL) {
        return;
    }

    symbol_table_clear(ui->symbol_table);

    for (i = 0; i < snapshot->count; ++i) {
        symbol_table_add(
            ui->symbol_table,
            snapshot->entries[i].address,
            snapshot->entries[i].name,
            SYMBOL_SOURCE_ASSEMBLER,
            "assembler",
            true);
    }

    symbol_table_make_resolver(ui->symbol_table, &ui->symbols);
}

void frontend_destroy(frontend *ui)
{
    if (ui == NULL) {
        return;
    }

    forensics_view_clear_transcript(&ui->forensics);
    (void)forensics_view_close(&ui->forensics);

    if (ui->ctx != NULL) {
        nk_sdl_shutdown();
    }

    if (ui->display_texture != NULL) {
        SDL_DestroyTexture(ui->display_texture);
    }
    if (ui->crt_texture != NULL) {
        SDL_DestroyTexture(ui->crt_texture);
    }
    if (ui->led_green_texture != NULL) {
        SDL_DestroyTexture(ui->led_green_texture);
    }
    if (ui->led_red_texture != NULL) {
        SDL_DestroyTexture(ui->led_red_texture);
    }

    symbol_table_destroy(ui->symbol_table);
    frontend_config_dialog_reset(&ui->config_dialog);
    app_disk_slot_clear(&ui->disk_queue[0]);
    app_disk_slot_clear(&ui->disk_queue[1]);
    free(ui->crt_pixels);
    free(ui->display_pixels);
    free(ui);
}

void frontend_begin_input(frontend *ui)
{
    if (ui == NULL || ui->ctx == NULL) {
        return;
    }

    nk_input_begin(ui->ctx);
}

void frontend_handle_event(frontend *ui, SDL_Event *event)
{
    if (ui == NULL || ui->ctx == NULL || event == NULL) {
        return;
    }

    if (help_view_is_open(&ui->help) || forensics_view_is_open(&ui->forensics)) {
        nk_sdl_handle_event(event);
        return;
    }

    if (frontend_any_dialog_open(ui) && event->type == SDL_MOUSEBUTTONDOWN) {
        float x = (float)event->button.x;
        float y = (float)event->button.y;
        if (!frontend_click_in_any_dialog(ui, x, y)) {
            return;
        }
    }

    if (!frontend_any_dialog_open(ui) &&
        event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {
        float x = (float)event->button.x;
        float y = (float)event->button.y;

        if (frontend_point_in_rect(x, y, ui->layout.display)) {
            frontend_set_active_view(ui, FRONTEND_ACTIVE_VIEW_MACHINE);
        } else if (frontend_point_in_rect(x, y, ui->layout.disassembly)) {
            frontend_set_active_view(ui, FRONTEND_ACTIVE_VIEW_DISASSEMBLY);
        } else if (frontend_point_in_rect(x, y, ui->layout.memory)) {
            frontend_set_active_view(ui, FRONTEND_ACTIVE_VIEW_MEMORY);
        } else if (frontend_point_in_rect(x, y, ui->layout.misc)) {
            frontend_set_active_view(ui, FRONTEND_ACTIVE_VIEW_MISC);
        } else if (frontend_point_in_rect(x, y, ui->layout.registers)) {
            frontend_set_active_view(ui, FRONTEND_ACTIVE_VIEW_NONE);
        }
    }

    if (event->type == SDL_KEYDOWN &&
        event->key.repeat == 0 &&
        event->key.keysym.sym == SDLK_ESCAPE) {
        ui->cancel_register_edit_requested = true;
        if (ui->symbol_lookup.open) {
            ui->symbol_lookup.open = false;
        }
        if (ui->memory_search.open) {
            ui->memory_search.open = false;
        }
    }

    if (ui->load_bin_dialog.open && !ui->file_browser.open &&
        event->type == SDL_KEYDOWN &&
        event->key.repeat == 0 &&
        (event->key.keysym.sym == SDLK_RETURN ||
         event->key.keysym.sym == SDLK_KP_ENTER)) {
        if (frontend_input_has_option_modifier(&event->key)) {
            frontend_push_machine_file_browse_intent(
                ui, FRONTEND_DEBUGGER_INTENT_LOAD_BIN_BROWSE,
                ui->load_bin_dialog.kind, ui->load_bin_dialog.binary_format);
        } else {
            frontend_commit_load_bin_dialog(ui);
        }
        return;
    }

    if (ui->symbol_lookup.open && event->type == SDL_KEYDOWN && event->key.repeat == 0) {
        SDL_Keycode sk = event->key.keysym.sym;
        frontend_symbol_lookup_state *dlg = &ui->symbol_lookup;
        if (sk == SDLK_TAB) {
            dlg->table_has_kb_focus = !dlg->table_has_kb_focus;
            /* consume: don't pass to pending key queues */
            nk_sdl_handle_event(event);
            return;
        }
        if (dlg->table_has_kb_focus) {
            if (sk == SDLK_UP) {
                if (dlg->selected > 0) {
                    dlg->selected--;
                    dlg->scroll_to_selected = true;
                }
                nk_sdl_handle_event(event);
                return;
            }
            if (sk == SDLK_DOWN) {
                if (dlg->selected < dlg->filtered_count - 1) {
                    dlg->selected++;
                    dlg->scroll_to_selected = true;
                }
                nk_sdl_handle_event(event);
                return;
            }
            if (sk == SDLK_RETURN) {
                frontend_symbol_lookup_commit(ui);
                nk_sdl_handle_event(event);
                return;
            }
        }
    }

    if (event->type == SDL_KEYDOWN && !ui->machine_input_active && !frontend_any_dialog_open(ui)) {
        ui->pending_memory_key = event->key;
        ui->has_pending_memory_key = true;
        ui->pending_disassembly_key = event->key;
        ui->has_pending_disassembly_key = true;
    }

    nk_sdl_handle_event(event);
}

bool frontend_routes_keyboard_to_machine(const frontend *ui)
{
    return ui != NULL && ui->machine_input_active
        && !help_view_is_open(&ui->help)
        && !forensics_view_is_open(&ui->forensics)
        && !frontend_any_dialog_open(ui);
}

bool frontend_wants_text_input(const frontend *ui)
{
    const struct nk_window *win;

    if (ui == NULL || ui->ctx == NULL) {
        return false;
    }

    /* The file browser consumes typed characters for its type-ahead search even
       when no edit field is focused, so text input must stay enabled the whole
       time it is open. */
    if (ui->file_browser.open) {
        return true;
    }

    /* Nuklear keeps at most one active text editor at a time, spread across its
       window list. Walk every window and report whether any edit widget
       currently holds focus. The main loop uses this to keep SDL text input
       (and thus the macOS press-and-hold accent popup) enabled only while the
       user is actually typing into a UI field. */
    for (win = ui->ctx->begin; win != NULL; win = win->next) {
        if (win->edit.active) {
            return true;
        }
    }
    return false;
}

bool frontend_handle_view_cycle_key(frontend *ui, const SDL_KeyboardEvent *key)
{
    bool reverse;

    if (ui == NULL || key == NULL || key->type != SDL_KEYDOWN || key->repeat != 0) {
        return false;
    }
    if (help_view_is_open(&ui->help) || forensics_view_is_open(&ui->forensics) ||
        frontend_any_dialog_open(ui)) {
        return false;
    }
    if (key->keysym.sym != SDLK_TAB || (key->keysym.mod & KMOD_ALT) == 0) {
        return false;
    }

    reverse = (key->keysym.mod & KMOD_SHIFT) != 0;
    frontend_set_active_view(ui, frontend_next_active_view(ui->active_view, reverse));
    return true;
}

void frontend_end_input(frontend *ui)
{
    if (ui == NULL || ui->ctx == NULL) {
        return;
    }

    nk_input_end(ui->ctx);
}

bool frontend_inspector_preview(
    const frontend *ui,
    uint64_t *out_picture_id,
    bool *out_keep_current_on_missing)
{
    /* After release the runtime owns presentation.  Keeping the pending
       preview active here could paint pink over a just-reconstructed frame
       before the next frontend render observes the completed land. */
    if (ui == NULL || !ui->misc.inspector_thumb_down) {
        return false;
    }
    if (out_picture_id != NULL) {
        *out_picture_id = ui->misc.inspector_preview_picture_id;
    }
    if (out_keep_current_on_missing != NULL) {
        *out_keep_current_on_missing =
            ui->misc.inspector_preview_keep_current;
    }
    return true;
}

bool frontend_submit_argb_frame(
    frontend *ui,
    const uint32_t *pixels,
    uint32_t width,
    uint32_t height,
    uint64_t frame_number)
{
    size_t nbytes;
    SDL_Rect frame_rect;

    if (ui == NULL || pixels == NULL || ui->renderer == NULL) {
        return false;
    }

    /* Reject non-Apple geometry early — do not silently UV-crop a wrong size. */
    if (width != DISPLAY_FRAME_WIDTH || height != DISPLAY_FRAME_HEIGHT) {
        log_error(
            "frontend: unexpected Apple frame size %ux%u (expected %ux%u)",
            width,
            height,
            (unsigned)DISPLAY_FRAME_WIDTH,
            (unsigned)DISPLAY_FRAME_HEIGHT);
        return false;
    }

    nbytes = (size_t)width * (size_t)height * sizeof(uint32_t);
    if (ui->display_texture != NULL &&
        (ui->current_frame.width != width || ui->current_frame.height != height)) {
        SDL_DestroyTexture(ui->display_texture);
        ui->display_texture = NULL;
    }
    if (ui->display_texture == NULL) {
        ui->display_texture = SDL_CreateTexture(
            ui->renderer,
            SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_STREAMING,
            (int)width,
            (int)height);
        if (ui->display_texture == NULL) {
            return false;
        }
        SDL_SetTextureBlendMode(ui->display_texture, SDL_BLENDMODE_NONE);
        frontend_apply_display_filter(ui);
    }

    free(ui->display_pixels);
    ui->display_pixels = (uint32_t *)malloc(nbytes);
    if (ui->display_pixels == NULL) {
        return false;
    }
    memcpy(ui->display_pixels, pixels, nbytes);

    memset(&ui->current_frame, 0, sizeof(ui->current_frame));
    ui->current_frame.width = width;
    ui->current_frame.height = height;
    ui->current_frame.stride_bytes = width;
    ui->current_frame.pixel_format = DISPLAY_FRAME_PIXEL_FORMAT_ARGB8888;
    ui->current_frame.frame_number = frame_number;

    frame_rect.x = 0;
    frame_rect.y = 0;
    frame_rect.w = (int)width;
    frame_rect.h = (int)height;
    if (SDL_UpdateTexture(
            ui->display_texture,
            &frame_rect,
            ui->display_pixels,
            (int)(width * sizeof(uint32_t))) != 0) {
        return false;
    }
    ui->has_frame = true;
    ui->layout.display_aspect = frontend_display_aspect(ui);
    if (!frontend_update_crt_texture(ui)) {
        log_warn("frontend: CRT output update failed; using unprocessed frame");
    }
    return true;
}

static bool frontend_push_load_bin_execute_intent(
    frontend *ui,
    const char *path,
    uint16_t address,
    frontend_machine_file_kind kind,
    apple2_binary_format format,
    bool reset_first,
    bool run_after_load)
{
    size_t next;

    if (ui == NULL || path == NULL) {
        return false;
    }

    next = (ui->intent_write + 1u) % FRONTEND_DEBUGGER_INTENT_CAPACITY;
    if (next == ui->intent_read) {
        return false;
    }

    memset(&ui->intents[ui->intent_write], 0, sizeof(ui->intents[ui->intent_write]));
    ui->intents[ui->intent_write].type = FRONTEND_DEBUGGER_INTENT_LOAD_BIN_EXECUTE;
    snprintf(
        ui->intents[ui->intent_write].load_bin_path,
        sizeof(ui->intents[ui->intent_write].load_bin_path),
        "%s", path);
    ui->intents[ui->intent_write].load_bin_address = address;
    ui->intents[ui->intent_write].load_file_kind = (uint8_t)kind;
    ui->intents[ui->intent_write].load_bin_format = format;
    ui->intents[ui->intent_write].load_bin_reset_first = reset_first;
    ui->intents[ui->intent_write].load_bin_run_after_load = run_after_load;
    ui->intent_write = next;
    return true;
}

static bool frontend_load_bin_dialog_uses_raw_address(
    const frontend_load_bin_dialog_state *dlg)
{
    return dlg != NULL &&
        (dlg->kind == FRONTEND_MACHINE_FILE_AUTO ||
         dlg->kind == FRONTEND_MACHINE_FILE_BINARY) &&
        (dlg->binary_format == APPLE2_BINARY_FORMAT_AUTO ||
         dlg->binary_format == APPLE2_BINARY_FORMAT_RAW);
}

static bool frontend_commit_load_bin_dialog(frontend *ui)
{
    frontend_load_bin_dialog_state *dlg;
    uint16_t address = 0u;

    if (ui == NULL || !ui->load_bin_dialog.open) {
        return false;
    }

    dlg = &ui->load_bin_dialog;
    if (dlg->path[0] == '\0') {
        snprintf(dlg->error, sizeof(dlg->error), "No file selected");
        return false;
    }
    if (frontend_load_bin_dialog_uses_raw_address(dlg) &&
        !frontend_parse_hex16_text(dlg->address_buf, &address)) {
        snprintf(dlg->error, sizeof(dlg->error), "Invalid address (XXXX hex required)");
        return false;
    }

    if (!frontend_push_load_bin_execute_intent(
            ui, dlg->path, address,
            dlg->kind, dlg->binary_format, dlg->reset_first,
            dlg->run_after_load)) {
        return false;
    }
    dlg->open = false;
    return true;
}

static bool frontend_push_save_bin_execute_intent(
    frontend *ui,
    const char *path,
    uint16_t start,
    uint16_t end,
    frontend_machine_file_kind kind,
    apple2_binary_format format)
{
    size_t next;

    if (ui == NULL || path == NULL) {
        return false;
    }

    next = (ui->intent_write + 1u) % FRONTEND_DEBUGGER_INTENT_CAPACITY;
    if (next == ui->intent_read) {
        return false;
    }

    memset(&ui->intents[ui->intent_write], 0, sizeof(ui->intents[ui->intent_write]));
    ui->intents[ui->intent_write].type = FRONTEND_DEBUGGER_INTENT_SAVE_BIN_EXECUTE;
    snprintf(
        ui->intents[ui->intent_write].save_bin_path,
        sizeof(ui->intents[ui->intent_write].save_bin_path),
        "%s", path);
    ui->intents[ui->intent_write].save_bin_start = start;
    ui->intents[ui->intent_write].save_bin_end = end;
    ui->intents[ui->intent_write].save_file_kind = (uint8_t)kind;
    ui->intents[ui->intent_write].save_bin_format = format;
    ui->intent_write = next;
    return true;
}

/* ---- Symbol lookup implementation ---- */

static void frontend_symbol_lookup_basename(const char *src, char *out, int max)
{
    const char *base;
    const char *ext;
    int len;

    if (src == NULL || out == NULL || max <= 0) {
        if (out && max > 0) out[0] = '\0';
        return;
    }

    base = src;
    for (const char *p = src; *p; ++p) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }

    ext = NULL;
    for (const char *p = base; *p; ++p) {
        if (*p == '.') ext = p;
    }

    len = ext ? (int)(ext - base) : (int)strlen(base);
    if (len >= max) len = max - 1;
    memcpy(out, base, (size_t)len);
    out[len] = '\0';
}

static void frontend_symbol_lookup_scope_str(const symbol_info *info, char *out, int max)
{
    int len;

    if (out == NULL || max <= 0) return;
    if (info == NULL || info->scope_path_length == 0) {
        out[0] = '\0';
        return;
    }

    len = (int)info->scope_path_length;
    if (len >= max) len = max - 1;
    memcpy(out, info->name, (size_t)len);
    out[len] = '\0';
}

static const frontend_symbol_lookup_entry *g_sort_entries_ptr = NULL;
static frontend_symbol_lookup_sort_col     g_sort_col_val;
static bool                                g_sort_asc_val;

static int frontend_symbol_lookup_compare(const void *a, const void *b)
{
    int ia = *(const int *)a;
    int ib = *(const int *)b;
    const frontend_symbol_lookup_entry *ea = &g_sort_entries_ptr[ia];
    const frontend_symbol_lookup_entry *eb = &g_sort_entries_ptr[ib];
    int cmp = 0;

    switch (g_sort_col_val) {
        case SYMBOL_LOOKUP_SORT_ADDR:
            cmp = (ea->address < eb->address) ? -1 : (ea->address > eb->address) ? 1 : 0;
            break;
        case SYMBOL_LOOKUP_SORT_SCOPE:
            cmp = strcmp(ea->scope, eb->scope);
            if (cmp == 0) cmp = (ea->address < eb->address) ? -1 : (ea->address > eb->address) ? 1 : 0;
            break;
        case SYMBOL_LOOKUP_SORT_LABEL:
            cmp = strcmp(ea->label, eb->label);
            if (cmp == 0) cmp = (ea->address < eb->address) ? -1 : (ea->address > eb->address) ? 1 : 0;
            break;
        case SYMBOL_LOOKUP_SORT_SOURCE:
            cmp = strcmp(ea->source, eb->source);
            if (cmp == 0) cmp = (ea->address < eb->address) ? -1 : (ea->address > eb->address) ? 1 : 0;
            break;
    }
    return g_sort_asc_val ? cmp : -cmp;
}

static void frontend_symbol_lookup_refilter(frontend_symbol_lookup_state *dlg)
{
    int i;
    char row[64];
    char addr_hex[5];

    dlg->filtered_count = 0;

    for (i = 0; i < dlg->entry_count; ++i) {
        const frontend_symbol_lookup_entry *e = &dlg->entries[i];
        if (dlg->search[0] == '\0') {
            dlg->filtered[dlg->filtered_count++] = i;
            continue;
        }
        snprintf(addr_hex, sizeof(addr_hex), "%04X", e->address);
        snprintf(row, sizeof(row), "%s %s %s %s", addr_hex, e->scope, e->label, e->source);
        if (nk_strfilter(row, dlg->search)) {
            dlg->filtered[dlg->filtered_count++] = i;
        }
    }

    /* Apply current sort to filtered indices */
    if (dlg->filtered_count > 1) {
        g_sort_entries_ptr = dlg->entries;
        g_sort_col_val     = dlg->sort_col;
        g_sort_asc_val     = dlg->sort_asc;
        qsort(dlg->filtered, (size_t)dlg->filtered_count, sizeof(int), frontend_symbol_lookup_compare);
    }

    if (dlg->selected >= dlg->filtered_count) {
        dlg->selected = dlg->filtered_count > 0 ? 0 : -1;
    }
}

static void frontend_symbol_lookup_set_sort(
    frontend_symbol_lookup_state *dlg,
    frontend_symbol_lookup_sort_col col)
{
    if (dlg->sort_col == col) {
        dlg->sort_asc = !dlg->sort_asc;
    } else {
        dlg->sort_col = col;
        dlg->sort_asc = true;
    }

    if (dlg->filtered_count > 1) {
        g_sort_entries_ptr = dlg->entries;
        g_sort_col_val     = dlg->sort_col;
        g_sort_asc_val     = dlg->sort_asc;
        qsort(dlg->filtered, (size_t)dlg->filtered_count, sizeof(int), frontend_symbol_lookup_compare);
    }
    dlg->selected = dlg->filtered_count > 0 ? 0 : -1;
    dlg->scroll_to_selected = true;
}

/* ---- File browser dialog ---- */

/* Match name against one extension token (no leading dot). */
static bool frontend_file_browser_name_matches_one_ext(const char *name, const char *ext)
{
    size_t name_len, ext_len;

    if (name == NULL || ext == NULL || ext[0] == '\0') {
        return false;
    }
    name_len = strlen(name);
    ext_len = strlen(ext);
    if (name_len <= ext_len + 1u || name[name_len - ext_len - 1u] != '.') {
        return false;
    }
    return SDL_strcasecmp(name + (name_len - ext_len), ext) == 0;
}

/* `ext` may be a single extension ("dsk") or a comma-separated list
   ("nib,dsk,po,do,woz"). Empty / NULL never matches. */
static bool frontend_file_browser_name_matches_ext(const char *name, const char *ext)
{
    const char *cursor;
    char token[16];
    size_t n;

    if (name == NULL || ext == NULL || ext[0] == '\0') {
        return false;
    }

    cursor = ext;
    while (*cursor != '\0') {
        while (*cursor == ' ' || *cursor == '\t' || *cursor == ',') {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }
        n = 0u;
        while (*cursor != '\0' && *cursor != ',' && *cursor != ' ' && *cursor != '\t' &&
               n + 1u < sizeof(token)) {
            token[n++] = *cursor++;
        }
        token[n] = '\0';
        while (*cursor != '\0' && *cursor != ',') {
            cursor++;
        }
        if (n > 0u && frontend_file_browser_name_matches_one_ext(name, token)) {
            return true;
        }
    }
    return false;
}

static bool frontend_file_browser_canonicalize(char *out, size_t out_size, const char *path)
{
    char resolved[4096];

#if defined(_WIN32)
    if (_fullpath(resolved, path, sizeof(resolved)) != NULL) {
        snprintf(out, out_size, "%s", resolved);
        return true;
    }
#else
    if (realpath(path, resolved) != NULL) {
        snprintf(out, out_size, "%s", resolved);
        return true;
    }
#endif
    return false;
}

static void frontend_file_browser_reload(frontend_file_browser_state *dlg)
{
    int i;

    dlg->filtered_count = 0;
    dlg->selected = -1;
    dlg->error[0] = '\0';
    dlg->typeahead_len = 0;        /* fresh directory, fresh search */
    dlg->scroll_to_selected = false;
    /* The list_view keeps its scroll offset (keyed by id) across directory
       changes, so a new listing would otherwise inherit the previous scroll and
       leave the new selection off-screen. Ask the next frame to reveal it. */
    dlg->scroll_ensure_visible = true;

    if (!platform_fs_list_dir(dlg->current_dir, &dlg->listing)) {
        dlg->listing.count = 0;
        snprintf(dlg->error, sizeof(dlg->error), "Cannot open directory");
        return;
    }

    for (i = 0; i < dlg->listing.count; i++) {
        const platform_fs_entry *entry = &dlg->listing.entries[i];
        if (entry->is_dir || dlg->filter_extension[0] == '\0' ||
                frontend_file_browser_name_matches_ext(entry->name, dlg->filter_extension)) {
            dlg->filtered[dlg->filtered_count++] = i;
        }
    }

    /* Give the list an initial cursor so type-ahead and Enter/Open have a
       target, matching the symbol-lookup dialog's behavior. */
    dlg->selected = dlg->filtered_count > 0 ? 0 : -1;
}

/* Maps a browse purpose to the slot whose remembered folder it should use. */
static int frontend_file_browser_slot_for(const frontend *ui, frontend_debugger_intent_type purpose)
{
    switch (purpose) {
        case FRONTEND_DEBUGGER_INTENT_ASSEMBLE_BROWSE:
            return FRONTEND_BROWSE_SLOT_ASSEMBLER;
        case FRONTEND_DEBUGGER_INTENT_DISK_MOUNT_DIALOG:
        case FRONTEND_DEBUGGER_INTENT_DISK_ADD_DIALOG:
            return FRONTEND_BROWSE_SLOT_FLOPPY;
        case FRONTEND_DEBUGGER_INTENT_STATE_SAVE_AS_DIALOG:
        case FRONTEND_DEBUGGER_INTENT_STATE_LOAD_DIALOG:
            return FRONTEND_BROWSE_SLOT_SNAPSHOT;
        case FRONTEND_DEBUGGER_INTENT_LOAD_BIN_BROWSE:
            if (ui->load_bin_dialog.kind == FRONTEND_MACHINE_FILE_SNAPSHOT)
                return FRONTEND_BROWSE_SLOT_SNAPSHOT;
            if (ui->load_bin_dialog.kind == FRONTEND_MACHINE_FILE_APPLESOFT_TEXT)
                return FRONTEND_BROWSE_SLOT_BASIC;
            return FRONTEND_BROWSE_SLOT_BINARY;
        case FRONTEND_DEBUGGER_INTENT_SAVE_BIN_BROWSE:
            if (ui->save_bin_dialog.kind == FRONTEND_MACHINE_FILE_SNAPSHOT)
                return FRONTEND_BROWSE_SLOT_SNAPSHOT;
            if (ui->save_bin_dialog.kind == FRONTEND_MACHINE_FILE_APPLESOFT_TEXT)
                return FRONTEND_BROWSE_SLOT_BASIC;
            return FRONTEND_BROWSE_SLOT_BINARY;
        case FRONTEND_DEBUGGER_INTENT_CONFIG_PICK_PATH_DIALOG:
            return ui->config_dialog.pending_browse_slot;
        default:
            return -1;
    }
}

/* Browse folders are stored and displayed relative to the INI file's directory
   (the applied config carries the current INI path), so the INI stays portable
   and the Paths tab shows short paths. Conversions fall back to the raw string
   when no INI context is available. */
static void frontend_browse_to_relative(const frontend *ui, const char *abs, char *out, size_t out_size)
{
    if (abs == NULL || abs[0] == '\0' ||
            !app_options_path_relative_to_ini(&ui->config_dialog.original, abs, out, out_size)) {
        snprintf(out, out_size, "%s", abs != NULL ? abs : "");
    }
}

static void frontend_browse_to_absolute(const frontend *ui, const char *rel, char *out, size_t out_size)
{
    if (rel == NULL || rel[0] == '\0' ||
            !app_options_path_absolute_from_ini(&ui->config_dialog.original, rel, out, out_size)) {
        snprintf(out, out_size, "%s", rel != NULL ? rel : "");
    }
}

/* Records dlg's current directory (relative to the INI) as the remembered folder
   for its slot. Called when a selection commits so the next open reopens there. */
static void frontend_file_browser_remember_dir(frontend *ui, const frontend_file_browser_state *dlg)
{
    if (ui != NULL && dlg->browse_slot >= 0 && dlg->browse_slot < FRONTEND_BROWSE_SLOT_COUNT) {
        frontend_browse_to_relative(ui, dlg->current_dir,
            ui->browse_dirs[dlg->browse_slot], sizeof(ui->browse_dirs[dlg->browse_slot]));
    }
}

void frontend_set_browse_dir(frontend *ui, frontend_browse_slot slot, const char *dir)
{
    if (ui == NULL || slot < 0 || slot >= FRONTEND_BROWSE_SLOT_COUNT) {
        return;
    }
    snprintf(ui->browse_dirs[slot], sizeof(ui->browse_dirs[slot]), "%s", dir != NULL ? dir : "");
}

const char *frontend_get_browse_dir(const frontend *ui, frontend_browse_slot slot)
{
    if (ui == NULL || slot < 0 || slot >= FRONTEND_BROWSE_SLOT_COUNT) {
        return "";
    }
    return ui->browse_dirs[slot];
}

/* Stores a folder chosen via a Paths-tab [...] button into its pending slot,
   converting to the INI-relative form used for display and persistence. */
void frontend_set_picked_browse_dir(frontend *ui, const char *path)
{
    int slot;
    if (ui == NULL || path == NULL) {
        return;
    }
    slot = ui->config_dialog.pending_browse_slot;
    if (slot >= 0 && slot < FRONTEND_BROWSE_SLOT_COUNT) {
        frontend_browse_to_relative(ui, path, ui->browse_dirs[slot], sizeof(ui->browse_dirs[slot]));
    }
}

static void frontend_open_file_browser_with_slot(
    frontend *ui,
    frontend_debugger_intent_type purpose,
    const char *title,
    bool save_mode,
    const char *filter_extension,
    const char *default_extension,
    uint8_t disk_device,
    int browse_slot)
{
    frontend_file_browser_state *dlg;
    const char *remembered;

    if (ui == NULL) {
        return;
    }

    dlg = &ui->file_browser;
    memset(dlg, 0, sizeof(*dlg));
    dlg->purpose = purpose;
    dlg->browse_slot = browse_slot;
    snprintf(dlg->title, sizeof(dlg->title), "%s", title != NULL ? title : "");
    dlg->save_mode = save_mode;
    snprintf(dlg->filter_extension, sizeof(dlg->filter_extension), "%s",
        filter_extension != NULL ? filter_extension : "");
    snprintf(dlg->default_extension, sizeof(dlg->default_extension), "%s",
        default_extension != NULL ? default_extension : "");
    dlg->disk_device = disk_device;
    dlg->selected = -1;
    dlg->pick_dir = (purpose == FRONTEND_DEBUGGER_INTENT_CONFIG_PICK_PATH_DIALOG);
    dlg->allow_folder_mount = false;

    if (save_mode && dlg->default_extension[0] != '\0') {
        snprintf(dlg->filename, sizeof(dlg->filename), "untitled.%s", dlg->default_extension);
    }

    /* The assembler browser follows the file currently shown in its edit box.
       This makes repeated browsing reopen beside that source even when it differs
       from the configured default assembler folder. */
    if (purpose == FRONTEND_DEBUGGER_INTENT_ASSEMBLE_BROWSE &&
        ui->assembler.file_path[0] != '\0') {
        char source_path[1024];
        char *slash;

        frontend_browse_to_absolute(
            ui, ui->assembler.file_path, source_path, sizeof(source_path));
        slash = strrchr(source_path, '/');
#if defined(_WIN32)
        {
            char *backslash = strrchr(source_path, '\\');
            if (backslash != NULL && (slash == NULL || backslash > slash)) {
                slash = backslash;
            }
        }
#endif
        if (platform_fs_is_dir(source_path)) {
            snprintf(dlg->current_dir, sizeof(dlg->current_dir), "%s", source_path);
        } else if (slash != NULL) {
            if (slash == source_path) {
                slash[1] = '\0';
            } else {
                *slash = '\0';
            }
            if (platform_fs_is_dir(source_path)) {
                snprintf(dlg->current_dir, sizeof(dlg->current_dir), "%s", source_path);
            }
        }
    }

    /* Otherwise prefer this slot's remembered folder (stored relative to the INI,
       so resolve it back to absolute); fall back to the shell cwd if it is unset
       or no longer a directory. */
    remembered = dlg->browse_slot >= 0 ? ui->browse_dirs[dlg->browse_slot] : "";
    if (dlg->current_dir[0] == '\0' && remembered[0] != '\0') {
        char resolved[1024];
        frontend_browse_to_absolute(ui, remembered, resolved, sizeof(resolved));
        if (resolved[0] != '\0' && platform_fs_is_dir(resolved)) {
            snprintf(dlg->current_dir, sizeof(dlg->current_dir), "%s", resolved);
        } else {
            remembered = "";
        }
    }
    if (dlg->current_dir[0] == '\0' &&
        !platform_fs_get_cwd(dlg->current_dir, sizeof(dlg->current_dir))) {
        dlg->current_dir[0] = '\0';
    }
    frontend_file_browser_reload(dlg);
    dlg->open = true;
}

void frontend_open_file_browser(
    frontend *ui,
    frontend_debugger_intent_type purpose,
    const char *title,
    bool save_mode,
    const char *filter_extension,
    const char *default_extension,
    uint8_t disk_device)
{
    if (ui == NULL) {
        return;
    }
    frontend_open_file_browser_with_slot(
        ui,
        purpose,
        title,
        save_mode,
        filter_extension,
        default_extension,
        disk_device,
        frontend_file_browser_slot_for(ui, purpose));
}

void frontend_open_media_file_browser(
    frontend *ui,
    const char *title,
    const char *filter_extension,
    uint8_t slot,
    uint8_t device,
    runtime_slot_card_type card_type)
{
    if (ui == NULL) {
        return;
    }
    frontend_open_file_browser_with_slot(
        ui,
        FRONTEND_DEBUGGER_INTENT_MEDIA_INSERT_DIALOG,
        title,
        false,
        filter_extension,
        NULL,
        device,
        card_type == RUNTIME_SLOT_CARD_DISKII ? FRONTEND_BROWSE_SLOT_FLOPPY :
        card_type == RUNTIME_SLOT_CARD_SMARTPORT ? FRONTEND_BROWSE_SLOT_SMARTPORT : -1);
    ui->file_browser.disk_slot = slot;
    ui->file_browser.disk_card_type = card_type;
    /* Directory path → HostFS; file path → image. Disk II stays file-only. */
    ui->file_browser.allow_folder_mount = (card_type == RUNTIME_SLOT_CARD_SMARTPORT);
}

/* Commit the directory currently shown (Paths pick_dir, or SmartPort HostFS). */
static void frontend_file_browser_commit_folder(
    frontend *ui, frontend_file_browser_state *dlg)
{
    if (dlg->current_dir[0] == '\0') {
        snprintf(dlg->error, sizeof(dlg->error), "No folder to select");
        return;
    }
    frontend_file_browser_remember_dir(ui, dlg);
    frontend_push_file_browser_result_intent(
        ui, dlg->purpose, dlg->current_dir, dlg->disk_device);
    dlg->open = false;
}

/* Navigates into a directory entry, or (for a file) either fills the save-mode
   filename field or commits an open-mode selection. Shared by double-click and
   the footer Open/Save button so both paths behave identically. */
static void frontend_file_browser_activate(frontend *ui, frontend_file_browser_state *dlg, int filtered_index)
{
    const platform_fs_entry *entry;
    char joined[1024];

    if (filtered_index < 0 || filtered_index >= dlg->filtered_count) {
        snprintf(dlg->error, sizeof(dlg->error), "No file selected");
        return;
    }

    entry = &dlg->listing.entries[dlg->filtered[filtered_index]];
    platform_fs_path_join(joined, sizeof(joined), dlg->current_dir, entry->name);

    if (entry->is_dir) {
        if (!frontend_file_browser_canonicalize(dlg->current_dir, sizeof(dlg->current_dir), joined)) {
            snprintf(dlg->current_dir, sizeof(dlg->current_dir), "%s", joined);
        }
        frontend_file_browser_reload(dlg);
        return;
    }

    if (dlg->save_mode) {
        snprintf(dlg->filename, sizeof(dlg->filename), "%s", entry->name);
        dlg->confirm_overwrite = false;
        dlg->error[0] = '\0';
        return;
    }

    frontend_file_browser_remember_dir(ui, dlg);
    frontend_push_file_browser_result_intent(ui, dlg->purpose, joined, dlg->disk_device);
    dlg->open = false;
}

static void frontend_file_browser_commit_save(frontend *ui, frontend_file_browser_state *dlg)
{
    char target[1024];
    bool has_ext;

    if (dlg->filename[0] == '\0') {
        snprintf(dlg->error, sizeof(dlg->error), "No filename entered");
        dlg->confirm_overwrite = false;
        return;
    }

    has_ext = dlg->default_extension[0] == '\0' ||
        frontend_file_browser_name_matches_ext(dlg->filename, dlg->default_extension);
    if (has_ext) {
        platform_fs_path_join(target, sizeof(target), dlg->current_dir, dlg->filename);
    } else {
        char named[PLATFORM_FS_NAME_MAX + 8];
        snprintf(named, sizeof(named), "%s.%s", dlg->filename, dlg->default_extension);
        platform_fs_path_join(target, sizeof(target), dlg->current_dir, named);
    }

    if (frontend_file_exists(target) && !dlg->confirm_overwrite) {
        dlg->confirm_overwrite = true;
        snprintf(dlg->error, sizeof(dlg->error), "File exists - click Save again to overwrite");
        return;
    }

    frontend_file_browser_remember_dir(ui, dlg);
    frontend_push_file_browser_result_intent(ui, dlg->purpose, target, dlg->disk_device);
    dlg->open = false;
}

/* Sliding-window timeout for the file-browser type-ahead search. Each accepted
   keystroke resets the window; once it lapses, the next keystroke starts a new
   search prefix. */
#define FRONTEND_FILE_BROWSER_TYPEAHEAD_MS 500u

/* Feeds this frame's typed characters into the type-ahead buffer and moves the
   selection to the first entry (scanning from the top) whose name starts with
   the accumulated prefix, case-insensitively. No match leaves the selection
   untouched. Called only when no text field has focus. */
static void frontend_file_browser_typeahead(
    frontend_file_browser_state *dlg, const char *text, int text_len, uint64_t now_ms)
{
    int i;

    if (dlg == NULL || text == NULL || text_len <= 0) {
        return;
    }

    /* Sliding window: drop the old prefix if the user paused too long. */
    if (dlg->typeahead_len > 0 &&
            (now_ms - dlg->typeahead_last_ms) > FRONTEND_FILE_BROWSER_TYPEAHEAD_MS) {
        dlg->typeahead_len = 0;
    }
    dlg->typeahead_last_ms = now_ms;

    /* Append the raw bytes the user typed; we do not filter what is accepted. */
    for (i = 0; i < text_len; i++) {
        if (dlg->typeahead_len < (int)sizeof(dlg->typeahead) - 1) {
            dlg->typeahead[dlg->typeahead_len++] = text[i];
        }
    }
    dlg->typeahead[dlg->typeahead_len] = '\0';
    if (dlg->typeahead_len == 0) {
        return;
    }

    /* First forward (prefix) match from the top of the current list. */
    for (i = 0; i < dlg->filtered_count; i++) {
        const platform_fs_entry *entry = &dlg->listing.entries[dlg->filtered[i]];
        if (SDL_strncasecmp(entry->name, dlg->typeahead, (size_t)dlg->typeahead_len) == 0) {
            dlg->selected = i;
            dlg->scroll_to_selected = true;
            break;
        }
    }
}

static void frontend_draw_file_browser(frontend *ui, int width, int height)
{
    frontend_file_browser_state *dlg;
    struct nk_context *ctx;
    struct nk_rect bounds;
    struct nk_list_view lv;
    nk_flags path_result;
    bool edit_active_this_frame;
    float dw, dh, list_h;
    int i;
    static const int ROW_H = 18;
    static const float SIZE_W = 84.0f;
    static const float TYPE_W = 56.0f;

    if (ui == NULL || !ui->file_browser.open || ui->ctx == NULL) {
        return;
    }

    ctx = ui->ctx;
    dlg = &ui->file_browser;

    dw = (float)width * 0.62f;
    if (dw < 460.0f) dw = 460.0f;
    if (dw > (float)width - 16.0f && width > 0) dw = (float)width - 16.0f;
    dh = (float)height * 0.66f;
    if (dh < 360.0f) dh = 360.0f;
    if (dh > (float)height - 16.0f && height > 0) dh = (float)height - 16.0f;

    bounds = nk_rect(((float)width - dw) * 0.5f, ((float)height - dh) * 0.5f, dw, dh);

    if (nk_begin(ctx, "File Browser", bounds,
            NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_CLOSABLE
            | NK_WINDOW_NO_SCROLLBAR)) {

        if (nk_window_is_closed(ctx, "File Browser")) {
            dlg->open = false;
            nk_end(ctx);
            return;
        }

        edit_active_this_frame = false;

        nk_layout_row_dynamic(ctx, 20.0f, 1);
        nk_label(ctx, dlg->title, NK_TEXT_LEFT);

        nk_layout_row_begin(ctx, NK_DYNAMIC, 24.0f, 2);
        nk_layout_row_push(ctx, 0.14f);
        nk_label(ctx, "Path", NK_TEXT_LEFT);
        nk_layout_row_push(ctx, 0.86f);
        path_result = frontend_edit_replace(
            ctx, (nk_flags)NK_EDIT_FIELD | NK_EDIT_SELECTABLE | NK_EDIT_CLIPBOARD,
            dlg->current_dir, (int)sizeof(dlg->current_dir), nk_filter_default);
        nk_layout_row_end(ctx);
        if (path_result & NK_EDIT_ACTIVE) {
            edit_active_this_frame = true;
        }
        if (path_result & NK_EDIT_COMMITED) {
            if (!platform_fs_is_dir(dlg->current_dir)) {
                snprintf(dlg->error, sizeof(dlg->error), "Not a directory");
            } else {
                frontend_file_browser_reload(dlg);
            }
        }

        /* Size the list so every row fits inside the window: no window-level
         * scrollbar should ever appear. Compute from the real content region and
         * the active style metrics rather than guessed constants. Each Nuklear row
         * consumes its height plus one spacing.y, and the panel reserves padding.y
         * at the top and bottom. The list_view keeps its own scrollbar for long
         * listings. */
        {
            struct nk_rect content = nk_window_get_content_region(ctx);
            float pad_y = ctx->style.window.padding.y;
            float sp_y  = ctx->style.window.spacing.y;
            int rows = dlg->save_mode ? 6 : 5; /* title,path,list,[filename],error,buttons */
            float other_h = 20.0f  /* title label */
                          + 24.0f  /* path row */
                          + (dlg->save_mode ? 24.0f : 0.0f) /* filename row */
                          + 18.0f  /* error/spacer row (reserve max) */
                          + 24.0f; /* buttons row */
            list_h = content.h - other_h - (float)rows * sp_y - 2.0f * pad_y;
        }
        if (list_h < 60.0f) list_h = 60.0f;

        /* When the selection moves, force the list scroll so the cursor is
           on-screen. Two strategies: a type-ahead jump anchors the row about a
           quarter of the way down; keyboard navigation (line/page/Home/End)
           scrolls the minimum needed to keep it visible so the view does not jump
           around. We work in whole rows: nk_list_view draws its rows from the top
           of the viewport, so the scroll offset must land on a row boundary and
           the first row index (begin) must not exceed rows - full_rows, otherwise
           the final row is only partially drawn and gets clipped. Both writes go
           to the same persistent offset nk_list_view reads by its id, so they take
           effect on this frame. Nuklear augments the row height with one spacing.y,
           so mirror that here; the viewport height was measured last frame. */
        if ((dlg->scroll_to_selected || dlg->scroll_ensure_visible) && dlg->selected >= 0) {
            int row_px    = ROW_H + (int)ctx->style.window.spacing.y;
            int rows      = dlg->filtered_count > 0 ? dlg->filtered_count : 1;
            float vis_h   = dlg->list_visible_h > 0.0f ? dlg->list_visible_h : list_h;
            int full_rows = (int)(vis_h / (float)row_px); /* rows that fit fully */
            int max_begin;
            int begin;
            if (full_rows < 1) full_rows = 1;
            max_begin = rows - full_rows;
            if (max_begin < 0) max_begin = 0;
            if (dlg->scroll_to_selected) {
                begin = dlg->selected - full_rows / 4;    /* anchor ~1/4 down */
            } else {
                nk_uint cur = 0;
                int cur_begin;
                nk_group_get_scroll(ctx, "file_browser_rows", NULL, &cur);
                cur_begin = (int)cur / row_px;
                if (dlg->selected < cur_begin) {
                    begin = dlg->selected;                /* reveal at the top */
                } else if (dlg->selected > cur_begin + full_rows - 1) {
                    begin = dlg->selected - full_rows + 1; /* reveal at the bottom */
                } else {
                    begin = cur_begin;                    /* already fully visible */
                }
            }
            if (begin < 0) begin = 0;
            if (begin > max_begin) begin = max_begin;
            nk_group_set_scroll(ctx, "file_browser_rows", 0, (nk_uint)(begin * row_px));
            dlg->scroll_to_selected = false;
            dlg->scroll_ensure_visible = false;
        }

        nk_layout_row_dynamic(ctx, list_h, 1);

        if (nk_list_view_begin(ctx, &lv, "file_browser_rows", 0, ROW_H, dlg->filtered_count)) {
            struct nk_style_selectable saved_sel = ctx->style.selectable;
            bool activated = false;

            /* Record the group's scroll viewport height, exactly as the group's
               own scrollbar measures it (nk_panel_end uses layout->bounds.h as the
               track height and layout->at_y - bounds.y as the content span). Using
               this same value below keeps our programmatic scroll clamp in lockstep
               with the scrollbar, so End reveals the final row fully instead of
               clamping short and clipping it. */
            dlg->list_visible_h = ctx->current->layout->bounds.h;

            for (i = lv.begin; i < lv.end; ++i) {
                const platform_fs_entry *entry;
                struct nk_rect row_bounds;
                bool sel;
                bool clicked = false;
                char size_buf[24];

                if (i < 0 || i >= dlg->filtered_count) continue;
                entry = &dlg->listing.entries[dlg->filtered[i]];
                sel = (i == dlg->selected);

                if (sel) {
                    ctx->style.selectable.normal  = nk_style_item_color(nk_rgb(21, 91, 116));
                    ctx->style.selectable.hover   = nk_style_item_color(nk_rgb(21, 91, 116));
                    ctx->style.selectable.pressed = nk_style_item_color(nk_rgb(21, 91, 116));
                    ctx->style.selectable.text_normal  = nk_rgb(226, 246, 255);
                    ctx->style.selectable.text_hover   = nk_rgb(226, 246, 255);
                    ctx->style.selectable.text_pressed = nk_rgb(226, 246, 255);
                } else {
                    ctx->style.selectable = saved_sel;
                }

                nk_layout_row_template_begin(ctx, (float)ROW_H);
                nk_layout_row_template_push_dynamic(ctx);
                nk_layout_row_template_push_static(ctx, SIZE_W);
                nk_layout_row_template_push_static(ctx, TYPE_W);
                nk_layout_row_template_end(ctx);

                row_bounds = nk_widget_bounds(ctx);
                {
                    bool s = sel;
                    if (nk_selectable_label(ctx, entry->name, NK_TEXT_LEFT, &s)) clicked = true;
                }
                if (entry->is_dir) {
                    size_buf[0] = '\0';
                } else {
                    snprintf(size_buf, sizeof(size_buf), "%llu", (unsigned long long)entry->size);
                }
                nk_label(ctx, size_buf, NK_TEXT_RIGHT);
                nk_label(ctx, entry->is_dir ? "Dir" : "File", NK_TEXT_LEFT);

                if (clicked) {
                    dlg->selected = i;
                    dlg->error[0] = '\0';
                    if (nk_input_is_mouse_click_down_in_rect(&ctx->input, NK_BUTTON_DOUBLE, row_bounds, nk_true)) {
                        frontend_file_browser_activate(ui, dlg, i);
                        activated = true;
                    } else if (dlg->save_mode && !entry->is_dir) {
                        snprintf(dlg->filename, sizeof(dlg->filename), "%s", entry->name);
                        dlg->confirm_overwrite = false;
                    }
                }
                if (activated) {
                    break;
                }
            }

            ctx->style.selectable = saved_sel;
            nk_list_view_end(&lv);
            if (activated && !dlg->open) {
                nk_end(ctx);
                return;
            }
        }

        if (dlg->save_mode) {
            nk_flags name_result;
            char prev_filename[PLATFORM_FS_NAME_MAX + 8];

            memcpy(prev_filename, dlg->filename, sizeof(dlg->filename));
            nk_layout_row_begin(ctx, NK_DYNAMIC, 24.0f, 2);
            nk_layout_row_push(ctx, 0.14f);
            nk_label(ctx, "Filename", NK_TEXT_LEFT);
            nk_layout_row_push(ctx, 0.86f);
            name_result = frontend_edit_replace(
                ctx, (nk_flags)NK_EDIT_FIELD | NK_EDIT_SELECTABLE | NK_EDIT_CLIPBOARD,
                dlg->filename, (int)sizeof(dlg->filename), nk_filter_default);
            nk_layout_row_end(ctx);
            if (name_result & NK_EDIT_ACTIVE) {
                edit_active_this_frame = true;
            }
            if (memcmp(prev_filename, dlg->filename, sizeof(dlg->filename)) != 0) {
                dlg->confirm_overwrite = false;
                dlg->error[0] = '\0';
            }
            if (name_result & NK_EDIT_COMMITED) {
                frontend_file_browser_commit_save(ui, dlg);
                if (!dlg->open) {
                    nk_end(ctx);
                    return;
                }
            }
        }

        if (dlg->error[0] != '\0') {
            nk_layout_row_dynamic(ctx, 18.0f, 1);
            nk_label_colored(ctx, dlg->error, NK_TEXT_LEFT, nk_rgb(255, 128, 118));
        } else {
            nk_layout_row_dynamic(ctx, 8.0f, 1);
            nk_spacing(ctx, 1);
        }

        /* Paths: Cancel | Use This Folder.
           SmartPort Insert: Cancel | Use This Folder | Open (file or HostFS).
           Everything else: Cancel | Open/Save. */
        if (dlg->allow_folder_mount && !dlg->pick_dir && !dlg->save_mode) {
            nk_layout_row_dynamic(ctx, 24.0f, 3);
            if (nk_button_label(ctx, "Cancel")) {
                dlg->open = false;
            }
            if (nk_button_label(ctx, "Use This Folder")) {
                frontend_file_browser_commit_folder(ui, dlg);
            }
            if (nk_button_label(ctx, "Open")) {
                frontend_file_browser_activate(ui, dlg, dlg->selected);
            }
        } else {
            nk_layout_row_dynamic(ctx, 24.0f, 2);
            if (nk_button_label(ctx, "Cancel")) {
                dlg->open = false;
            }
            if (nk_button_label(ctx, dlg->pick_dir ? "Use This Folder" :
                    (dlg->save_mode ? "Save" : "Open"))) {
                if (dlg->pick_dir) {
                    frontend_file_browser_commit_folder(ui, dlg);
                } else if (dlg->save_mode) {
                    frontend_file_browser_commit_save(ui, dlg);
                } else {
                    frontend_file_browser_activate(ui, dlg, dlg->selected);
                }
            }
        }

        /* Keyboard handling for the list itself, only while no text field has
           focus (otherwise the keys belong to that field). Type-ahead moves the
           selection; Enter acts as the Open/Save button on it. Processed here,
           after every widget, so the focus state for this frame is final. */
        if (dlg->open && !edit_active_this_frame) {
            const struct nk_input *in = &ctx->input;
            frontend_file_browser_typeahead(
                dlg, in->keyboard.text, in->keyboard.text_len, SDL_GetTicks64());

            /* Cursor navigation: Up/Down by a line, PageUp/PageDown by a page,
               Home/End to the ends. macOS keyboards have no Home/End keys, so
               Cmd+Up / Cmd+Down stand in for them. Any move resets the
               type-ahead prefix and keeps the selection on-screen. */
            if (dlg->filtered_count > 0) {
                int row_px = ROW_H + (int)ctx->style.window.spacing.y;
                float vis_h = dlg->list_visible_h > 0.0f ? dlg->list_visible_h : list_h;
                int visible_rows = (int)vis_h / row_px;
                int page, old_sel = dlg->selected;
                int sel = dlg->selected < 0 ? 0 : dlg->selected;
                bool to_top = false, to_bottom = false;
                if (visible_rows < 1) visible_rows = 1;
                page = visible_rows > 1 ? visible_rows - 1 : 1;
#if defined(__APPLE__)
                {
                    SDL_Keymod mods = SDL_GetModState();
                    if (mods & KMOD_GUI) {
                        if (nk_input_is_key_pressed(in, NK_KEY_UP))   to_top = true;
                        if (nk_input_is_key_pressed(in, NK_KEY_DOWN)) to_bottom = true;
                    }
                }
#endif
                if (nk_input_is_key_pressed(in, NK_KEY_SCROLL_START)) to_top = true;
                if (nk_input_is_key_pressed(in, NK_KEY_SCROLL_END))   to_bottom = true;

                if (to_top) {
                    sel = 0;
                } else if (to_bottom) {
                    sel = dlg->filtered_count - 1;
                } else {
                    if (nk_input_is_key_pressed(in, NK_KEY_UP))          sel -= 1;
                    if (nk_input_is_key_pressed(in, NK_KEY_DOWN))        sel += 1;
                    if (nk_input_is_key_pressed(in, NK_KEY_SCROLL_UP))   sel -= page;
                    if (nk_input_is_key_pressed(in, NK_KEY_SCROLL_DOWN)) sel += page;
                }
                if (sel < 0) sel = 0;
                if (sel >= dlg->filtered_count) sel = dlg->filtered_count - 1;
                if (sel != old_sel) {
                    dlg->selected = sel;
                    dlg->scroll_ensure_visible = true;
                    dlg->typeahead_len = 0;
                }
            }

            if (nk_input_is_key_pressed(in, NK_KEY_ENTER)) {
                const platform_fs_entry *sel_entry = NULL;
                if (dlg->selected >= 0 && dlg->selected < dlg->filtered_count) {
                    sel_entry = &dlg->listing.entries[dlg->filtered[dlg->selected]];
                }
                /* A highlighted folder is always entered (like a double-click).
                   Otherwise Enter mirrors the primary footer action: Use This
                   Folder in Paths mode, Save in save mode, Open otherwise.
                   SmartPort's Use This Folder is button-only so Enter never
                   mounts a parent while browsing into a HostFS tree. */
                if (sel_entry != NULL && sel_entry->is_dir) {
                    frontend_file_browser_activate(ui, dlg, dlg->selected);
                } else if (dlg->pick_dir) {
                    frontend_file_browser_commit_folder(ui, dlg);
                } else if (dlg->save_mode) {
                    frontend_file_browser_commit_save(ui, dlg);
                } else {
                    frontend_file_browser_activate(ui, dlg, dlg->selected);
                }
            }
        }

    } else if (nk_window_is_closed(ctx, "File Browser")) {
        dlg->open = false;
    }
    nk_end(ctx);
}

static void frontend_open_symbol_lookup(frontend *ui, bool from_memory)
{
    frontend_symbol_lookup_state *dlg;
    size_t i, count;
    symbol_info info;

    if (ui == NULL) return;

    dlg = &ui->symbol_lookup;
    memset(dlg, 0, sizeof(*dlg));
    dlg->sort_col = SYMBOL_LOOKUP_SORT_ADDR;
    dlg->sort_asc = true;
    dlg->selected = -1;
    dlg->from_memory = from_memory;

    if (ui->symbol_table != NULL) {
        count = symbol_table_count(ui->symbol_table);
        if (count > SYMBOL_LOOKUP_ENTRY_MAX) count = SYMBOL_LOOKUP_ENTRY_MAX;
        for (i = 0; i < count; ++i) {
            frontend_symbol_lookup_entry *e = &dlg->entries[dlg->entry_count];
            if (symbol_table_get(ui->symbol_table, i, &info) != SYMBOL_OK) continue;
            e->address = info.address;
            frontend_symbol_lookup_scope_str(&info, e->scope, sizeof(e->scope));
            snprintf(e->label, sizeof(e->label), "%.*s",
                SYMBOL_LOOKUP_COL_MAX, info.display_name);
            frontend_symbol_lookup_basename(info.source_name, e->source, sizeof(e->source));
            dlg->entry_count++;
        }
    }

    frontend_symbol_lookup_refilter(dlg);
    dlg->selected = dlg->filtered_count > 0 ? 0 : -1;
    dlg->just_opened = true;
    dlg->open = true;
}

static void frontend_symbol_lookup_commit(frontend *ui)
{
    frontend_symbol_lookup_state *dlg;
    const frontend_symbol_lookup_entry *e;
    uint16_t addr;

    if (ui == NULL) return;
    dlg = &ui->symbol_lookup;
    if (dlg->selected < 0 || dlg->selected >= dlg->filtered_count) {
        dlg->open = false;
        return;
    }

    e    = &dlg->entries[dlg->filtered[dlg->selected]];
    addr = e->address;
    dlg->open = false;

    if (dlg->from_memory) {
        frontend_memory_view_state *mv = &ui->memory_views[ui->memory_active_view_index];
        uint16_t cols = mv->columns > 0 ? mv->columns : 16;
        mv->view_address    = (uint16_t)(addr & ~(uint16_t)(cols - 1u));
        mv->cursor_address  = addr;
        mv->edit_field      = FRONTEND_MEMORY_EDIT_HEX;
        mv->request_pending = false;
    } else {
        frontend_disassembly_view_state *dv = &ui->disassembly;
        dv->cursor_address  = addr;
        dv->top_address     = frontend_disassembly_center_top(addr, dv->rows);
        dv->has_user_cursor = true;
        dv->cursor_row      = dv->rows / 2u;
        dv->cursor_length   = 1;
        dv->follow_pc       = false;
        dv->pc_lock_active  = true;
        dv->pc_lock_address = addr;
        dv->address_entry   = false;
        dv->request_pending = false;
    }
}

static void frontend_draw_memory_search(
    frontend *ui,
    int width,
    int height,
    const frontend_debug_state *debug_state)
{
    frontend_memory_search_state *search;
    struct nk_context *ctx;
    struct nk_rect bounds;
    nk_flags edit_result;
    bool submit = false;

    if (ui == NULL || !ui->memory_search.open || ui->ctx == NULL) {
        return;
    }
    search = &ui->memory_search;
    ctx = ui->ctx;
    bounds = nk_rect((float)(width - 440) * 0.5f, (float)(height - 180) * 0.5f,
        440.0f, 180.0f);

    if (nk_begin(ctx, "Find Memory", bounds,
            NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_CLOSABLE)) {
        if (nk_window_is_closed(ctx, "Find Memory")) {
            search->open = false;
            nk_end(ctx);
            return;
        }

        nk_layout_row_dynamic(ctx, 22.0f, 3);
        if (nk_option_label(ctx, "String", search->mode == MEMORY_SEARCH_STRING)) {
            search->mode = MEMORY_SEARCH_STRING;
            search->status[0] = '\0';
        }
        if (nk_option_label(ctx, "Hex", search->mode == MEMORY_SEARCH_HEX)) {
            search->mode = MEMORY_SEARCH_HEX;
            search->status[0] = '\0';
        }
        if (search->mode != MEMORY_SEARCH_STRING) nk_widget_disable_begin(ctx);
        frontend_checkbox_bool(ctx, "Ignore case", &search->ignore_case);
        if (search->mode != MEMORY_SEARCH_STRING) nk_widget_disable_end(ctx);

        nk_layout_row_begin(ctx, NK_DYNAMIC, 26.0f, 2);
        nk_layout_row_push(ctx, 0.18f);
        nk_label(ctx, search->mode == MEMORY_SEARCH_STRING ? "String" : "Hex bytes", NK_TEXT_LEFT);
        nk_layout_row_push(ctx, 0.82f);
        if (search->just_opened) {
            nk_edit_focus(ctx, 0);
            search->just_opened = false;
        }
        edit_result = frontend_edit_replace(
            ctx,
            (nk_flags)NK_EDIT_FIELD | NK_EDIT_SELECTABLE | NK_EDIT_CLIPBOARD | NK_EDIT_SIG_ENTER,
            search->query,
            sizeof(search->query),
            nk_filter_default);
        nk_layout_row_end(ctx);
        submit = (edit_result & NK_EDIT_COMMITED) != 0;

        nk_layout_row_dynamic(ctx, 18.0f, 1);
        nk_label(ctx, search->status[0] != '\0' ? search->status :
            (search->mode == MEMORY_SEARCH_HEX ? "Example: DE AD BE EF" : "Searches the active memory view"),
            NK_TEXT_LEFT);

        nk_layout_row_dynamic(ctx, 26.0f, 2);
        if (nk_button_label(ctx, "Find")) submit = true;
        if (nk_button_label(ctx, "Cancel")) search->open = false;

        if (submit) {
            if (memory_search_parse(
                    search->query,
                    search->mode,
                    search->ignore_case,
                    &search->pattern,
                    search->status,
                    sizeof(search->status))) {
                search->has_pattern = true;
                (void)frontend_memory_search_run(ui, debug_state, false);
                search->open = false;
            }
        }
    } else if (nk_window_is_closed(ctx, "Find Memory")) {
        search->open = false;
    }
    nk_end(ctx);
}

static void frontend_draw_symbol_lookup(frontend *ui, int width, int height)
{
    frontend_symbol_lookup_state *dlg;
    struct nk_context *ctx;
    struct nk_rect bounds;
    struct nk_list_view lv;
    char prev_search[SYMBOL_LOOKUP_SEARCH_MAX];
    float dw, dh, table_h;
    int i;

    static const int ROW_H    = 18;
    static const float ADDR_W = 52.0f;

    if (ui == NULL || !ui->symbol_lookup.open || ui->ctx == NULL) return;

    ctx = ui->ctx;
    dlg = &ui->symbol_lookup;

    dw = (float)width * 0.72f;
    if (dw < 520.0f) dw = 520.0f;
    if (dw > (float)width - 16.0f && width > 0) dw = (float)width - 16.0f;
    dh = (float)height * 0.70f;
    if (dh < 340.0f) dh = 340.0f;
    if (dh > (float)height - 16.0f && height > 0) dh = (float)height - 16.0f;

    bounds = nk_rect(((float)width - dw) * 0.5f, ((float)height - dh) * 0.5f, dw, dh);

    if (nk_begin(ctx, "Symbol Lookup", bounds,
            NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_CLOSABLE)) {

        if (nk_window_is_closed(ctx, "Symbol Lookup")) {
            dlg->open = false;
            nk_end(ctx);
            return;
        }

        /* Search box */
        memcpy(prev_search, dlg->search, sizeof(dlg->search));
        nk_layout_row_dynamic(ctx, 24.0f, 1);
        if (dlg->just_opened) {
            nk_edit_focus(ctx, 0);
            dlg->just_opened = false;
        }
        frontend_edit_replace(
            ctx,
            (nk_flags)NK_EDIT_FIELD | NK_EDIT_SELECTABLE | NK_EDIT_CLIPBOARD,
            dlg->search, sizeof(dlg->search), nk_filter_default);
        if (memcmp(prev_search, dlg->search, sizeof(dlg->search)) != 0) {
            frontend_symbol_lookup_refilter(dlg);
            dlg->scroll_to_selected = true;
        }

        /* Column headers */
        {
            char h0[12], h1[12], h2[12], h3[12];
            snprintf(h0, sizeof(h0), "ADDR%s",
                dlg->sort_col == SYMBOL_LOOKUP_SORT_ADDR   ? (dlg->sort_asc ? "^" : "v") : "");
            snprintf(h1, sizeof(h1), "SCOPE%s",
                dlg->sort_col == SYMBOL_LOOKUP_SORT_SCOPE  ? (dlg->sort_asc ? "^" : "v") : "");
            snprintf(h2, sizeof(h2), "LABEL%s",
                dlg->sort_col == SYMBOL_LOOKUP_SORT_LABEL  ? (dlg->sort_asc ? "^" : "v") : "");
            snprintf(h3, sizeof(h3), "SOURCE%s",
                dlg->sort_col == SYMBOL_LOOKUP_SORT_SOURCE ? (dlg->sort_asc ? "^" : "v") : "");

            nk_layout_row_template_begin(ctx, 22.0f);
            nk_layout_row_template_push_static(ctx, ADDR_W);
            nk_layout_row_template_push_dynamic(ctx);
            nk_layout_row_template_push_dynamic(ctx);
            nk_layout_row_template_push_dynamic(ctx);
            nk_layout_row_template_end(ctx);

            if (nk_button_label(ctx, h0))
                frontend_symbol_lookup_set_sort(dlg, SYMBOL_LOOKUP_SORT_ADDR);
            if (nk_button_label(ctx, h1))
                frontend_symbol_lookup_set_sort(dlg, SYMBOL_LOOKUP_SORT_SCOPE);
            if (nk_button_label(ctx, h2))
                frontend_symbol_lookup_set_sort(dlg, SYMBOL_LOOKUP_SORT_LABEL);
            if (nk_button_label(ctx, h3))
                frontend_symbol_lookup_set_sort(dlg, SYMBOL_LOOKUP_SORT_SOURCE);
        }

        /* Scrollable table */
        table_h = nk_window_get_height(ctx) - 24.0f /* title */ - 24.0f /* search */
                  - 22.0f /* headers */ - 30.0f /* close btn */ - 24.0f /* padding */;
        if (table_h < 40.0f) table_h = 40.0f;
        nk_layout_row_dynamic(ctx, table_h, 1);

        if (nk_list_view_begin(ctx, &lv, "sym_rows", 0, ROW_H, dlg->filtered_count)) {
            struct nk_style_selectable saved_sel = ctx->style.selectable;

            /* Scroll to selected if needed */
            if (dlg->scroll_to_selected && lv.scroll_pointer != NULL && dlg->selected >= 0) {
                nk_uint top    = *lv.scroll_pointer;
                nk_uint bot    = top + (nk_uint)table_h;
                nk_uint item_y = (nk_uint)(dlg->selected * ROW_H);
                if (item_y < top) {
                    *lv.scroll_pointer = item_y;
                } else if (item_y + (nk_uint)ROW_H > bot) {
                    *lv.scroll_pointer = item_y + (nk_uint)ROW_H - (nk_uint)table_h;
                }
                dlg->scroll_to_selected = false;
            }

            for (i = lv.begin; i < lv.end; ++i) {
                const frontend_symbol_lookup_entry *e;
                bool sel;
                char addr_buf[6];
                bool clicked = false;

                if (i < 0 || i >= dlg->filtered_count) continue;
                e   = &dlg->entries[dlg->filtered[i]];
                sel = (i == dlg->selected);

                snprintf(addr_buf, sizeof(addr_buf), "%04X", e->address);

                if (sel) {
                    ctx->style.selectable.normal  = nk_style_item_color(nk_rgb(21, 91, 116));
                    ctx->style.selectable.hover   = nk_style_item_color(nk_rgb(21, 91, 116));
                    ctx->style.selectable.pressed = nk_style_item_color(nk_rgb(21, 91, 116));
                    ctx->style.selectable.text_normal  = nk_rgb(226, 246, 255);
                    ctx->style.selectable.text_hover   = nk_rgb(226, 246, 255);
                    ctx->style.selectable.text_pressed = nk_rgb(226, 246, 255);
                } else {
                    ctx->style.selectable = saved_sel;
                }

                nk_layout_row_template_begin(ctx, (float)ROW_H);
                nk_layout_row_template_push_static(ctx, ADDR_W);
                nk_layout_row_template_push_dynamic(ctx);
                nk_layout_row_template_push_dynamic(ctx);
                nk_layout_row_template_push_dynamic(ctx);
                nk_layout_row_template_end(ctx);

                {
                    bool s = sel;
                    if (nk_selectable_label(ctx, addr_buf,  NK_TEXT_LEFT, &s)) clicked = true;
                }
                {
                    bool s = sel;
                    if (nk_selectable_label(ctx, e->scope,  NK_TEXT_LEFT, &s)) clicked = true;
                }
                {
                    bool s = sel;
                    if (nk_selectable_label(ctx, e->label,  NK_TEXT_LEFT, &s)) clicked = true;
                }
                {
                    bool s = sel;
                    if (nk_selectable_label(ctx, e->source, NK_TEXT_LEFT, &s)) clicked = true;
                }

                if (clicked) {
                    dlg->selected = i;
                    frontend_symbol_lookup_commit(ui);
                    ctx->style.selectable = saved_sel;
                    nk_list_view_end(&lv);
                    nk_end(ctx);
                    return;
                }
            }

            ctx->style.selectable = saved_sel;
            nk_list_view_end(&lv);
        }

        /* Close button */
        nk_layout_row_dynamic(ctx, 24.0f, 3);
        nk_spacing(ctx, 2);
        if (nk_button_label(ctx, "Close")) {
            dlg->open = false;
        }

    } else if (nk_window_is_closed(ctx, "Symbol Lookup")) {
        dlg->open = false;
    }
    nk_end(ctx);
}

static void frontend_draw_load_bin_dialog(frontend *ui, int width, int height)
{
    static const char *const kind_items[] = {
        "Auto", "Snapshot", "Binary", "Applesoft text"
    };
    static const char *const format_items[] = {
        "Auto", "Raw", "NAPS #TTAAAA", "AppleSingle", "Legacy DOS"
    };
    frontend_load_bin_dialog_state *dlg;
    struct nk_context *ctx;
    struct nk_rect bounds;
    nk_flags edit_flags = (nk_flags)NK_EDIT_FIELD | NK_EDIT_SELECTABLE | NK_EDIT_CLIPBOARD;

    if (ui == NULL || !ui->load_bin_dialog.open || ui->ctx == NULL) {
        return;
    }

    ctx = ui->ctx;
    dlg = &ui->load_bin_dialog;
    bounds = nk_rect((float)(width - 440) * 0.5f, (float)(height - 330) * 0.5f, 440.0f, 330.0f);
    if (bounds.x < 8.0f) bounds.x = 8.0f;
    if (bounds.y < 8.0f) bounds.y = 8.0f;

    if (nk_begin(ctx, "Load", bounds,
                 NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_CLOSABLE)) {
        if (!nk_window_is_closed(ctx, "Load")) {
            /* Name / Browse row */
            nk_layout_row_begin(ctx, NK_DYNAMIC, 24.0f, 3);
            nk_layout_row_push(ctx, 0.18f);
            nk_label(ctx, "Name", NK_TEXT_LEFT);
            nk_layout_row_push(ctx, 0.60f);
            frontend_edit_replace(ctx, edit_flags, dlg->path, (int)sizeof(dlg->path), nk_filter_default);
            nk_layout_row_push(ctx, 0.22f);
            if (nk_button_label(ctx, "Browse...")) {
                frontend_push_machine_file_browse_intent(
                    ui, FRONTEND_DEBUGGER_INTENT_LOAD_BIN_BROWSE,
                    dlg->kind, dlg->binary_format);
            }
            nk_layout_row_end(ctx);

            nk_layout_row_begin(ctx, NK_DYNAMIC, 24.0f, 2);
            nk_layout_row_push(ctx, 0.18f);
            nk_label(ctx, "Type", NK_TEXT_LEFT);
            nk_layout_row_push(ctx, 0.82f);
            {
                frontend_machine_file_kind previous = dlg->kind;
                dlg->kind = (frontend_machine_file_kind)nk_combo(
                    ctx, kind_items, 4, (int)dlg->kind, 18, nk_vec2(250.0f, 130.0f));
                if (dlg->kind != previous && dlg->path[0] != '\0') {
                    frontend_set_load_bin_path(ui, dlg->path);
                } else if (dlg->kind != previous) {
                    dlg->detected[0] = '\0';
                    dlg->decode_ok = false;
                }
            }
            nk_layout_row_end(ctx);

            if (dlg->kind == FRONTEND_MACHINE_FILE_AUTO ||
                dlg->kind == FRONTEND_MACHINE_FILE_BINARY) {
                nk_layout_row_begin(ctx, NK_DYNAMIC, 24.0f, 2);
                nk_layout_row_push(ctx, 0.18f);
                nk_label(ctx, "Format", NK_TEXT_LEFT);
                nk_layout_row_push(ctx, 0.82f);
                {
                    apple2_binary_format previous = dlg->binary_format;
                    dlg->binary_format = (apple2_binary_format)nk_combo(
                        ctx, format_items, 5, (int)dlg->binary_format, 18,
                        nk_vec2(250.0f, 150.0f));
                    if (dlg->binary_format != previous && dlg->path[0] != '\0') {
                        frontend_set_load_bin_path(ui, dlg->path);
                    } else if (dlg->binary_format != previous) {
                        dlg->detected[0] = '\0';
                        dlg->decode_ok = false;
                    }
                }
                nk_layout_row_end(ctx);
            } else {
                nk_layout_row_dynamic(ctx, 24.0f, 1);
                nk_label(ctx,
                    dlg->kind == FRONTEND_MACHINE_FILE_SNAPSHOT ?
                        "Restores a complete .a2state machine snapshot" :
                        "Tokenizes an ASCII listing as Applesoft at $0801",
                    NK_TEXT_LEFT);
            }

            if (frontend_load_bin_dialog_uses_raw_address(dlg)) {
                nk_layout_row_begin(ctx, NK_DYNAMIC, 24.0f, 2);
                nk_layout_row_push(ctx, 0.35f);
                nk_label(ctx, "Raw address", NK_TEXT_LEFT);
                nk_layout_row_push(ctx, 0.65f);
                frontend_edit_replace(
                    ctx, edit_flags, dlg->address_buf,
                    (int)sizeof(dlg->address_buf), nk_filter_hex);
                nk_layout_row_end(ctx);
            }

            nk_layout_row_dynamic(ctx, 24.0f, 1);
            if (dlg->kind == FRONTEND_MACHINE_FILE_SNAPSHOT) {
                nk_label(ctx, "Snapshot restore supplies the complete machine state", NK_TEXT_LEFT);
            } else {
                frontend_checkbox_bool(ctx, "Reset before load", &dlg->reset_first);
            }
            nk_layout_row_dynamic(ctx, 24.0f, 1);
            if (dlg->kind == FRONTEND_MACHINE_FILE_AUTO ||
                dlg->kind == FRONTEND_MACHINE_FILE_BINARY) {
                frontend_checkbox_bool(ctx, "Run after load", &dlg->run_after_load);
            } else {
                nk_spacing(ctx, 1);
            }

            if (dlg->detected[0] != '\0') {
                nk_layout_row_dynamic(ctx, 18.0f, 1);
                nk_label(ctx, dlg->detected, NK_TEXT_LEFT);
            }
            /* Soft warning only when Run is requested for a non-BIN/SYS NAPS file
               that still decoded (typically Format=Raw bypassing the type gate). */
            if (dlg->run_after_load && dlg->decode_ok && dlg->has_naps_type &&
                !apple2_binary_prodos_type_is_loadable(dlg->naps_type)) {
                char run_note[96];
                snprintf(run_note, sizeof(run_note),
                    "Note: NAPS type $%02X is not BIN/SYS; Run sets PC to the load address",
                    (unsigned)dlg->naps_type);
                nk_layout_row_dynamic(ctx, 18.0f, 1);
                nk_label_colored(ctx, run_note, NK_TEXT_LEFT, nk_rgb(255, 200, 120));
            }

            /* Error / spacer */
            if (dlg->error[0] != '\0') {
                nk_layout_row_dynamic(ctx, 18.0f, 1);
                nk_label_colored(ctx, dlg->error, NK_TEXT_LEFT, nk_rgb(255, 128, 118));
            } else {
                nk_layout_row_dynamic(ctx, 8.0f, 1);
                nk_spacing(ctx, 1);
            }

            /* Buttons */
            nk_layout_row_dynamic(ctx, 24.0f, 2);
            if (nk_button_label(ctx, "Cancel")) {
                dlg->open = false;
            }
            if (nk_button_label(ctx, "OK")) {
                frontend_commit_load_bin_dialog(ui);
            }
        } else {
            dlg->open = false;
        }
    } else if (nk_window_is_closed(ctx, "Load")) {
        dlg->open = false;
    }
    nk_end(ctx);
}

static void frontend_draw_save_bin_dialog(frontend *ui, int width, int height)
{
    static const char *const kind_items[] = {
        "Snapshot", "Binary", "Applesoft listing"
    };
    static const char *const format_items[] = {
        "NAPS #06AAAA", "Raw", "AppleSingle"
    };
    frontend_save_bin_dialog_state *dlg;
    struct nk_context *ctx;
    struct nk_rect bounds;
    nk_flags edit_flags = (nk_flags)NK_EDIT_FIELD | NK_EDIT_SELECTABLE | NK_EDIT_CLIPBOARD;
    uint16_t start_addr;
    uint16_t end_addr;

    if (ui == NULL || !ui->save_bin_dialog.open || ui->ctx == NULL) {
        return;
    }

    ctx = ui->ctx;
    dlg = &ui->save_bin_dialog;
    bounds = nk_rect((float)(width - 440) * 0.5f, (float)(height - 306) * 0.5f, 440.0f, 306.0f);
    if (bounds.x < 8.0f) bounds.x = 8.0f;
    if (bounds.y < 8.0f) bounds.y = 8.0f;

    if (nk_begin(ctx, "Save", bounds,
                 NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_CLOSABLE)) {
        if (!nk_window_is_closed(ctx, "Save")) {
            /* Name / Browse row */
            nk_layout_row_begin(ctx, NK_DYNAMIC, 24.0f, 3);
            nk_layout_row_push(ctx, 0.18f);
            nk_label(ctx, "Name", NK_TEXT_LEFT);
            nk_layout_row_push(ctx, 0.60f);
            frontend_edit_replace(ctx, edit_flags, dlg->path, (int)sizeof(dlg->path), nk_filter_default);
            nk_layout_row_push(ctx, 0.22f);
            if (nk_button_label(ctx, "Browse...")) {
                frontend_push_machine_file_browse_intent(
                    ui, FRONTEND_DEBUGGER_INTENT_SAVE_BIN_BROWSE,
                    dlg->kind, dlg->binary_format);
            }
            nk_layout_row_end(ctx);

            nk_layout_row_begin(ctx, NK_DYNAMIC, 24.0f, 2);
            nk_layout_row_push(ctx, 0.18f);
            nk_label(ctx, "Type", NK_TEXT_LEFT);
            nk_layout_row_push(ctx, 0.82f);
            {
                int selected = (int)dlg->kind - (int)FRONTEND_MACHINE_FILE_SNAPSHOT;
                if (selected < 0 || selected > 2) selected = 0;
                selected = nk_combo(ctx, kind_items, 3, selected, 18, nk_vec2(250.0f, 120.0f));
                dlg->kind = (frontend_machine_file_kind)(selected + FRONTEND_MACHINE_FILE_SNAPSHOT);
            }
            nk_layout_row_end(ctx);

            if (dlg->kind == FRONTEND_MACHINE_FILE_BINARY) {
                int selected = dlg->binary_format == APPLE2_BINARY_FORMAT_RAW ? 1 :
                    dlg->binary_format == APPLE2_BINARY_FORMAT_APPLESINGLE ? 2 : 0;
                nk_layout_row_begin(ctx, NK_DYNAMIC, 24.0f, 2);
                nk_layout_row_push(ctx, 0.18f);
                nk_label(ctx, "Format", NK_TEXT_LEFT);
                nk_layout_row_push(ctx, 0.82f);
                selected = nk_combo(ctx, format_items, 3, selected, 18, nk_vec2(250.0f, 120.0f));
                dlg->binary_format = selected == 1 ? APPLE2_BINARY_FORMAT_RAW :
                    selected == 2 ? APPLE2_BINARY_FORMAT_APPLESINGLE : APPLE2_BINARY_FORMAT_NAPS;
                nk_layout_row_end(ctx);
            } else {
                nk_layout_row_dynamic(ctx, 24.0f, 1);
                nk_label(ctx,
                    dlg->kind == FRONTEND_MACHINE_FILE_SNAPSHOT ?
                        "Saves a complete .a2state machine snapshot" :
                        "Detokenizes the live Applesoft program to ASCII",
                    NK_TEXT_LEFT);
            }

            if (dlg->kind == FRONTEND_MACHINE_FILE_BINARY &&
                dlg->binary_format == APPLE2_BINARY_FORMAT_NAPS && dlg->path[0] != '\0' &&
                frontend_parse_hex16_text(dlg->start_address_buf, &start_addr)) {
                char naps_path[1024];
                char label[1100];
                if (apple2_naps_make_path(
                        dlg->path, 0x06u, start_addr, naps_path, sizeof(naps_path))) {
                    snprintf(label, sizeof(label), "Output: %s", naps_path);
                    nk_layout_row_dynamic(ctx, 18.0f, 1);
                    nk_label(ctx, label, NK_TEXT_LEFT);
                }
            }

            if (dlg->kind == FRONTEND_MACHINE_FILE_BINARY) {
                nk_layout_row_begin(ctx, NK_DYNAMIC, 24.0f, 4);
                nk_layout_row_push(ctx, 0.22f);
                nk_label(ctx, "Start", NK_TEXT_LEFT);
                nk_layout_row_push(ctx, 0.28f);
                frontend_edit_replace(
                    ctx, edit_flags, dlg->start_address_buf,
                    (int)sizeof(dlg->start_address_buf), nk_filter_hex);
                nk_layout_row_push(ctx, 0.22f);
                nk_label(ctx, "End", NK_TEXT_LEFT);
                nk_layout_row_push(ctx, 0.28f);
                frontend_edit_replace(
                    ctx, edit_flags, dlg->end_address_buf,
                    (int)sizeof(dlg->end_address_buf), nk_filter_hex);
                nk_layout_row_end(ctx);
            }

            /* Error / spacer */
            if (dlg->error[0] != '\0') {
                nk_layout_row_dynamic(ctx, 18.0f, 1);
                nk_label_colored(ctx, dlg->error, NK_TEXT_LEFT, nk_rgb(255, 128, 118));
            } else {
                nk_layout_row_dynamic(ctx, 8.0f, 1);
                nk_spacing(ctx, 1);
            }

            /* Buttons */
            nk_layout_row_dynamic(ctx, 24.0f, 2);
            if (nk_button_label(ctx, "Cancel")) {
                dlg->open = false;
            }
            if (nk_button_label(ctx, "OK")) {
                if (dlg->path[0] == '\0') {
                    snprintf(dlg->error, sizeof(dlg->error), "No file selected");
                } else if (dlg->kind == FRONTEND_MACHINE_FILE_BINARY &&
                           !frontend_parse_hex16_text(dlg->start_address_buf, &start_addr)) {
                    snprintf(dlg->error, sizeof(dlg->error), "Invalid start address (XXXX hex required)");
                } else if (dlg->kind == FRONTEND_MACHINE_FILE_BINARY &&
                           !frontend_parse_hex16_text(dlg->end_address_buf, &end_addr)) {
                    snprintf(dlg->error, sizeof(dlg->error), "Invalid end address (XXXX hex required)");
                } else if (dlg->kind == FRONTEND_MACHINE_FILE_BINARY && start_addr > end_addr) {
                    snprintf(dlg->error, sizeof(dlg->error), "Start must be <= end");
                } else {
                    if (dlg->kind != FRONTEND_MACHINE_FILE_BINARY) {
                        start_addr = 0;
                        end_addr   = 0;
                    }
                    frontend_push_save_bin_execute_intent(
                        ui, dlg->path, start_addr, end_addr,
                        dlg->kind, dlg->binary_format);
                    dlg->open = false;
                }
            }
        } else {
            dlg->open = false;
        }
    } else if (nk_window_is_closed(ctx, "Save")) {
        dlg->open = false;
    }
    nk_end(ctx);
}

static void frontend_draw_assembler_error_dialog(frontend *ui, int width, int height)
{
    struct nk_context *ctx;
    frontend_assembler_state *asm_state;
    struct nk_rect bounds;
    float dialog_w, dialog_h;

    if (ui == NULL || ui->ctx == NULL || !ui->assembler.error_dialog_open) {
        return;
    }

    ctx = ui->ctx;
    asm_state = &ui->assembler;
    dialog_w = (float)width * 0.7f;
    if (dialog_w < 400.0f) dialog_w = 400.0f;
    dialog_h = (float)height * 0.6f;
    if (dialog_h < 200.0f) dialog_h = 200.0f;
    bounds = nk_rect(
        ((float)width - dialog_w) * 0.5f,
        ((float)height - dialog_h) * 0.5f,
        dialog_w, dialog_h);

    if (nk_begin(
            ctx,
            asm_state->error_dialog_is_notice ?
                "Assembly Adjustments" : "Assembly Errors",
            bounds,
            NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_NO_SCROLLBAR)) {
        float content_h = dialog_h - 80.0f;
        if (content_h < 60.0f) content_h = 60.0f;

        nk_layout_row_dynamic(ctx, content_h, 1);
        {
            const struct nk_user_font *font = ctx->style.font;
            float max_row_w = dialog_w;
            {
                const char *sp = asm_state->error_text;
                const char *se = sp + strlen(sp);
                while (sp < se) {
                    const char *nl = strchr(sp, '\n');
                    int line_chars = nl ? (int)(nl - sp) : (int)(se - sp);
                    float w = font->width(font->userdata, font->height, sp, line_chars) + 20.0f;
                    if (w > max_row_w) max_row_w = w;
                    sp = nl ? nl + 1 : se;
                }
            }
            struct nk_scroll scroll = { asm_state->error_scroll_x, asm_state->error_scroll_y };
            if (nk_group_scrolled_begin(ctx, &scroll, "asm-error-scroll", NK_WINDOW_BORDER)) {
                const char *p = asm_state->error_text;
                const char *end = p + strlen(p);
                while (p < end) {
                    const char *nl = strchr(p, '\n');
                    size_t len = nl != NULL ? (size_t)(nl - p) : (size_t)(end - p);
                    char line[256];
                    if (len >= sizeof(line)) len = sizeof(line) - 1u;
                    memcpy(line, p, len);
                    line[len] = '\0';
                    nk_layout_row_static(ctx, 16.0f, (int)max_row_w, 1);
                    nk_label(ctx, line, NK_TEXT_LEFT);
                    p = nl != NULL ? nl + 1 : end;
                }
                nk_group_scrolled_end(ctx);
            }
            asm_state->error_scroll_x = scroll.x;
            asm_state->error_scroll_y = scroll.y;
        }

        nk_layout_row_template_begin(ctx, 28.0f);
        nk_layout_row_template_push_dynamic(ctx);
        nk_layout_row_template_push_static(ctx, 70.0f);
        nk_layout_row_template_push_static(ctx, 8.0f);
        nk_layout_row_template_push_static(ctx, 70.0f);
        nk_layout_row_template_push_dynamic(ctx);
        nk_layout_row_template_end(ctx);
        nk_spacing(ctx, 1);
        if (nk_button_label(ctx, "OK")) {
            asm_state->error_dialog_open = false;
        }
        nk_spacing(ctx, 1);
        if (nk_button_label(ctx, "Copy")) {
            SDL_SetClipboardText(asm_state->error_text);
        }
        nk_spacing(ctx, 1);
    }
    nk_end(ctx);
}

/* Display-only path (debugger UI hidden). With True Aspect Ratio on, ordinary
   letterbox/pillar bars stay black. Curvature instead extends its dark matte to
   the viewport edges so the fitted texture does not reveal a second straight
   frame around the curved screen. With True Aspect Ratio off the crop stretches
   to fill the window and there are no bars at all. */
static void frontend_render_display_only(frontend *ui)
{
    int width = 0;
    int height = 0;
    int fit_w = 0;
    int fit_h = 0;
    SDL_Rect dest;
    SDL_Rect src;
    SDL_Texture *render_texture;
    int texture_scale;

    if (ui == NULL || ui->display_texture == NULL || !ui->has_frame) {
        return;
    }

    platform_window_get_size(ui->window, &width, &height);
    if (frontend_display_fills_view(ui)) {
        dest.x = 0;
        dest.y = 0;
        dest.w = width;
        dest.h = height;
    } else {
        frontend_display_fit_source(ui, &fit_w, &fit_h);
        dest = frontend_fit_rect(0, 0, width, height, fit_w, fit_h);
    }
    if (dest.w <= 0 || dest.h <= 0) {
        return;
    }

    render_texture = frontend_display_texture_for_render(ui);
    if (render_texture == ui->crt_texture && ui->crt_effects.curvature &&
        !frontend_display_fills_view(ui)) {
        SDL_SetRenderDrawColor(
            ui->renderer,
            FRONTEND_DISPLAY_MATTE_R,
            FRONTEND_DISPLAY_MATTE_G,
            FRONTEND_DISPLAY_MATTE_B,
            255);
        SDL_RenderFillRect(ui->renderer, NULL);
        SDL_SetRenderDrawColor(ui->renderer, 0, 0, 0, 255);
    }
    texture_scale = render_texture == ui->crt_texture ? FRONTEND_CRT_RENDER_SCALE : 1;
    src.x = frontend_display_crop_x_for_frame(&ui->current_frame) * texture_scale;
    src.y = frontend_display_crop_y_for_frame(&ui->current_frame) * texture_scale;
    src.w = frontend_display_crop_w_for_frame(&ui->current_frame) * texture_scale;
    src.h = frontend_display_crop_h_for_frame(&ui->current_frame) * texture_scale;
    SDL_RenderCopy(ui->renderer, render_texture, &src, &dest);
}

void frontend_render(frontend *ui, bool ui_visible, const frontend_debug_state *debug_state)
{
    int width = 0;
    int height = 0;
    struct nk_rect parent;
    int split_display_active = 0;
    int split_top_bottom_active = 0;
    int split_memory_misc_active = 0;
    int display_corner_active = 0;
    bool debugger_scrollbar_active = false;

    if (ui == NULL || ui->ctx == NULL) {
        return;
    }

    if (!ui->input_focus_initialized) {
        frontend_set_active_view(ui, FRONTEND_ACTIVE_VIEW_MACHINE);
        ui->input_focus_initialized = true;
    }

    platform_window_get_size(ui->window, &width, &height);
    if (width <= 0 || height <= 0) {
        return;
    }

    if (!ui_visible && !help_view_is_open(&ui->help) &&
        !forensics_view_is_open(&ui->forensics)) {
        frontend_render_display_only(ui);
        frontend_draw_disk_activity_leds(ui, width, height, debug_state);
        return;
    }

    if (debug_state == NULL && ui_visible && !help_view_is_open(&ui->help) &&
        !forensics_view_is_open(&ui->forensics)) {
        return;
    }

    if (help_view_is_open(&ui->help)) {
        frontend_render_display_only(ui);
        help_view_render(ui->ctx, &ui->help, ui->help_font, width, height);
        nk_sdl_render(NK_ANTI_ALIASING_ON);
        frontend_draw_disk_activity_leds(ui, width, height, debug_state);
        return;
    }

    if (forensics_view_is_open(&ui->forensics)) {
        frontend_forensics_land_context land;
        /* Full-window mode: no CRT behind (unlike Help overlay). */
        frontend_forensics_land_context_from_debug(debug_state, &land);
        forensics_view_apply_land_focus(&ui->forensics, &land);
        forensics_view_render(ui->ctx, &ui->forensics, width, height, &land);
        frontend_forensics_flush_query_submit(ui);
        frontend_forensics_flush_land(ui);
        nk_sdl_render(NK_ANTI_ALIASING_ON);
        return;
    }

    if (ui_visible && debug_state != NULL) {
        int min_debug_w = ui->limits.min_display_w_px + ui->limits.min_right_w_px;
        int min_debug_h = ui->limits.registers_h_px + ui->limits.min_disassembly_h_px + ui->limits.min_bottom_h_px;
        struct nk_style_window saved_window_style;
        int inspect_style = 0;

        if (width < min_debug_w || height < min_debug_h) {
            frontend_render_display_only(ui);
        } else {
            parent = nk_rect(0.0f, 0.0f, (float)width, (float)height);
            if (debug_state->inspecting) {
                saved_window_style = ui->ctx->style.window;
                inspect_style = 1;
                ui->ctx->style.window.header.normal =
                    nk_style_item_color(nk_rgb(24, 62, 118));
                ui->ctx->style.window.header.hover =
                    nk_style_item_color(nk_rgb(32, 76, 136));
                ui->ctx->style.window.header.active =
                    nk_style_item_color(nk_rgb(40, 88, 152));
            }
            debugger_layout_compute(&ui->layout, parent, &ui->limits);
            if (!frontend_any_dialog_open(ui)) {
                debugger_scrollbar_active = ui->memory_views[ui->memory_active_view_index].scrollbar_dragging ||
                    ui->disassembly.scrollbar_dragging ||
                    (ui->has_memory_scrollbar_bounds &&
                     nk_input_is_mouse_down(&ui->ctx->input, NK_BUTTON_LEFT) &&
                     nk_input_is_mouse_hovering_rect(&ui->ctx->input, ui->memory_scrollbar_bounds)) ||
                    (ui->has_disassembly_scrollbar_bounds &&
                     nk_input_is_mouse_down(&ui->ctx->input, NK_BUTTON_LEFT) &&
                     nk_input_is_mouse_hovering_rect(&ui->ctx->input, ui->disassembly_scrollbar_bounds));
                if (!debugger_scrollbar_active) {
                    debugger_layout_handle_drag(&ui->layout, &ui->ctx->input, parent, &ui->limits);
                } else {
                    ui->layout.drag_active = DEBUGGER_LAYOUT_DRAG_NONE;
                }
            } else {
                ui->layout.drag_active = DEBUGGER_LAYOUT_DRAG_NONE;
            }

            frontend_draw_display_placeholder(ui, ui->layout.display, debug_state);
            frontend_draw_registers(ui, ui->layout.registers, debug_state);
            frontend_draw_disassembly_view(ui, ui->layout.disassembly, debug_state);
            frontend_draw_memory(ui, ui->layout.memory, debug_state);
            frontend_draw_misc(ui, ui->layout.misc, debug_state);

            if (!frontend_any_dialog_open(ui)) {
                split_display_active = ui->layout.drag_active == DEBUGGER_LAYOUT_DRAG_SPLIT_DISPLAY ||
                    nk_input_is_mouse_hovering_rect(&ui->ctx->input, ui->layout.hit_split_display);
                split_top_bottom_active = ui->layout.drag_active == DEBUGGER_LAYOUT_DRAG_SPLIT_TOP_BOTTOM ||
                    nk_input_is_mouse_hovering_rect(&ui->ctx->input, ui->layout.hit_split_top_bottom);
                split_memory_misc_active = ui->layout.drag_active == DEBUGGER_LAYOUT_DRAG_SPLIT_BOTTOM ||
                    nk_input_is_mouse_hovering_rect(&ui->ctx->input, ui->layout.hit_split_memory_misc);
                display_corner_active = ui->layout.drag_active == DEBUGGER_LAYOUT_DRAG_DISPLAY_CORNER ||
                    nk_input_is_mouse_hovering_rect(&ui->ctx->input, ui->layout.hit_display_corner);
            }

            frontend_draw_splitter(ui->ctx, "split-display", ui->layout.hit_split_display, split_display_active);
            frontend_draw_splitter(ui->ctx, "split-top-bottom", ui->layout.hit_split_top_bottom, split_top_bottom_active);
            frontend_draw_splitter(ui->ctx, "split-memory-misc", ui->layout.hit_split_memory_misc, split_memory_misc_active);
            frontend_draw_corner_handle(ui->ctx, ui->layout.hit_display_corner, display_corner_active);
            frontend_draw_config_dialog(ui, width, height);
            frontend_draw_breakpoint_editor(ui, width, height);
            frontend_draw_load_bin_dialog(ui, width, height);
            frontend_draw_save_bin_dialog(ui, width, height);
            frontend_draw_assembler_error_dialog(ui, width, height);
            frontend_draw_memory_search(ui, width, height, debug_state);
            frontend_draw_symbol_lookup(ui, width, height);
            frontend_draw_file_browser(ui, width, height);
            if (inspect_style) {
                ui->ctx->style.window = saved_window_style;
            }
        }
    } else {
        frontend_render_display_only(ui);
    }

    nk_sdl_render(NK_ANTI_ALIASING_ON);
    frontend_draw_disk_activity_leds(ui, width, height, debug_state);
}

void frontend_open_help(frontend *ui, bool paused_by_help)
{
    if (ui == NULL) {
        return;
    }
    /* Mutual exclusion: leave Forensics without resuming (machine already paused). */
    if (forensics_view_is_open(&ui->forensics)) {
        (void)forensics_view_close(&ui->forensics);
        /* Forensics already paused the machine; do not make Help auto-resume. */
        paused_by_help = false;
    }
    help_view_open(&ui->help, paused_by_help);
    frontend_set_active_view(ui, FRONTEND_ACTIVE_VIEW_NONE);
}

bool frontend_close_help(frontend *ui)
{
    bool paused_by_help;

    if (ui == NULL || !help_view_is_open(&ui->help)) {
        return false;
    }

    paused_by_help = help_view_paused_by_help(&ui->help);
    help_view_close(&ui->help);
    return paused_by_help;
}

void frontend_open_forensics(
    frontend *ui,
    bool from_debugger,
    bool crt_was_running)
{
    frontend_forensics_entry entry;

    if (ui == NULL) {
        return;
    }
    /* Mutual exclusion with Help: close without resuming. */
    if (help_view_is_open(&ui->help)) {
        help_view_close(&ui->help);
    }
    entry = from_debugger ? FRONTEND_FORENSICS_ENTRY_DEBUGGER :
                            FRONTEND_FORENSICS_ENTRY_CRT;
    forensics_view_open(&ui->forensics, entry, crt_was_running);
    frontend_set_active_view(ui, FRONTEND_ACTIVE_VIEW_NONE);
    /* history-info on open (status strip only; no transcript note). */
    {
        frontend_forensics_query_parsed info;
        memset(&info, 0, sizeof(info));
        info.ok = true;
        info.verb_code = (int)FRONTEND_HISTORY_VERB_INFO;
        (void)frontend_push_history_intent(ui, &info);
    }
}

void frontend_close_forensics(frontend *ui)
{
    if (ui == NULL || !forensics_view_is_open(&ui->forensics)) {
        return;
    }
    forensics_view_close(&ui->forensics);
    ui->misc.active_tab = FRONTEND_MISC_TAB_INSPECTOR;
    ui->misc.initialized = true;
}

bool frontend_forensics_is_open(const frontend *ui)
{
    return ui != NULL && forensics_view_is_open(&ui->forensics);
}

bool frontend_forensics_entered_from_crt(const frontend *ui)
{
    return ui != NULL &&
        forensics_view_entry(&ui->forensics) == FRONTEND_FORENSICS_ENTRY_CRT;
}

bool frontend_forensics_crt_was_running(const frontend *ui)
{
    return ui != NULL && forensics_view_crt_was_running(&ui->forensics);
}

bool frontend_forensics_consume_close_request(frontend *ui)
{
    if (ui == NULL || !ui->forensics.request_close) {
        return false;
    }
    ui->forensics.request_close = false;
    return true;
}

bool frontend_forensics_consume_leave_debugger_request(frontend *ui)
{
    if (ui == NULL || !ui->forensics.request_leave_debugger) {
        return false;
    }
    ui->forensics.request_leave_debugger = false;
    return true;
}

bool frontend_forensics_consume_pause_request(frontend *ui)
{
    if (ui == NULL || !ui->forensics.request_host_pause) {
        return false;
    }
    ui->forensics.request_host_pause = false;
    return true;
}

bool frontend_handle_forensics_key(frontend *ui, const SDL_KeyboardEvent *key)
{
    if (ui == NULL || key == NULL || !forensics_view_is_open(&ui->forensics)) {
        return false;
    }
    switch (key->keysym.sym) {
        case SDLK_UP:
            return forensics_view_query_history_prev(&ui->forensics);
        case SDLK_DOWN:
            return forensics_view_query_history_next(&ui->forensics);
        case SDLK_TAB:
            /* Always consume Tab in Forensics so focus does not escape. */
            (void)forensics_view_autocomplete(&ui->forensics);
            return true;
        default:
            break;
    }
    return false;
}

void frontend_forensics_apply_result(
    frontend *ui,
    frontend_history_verb verb,
    const char *label,
    const runtime_history_rpc_meta *meta,
    const runtime_history_record *records,
    size_t record_count,
    const bool *anchor_matches)
{
    if (ui == NULL || !forensics_view_is_open(&ui->forensics)) {
        return;
    }
    forensics_view_apply_result(
        &ui->forensics,
        (int)verb,
        label,
        meta,
        records,
        record_count,
        anchor_matches);
}

void frontend_forensics_apply_status(
    frontend *ui,
    const runtime_history_status *status,
    bool append_transcript_note)
{
    if (ui == NULL || !forensics_view_is_open(&ui->forensics)) {
        return;
    }
    forensics_view_apply_status(&ui->forensics, status, append_transcript_note);
}

void frontend_forensics_apply_rpc_error(
    frontend *ui,
    runtime_history_rpc_status status)
{
    if (ui == NULL || !forensics_view_is_open(&ui->forensics)) {
        return;
    }
    forensics_view_apply_rpc_error(&ui->forensics, status);
}

uint64_t frontend_forensics_last_cursor(const frontend *ui)
{
    if (ui == NULL) {
        return 0u;
    }
    return ui->forensics.last_cursor;
}

bool frontend_help_is_open(const frontend *ui)
{
    return ui != NULL && help_view_is_open(&ui->help);
}

bool frontend_help_paused_by_help(const frontend *ui)
{
    return ui != NULL && help_view_paused_by_help(&ui->help);
}

bool frontend_handle_help_key(frontend *ui, const SDL_KeyboardEvent *key, int scroll_wheel_lines)
{
    nk_uint page;
    int line_scroll;
    float row_h;

    if (ui == NULL || ui->ctx == NULL || key == NULL || !help_view_is_open(&ui->help)) {
        return false;
    }

    if (scroll_wheel_lines < 1) {
        scroll_wheel_lines = 1;
    }
    row_h = ui->ctx->style.font != NULL ? ui->ctx->style.font->height + 2.0f : 12.0f;
    line_scroll = (int)(row_h * (float)scroll_wheel_lines);
    if (line_scroll < 1) {
        line_scroll = scroll_wheel_lines;
    }
    page = ui->help.content_page_y > 0 ? ui->help.content_page_y : 400u;
    switch (key->keysym.sym) {
        case SDLK_UP:
            (void)help_view_scroll_content(ui->ctx, &ui->help, -line_scroll);
            return true;
        case SDLK_DOWN:
            (void)help_view_scroll_content(ui->ctx, &ui->help, line_scroll);
            return true;
        case SDLK_PAGEUP:
            (void)help_view_scroll_content(ui->ctx, &ui->help, -(int)page);
            return true;
        case SDLK_PAGEDOWN:
            (void)help_view_scroll_content(ui->ctx, &ui->help, (int)page);
            return true;
        case SDLK_HOME:
            (void)help_view_scroll_content_to(ui->ctx, &ui->help, 0);
            return true;
        case SDLK_END:
            (void)help_view_scroll_content_to(ui->ctx, &ui->help, 0x3fffffffu);
            return true;
        case SDLK_LEFT:
            return help_view_select_section(ui->ctx, &ui->help, ui->help.section_index - 1);
        case SDLK_RIGHT:
            return help_view_select_section(ui->ctx, &ui->help, ui->help.section_index + 1);
        default:
            break;
    }

    return false;
}

bool frontend_poll_debugger_intent(frontend *ui, frontend_debugger_intent *out_intent)
{
    if (ui == NULL || out_intent == NULL || ui->intent_read == ui->intent_write) {
        return false;
    }

    *out_intent = ui->intents[ui->intent_read];
    memset(&ui->intents[ui->intent_read], 0, sizeof(ui->intents[ui->intent_read]));
    ui->intent_read = (ui->intent_read + 1u) % FRONTEND_DEBUGGER_INTENT_CAPACITY;
    return true;
}

bool frontend_get_disassembly_cursor(const frontend *ui, uint16_t *out_address) {
    if (ui == NULL || out_address == NULL || !ui->disassembly.has_user_cursor) {
        return false;
    }
    *out_address = ui->disassembly.cursor_address;
    return true;
}
