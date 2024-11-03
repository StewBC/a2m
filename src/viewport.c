// Apple ][+ emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#include "header.h"

#define NK_IMPLEMENTATION
#include "nuklear.h"
#define NK_SDL_RENDERER_IMPLEMENTATION
#include "nuklrsdl.h"

#define color_active_win        nk_rgb( 50,100, 50)
#define color_popup_border      nk_rgb(255,  0,  0)
#define color_help_master       nk_rgb(  0,255,255)
#define color_help_heading      nk_rgb(255,255,255)
#define color_help_notice       nk_rgb(255,255,  0)
#define color_help_key_heading  nk_rgb(  0,255,128)

int viewport_init(VIEWPORT *v, int w, int h) {

    // Clear the whole viewport struct to 0's
    memset(v, 0, sizeof(VIEWPORT));

    // Set the window width and height
    v->target_rect.w = w;    // Width of the window
    v->target_rect.h = h;    // Height of the window
    v->full_window_rect = v->target_rect; // And remember the setting

    // Initialize SDL with video and audio
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) {
        printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        goto error;
    }

    // Create window
    v->window = SDL_CreateWindow("Apple ][+ Emulator", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, w, h, SDL_WINDOW_SHOWN);
    if(v->window == NULL) {
        printf("Window could not be created! SDL_Error: %s\n", SDL_GetError());
        goto error;
    }

    // Create renderer
    v->renderer = SDL_CreateRenderer(v->window, -1, SDL_RENDERER_ACCELERATED);
    if(v->renderer == NULL) {
        printf("Renderer could not be created! SDL_Error: %s\n", SDL_GetError());
        goto error;
    }

    // Create RGB surface
    v->surface = SDL_CreateRGBSurface(0, 280, 192, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
    if(v->surface == NULL) {
        printf("Surface could not be created! SDL_Error: %s\n", SDL_GetError());
        goto error;
    }

    // Create texture for pixel rendering
    v->texture = SDL_CreateTextureFromSurface(v->renderer, v->surface);
    if(v->texture == NULL) {
        printf("Texture could not be created! SDL_Error: %s\n", SDL_GetError());
        goto error;
    }

    // The speaker is in the machine but only a machine in view makes sounds
    if(A2_OK != viewapl2_speaker_init()) {
        goto error;
    }

    if(A2_OK != viewdbg_init(&v->debugger, CODE_LINES_COUNT)) {
        goto error;
    }

    if(A2_OK != viewmem_init(&v->memshow, MEMSHOW_ROWS)) {
        goto error;
    }

    // Init nuklear
    struct nk_font_atlas *atlas;
    struct nk_font_config config = nk_font_config(0);

    v->ctx = nk_sdl_init(v->window, v->renderer);
    if(!v->ctx) {
        goto error;
    }
    nk_sdl_font_stash_begin(&atlas);
    v->font = nk_font_atlas_add_default(atlas, 13, &config);
    nk_sdl_font_stash_end();
    nk_style_set_font(v->ctx, &v->font->handle);
    v->font_height = v->ctx->style.font->height;
    v->font_width = v->ctx->style.font->width(v->ctx->style.font->userdata, v->font_height, "W", 1);
    v->ctx->style.window.header.active.data.color = color_active_win;
    v->ctx->style.window.popup_border_color = color_popup_border;


    // Init the file_broswer dynamic array
    array_init(&v->viewmisc.file_browser.dir_contents, sizeof(FILE_INFO));

    // Currently, all windows are always visible when any are visible
    v->viewcpu_show = 1;
    v->viewdbg_show = 1;
    v->viewmem_show = 1;
    v->viewmisc_show = 1;
    return A2_OK;

error:
    SDL_Quit();
    return A2_ERR;
}

