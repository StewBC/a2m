// Apple ][+ emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#include "header.h"

// Display variables
static int screen_mode[3] = {0,1,5};
static int display_mode = 0;
static int display_active_page = 0;
static int display_mixed = 0;
static int display_override = 0;
static int display_undo_change = 0;

// static const char *text_condition[] = {
//     "",
//     "PC",
//     "COUNT",
//     "REG_VALUE",
//     "MEM_VALUE,",
//     "ONE_TIME",
// };

void viewmisc_show(APPLE2* m) {
    static int last_state = 0;
    VIEWPORT *v = m->viewport;
    struct nk_context *ctx = v->ctx;
    DEBUGGER *d = &v->debugger;
    FILE_BROWSER *fb = &v->viewmisc.file_browser;

    int display_mode_setting = 0;
    int display_active_page_setting = 0;
    int display_mixed_setting = 0;
    int force_redraw = 0;

    struct nk_color ob = ctx->style.window.background;
    int x = 512;
    int w = m->viewport->full_window_rect.w - x;
    if(nk_begin(ctx, "Miscellaneous", nk_rect(x, m->viewport->target_rect.h, w, m->viewport->full_window_rect.h - m->viewport->target_rect.h),
        NK_WINDOW_SCROLL_AUTO_HIDE | NK_WINDOW_TITLE | NK_WINDOW_BORDER)) {
        // The Smartport
        if (nk_tree_push(ctx, NK_TREE_TAB, "SmartPort", NK_MAXIMIZED)) {
            SP_DEVICE *spd = m->sp_device;
            nk_layout_row_dynamic(ctx, 13, 1);
            nk_label(ctx, "Devices", NK_TEXT_LEFT);
            for(int i = 0; i < 8; i++) {
                if(spd[i].sp_active) {
                    for(int j = 0; j < 2; j++) {
                        nk_layout_row_begin(ctx, NK_DYNAMIC, 18, 5);
                        nk_layout_row_push(ctx, 0.08f);
                        if(!j) {
                            char label[4];
                            sprintf(label, "%d.%d", i,j);
                            if(nk_button_label(ctx, label)) {
                                m->cpu.pc = 0xc000 + i * 0x100;
                                m->stopped = 0;
                            }
                        } else {
                            nk_labelf(ctx, NK_TEXT_ALIGN_CENTERED | NK_TEXT_ALIGN_MIDDLE, "%d.%d", i,j);
                        }
                        nk_layout_row_push(ctx, 0.08f);
                        if(nk_button_label(ctx, "Eject")) {
                            util_file_discard(&spd[i].sp_files[j]);
                        }
                        nk_layout_row_push(ctx, 0.1f);
                        if(nk_button_label(ctx, "Insert")) {
                            if(!v->viewdlg_modal) {
                                fb->slot = i;
                                fb->device = j;
                                fb->dir_contents.items = 0;
                                v->viewdlg_modal = 1;
                                v->dlg_filebrowser = 1;
                            }
                        }
                        nk_layout_row_push(ctx, 0.74f);
                        if(spd[i].sp_files[j].is_file_open) {
                            nk_label(ctx, spd[i].sp_files[j].file_display_name, NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_MIDDLE);
                        }
                        nk_layout_row_end(ctx);
                    }
                }
            }
            nk_tree_pop(ctx);
        }

        // The Breakpoints tab
        if (nk_tree_push(ctx, NK_TREE_TAB, "Debugger", NK_MAXIMIZED)) {
            FLOWMANAGER *fm = &d->flowmanager;
            nk_layout_row_dynamic(ctx, 13, 1);
            nk_label(ctx, "Debug Status", NK_TEXT_LEFT);
            nk_layout_row_begin(ctx, NK_DYNAMIC, 60, 2);
            nk_layout_row_push(ctx, 0.40f);
            if(nk_group_begin(ctx, "run status group", NK_WINDOW_BORDER)) {
                nk_layout_row_begin(ctx, NK_DYNAMIC, 18, 2);
                nk_layout_row_push(ctx, 0.49f);
                nk_option_label(ctx, "Run to PC", fm->run_to_pc_set);
                nk_layout_row_push(ctx, 0.49f);
                nk_labelf(ctx, NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_MIDDLE, "%04X", fm->run_to_pc);
                nk_layout_row_end(ctx);
                nk_layout_row_dynamic(ctx, 18, 1);
                nk_option_label(ctx, "Step Out", fm->run_to_rts_set);
                nk_group_end(ctx);
            }
            nk_layout_row_push(ctx, 0.59999f);
            if(nk_group_begin(ctx, "cycles group", NK_WINDOW_BORDER)) {
                nk_layout_row_begin(ctx, NK_DYNAMIC, 18, 2);
                nk_layout_row_push(ctx, 0.49f);
                nk_label(ctx, "Step Cycles", NK_TEXT_LEFT | NK_TEXT_ALIGN_MIDDLE);
                nk_layout_row_push(ctx, 0.49f);
                nk_label(ctx, "Total Cycles", NK_TEXT_LEFT | NK_TEXT_ALIGN_MIDDLE);
                nk_layout_row_end(ctx);
                nk_layout_row_begin(ctx, NK_DYNAMIC, 18, 2);
                nk_layout_row_push(ctx, 0.49f);
                nk_labelf(ctx, NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_MIDDLE, "%zd", m->cpu.cycles - d->prev_stop_cycles);
                nk_layout_row_push(ctx, 0.49f);
                nk_labelf(ctx, NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_MIDDLE, "%zd", m->cpu.cycles);
                nk_layout_row_end(ctx);
                nk_group_end(ctx);
            }
            // nk_spacing(ctx, 1);

            // Now a list of active breakpoints
            
            if(fm->breakpoints.items) {
                // nk_layout_row_dynamic(ctx, 29*(fm->breakpoints.items + 1), 2);
                nk_layout_row_begin(ctx, NK_DYNAMIC, 29*(fm->breakpoints.items + 1), 2);
                nk_layout_row_push(ctx, 0.80f);
                if(nk_group_begin(ctx, "breakpoints group", 0)) {
                    for(int i=0; i < fm->breakpoints.items; i++) {
                        char label[32];
                        BREAKPOINT *bp = ARRAY_GET(&fm->breakpoints, BREAKPOINT, i);
                        if(bp->condition == CONDITION_PC) { 
                            nk_layout_row_begin(ctx, NK_DYNAMIC, 25, 4);
                            nk_layout_row_push(ctx, 0.10f);
                            nk_labelf(ctx, NK_TEXT_ALIGN_CENTERED | NK_TEXT_ALIGN_MIDDLE, "%04X", bp->pc);
                            nk_layout_row_push(ctx, 0.3f);
                            if(nk_button_label(ctx, bp->disabled ? "Enable" : "Disable")) {
                                bp->disabled ^= 1;
                            }
                            nk_layout_row_push(ctx, 0.30f);
                            if(nk_button_label(ctx, "View PC")) {
                                d->cursor_pc = bp->pc;
                            }
                            nk_layout_row_push(ctx, 0.3f);
                            if(nk_button_label(ctx, "Clear")) {
                                array_remove(&fm->breakpoints, bp);
                            }
                        }
                        nk_layout_row_end(ctx);
                    }
                    nk_group_end(ctx);
                    nk_layout_row_push(ctx, 0.19f);
                    if(nk_group_begin(ctx, "clear all group", 0)) {
                        if(fm->breakpoints.items >= 2) {
                            nk_layout_row_dynamic(ctx, 25, 1);
                            if(nk_button_label(ctx, "Clear All")) {
                                d->flowmanager.breakpoints.items = 0;
                            }
                        }
                        nk_group_end(ctx);
                    }
                }
                nk_layout_row_end(ctx);
            }
            nk_tree_pop(ctx);
        }

        // The Display tab
        if (nk_tree_push(ctx, NK_TREE_TAB, "Display", NK_MAXIMIZED)) {
            nk_layout_row_begin(ctx, NK_DYNAMIC, 50, 3);
            nk_layout_row_push(ctx, 0.40f);
            if (nk_group_begin(ctx, "", 0)) {
                nk_layout_row_static(ctx, 13, 100, 1);
                nk_label(ctx, "Display Mode", NK_TEXT_LEFT);            
                nk_layout_row_static(ctx, 13, 60, 3);
                display_mode_setting = nk_option_label(ctx, "Text", display_mode == 0) ? 0 : display_mode;
                display_mode_setting = nk_option_label(ctx, "Lores", display_mode_setting == 1) ? 1 : display_mode_setting;
                display_mode_setting = nk_option_label(ctx, "HGR", display_mode_setting == 2) ? 2 : display_mode_setting; 
                nk_group_end(ctx);
            }

            nk_layout_row_push(ctx, 0.30f);
            if (nk_group_begin(ctx, "", 0)) {
                nk_layout_row_static(ctx, 13, 80, 1);
                nk_label(ctx, "Mixed Mode", NK_TEXT_LEFT);            
                nk_layout_row_static(ctx, 13, 60, 2);
                display_mixed_setting = nk_option_label(ctx, "Off", display_mixed == 0) ? 0 : display_mixed;
                display_mixed_setting = nk_option_label(ctx, "On", display_mixed_setting == 1) ? 1 : display_mixed_setting;
                nk_group_end(ctx);
            }

            nk_layout_row_push(ctx, 0.30f);
            if (nk_group_begin(ctx, "", 0)) {
                nk_layout_row_static(ctx, 13, 100, 1);
                nk_label(ctx, "Display Page", NK_TEXT_LEFT);            
                nk_layout_row_static(ctx, 13, 60, 2);
                display_active_page_setting = nk_option_label(ctx, "Page 0", display_active_page == 0) ? 0 : display_active_page;
                display_active_page_setting = nk_option_label(ctx, "Page 1", display_active_page_setting == 1) ? 1 : display_active_page_setting;
                nk_group_end(ctx);
            }
            nk_layout_row_end(ctx);

            nk_layout_row_static(ctx, 13, 100, 1);
            nk_label(ctx, "Override", NK_TEXT_LEFT);            
            nk_layout_row_static(ctx, 13, 60, 2);
            display_override = nk_option_label(ctx, "No", display_override == 0) ? 0 : display_override;
            display_override = nk_option_label(ctx, "Yes", display_override == 1) ? 1 : display_override;

            if(display_override) {
                // Only redraw the screen, when stopped, if a change is made
                if(display_mode != display_mode_setting || display_mixed != display_mixed_setting || display_active_page != display_active_page_setting) {
                    display_mode = display_mode_setting;
                    display_mixed = display_mixed_setting;
                    display_active_page = display_active_page_setting;
                    if(m->stopped) {
                        force_redraw = 1;
                        display_undo_change = 1;
                    };
                }
            } else {
                // Get the settings from the "hardware"
                display_mode = m->screen_mode & 1 ? m->screen_mode & 4 ? 2 : 1 : 0; 
                display_active_page = m->active_page != 0;
                display_mixed = display_mode && m->screen_mode & 2;
                if(display_undo_change) {
                    // If the settings were overridden but is now driven from the hardware
                    // a redraw is needed (can only happen when stopped since UI only
                    // works when stopped)
                    display_undo_change = 0;
                    force_redraw = 1;
                }
            }
            // Set the view draw settings based on whatever is active
            v->screen_mode_shadow = screen_mode[display_mode] | (display_mixed << 1);
            v->active_page_shadow = display_active_page;
            if(force_redraw) {
                // stopped and a change was made, so update the Apple II display
                viewapl2_screen_apple2(m);
            }
            nk_tree_pop(ctx);
        }
    }
    nk_end(ctx);
    ctx->style.window.background = ob;
    if(v->dlg_filebrowser) {
        int ret = viewdlg_file_browser(ctx, &v->viewmisc.file_browser); 
        if(ret >= 0) {
            v->dlg_filebrowser = 0;
            v->viewdlg_modal = 0;
            if(1 == ret) {
                // A file was selected, so get a FQN
                strncat(fb->dir_selected.name, "/", PATH_MAX);
                strncat(fb->dir_selected.name, fb->file_selected.name, PATH_MAX);
                util_file_discard(&m->sp_device[fb->slot].sp_files[fb->device]);
                // Eject the file that's active, if there is one, and mount the new one
                if(A2_ERR == sp_mount(m, fb->slot, fb->device, fb->dir_selected.name)) {
                    // If it fails, just discard the file
                    util_file_discard(&m->sp_device[fb->slot].sp_files[fb->device]);
                }
            }
        }
    }
}