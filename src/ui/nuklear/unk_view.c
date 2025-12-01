// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "unk_lib.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:5287)  // different enum types
#endif

#define NK_IMPLEMENTATION
#include "nuklear.h"
#define NK_SDL_RENDERER_IMPLEMENTATION
#include "nuklrsdl.h"

#include "unk_leds.h"

#ifdef _MSC_VER
#pragma warning(pop)
#endif

const UI_OPS unk_ops = {
    .disk_read_led     = unk_disk_read,
    .disk_write_led    = unk_disk_write,
    .process_events    = unk_process_events,
    .ptrig             = unk_joy_ptrig,
    .read_button       = unk_joy_read_button,
    .read_axis         = unk_joy_read_axis,
    .speaker_toggle    = unk_audio_speaker_toggle,
    .speaker_on_cycles = unk_audio_speaker_on_cycles,
    .render            = unk_render_frame,
    .set_runtime       = unk_set_runtime,
    .set_shadow_flags  = unk_set_shadow_flags,
};

static inline SDL_Rect nk_to_sdl_rect(struct nk_rect r) {
    SDL_Rect s;
    s.x = (int)lroundf(r.x);
    s.y = (int)lroundf(r.y);
    s.w = (int)lroundf(r.w);
    s.h = (int)lroundf(r.h);
    return s;
}

static inline struct nk_rect sdl_to_nk_rect(struct SDL_Rect r) {
    struct nk_rect n;
    n.x = (float)r.x;
    n.y = (float)r.y;
    n.w = (float)r.w;
    n.h = (float)r.h;
    return n;
}

