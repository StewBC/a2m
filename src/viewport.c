// Apple ][+ and //e Emhanced emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#include "header.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:5287)  // different enum types
#endif

#define NK_IMPLEMENTATION
#include "nuklear.h"
#define NK_SDL_RENDERER_IMPLEMENTATION
#include "nuklrsdl.h"

#include "leds.h"

#ifdef _MSC_VER
#pragma warning(pop)
#endif

// These values are picked up in nuklrsdl.h
float sdl_x_scale, sdl_y_scale;

static inline SDL_Rect nk_to_sdl_rect(struct nk_rect r)
{
    SDL_Rect s;
    s.x = (int)lroundf(r.x);
    s.y = (int)lroundf(r.y);
    s.w = (int)lroundf(r.w);
    s.h = (int)lroundf(r.h);
    return s;
}

static inline struct nk_rect sdl_to_nk_rect(struct SDL_Rect r)
{
    struct nk_rect n;
    n.x = (float)r.x;
    n.y = (float)r.y;
    n.w = (float)r.w;
    n.h = (float)r.h;
    return n;
}

static inline struct nk_rect nk_rect_fit_4x3_center(struct nk_rect r)
{
    if (r.w <= 0.0f || r.h <= 0.0f)
        return (struct nk_rect){ r.x, r.y, 0.0f, 0.0f };

    const float target = 4.0f / 3.0f;
    const float rw = r.w, rh = r.h;

    float w, h;
    if ((rw / rh) >= target) {
        // Height-limited: use full height, shrink width to 4:3
        h = rh;
        w = rh * target;
    } else {
        // Width-limited: use full width, shrink height to 4:3
        w = rw;
        h = rw / target;
    }

    const float x = r.x + (rw - w) * 0.5f;
    const float y = r.y + (rh - h) * 0.5f;
    return (struct nk_rect){ x, y, w, h };
}

// returns changes to state when enabled, otherwise state
int nk_option_label_disabled(struct nk_context *ctx, const char *label, int state, int disabled) {
    struct nk_style_toggle saved = ctx->style.option;

    if (disabled) {
        struct nk_style_toggle t = saved;
        t.normal        = nk_style_item_color(nk_rgba(70,70,70,255));
        t.hover         = t.normal;
        t.active        = t.normal;
        t.cursor_normal = nk_style_item_color(nk_rgba(110,110,110,255));
        t.cursor_hover  = t.cursor_normal;
        t.text_normal   = nk_rgba(150,150,150,255);
        ctx->style.option = t;
    }

    // Draw and process
    int new_state = nk_option_label(ctx, label, state);

    // Restore style
    ctx->style.option = saved;

    // Ignore processed result if disabled
    return disabled ? state : new_state;
}

void viewport_config(APPLE2 *m) {
    INI_SECTION *s;
    VIEWPORT *v = (VIEWPORT *)m->viewport;
    s = ini_find_section(&m->ini_store, "display");
    if(s) {
        for(int i = 0; i < s->kv.items; i++) {
            INI_KV *kv = ARRAY_GET(&s->kv, INI_KV, i);
            const char *key = kv->key;
            const char *val = kv->val;
            if(0 == stricmp(key, "scale")) {
                float scale = 1.0f;
                sscanf(val, "%f", &scale);
                if(scale > 0.0f) {
                    v->display_scale = scale;
                }
            } else if(0 == stricmp(key, "disk_leds")) {
                int state = 0;
                sscanf(val, "%d", &state);
                if(state == 1) {
                    v->show_leds = 1;
                }
            }
        }
    }
}

static SDL_Texture *load_png_texture_from_ram(SDL_Renderer *r, uint8_t *image, int image_size) {
    SDL_RWops *rw = SDL_RWFromConstMem(image, image_size);
    if(!rw) {
        return NULL;
    }

    SDL_Surface *surf = IMG_Load_RW(rw, 1);
    if(!surf) {
        return NULL;
    }

    SDL_Texture *tex = SDL_CreateTextureFromSurface(r, surf);
    SDL_FreeSurface(surf);

    return tex;
}

