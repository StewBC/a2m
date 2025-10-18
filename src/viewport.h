// Apple ][+ and //e Emhanced emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#pragma once

typedef struct VIEWPORT {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Surface *surface;
    SDL_Surface *surface640;
    SDL_Texture *texture;
    SDL_Texture *texture640;
    SDL_Texture *greenLED;
    SDL_Texture *redLED;
    SDL_Rect target_rect;
    SDL_Rect full_window_rect;

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
    int shadow_screen_mode;
    int help_page;

    // Flags
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
    uint32_t shadow_active_page: 1;                              // Flags that shadow machine states
    uint32_t shadow_free_run: 1;
    uint32_t shadow_stopped: 1;
    uint32_t show_help: 1;
    uint32_t show_leds: 1;
    uint32_t viewcpu_show: 1;
    uint32_t viewdbg_show: 1;
    uint32_t viewdlg_modal: 1;
    uint32_t viewmem_show: 1;
    uint32_t viewmisc_show: 1;
} VIEWPORT;

int viewport_init(APPLE2 *m, int w, int h);
void viewport_init_nuklear(VIEWPORT *v);
int viewport_process_events(APPLE2 *m);
void viewport_show(APPLE2 *m);
void viewport_show_help(APPLE2 *m);
void viewport_shutdown(VIEWPORT *v);
void viewport_toggle_debug(APPLE2 *m);
void viewport_update(APPLE2 *m);
