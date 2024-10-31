// Apple ][+ emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#include "header.h"

#define NK_IMPLEMENTATION
#include "nuklear.h"
#define NK_SDL_RENDERER_IMPLEMENTATION
#include "nuklrsdl.h"

#define color_active_win        nk_rgb( 50,100, 50)

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
    nk_sdl_font_stash_begin(&atlas);
    v->font = nk_font_atlas_add_default(atlas, 13, &config);
    nk_sdl_font_stash_end();
    nk_style_set_font(v->ctx, &v->font->handle);
    v->font_height = v->ctx->style.font->height;
    v->font_width = v->ctx->style.font->width(v->ctx->style.font->userdata, v->font_height, "W", 1);    
    v->ctx->style.window.header.active.data.color = color_active_win;

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
        int force_update = 0;
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
