// Apple ][+ and //e Emhanced emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#include "header.h"

// Display variables
static const char *access_mode[3] = { "R", "W", "RW" };
static int screen_mode[3] = { 0, 1, 5 };

static int display_mode = 0;
static int display_page2set = 0;
static int display_mixed = 0;
static int display_override = 0;
static int display_undo_change = 0;

void viewmisc_show(APPLE2 *m) {
    static int last_state = 0;
    VIEWPORT *v = m->viewport;
    struct nk_context *ctx = v->ctx;
    DEBUGGER *d = &v->debugger;
    FILE_BROWSER *fb = &v->viewmisc.file_browser;
    BREAKPOINT_EDIT *bpe = &v->viewmisc.breakpoint_edit;

    int display_mode_setting = 0;
    int display_page2set_setting = 0;
    int display_mixed_setting = 0;
    int force_redraw = 0;

    struct nk_color ob = ctx->style.window.background;
    int x = 512;
    int w = m->viewport->full_window_rect.w - x;
    if(nk_begin(ctx, "Miscellaneous", nk_rect(512, 560, 608, 280), NK_WINDOW_SCROLL_AUTO_HIDE | NK_WINDOW_TITLE | NK_WINDOW_BORDER)) {
        // Show the slot if it has a card in it
        if(nk_tree_push(ctx, NK_TREE_TAB, "Slots", NK_MAXIMIZED)) {
            for(int i = 2; i < 8; i++) {
                if(m->slot_cards[i].slot_type != SLOT_TYPE_EMPTY) {
                    nk_layout_row_dynamic(ctx, 13, 1);
                }
                if(m->slot_cards[i].slot_type == SLOT_TYPE_SMARTPORT) {
                    SP_DEVICE *spd = m->sp_device;
                    nk_labelf(ctx, NK_TEXT_LEFT, "Slot %d: SmartPort", i);
                    for(int j = 0; j < 2; j++) {
                        nk_layout_row_begin(ctx, NK_DYNAMIC, 18, 5);
                        nk_layout_row_push(ctx, 0.08f);
                        if(!j) {
                            char label[4];
                            sprintf(label, "%d.%d", i, j);
                            if(nk_button_label(ctx, label)) {
                                ram_card(m, 0, 1, 0);  // Make sure the ROM is active
                                ram_card(m, 1, 1, 0);  // Make sure the ROM is active
                                m->cpu.pc = 0xc000 + i * 0x100;
                                m->stopped = 0;
                            }
                        } else {
                            nk_labelf(ctx, NK_TEXT_CENTERED, "%d.%d", i, j);
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
                                fb->device_type = SLOT_TYPE_SMARTPORT;
                                fb->dir_contents.items = 0;
                                v->viewdlg_modal = 1;
                                v->dlg_filebrowser = 1;
                            }
                        }
                        nk_layout_row_push(ctx, 0.74f);
                        if(spd[i].sp_files[j].is_file_open) {
                            nk_label(ctx, spd[i].sp_files[j].file_display_name, NK_TEXT_LEFT);
                        }
                        nk_layout_row_end(ctx);
                    }
                } else if(m->slot_cards[i].slot_type == SLOT_TYPE_DISKII) {
                    DISKII_CONTROLLER *d = m->diskii_controller;
                    nk_labelf(ctx, NK_TEXT_LEFT, "Slot %d: Disk II", i);
                    for(int j = 0; j < 2; j++) {
                        nk_layout_row_begin(ctx, NK_DYNAMIC, 18, 5);
                        nk_layout_row_push(ctx, 0.08f);
                        if(!j) {
                            char label[4];
                            sprintf(label, "%d.%d", i, j);
                            if(nk_button_label(ctx, label)) {
                                // SQW - Maybe re-think all of this...
                                ram_card(m, 0, 1, 0);  // Make sure the ROM is active
                                ram_card(m, 1, 1, 0);  // Make sure the ROM is active
                                apple2_softswitch_read_callback(m, CLRRAMRD);
                                apple2_softswitch_read_callback(m, CLRRAMWRT);
                                apple2_softswitch_read_callback(m, CLRCXROM);
                                apple2_softswitch_read_callback(m, CLRC3ROM);
                                apple2_softswitch_read_callback(m, CLR80COL);
                                apple2_softswitch_read_callback(m, CLRALTCHAR);
                                apple2_softswitch_read_callback(m, TXTSET);
                                apple2_softswitch_read_callback(m, MIXCLR);
                                apple2_softswitch_read_callback(m, CLRPAGE2);
                                apple2_softswitch_read_callback(m, CLRALTZP);
                                apple2_softswitch_read_callback(m, CLRROM);
                                apple2_softswitch_read_callback(m, 0xC000 + i * 0x100);
                                m->cpu.pc = 0xc000 + i * 0x100;
                                m->stopped = 0;
                            }
                        } else {
                            nk_labelf(ctx, NK_TEXT_CENTERED, "%d.%d", i, j);
                        }
                        nk_layout_row_push(ctx, 0.08f);
                        if(nk_button_label(ctx, "Eject")) {
                            image_shutdown(&d[i].diskii_drive[j].image);
                        }
                        nk_layout_row_push(ctx, 0.1f);
                        if(nk_button_label(ctx, "Insert")) {
                            if(!v->viewdlg_modal) {
                                fb->slot = i;
                                fb->device = j;
                                fb->device_type = SLOT_TYPE_DISKII;
                                fb->dir_contents.items = 0;
                                v->viewdlg_modal = 1;
                                v->dlg_filebrowser = 1;
                            }
                        }
                        nk_layout_row_push(ctx, 0.74f);
                        if(d[i].diskii_drive[j].image.file.is_file_loaded) {
                            nk_label(ctx, d[i].diskii_drive[j].image.file.file_display_name, NK_TEXT_LEFT);
                        }
                        nk_layout_row_end(ctx);
                    }
                } else if(m->slot_cards[i].slot_type == SLOT_TYPE_VIDEX_API) {
                    nk_labelf(ctx, NK_TEXT_LEFT, "Slot %d: Franklin Ace Display", i);
                } else if(m->slot_cards[i].slot_type != SLOT_TYPE_EMPTY) {
                    nk_labelf(ctx, NK_TEXT_LEFT, "Slot %d: Undefined Card", i);
                }
            }
            nk_tree_pop(ctx);
        }
        // The Disk II
        // The Breakpoints tab
        if(nk_tree_push(ctx, NK_TREE_TAB, "Debugger", NK_MAXIMIZED)) {
            FLOWMANAGER *fm = &d->flowmanager;
            nk_layout_row_dynamic(ctx, 13, 1);
            nk_label(ctx, "Debug Status", NK_TEXT_LEFT);
            nk_layout_row_begin(ctx, NK_DYNAMIC, 120, 2);
            nk_layout_row_push(ctx, 0.40f);
            if(nk_group_begin(ctx, "run status group", NK_WINDOW_BORDER)) {
                nk_layout_row_begin(ctx, NK_DYNAMIC, 18, 2);
                nk_layout_row_push(ctx, 0.49f);
                nk_option_label(ctx, "Run to PC", fm->run_to_pc_set);
                nk_layout_row_push(ctx, 0.49f);
                nk_labelf(ctx, NK_TEXT_LEFT, "%04X", fm->run_to_pc);
                nk_layout_row_end(ctx);
                nk_layout_row_dynamic(ctx, 18, 1);
                nk_option_label(ctx, "Step Out", fm->run_to_rts_set);
                nk_layout_row_dynamic(ctx, 28, 2);
                nk_label(ctx, "Step Cycles", NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_BOTTOM);
                nk_labelf(ctx, NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_BOTTOM, "%"PRIu64, m->cpu.cycles - d->prev_stop_cycles);
                nk_layout_row_dynamic(ctx, 18, 2);
                nk_label(ctx, "Total Cycles", NK_TEXT_LEFT);
                nk_labelf(ctx, NK_TEXT_LEFT, "%"PRIu64, m->cpu.cycles);
                nk_group_end(ctx);
            }
            nk_layout_row_push(ctx, 0.59999f);
            if(nk_group_begin(ctx, "callstack group", NK_WINDOW_BORDER)) {
                nk_layout_row_dynamic(ctx, 18, 1);
                nk_label(ctx, "Call Stack", NK_TEXT_LEFT);
                nk_layout_row_dynamic(ctx, 75, 1);
                if(nk_group_begin(ctx, "Callstack", NK_WINDOW_BORDER)) {
                    char callstack_display[256];
                    uint16_t address = m->cpu.sp + 1;
                    while(address < 0x1ff) {
                        uint16_t stack_addr = read_from_memory_debug(m, address) + read_from_memory_debug(m, address + 1) * 256;
                        if(read_from_memory_debug(m, stack_addr - 2) == 0x20) {
                            nk_layout_row_begin(ctx, NK_DYNAMIC, 18, 2);
                            uint16_t dest_addr = read_from_memory_debug(m, stack_addr - 1) + read_from_memory_debug(m, stack_addr) * 256;
                            char *symbol = viewdbg_find_symbols(d, dest_addr);
                            sprintf(callstack_display, "%04X", stack_addr - 2);
                            nk_layout_row_push(ctx, 0.1f);
                            if(nk_select_label(ctx, callstack_display, NK_TEXT_ALIGN_LEFT, 0)) {
                                d->cursor_pc = strtoul(callstack_display, NULL, 16);
                            }
                            if(symbol) {
                                snprintf(callstack_display, 256, "JSR %04X %s", dest_addr, symbol);
                            } else {
                                snprintf(callstack_display, 256, "JSR %04X", dest_addr);
                            }
                            nk_layout_row_push(ctx, 0.9f);
                            if(nk_select_label(ctx, callstack_display, NK_TEXT_ALIGN_LEFT, 0)) {
                                d->cursor_pc = strtoul(callstack_display + 4, NULL, 16);
                            }
                            address += 2;
                        } else {
                            address++;
                        }
                    }
                    nk_group_end(ctx);
                }
                nk_group_end(ctx);
            }

            // Now a list of breakpoints
            if(fm->breakpoints.items) {
                // nk_layout_row_dynamic(ctx, 29*(fm->breakpoints.items + 1), 2);
                nk_layout_row_begin(ctx, NK_DYNAMIC, 29 * (fm->breakpoints.items + 1), 2);
                nk_layout_row_push(ctx, 0.80f);
                if(nk_group_begin(ctx, "breakpoints group", 0)) {
                    for(int i = 0; i < fm->breakpoints.items; i++) {
                        char label[32];
                        BREAKPOINT *bp = ARRAY_GET(&fm->breakpoints, BREAKPOINT, i);
                        nk_layout_row_begin(ctx, NK_DYNAMIC, 25, 5);
                        nk_layout_row_push(ctx, 0.399f);
                        if(bp->use_pc) {
                            if(bp->use_counter) {
                                nk_labelf(ctx, NK_TEXT_CENTERED, "%04X (%d/%d)", bp->address,
                                          bp->counter_count, bp->counter_stop_value);
                            } else {
                                nk_labelf(ctx, NK_TEXT_CENTERED, "%04X", bp->address);
                            }
                        } else {
                            int access = (bp->access >> 1) - 1;
                            if(bp->use_range) {
                                if(bp->use_counter) {
                                    nk_labelf(ctx, NK_TEXT_LEFT, "%s[%04X-%04X] (%d/%d)", access_mode[access],
                                              bp->address, bp->address_range_end, bp->counter_count, bp->counter_stop_value);
                                } else {
                                    nk_labelf(ctx, NK_TEXT_CENTERED, "%s[%04X-%04X]", access_mode[access],
                                              bp->address, bp->address_range_end);
                                }
                            } else {
                                if(bp->use_counter) {
                                    nk_labelf(ctx, NK_TEXT_CENTERED, "%s[%04X] (%d/%d)", access_mode[access],
                                              bp->address, bp->counter_count, bp->counter_stop_value);
                                } else {
                                    nk_labelf(ctx, NK_TEXT_CENTERED, "%s[%04X]", access_mode[access],
                                              bp->address);
                                }
                            }
                        }
                        nk_layout_row_push(ctx, 0.15f);
                        if(nk_button_label(ctx, "Edit")) {
                            bpe->bp_original = bp;
                            bpe->bp_under_edit = *bp;
                            bpe->string_address_len[0] = sprintf(bpe->string_address[0], "%04X", bp->address);
                            bpe->string_address_len[1] = sprintf(bpe->string_address[1], "%04X", bp->address_range_end);
                            bpe->string_counter_len[0] = sprintf(bpe->string_counter[0], "%d", bp->counter_stop_value);
                            bpe->string_counter_len[1] = sprintf(bpe->string_counter[1], "%d", bp->counter_reset);
                            v->viewdlg_modal = 1;
                            v->dlg_breakpoint = 1;
                            // This fixes an issue where selecting edit passes through to the pop-up and
                            // selects Cancel as well.
                            ctx->input.mouse.buttons->down = 0;
                        }
                        nk_layout_row_push(ctx, 0.15f);
                        if(nk_button_label(ctx, bp->disabled ? "Enable" : "Disable")) {
                            bp->disabled ^= 1;
                            if(!bp->use_pc) {
                                breakpoint_reapply_address_masks(m);
                            }
                        }
                        if(!bp->use_pc) {
                            nk_widget_disable_begin(ctx);
                        }
                        nk_layout_row_push(ctx, 0.15f);
                        if(nk_button_label(ctx, "View PC")) {
                            d->cursor_pc = bp->address;
                        }
                        if(!bp->use_pc) {
                            nk_widget_disable_end(ctx);
                        }
                        nk_layout_row_push(ctx, 0.15f);
                        if(nk_button_label(ctx, "Clear")) {
                            // Delete the breakpoint
                            array_remove(&fm->breakpoints, bp);
                            // And reset the mask on memory to watch for any remaining address breakpoint
                            breakpoint_reapply_address_masks(m);
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
        if(nk_tree_push(ctx, NK_TREE_TAB, "Display", NK_MAXIMIZED)) {
            nk_layout_row_begin(ctx, NK_DYNAMIC, 50, 3);
            nk_layout_row_push(ctx, 0.40f);
            if(nk_group_begin(ctx, "", 0)) {
                nk_layout_row_static(ctx, 13, 100, 1);
                nk_label(ctx, "Display Mode", NK_TEXT_LEFT);
                nk_layout_row_static(ctx, 13, 60, 3);
                display_mode_setting = nk_option_label(ctx, "Text", display_mode == 0) ? 0 : display_mode;
                display_mode_setting = nk_option_label(ctx, "Lores", display_mode_setting == 1) ? 1 : display_mode_setting;
                display_mode_setting = nk_option_label(ctx, "HGR", display_mode_setting == 2) ? 2 : display_mode_setting;
                nk_group_end(ctx);
            }

            nk_layout_row_push(ctx, 0.30f);
            if(nk_group_begin(ctx, "", 0)) {
                nk_layout_row_static(ctx, 13, 80, 1);
                nk_label(ctx, "Mixed Mode", NK_TEXT_LEFT);
                nk_layout_row_static(ctx, 13, 60, 2);
                display_mixed_setting = nk_option_label(ctx, "Off", display_mixed == 0) ? 0 : display_mixed;
                display_mixed_setting = nk_option_label(ctx, "On", display_mixed_setting == 1) ? 1 : display_mixed_setting;
                nk_group_end(ctx);
            }

            nk_layout_row_push(ctx, 0.30f);
            if(nk_group_begin(ctx, "", 0)) {
                nk_layout_row_static(ctx, 13, 100, 1);
                nk_label(ctx, "Display Page", NK_TEXT_LEFT);
                nk_layout_row_static(ctx, 13, 60, 2);
                display_page2set_setting = nk_option_label(ctx, "Page 0", display_page2set == 0) ? 0 : display_page2set;
                display_page2set_setting =
                    nk_option_label(ctx, "Page 1", display_page2set_setting == 1) ? 1 : display_page2set_setting;
                nk_group_end(ctx);
            }
            nk_layout_row_end(ctx);

            nk_layout_row_dynamic(ctx, 13, 1);
            nk_label(ctx, "Override", NK_TEXT_LEFT);
            nk_layout_row_static(ctx, 26, 60, 2);
            display_override = nk_option_label(ctx, "No", display_override == 0) ? 0 : display_override;
            display_override = nk_option_label(ctx, "Yes", display_override == 1) ? 1 : display_override;

            if(display_override) {
                // Only redraw the screen, when stopped, if a change is made
                if(display_mode != display_mode_setting || display_mixed != display_mixed_setting
                        || display_page2set != display_page2set_setting) {
                    display_mode = display_mode_setting;
                    display_mixed = display_mixed_setting;
                    display_page2set = display_page2set_setting;
                    if(m->stopped) {
                        force_redraw = 1;
                        display_undo_change = 1;
                    };
                }
            } else {
                // Get the settings from the "hardware"
                display_mode = m->screen_mode & 1 ? m->screen_mode & 4 ? 2 : 1 : 0;
                display_page2set = m->page2set != 0;
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
            v->shadow_screen_mode = screen_mode[display_mode] | (display_mixed << 1);
            v->shadow_page2set = display_page2set;
            if(force_redraw) {
                // stopped and a change was made, so update the Apple II display
                viewapl2_screen_apple2(m);
            }
            nk_tree_pop(ctx);
        }
        // The Language Card tab
        if(nk_tree_push(ctx, NK_TREE_TAB, "Language Card", NK_MAXIMIZED)) {
            RAM_CARD *lc = &m->ram_card[m->ramrdset];
            nk_layout_row_dynamic(ctx, 26, 4);
            if(nk_option_label(ctx, "Read ROM", lc->read_ram_enable ? 0 : 1) && lc->read_ram_enable) {
                pages_map_memory_block(&m->read_pages, &m->roms.blocks[ROM_APPLE2]);
                lc->read_ram_enable = 0;
            }
            if(nk_option_label(ctx, "Read RAM", lc->read_ram_enable ? 1 : 0) && !lc->read_ram_enable) {
                pages_map(&m->read_pages, 0xD000 / PAGE_SIZE, 0x1000 / PAGE_SIZE, &lc->RAM[lc->bank2_enable ? 0x1000 : 0x0000]);
                pages_map(&m->read_pages, 0xE000 / PAGE_SIZE, 0x2000 / PAGE_SIZE, &lc->RAM[0x2000]);
                lc->read_ram_enable = 1;
            }
            nk_option_label(ctx, "Pre-Write", lc->pre_write ? 1 : 0);
            nk_option_label(ctx, "Write", lc->write_enable ? 1 : 0);
            nk_option_label(ctx, "Bank 1", lc->bank2_enable ? 0 : 1);
            nk_option_label(ctx, "Bank 2", lc->bank2_enable ? 1 : 0);
            nk_tree_pop(ctx);
        }
    }
    // Pop-up windows after the main windows
    if(v->dlg_breakpoint) {
        struct nk_rect r = nk_rect(10, 40, 568, 160);
        int ret = viewdlg_breakpoint_edit(ctx, r, &v->viewmisc.breakpoint_edit);
        if(ret >= 0) {
            v->dlg_breakpoint = 0;
            v->viewdlg_modal = 0;
            if(1 == ret) {
                // Apply changes
                *bpe->bp_original = bpe->bp_under_edit;
                breakpoint_reapply_address_masks(m);
            }
            // This is necessary to keep the Misc window active. Why 193?
            // That's the value ctx->current->flags had coming in and
            // ctx->current->layout->flags is assigned to ctx->current->flags in
            // nuklear.h line 20330 inside nk_panel_end just before a comment
            // /* property garbage collector */
            // Without this, you have to mouse out of the misc window, and back in
            // for any widgets to be active in the misc window
            ctx->current->layout->flags = 193;
        }
    }
    nk_end(ctx);
    // Floating windows outside the main misc window
    ctx->style.window.background = ob;
    if(v->dlg_filebrowser) {
        int ret = viewdlg_file_browser(ctx, &v->viewmisc.file_browser);
        if(ret >= 0) {
            v->dlg_filebrowser = 0;
            v->viewdlg_modal = 0;
            if(1 == ret) {
                // A file was selected, so get a FQN
                strncat(fb->dir_selected.name, "/", PATH_MAX - 1);
                strncat(fb->dir_selected.name, fb->file_selected.name, PATH_MAX - 1);
                if(v->viewmisc.file_browser.device_type == SLOT_TYPE_SMARTPORT) {
                    util_file_discard(&m->sp_device[fb->slot].sp_files[fb->device]);
                    // Eject the file that's active, if there is one, and mount the new one
                    if(A2_ERR == sp_mount(m, fb->slot, fb->device, fb->dir_selected.name)) {
                        // If it fails, just discard the file
                        util_file_discard(&m->sp_device[fb->slot].sp_files[fb->device]);
                    }
                } else {
                    // SLOT_TYPE_DISKII
                    // Eject the file that's active, if there is one, and mount the new one
                    util_file_discard(&m->diskii_controller[fb->slot].diskii_drive[fb->device].image.file);
                    if(A2_ERR == diskii_mount(m, fb->slot, fb->device, fb->dir_selected.name)) {
                        // If it fails, just discard the file
                        util_file_discard(&m->diskii_controller[fb->slot].diskii_drive[fb->device].image.file);
                    }
                }
            }
        }
    }
}
