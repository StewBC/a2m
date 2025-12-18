// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

typedef struct UNK {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Surface *surface;
    SDL_Surface *surface_wide;  // The 80 col / double res surface
    SDL_Texture *texture;
    SDL_Texture *texture_wide;  // The 80 col / double res backing texture
    SDL_Texture *greenLED;
    SDL_Texture *redLED;
    SDL_GameController *game_controller[2];
    SDL_Rect target_rect;
    SDL_Rect sdl_os_rect;
    SDL_Rect full_screen_rect;
    SDL_Rect *draw_rect;
    struct nk_rect nk_os_rect;

    LAYOUT layout;
    LAYOUT_LIMITS lim;

    // Window title MHz display helpers
    uint64_t prev_cycles;
    uint64_t prev_ticks;
    double mhz_moving_average;
    double fps_moving_average;

    struct nk_context *ctx;
    struct nk_font *font;
    int font_height;
    int font_width;

    int scroll_wheel_lines;

    VIEWDASM viewdasm;
    VIEWMEM viewmem;
    VIEWCPU viewcpu;
    VIEWMISC viewmisc;
    VIEWSPEAKER viewspeaker;

    // Help related
    int help_page;
    struct nk_scroll help_scroll[HELP_MAX_PAGES];

    // Right-click menu
    struct nk_vec2 right_click_menu_pos;
    uint32_t right_click_address;

    // These are so the UI can pull data out with ease
    // APPLE2 *m is set/reset ever time RT calls UI so the UI can
    // be asked to display any APPLE2 machine at any time - the UI
    // doesn't hold state.  RT is persistant
    APPLE2 *m;
    RUNTIME *rt;

    // Shadow of machine states
    A2FLAGSPACK shadow_flags;

    // Game controller values
    int8_t num_controllers;
    uint8_t button[2][3];
    uint8_t axis_left[2][2];
    uint64_t ptrig_cycle;

    // State Flags
    uint32_t clear_a2_view: 1;
    uint32_t debug_view: 1;
    uint32_t dirty_view: 1;
    uint32_t disk_activity_read: 1;
    uint32_t disk_activity_write: 1;
    uint32_t display_override: 1;
    uint32_t dlg_assembler_config: 1;
    uint32_t dlg_assembler_errors: 1;
    uint32_t dlg_breakpoint: 1;
    uint32_t dlg_filebrowser: 1;
    uint32_t dlg_memory_find: 1;
    uint32_t dlg_memory_go: 1;
    uint32_t dlg_modal_active: 1;
    uint32_t dlg_modal_mouse_down: 1;
    uint32_t dlg_symbol_lookup_dbg: 1;
    uint32_t dlg_symbol_lookup_mem: 1;
    uint32_t model: 1;
    uint32_t monitor_type: 2;
    uint32_t original_del: 1;
    uint32_t right_click_menu_open: 1;
    uint32_t shadow_run: 1;
    uint32_t show_help: 1;
    uint32_t show_leds: 1;
    uint32_t pad: 8;
} UNK;

extern const UI_OPS unk_ops;

#define color_active_win        nk_rgb( 50,100, 50)
#define color_popup_border      nk_rgb(255,  0,  0)
#define color_help_master       nk_rgb(  0,255,255)
#define color_help_heading      nk_rgb(255,255,255)
#define color_help_sub_heading  nk_rgb(  0,128,255)
#define color_help_notice       nk_rgb(255,255,  0)
#define color_help_key_heading  nk_rgb(  0,255,128)

// a helper to make a Nuklear function a bit nicer
int nk_option_label_disabled(struct nk_context *ctx, const char *label, int state, int disabled);

// a helper to do a custom vertical scrollbar (I can't seem to just get a scrollbar v from nuklear but this is better anyway)
void nk_custom_scrollbarv(struct nk_context *ctx, struct nk_rect sbar, int total_rows, int rows_visible, int *top_row, int *dragging, float *grab_offset);

void unk_config_ui(UNK *v, INI_STORE *ini_store);
int unk_init(UNK *v, int model, INI_STORE *ini_store);
int unk_process_events(UI *ui, APPLE2 *m);
void unk_show(UNK *v);
void unk_shutdown(UNK *v);
void unk_toggle_debug(UNK *v);
void unk_present(UNK *v);
void unk_render_frame(UI *ui, APPLE2 *m, int dirty);
void unk_set_runtime(UI *ui, RUNTIME *rt);
void unk_set_shadow_flags(UI *ui, uint32_t shadow_flags);
void unk_disk_read(UI *ui);
void unk_disk_write(UI *ui);