int viewport_process_events(APPLE2 *m){
    SDL_Event e;
    int ret = 0;
    VIEWPORT *v = m->viewport;

    // If this instance of an Apple II has no view, it also gets no events
    if(!v) {
        return 0;
    }

    // Update the CPU view to the latest stats
    if(m->stopped && v->debug_view) {
        viewcpu_update(m);
    }

    nk_input_begin(v->ctx);
    // Process all SDL events, and keep doing if stopped and not stepping
    while (SDL_PollEvent(&e) != 0 || (m->stopped && !m->step)) {
        int force_update = v->show_help;
        if(e.type == SDL_QUIT) {
            ret = 1;
            break;
        }
        nk_sdl_handle_event(&e);

        // Function keys all go to the disassembly view no matter what is active
        if(e.type == SDL_KEYDOWN && e.key.keysym.sym >= SDLK_F1 && e.key.keysym.sym <= SDLK_F12) {
            force_update = viewdbg_process_event(m, &e);
        } else if(m->stopped) {
            // Stopped means input does not go to the Apple II
            if(v->debug_view && e.type == SDL_KEYDOWN || e.type == SDL_TEXTINPUT) {
                // A view must be open to receive input, and no modal dialog open
                if(!v->viewdlg_modal && v->ctx->active) {
                    // Active view is known by the name hash
                    switch(v->ctx->active->name) {
                        case 0XE5EA0BC3:    // CPU view
                            viewcpu_process_event(m, &e);
                            break;

                        case 0X81101438:    // Disassembly view
                            viewdbg_process_event(m, &e);
                            break;

                        case 0XB9A77E29:    // Memory view
                            viewmem_process_event(m, &e, 0);
                            break;

                        case 0X97446A22:    // Miscellaneous view
                            viewmem_process_event(m, &e, 1);
                            break;
                    }
                }
            }
        } else {
            // Running means input goes to Apple 2
            viewapl2_process_event(m, &e);
        }

        if(v->debug_view || force_update) {
            if((m->stopped && !m->step)) {
                viewport_show(m);
                viewport_update(m);
            }
            // The begin is needed to clear out keys that were handled
            // The end is called for good measure
            nk_input_end(v->ctx);
            nk_input_begin(v->ctx);
        }
    }
    nk_input_end(v->ctx);
    return ret;
}

void viewport_show(APPLE2 *m) {
    VIEWPORT *v = m->viewport;
    if(!v) {
        return;
    }

    if(v->viewmem_show) {
        viewmem_show(m);
    }

    // For some reason, if I do this first, hovering the mouse
    // obver the window will lock up the app till I hover a different
    // window, therefor this is now second and all seems well
    if(v->viewcpu_show) {
        viewcpu_show(m);
    }

    if(v->viewmisc_show) {
        viewmisc_show(m);
    }

    if(v->viewdbg_show) {
        viewdbg_show(m);
    }

    if(v->show_help) {
        viewport_show_help(m);
    }
}