int viewport_init(APPLE2 *m, int w, int h) {

    VIEWPORT *v = m->viewport;
    if(!v) {
        return A2_OK;
    }

    // Clear the whole viewport struct to 0's
    memset(v, 0, sizeof(VIEWPORT));

    v->display_scale = 1.0f;

    // Configure display_scale from the ini file
    viewport_config(m);

    // Scale the window, and set the SDL render scale accordingly
    w *= v->display_scale;
    h *= v->display_scale;
    sdl_x_scale = sdl_y_scale = v->display_scale;

    // Set the window width and height
    v->target_rect.w = w;                   // Width of the window
    v->target_rect.h = h;                   // Height of the window
    v->sdl_os_rect = v->target_rect;        // And remember the setting

    // Initialize SDL with video and audio
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) < 0) {
        printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        goto error;
    }
    if((IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG) == 0) {
        printf("SDL image could not initialize! SDL_Error: %s\n", IMG_GetError());
        goto error;
    }
    // Create window
    v->window = SDL_CreateWindow("Apple ][+ Emulator", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, w, h, SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN);
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
    v->surface = SDL_CreateRGBSurfaceWithFormat(0, 40 * 7, 24 * 8, 32, SDL_PIXELFORMAT_ARGB8888);
    if(v->surface == NULL) {
        printf("Surface could not be created! SDL_Error: %s\n", SDL_GetError());
        goto error;
    }
    SDL_FillRect(v->surface, NULL, SDL_MapRGB(v->surface->format, 0, 0, 0));

    v->surface_wide = SDL_CreateRGBSurfaceWithFormat(0, 80 * (m->model ? 7 : 8), 24 * 8, 32, SDL_PIXELFORMAT_ARGB8888);
    if(v->surface_wide == NULL) {
        printf("Surface640 could not be created! SDL_Error: %s\n", SDL_GetError());
        goto error;
    }
    SDL_FillRect(v->surface_wide, NULL, SDL_MapRGB(v->surface_wide->format, 0, 0, 0));

    // Create texture for pixel rendering
    v->texture = SDL_CreateTextureFromSurface(v->renderer, v->surface);
    if(v->texture == NULL) {
        printf("Texture could not be created! SDL_Error: %s\n", SDL_GetError());
        goto error;
    }
    // Create texture for pixel rendering
    v->texture_wide = SDL_CreateTextureFromSurface(v->renderer, v->surface_wide);
    if(v->texture_wide == NULL) {
        printf("Texture could not be created! SDL_Error: %s\n", SDL_GetError());
        goto error;
    }
    // Create the green led for shoing disk read activity
    SDL_RWops *rw = SDL_RWFromConstMem(green_led, (int)green_led_len);
    if(!rw) {
        goto error;
    }
    SDL_Surface *surf = IMG_Load_RW(rw, 1);
    if(!surf) {
        goto error;
    }
    v->greenLED = load_png_texture_from_ram(v->renderer, green_led, green_led_len);
    v->redLED = load_png_texture_from_ram(v->renderer, red_led, red_led_len);
    if(!v->greenLED || !v->redLED) {
        goto error;
    }

    if(A2_OK != viewdbg_init(m, CODE_LINES_COUNT)) {
        goto error;
    }

    if(A2_OK != viewmem_init(&v->memshow)) {
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

    // See if there's a game controller
    v->num_controllers = 0;
    for(int i = 0; i < SDL_NumJoysticks() && v->num_controllers < 2; i++) {
        if (!SDL_IsGameController(i)) {
            continue;
        }
        v->game_controller[v->num_controllers] = SDL_GameControllerOpen(i);
        if(v->game_controller[v->num_controllers]) {
            v->num_controllers++;
        }
    }
	
    v->lim.cpu_h_px = 90;
    v->lim.min_a2_w_px = 320;
    v->lim.min_right_w_px = 240;
    v->lim.min_dis_h_px = 80;
    v->lim.min_bot_h_px = 120;
    v->lim.min_mem_w_px = 200;
    v->lim.min_misc_w_px = 160;
    v->lim.gutter_px = 8;
    v->lim.corner_px = 16;

    v->nk_os_rect = sdl_to_nk_rect(v->sdl_os_rect);
    layout_init_apple2(&v->layout, v->nk_os_rect, v->target_rect.w * 0.60f, v->target_rect.h * 0.60f, /*mem ratio*/ 0.456f);
    return A2_OK;

error:
    SDL_Quit();
    return A2_ERR;
}

