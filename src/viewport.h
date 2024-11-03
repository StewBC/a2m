// Apple ][+ emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#pragma once

typedef struct VIEWPORT {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Surface *surface;
    SDL_Texture *texture;
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

    // Flags
    int debug_view:1;
    int dlg_dissassembler_go:1;
    int dlg_filebrowser:1;
    int dlg_memory_find:1;
    int dlg_memory_go:1;
    int dlg_breakpoint:1;
    int shadow_active_page:1;                               // Flags that shadow machine states
    int shadow_stopped:1;
    int show_help:1;
    int viewcpu_show:1;
    int viewdbg_show:1;
    int viewdlg_modal:1;
    int viewmem_show:1;
    int viewmisc_show:1;
} VIEWPORT;

int viewport_init(VIEWPORT *v, int w, int h);
void viewport_init_nuklear(VIEWPORT *v);
int viewport_process_events(APPLE2 *m);
void viewport_show(APPLE2 *m);
void viewport_show_help(APPLE2 *m);
void viewport_shutdown(VIEWPORT *v);
void viewport_toggle_debug(APPLE2 *m);
void viewport_update(APPLE2 *m);