void viewport_show_help(APPLE2 *m) {
    VIEWPORT *v = m->viewport;
    struct nk_context *ctx = v->ctx;
    SDL_Rect *r = &v->full_window_rect;
    if(nk_begin(ctx, "Help", nk_rect(r->x, r->y, r->w, r->h), NK_WINDOW_NO_SCROLLBAR)) {
        nk_layout_row_dynamic(ctx, 30, 1);
        nk_label_colored(ctx, "Apple ][+ emulator by Stefan Wessels, 2024.", NK_TEXT_ALIGN_CENTERED | NK_TEXT_ALIGN_MIDDLE, color_help_master);
        nk_layout_row_dynamic(ctx, 30, 1);
        nk_label_colored(ctx, "While emulation is running:", NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_MIDDLE, color_help_notice);
        nk_layout_row_dynamic(ctx, 20, 1);
        nk_label(ctx, "All keys go to the emulated machine, except for the function keys.", NK_TEXT_ALIGN_LEFT);
        nk_label_colored(ctx, "Function keys always go to the emulator.", NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_MIDDLE, color_help_key_heading);
        nk_layout_row_dynamic(ctx, 13, 2);
        nk_label(ctx, "F1  - Help.", NK_TEXT_ALIGN_LEFT);
        nk_label(ctx, "F9  - Set a breakpoint at the cursor PC.", NK_TEXT_ALIGN_LEFT);
        nk_label(ctx, "F2  - Show / Hide debugger windows.", NK_TEXT_ALIGN_LEFT);
        nk_label(ctx, "F10 - Step over - Single step but not into a JSR call.", NK_TEXT_ALIGN_LEFT);
        nk_label(ctx, "F3  - Toggle emulation speed between 1 MHZ and as fast as possible.", NK_TEXT_ALIGN_LEFT);
        nk_label(ctx, "F11 - Step into - Single step, even into a JSR call.", NK_TEXT_ALIGN_LEFT);
        nk_label(ctx, "F5  - Run (Go) when emulation is stopped.", NK_TEXT_ALIGN_LEFT);
        nk_label(ctx, "F11 + Shift - Step out - Step past RTS at this calling level.", NK_TEXT_ALIGN_LEFT);
        nk_label(ctx, "F6  - Set Program Counter (PC) to cursor PC.", NK_TEXT_ALIGN_LEFT);
        nk_label(ctx, "F12 - Switch display between color and monochrome.", NK_TEXT_ALIGN_LEFT);
        nk_layout_row_dynamic(ctx, 30, 1);
        nk_label_colored(ctx, "While emulation is stopped:", NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_MIDDLE, color_help_notice);
        nk_layout_row_dynamic(ctx, 13, 1);
        nk_label(ctx, "If the debug view is visible (F2) keys go to the debug window over which the mouse is hovered.", NK_TEXT_ALIGN_LEFT);
        nk_layout_row_dynamic(ctx, 30, 1);
        nk_label_colored(ctx, "CPU Window", NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_MIDDLE, color_help_heading);
        nk_layout_row_dynamic(ctx, 13, 1);
        nk_label(ctx, "Click into a box to edit, i.e. PC, SP, a register or flag and change the value.  Press ENTER to make the change effective.", NK_TEXT_ALIGN_LEFT);
        nk_layout_row_dynamic(ctx, 30, 1);
        nk_label_colored(ctx, "Disassembly window", NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_MIDDLE, color_help_heading);
        nk_layout_row_dynamic(ctx, 13, 2);
        nk_label(ctx, "CTRL + g - Set cursor PC to address.", NK_TEXT_ALIGN_LEFT);
        nk_label(ctx, "CURSOR UP/DOWN - Move the cursor PC by a line.", NK_TEXT_ALIGN_LEFT);
        nk_label(ctx, "CTRL + p - Set Apple ][+ PC to the cursor PC .", NK_TEXT_ALIGN_LEFT);
        nk_label(ctx, "PAGE UP/DOWN   - Move the cursor PC by a page.", NK_TEXT_ALIGN_LEFT);
        nk_label(ctx, "TAB    - Toggle symbol display (4 possible states).", NK_TEXT_ALIGN_LEFT);
        nk_layout_row_dynamic(ctx, 30, 1);
        nk_label_colored(ctx, "Memory window", NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_MIDDLE, color_help_heading);
        nk_layout_row_dynamic(ctx, 30, 1);
        nk_label_wrap(ctx, "Type HEX digits to edit the memory in HEX edit mode, or type any key when editing in ASCII mode.  The address that will be edited is shown at the bottom of the window.");
        nk_layout_row_dynamic(ctx, 13, 2);
        nk_label(ctx, "CRTL + g - Set view start to address.", NK_TEXT_ALIGN_LEFT);
        nk_label(ctx, "CTRL + s - Split the display (up to 16 times).", NK_TEXT_ALIGN_LEFT);
        nk_label(ctx, "CTRL + f - Find by ASCII or HEX.", NK_TEXT_ALIGN_LEFT);
        nk_label(ctx, "CTRL + j - Join a split window with the one below.", NK_TEXT_ALIGN_LEFT);
        nk_label(ctx, "CTRL + n - Find next (forward).", NK_TEXT_ALIGN_LEFT);
        nk_label(ctx, "CTRL + Shift + j - Join a split window with the one above.", NK_TEXT_ALIGN_LEFT);
        nk_label(ctx, "CTRL + Shift + n - Find previous (backwards).", NK_TEXT_ALIGN_LEFT);
        nk_label(ctx, "CTRL + t - Toggle editing HEX or ASCII at the cursor location.", NK_TEXT_ALIGN_LEFT);
        nk_label(ctx, "CURSOR UP/DOWN - Move the cursor a line up or down.", NK_TEXT_ALIGN_LEFT);
        nk_label(ctx, "ALT-0 through ALT-f - Select the virtual memory window (made with CTRL+S).", NK_TEXT_ALIGN_LEFT);
        nk_label(ctx, "PAGE   UP/DOWN - Move the cursor a page up or down.", NK_TEXT_ALIGN_LEFT);
        nk_spacer(ctx);
        nk_layout_row_dynamic(ctx, 30, 2);
        nk_label_colored(ctx, "Miscellaneous window", NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_MIDDLE, color_help_heading);
        nk_layout_row_dynamic(ctx, 13, 1);
        nk_label(ctx, "Note that this window updates while running, but changes can only be made while the emulation is stopped.", NK_TEXT_ALIGN_LEFT);
        nk_layout_row_dynamic(ctx, 20, 1);
        nk_label_colored(ctx, "Miscellaneous SmartPort", NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_MIDDLE, color_help_key_heading);
        nk_layout_row_dynamic(ctx, 40, 1);
        nk_label_wrap(ctx, "This section can be updated by using the apple2.ini file.  Add slots and drives using the [smartoprt] section and Slot=, drive0=, drive1= and Boot=.  Use the Slot.0 button to boot that disk.  Use Eject to eject the disk and Insert will bring up a file chooser to select a new disk.  No validation done on disk files selected.");
        nk_layout_row_dynamic(ctx, 20, 1);
        nk_label_colored(ctx, "Miscellaneous Debug", NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_MIDDLE, color_help_key_heading);
        nk_layout_row_dynamic(ctx, 28, 1);
        nk_label_wrap(ctx, "The status shows when a Step Over or Step out is actively running.  The Step Cycles show how many cycles the last step took (for profiling) and Total Cycles show all cycles since start (will wrap).");
        nk_layout_row_dynamic(ctx, 28, 1);
        nk_label_wrap(ctx, "Breakpoints show up at the address and can be disabled/enabled, cursor PC set to the breakpoint and the breakpoint can be cleared (deleted).  Clear all, shown when multiple breakpoints exist, will remove all breakpoints.");
        nk_layout_row_dynamic(ctx, 20, 1);
        nk_label_colored(ctx, "Miscellaneous Display", NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_MIDDLE, color_help_key_heading);
        nk_layout_row_dynamic(ctx, 28, 1);
        nk_label_wrap(ctx, "Shows the status of the display soft-switches.  Can be overridden to, for example, see the off-screen page where the application or game may be making changes if page flipping is used.  Turning Override off will reset back to the actual machine status.");
    }
    nk_end(ctx);
}