int viewport_process_events(APPLE2 *m) {
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
    while(SDL_PollEvent(&e) != 0 || (m->stopped && !m->step)) {
        if(e.type == SDL_QUIT) {
            ret = 1;
            break;
        }

        if (e.type == SDL_WINDOWEVENT) {
            int update = 1;
            switch (e.window.event) {
            case SDL_WINDOWEVENT_SIZE_CHANGED:   // fires continuously while resizing, or programmatic changes
                v->sdl_os_rect.w = e.window.data1;  // defined here
                v->sdl_os_rect.h = e.window.data2;  // defined here
                break;

            case SDL_WINDOWEVENT_RESIZED:        // often once at end of user drag
                SDL_GetWindowSize(v->window, &v->sdl_os_rect.w, &v->sdl_os_rect.h);
                break;

            // case SDL_WINDOWEVENT_MINIMIZED:
            //     // size may effectively be 0; ignore or special-case
            //     break;

            // case SDL_WINDOWEVENT_EXPOSED:
            // case SDL_WINDOWEVENT_RESTORED:
            // case SDL_WINDOWEVENT_MAXIMIZED:
            case SDL_WINDOWEVENT_SHOWN:
            //     // data1/data2 are NOT defined for these; if you need size, query it:
            //     // SDL_GetWindowSize(v->window, &v->sdl_os_rect.w, &v->sdl_os_rect.h);
                break;

            default:
                update = 0;
                break;
            }

            if(update) {
                LAYOUT *l = &v->layout;
                v->nk_os_rect = sdl_to_nk_rect(v->sdl_os_rect);
                layout_on_window_resize(l, v->nk_os_rect, &v->lim);
                layout_compute(l, v->nk_os_rect, &v->lim);
                v->target_rect = nk_to_sdl_rect(nk_rect_fit_4x3_center(l->apple2));
                viewmem_resize_view(m);
                v->clear_a2_view = 1;
            }
        }      

        nk_sdl_handle_event(&e);

        // Function keys all go to the disassembly view no matter what is active
        if(e.type == SDL_KEYDOWN && e.key.keysym.sym >= SDLK_F1 && e.key.keysym.sym <= SDLK_F12) {
            viewdbg_process_event(m, &e);
        } else if(m->stopped) {
            // Stopped means input does not go to the Apple II
            if(v->debug_view && e.type == SDL_KEYDOWN || e.type == SDL_TEXTINPUT) {
                // A view must be open to receive input, and no modal dialog open
                if(!v->viewdlg_modal && v->ctx->active) {
                    // Active view is known by the name hash
                    switch(v->ctx->active->name) {
                        case 0XE5EA0BC3:                        // CPU view
                            viewcpu_process_event(m, &e);
                            break;

                        case 0X81101438:                        // Disassembly view
                            viewdbg_process_event(m, &e);
                            break;

                        case 0XB9A77E29:                        // Memory view
                            viewmem_process_event(m, &e, 0);
                            break;

                        case 0X97446A22:                        // Miscellaneous view
                            viewmem_process_event(m, &e, 1);
                            break;
                    }
                }
            }
        } else {
            // Running means input goes to Apple 2
            viewapl2_process_event(m, &e);
        }

        if((m->stopped && !m->step)) {
            viewport_show(m);
            viewport_update(m);
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
        // If the APPLE2 has no view attached, just return
        return;
    }

    if(v->show_help) {
        global_entry_length = 0;
        viewhelp_show(m);
        // If help is up, it covers the whole screen so don't draw anything else
        return;
    }

    if(!v->debug_view) {
        // If the debug view isn't open, don't draw anything, don't run the splitters, etc.
        return;
    }

	if(layout_handle_drag(&v->layout, &v->ctx->input, v->nk_os_rect, &v->lim)) {
        struct nk_rect tr = nk_rect_fit_4x3_center(v->layout.apple2);
        v->target_rect = nk_to_sdl_rect(tr);
        viewmem_resize_view(m);
        v->clear_a2_view = 1;
    }
    viewcpu_show(m);
    viewdbg_show(m);
    viewmem_show(m);
    viewmisc_show(m);
}

void viewport_shutdown(VIEWPORT *v) {
    if(!v) {
        return;
    }
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
    VIEWPORT *v = m->viewport;
    v->debug_view ^= 1;
    if(v->debug_view) {
        v->draw_rect = &v->target_rect;
        SDL_SetRenderDrawColor(v->renderer, 0, 0, 0, 255);
        SDL_RenderClear(v->renderer);
        viewcpu_update(m);
    } else {
        v->draw_rect = NULL;
    }
}

void viewport_update(APPLE2 *m) {
    // Commit changes to the texture
    // And update renderer with the texture
    VIEWPORT *v = m->viewport;
    if(!v) {
        return;
    }

    // Clear the background that prev may have contained the texture to gray
    if(v->clear_a2_view) {
        SDL_SetRenderDrawColor(v->renderer, 0x2d, 0x2d, 0x2d, 0xFF);
        SDL_Rect clear_rect = nk_to_sdl_rect(v->layout.apple2);
        SDL_RenderFillRect(v->renderer, &clear_rect);
        v->clear_a2_view = 0;
    }

    if(m->franklin80active || v->shadow_flags.b.col80set && (v->shadow_flags.b.dhires || v->shadow_flags.b.text)) {
        SDL_UpdateTexture(v->texture_wide, NULL, v->surface_wide->pixels, v->surface_wide->pitch);
        SDL_RenderCopy(v->renderer, v->texture_wide, NULL, v->draw_rect);
    } else {
        SDL_UpdateTexture(v->texture, NULL, v->surface->pixels, v->surface->pitch);
        SDL_RenderCopy(v->renderer, v->texture, NULL, v->draw_rect);
    }

    // if the help screen is open, or any debug windows are up, nuklear must be updated
    if(v->debug_view | v->show_help) {
        nk_sdl_render(NK_ANTI_ALIASING_ON);
    }

    // Add after nuklear so that the LEDs render over any nuklear windows
    if(v->show_leds) {
        if(m->disk_activity_read) {
            SDL_Rect led = {v->sdl_os_rect.w - 16, v->sdl_os_rect.h - 16, 16, 16};
            SDL_RenderCopy(v->renderer, v->greenLED, NULL, &led);
            m->disk_activity_read = 0;
        }
        if(m->disk_activity_write) {
            SDL_Rect led = {v->sdl_os_rect.w - 32, v->sdl_os_rect.h - 16, 16, 16};
            SDL_RenderCopy(v->renderer, v->redLED, NULL, &led);
            m->disk_activity_write = 0;
        }
    }

    // Finally show the user
    SDL_RenderPresent(v->renderer);

    // Show the window title after calculating a moving average MHz
    if(1) {
        char sdl_window_title[80];
        if(!m->stopped) {
            uint64_t freq = SDL_GetPerformanceFrequency();
            uint64_t now_ticks = SDL_GetPerformanceCounter();
            uint64_t dt_ticks = now_ticks - v->prev_ticks;
            uint64_t dc = m->cpu.cycles - v->prev_cycles;
            v->prev_ticks  = now_ticks;
            v->prev_cycles = m->cpu.cycles;

            double mhz = (double)(dc * freq) / ((double)dt_ticks * 1e6);
            v->mhz_moving_average = 0.1 * mhz + (1 - 0.1) * v->mhz_moving_average;

            double fps = (1.0 / (double)dt_ticks) * freq;
            v->fps_moving_average  = 0.1 * fps + (1 - 0.1) * v->fps_moving_average;
        }

        sprintf(sdl_window_title, "Apple %s Emulator %s : %2.1f MHz @ %2.1f FPS", m->model ? "//e" : "][+", m->stopped ? "[stopped] " : "[running]", v->mhz_moving_average, v->fps_moving_average);
        SDL_SetWindowTitle(v->window, sdl_window_title);
    }
}
