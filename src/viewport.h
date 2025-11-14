// Apple ][+ and //e Emhanced emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#pragma once

typedef struct VIEWPORT {
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
    SDL_Rect *draw_rect;
    struct nk_rect nk_os_rect;

    LAYOUT layout;
    LAYOUT_LIMITS lim;

    // Window title MHz display helpers
    uint64_t prev_cycles;
    uint64_t prev_ticks;
    double mhz_moving_average;
    double fps_moving_average;

    float display_scale;

    struct nk_context *ctx;
    struct nk_font *font;
    int font_height;
    int font_width;

    DEBUGGER debugger;
    MEMSHOW memshow;
    VIEWCPU viewcpu;
    VIEWMISC viewmisc;

    // Shadow of machine states
    // int shadow_flags;
    A2FLAGSPACK shadow_flags;
    int help_page;

    // Game controller values
    int8_t num_controllers;
    uint8_t button_a[2];
    uint8_t button_b[2];
    uint8_t button_x[2];
    uint8_t axis_left_x[2];
    uint8_t axis_left_y[2];
    uint64_t ptrig_cycle;

    // Flags that contain machine states
    uint32_t debug_view: 1;
    uint32_t dlg_assassembler_config: 1;
    uint32_t dlg_assassembler_errors: 1;
    uint32_t dlg_breakpoint: 1;
    uint32_t dlg_disassembler_go: 1;
    uint32_t dlg_filebrowser: 1;
    uint32_t dlg_memory_find: 1;
    uint32_t dlg_memory_go: 1;
    uint32_t dlg_symbol_lookup_dbg: 1;
    uint32_t dlg_symbol_lookup_mem: 1;
    uint32_t shadow_stopped: 1;
    uint32_t show_help: 1;
    uint32_t show_leds: 1;
    uint32_t viewdlg_modal: 1;
    uint32_t display_override: 1;
    uint32_t clear_a2_view: 1;
} VIEWPORT;

#define color_active_win        nk_rgb( 50,100, 50)
#define color_popup_border      nk_rgb(255,  0,  0)
#define color_help_master       nk_rgb(  0,255,255)
#define color_help_heading      nk_rgb(255,255,255)
#define color_help_sub_heading  nk_rgb(  0,128,255)
#define color_help_notice       nk_rgb(255,255,  0)
#define color_help_key_heading  nk_rgb(  0,255,128)

// These values are picked up in nuklrsdl.h
extern float sdl_x_scale, sdl_y_scale;

// a helper to make a Nuklear function a bit nicer
int nk_option_label_disabled(struct nk_context *ctx, const char *label, int state, int disabled);

// a helper to do a custom vertical scrollbar (I can't seem to just get a scrollbar v from nuklear but this is better anyway)
void nk_custom_scrollbarv(struct nk_context *ctx, struct nk_rect sbar, int total_rows, int rows_visible, int *top_row, int *dragging, float *grab_offset);

int viewport_init(APPLE2 *m, int w, int h);
void viewport_init_nuklear(VIEWPORT *v);
int viewport_process_events(APPLE2 *m);
void viewport_show(APPLE2 *m);
void viewhelp_show(APPLE2 *m);
void viewport_shutdown(VIEWPORT *v);
void viewport_toggle_debug(APPLE2 *m);
void viewport_update(APPLE2 *m);