void viewport_shutdown(VIEWPORT *v) {
    // Shut nuklear down
    nk_sdl_shutdown();
    // Then the debugger
    viewdbg_shutdown(&v->debugger);
    // Then shut SDL down
    SDL_DestroyTexture(v->texture);
    SDL_DestroyWindow(v->window);
    SDL_CloseAudio();
    SDL_Quit();
}

void viewport_toggle_debug(APPLE2 *m) {
    m->viewport->debug_view ^= 1;
    if(m->viewport->debug_view) {
        m->viewport->target_rect.w *= 0.667f;
        m->viewport->target_rect.h *= 0.667f;
        SDL_SetRenderDrawColor(m->viewport->renderer, 0, 0, 0, 255);
        SDL_RenderClear(m->viewport->renderer);
    } else {
        m->viewport->target_rect = m->viewport->full_window_rect;
    }
}

void viewport_update(APPLE2 *m) {
    // Comit changes to the texture
    SDL_UpdateTexture(m->viewport->texture, NULL, m->viewport->surface->pixels, m->viewport->surface->pitch);
    // And updare renderer with the texture
    SDL_RenderCopy(m->viewport->renderer, m->viewport->texture, NULL, &m->viewport->target_rect);
    if(m->viewport->debug_view){
        // In debug view, the A2 screen is maube small black on black so outline it
        SDL_SetRenderDrawColor(m->viewport->renderer, 255, 255, 255, 255);
        SDL_RenderDrawRect(m->viewport->renderer, &m->viewport->target_rect);
    }
    nk_sdl_render(NK_ANTI_ALIASING_ON);
    SDL_RenderPresent(m->viewport->renderer);
}