static inline struct nk_rect nk_rect_fit_4x3_center(struct nk_rect r) {
    if(r.w <= 0.0f || r.h <= 0.0f)
        return (struct nk_rect) {
        r.x, r.y, 0.0f, 0.0f
    };

    const float target = 4.0f / 3.0f;
    const float rw = r.w, rh = r.h;

    float w, h;
    if((rw / rh) >= target) {
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
    return (struct nk_rect) {
        x, y, w, h
    };
}

// returns changes to state when enabled, otherwise state
int nk_option_label_disabled(struct nk_context *ctx, const char *label, int state, int disabled) {
    struct nk_style_toggle saved = ctx->style.option;

    if(disabled) {
        struct nk_style_toggle t = saved;
        t.normal        = nk_style_item_color(nk_rgba(70, 70, 70, 255));
        t.hover         = t.normal;
        t.active        = t.normal;
        t.cursor_normal = nk_style_item_color(nk_rgba(110, 110, 110, 255));
        t.cursor_hover  = t.cursor_normal;
        t.text_normal   = nk_rgba(150, 150, 150, 255);
        ctx->style.option = t;
    }

    // Draw and process
    int new_state = nk_option_label(ctx, label, state);

    // Restore style
    ctx->style.option = saved;

    // Ignore processed result if disabled
    return disabled ? state : new_state;
}

void nk_custom_scrollbarv(struct nk_context *ctx, struct nk_rect sbar, int total_rows, int rows_visible, int *top_row, int *dragging, float *grab_offset) {
    // Visuals
    struct nk_command_buffer *out = nk_window_get_canvas(ctx);
    float pad = 2.0f;
    struct nk_rect track = nk_rect(sbar.x + pad, sbar.y + pad,
                                   sbar.w - 2 * pad, sbar.h - 2 * pad);

    // Fixed thumb height (virtual scrollbar)
    float thumb_h = 16.0f;
    float range_px = NK_MAX(1.0f, track.h - thumb_h);

    // Map top_row -> thumb_y
    float t = (float)(*top_row) / (float)(total_rows);    // 0..1
    float thumb_y = track.y + t * range_px;
    struct nk_rect thumb = nk_rect(track.x, thumb_y, track.w, thumb_h);

    // Draw track
    nk_fill_rect(out, track, 2.0f, nk_rgb(60, 60, 60));
    // Draw thumb (active color)
    nk_fill_rect(out, thumb, 2.0f, nk_input_is_mouse_hovering_rect(&ctx->input, thumb) || *dragging ? nk_rgb(160, 160, 160) : nk_rgb(120, 120, 120));

    // Mouse logic
    const struct nk_mouse *mouse = &ctx->input.mouse;

    // Begin drag if mouse pressed on thumb
    if(nk_input_is_mouse_hovering_rect(&ctx->input, thumb) && nk_input_is_mouse_down(&ctx->input, NK_BUTTON_LEFT) && !*dragging) {
        *dragging = 1;
        *grab_offset = mouse->pos.y - thumb.y;
    }

    // End drag on release (anywhere)
    if(!nk_input_is_mouse_down(&ctx->input, NK_BUTTON_LEFT)) {
        *dragging = 0;
    }

    // Dragging: update top_row by mapping mouse to track
    if(*dragging) {
        float y = mouse->pos.y - *grab_offset;
        // clamp to track
        if(y < track.y) {
            y = track.y;
        }
        if(y > track.y + range_px) {
            y = track.y + range_px;
        }
        float new_t = (range_px > 0) ? (y - track.y) / range_px : 0.0f;
        int new_top = (int)NK_CLAMP(0.0f, new_t * total_rows, (float)(total_rows - 1));
        *top_row = new_top;
    }

    // Click on track (page jump)
    if(nk_input_is_mouse_hovering_rect(&ctx->input, track) &&
            nk_input_is_mouse_pressed(&ctx->input, NK_BUTTON_LEFT) &&
            !nk_input_is_mouse_hovering_rect(&ctx->input, thumb)) {
        // Jump by a page toward click
        if(mouse->pos.y < thumb.y) {
            *top_row = (*top_row - rows_visible + total_rows) % total_rows;
        } else {
            *top_row = (*top_row + rows_visible) % total_rows;
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

void unk_config_ui(UNK *v, INI_STORE *ini_store) {
    // Display LEDs
    const char *val = ini_get(ini_store, "state", "disk_leds");
    if(val) {
        int state = 0;
        sscanf(val, "%d", &state);
        if(stricmp(val, "on") == 0 || state == 1) {
            v->show_leds = 1;
        }
    }
}

int unk_init(UNK *v, int model, INI_STORE *ini_store) {
    unk_config_ui(v, ini_store);
    v->sdl_os_rect = v->target_rect;        // Remember the size

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
    v->window = SDL_CreateWindow("Apple ][+ Emulator", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, v->target_rect.w, v->target_rect.h, SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN);
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

    v->surface_wide = SDL_CreateRGBSurfaceWithFormat(0, 80 * (model ? 7 : 8), 24 * 8, 32, SDL_PIXELFORMAT_ARGB8888);
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
    // Create the green led for showing disk read activity
    SDL_RWops *rw = SDL_RWFromConstMem(led_green, (int)led_green_len);
    if(!rw) {
        goto error;
    }
    SDL_Surface *surf = IMG_Load_RW(rw, 1);
    if(!surf) {
        goto error;
    }
    v->greenLED = load_png_texture_from_ram(v->renderer, led_green, led_green_len);
    v->redLED = load_png_texture_from_ram(v->renderer, led_red, led_red_len);
    if(!v->greenLED || !v->redLED) {
        goto error;
    }

    if(A2_OK != unk_dasm_init(&v->viewdasm)) {
        goto error;
    }

    if(A2_OK != unk_mem_init(&v->viewmem)) {
        goto error;
    }

    if(A2_OK != unk_audio_speaker_init(&v->viewspeaker, CPU_FREQUENCY, 48000, 2, 40.0f, 256)) {
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

    // // See if there's a game controller
    // v->num_controllers = 0;
    // for(int i = 0; i < SDL_NumJoysticks() && v->num_controllers < 2; i++) {
    //     if(!SDL_IsGameController(i)) {
    //         continue;
    //     }
    //     v->game_controller[v->num_controllers] = SDL_GameControllerOpen(i);
    //     if(v->game_controller[v->num_controllers]) {
    //         v->num_controllers++;
    //     }
    // }

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
    unk_layout_init_apple2(&v->layout, v->nk_os_rect, v->target_rect.w * 0.60f, v->target_rect.h * 0.60f, /*mem ratio*/ 0.456f);

    // Set up a table to speed up rendering HGR
    unk_apl2_init_color_table(v);

    // Enable text input since it handles the keys better
    SDL_StartTextInput();

    return A2_OK;

error:
    SDL_Quit();
    return A2_ERR;
}

int unk_process_events(UI *ui, APPLE2 *m) {
    UNK *v = (UNK *)ui->user;
    RUNTIME *rt = v->rt;
    v->m = m;
    SDL_Event e;
    int ret = 0;

    nk_input_begin(v->ctx);

    while(SDL_PollEvent(&e) != 0) {
        if(e.type == SDL_QUIT) {
            ret = 1;
            break;
        }

        if(e.type == SDL_WINDOWEVENT) {
            int update = 1;
            switch(e.window.event) {
                case SDL_WINDOWEVENT_SIZE_CHANGED:
                    v->sdl_os_rect.w = e.window.data1;
                    v->sdl_os_rect.h = e.window.data2;
                    break;

                // SQW I don't think this added value but before I take it out and forget
                // I'll just comment it out till I test this more...
                // case SDL_WINDOWEVENT_RESIZED:
                //     SDL_GetWindowSize(v->window, &v->sdl_os_rect.w, &v->sdl_os_rect.h);
                //     break;

                case SDL_WINDOWEVENT_SHOWN:
                    break;

                default:
                    update = 0;
                    break;
            }

            if(update) {
                LAYOUT *l = &v->layout;
                v->nk_os_rect = sdl_to_nk_rect(v->sdl_os_rect);
                unk_layout_on_window_resize(l, v->nk_os_rect, &v->lim);
                unk_layout_compute(l, v->nk_os_rect, &v->lim);
                v->target_rect = nk_to_sdl_rect(nk_rect_fit_4x3_center(l->apple2));
                unk_mem_resize_view(v);
                unk_dasm_resize_view(v);
                v->clear_a2_view = 1;
            }
        }

        nk_sdl_handle_event(&e);

        // Function keys all go to the disassembly view no matter what is active
        if(e.type == SDL_KEYDOWN && e.key.keysym.sym >= SDLK_F1 && e.key.keysym.sym <= SDLK_F12) {
            unk_dasm_process_event(v, &e);
        } else if(rt->run) {
            // Running means input goes to Apple 2
            unk_apl2_process_event(v, &e);
        } else {
            // Not running - goes to the active view
            if(v->debug_view && e.type == SDL_KEYDOWN || e.type == SDL_TEXTINPUT) {
                // A view must be open to receive input, and no modal dialog open
                if(!v->unk_dlg_modal && v->ctx->active) {
                    // Active view is known by the name hash
                    switch(v->ctx->active->name) {
                        case VIEWCPU_NAME_HASH:                         // CPU view (no events)
                            break;

                        case VIEWDASM_NAME_HASH:                        // Disassembly view
                            unk_dasm_process_event(v, &e);
                            break;

                        case VIEWMEM_NAME_HASH:                         // Memory view
                            unk_mem_process_event(v, &e, 0);
                            break;

                        case VIEWMISC_NAME_HASH:                        // Miscellaneous view (no events)
                            break;
                    }
                }
            }
        }
    }

    // Get joystick states once per event loop
    for(int i = 0; i < v->num_controllers; i++) {
        if(v->game_controller[i]) {
            v->axis_left[i][0] = (32768 + SDL_GameControllerGetAxis(v->game_controller[i], SDL_CONTROLLER_AXIS_LEFTX)) >> 8;
            v->axis_left[i][1] = (32768 + SDL_GameControllerGetAxis(v->game_controller[i], SDL_CONTROLLER_AXIS_LEFTY)) >> 8;
            v->button[i][0] = SDL_GameControllerGetButton(v->game_controller[i], SDL_CONTROLLER_BUTTON_A);
            v->button[i][1] = SDL_GameControllerGetButton(v->game_controller[i], SDL_CONTROLLER_BUTTON_B);
            v->button[i][2] = SDL_GameControllerGetButton(v->game_controller[i], SDL_CONTROLLER_BUTTON_X);
        }
    }

    nk_input_end(v->ctx);

    return ret;
}

void unk_shutdown(UNK *v) {
    // Shut nuklear down
    nk_sdl_shutdown();

    // Then the views
    unk_dasm_shutdown(&v->viewdasm);
    unk_audio_speaker_shutdown(&v->viewspeaker);

    // Then shut SDL down
    SDL_DestroyTexture(v->texture);
    SDL_DestroyWindow(v->window);
    SDL_CloseAudio();
    SDL_Quit();
}

void unk_toggle_debug(UNK *v) {
    v->debug_view ^= 1;
    if(v->debug_view) {
        v->draw_rect = &v->target_rect;
        v->dirty_view = 1;
    } else {
        v->draw_rect = NULL;
    }
}

void unk_present(UNK *v) {
    RUNTIME *rt = v->rt;
    APPLE2 *m = v->m;
    // Commit changes to the texture And update renderer with the texture
    if(v->clear_a2_view) {
        // Clear the background that prev may have contained the texture to gray
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

    // Render after nuklear so that the LEDs render over any nuklear windows
    if(v->show_leds) {
        if(v->disk_activity_read) {
            SDL_Rect led = {v->sdl_os_rect.w - 16, v->sdl_os_rect.h - 16, 16, 16};
            SDL_RenderCopy(v->renderer, v->greenLED, NULL, &led);
            v->disk_activity_read = 0;
        }
        if(v->disk_activity_write) {
            SDL_Rect led = {v->sdl_os_rect.w - 32, v->sdl_os_rect.h - 16, 16, 16};
            SDL_RenderCopy(v->renderer, v->redLED, NULL, &led);
            v->disk_activity_write = 0;
        }
    }

    // Show the window title after calculating a moving average MHz
    if(1) {
        char sdl_window_title[80];
        if(rt->run) {
            uint64_t freq = SDL_GetPerformanceFrequency();
            uint64_t now_ticks = SDL_GetPerformanceCounter();
            uint64_t dt_ticks = now_ticks - v->prev_ticks;
            uint64_t dc = v->m->cpu.cycles - v->prev_cycles;
            v->prev_ticks  = now_ticks;
            v->prev_cycles = v->m->cpu.cycles;

            double mhz = (double)(dc * freq) / ((double)dt_ticks * 1e6);
            v->mhz_moving_average = 0.1 * mhz + (1 - 0.1) * v->mhz_moving_average;

            double fps = (1.0 / (double)dt_ticks) * freq;
            v->fps_moving_average  = 0.1 * fps + (1 - 0.1) * v->fps_moving_average;
        }

        sprintf(sdl_window_title, "Apple %s Emulator %s : %2.1f MHz @ %2.1f FPS", v->m->model ? "//e" : "][+", rt->run ? "[running]" : "[stopped]", v->mhz_moving_average, v->fps_moving_average);
        SDL_SetWindowTitle(v->window, sdl_window_title);
    }

    // Finally show the user
    SDL_RenderPresent(v->renderer);
}

void unk_render_frame(UI *ui, APPLE2 *m, int dirty) {
    // get the view
    UNK *v = (UNK *)ui->user;
    // And in the view, set the APPLE2 that the view now services
    v->m = m;
    if(v->show_help) {
        unk_show_help(v);
    } else {
        unk_apl2_screen_apple2(v);
        if(v->debug_view) {
            if(!v->viewmem.dragging && unk_layout_handle_drag(&v->layout, &v->ctx->input, v->nk_os_rect, &v->lim)) {
                struct nk_rect tr = nk_rect_fit_4x3_center(v->layout.apple2);
                v->target_rect = nk_to_sdl_rect(tr);
                unk_mem_resize_view(v);
                unk_dasm_resize_view(v);
                v->clear_a2_view = 1;
            }

            dirty |= v->dirty_view;
            unk_cpu_show(v, dirty);
            unk_dasm_show(v, dirty);
            unk_misc_show(v);
            unk_mem_show(v);
            v->dirty_view = 0;
        }
    }
    unk_present(v);
}

void unk_set_runtime(UI *ui, RUNTIME *rt) {
    UNK *v = (UNK *)ui->user;
    // Set the runtime that the view sits on top of
    v->rt = rt;
}

void unk_set_shadow_flags(UI *ui, uint32_t shadow_flags) {
    UNK *v = (UNK *)ui->user;
    if(!v->display_override) {
        v->shadow_flags.u32 = shadow_flags;
    }
}

void unk_disk_read(UI *ui) {
    UNK *v = (UNK *)ui->user;
    v->disk_activity_read = 1;
}

void unk_disk_write(UI *ui) {
    UNK *v = (UNK *)ui->user;
    v->disk_activity_write = 1;
}
