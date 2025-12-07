// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "unk_lib.h"

// Display variables
static const char *access_mode[3] = { "R", "W", "RW" };

// Calculate the rect where the scrollbar lives
static struct nk_rect nk_window_vscroll_rect(struct nk_context *ctx) {
    struct nk_rect bounds = nk_window_get_bounds(ctx);
    struct nk_vec2 content_min = nk_window_get_content_region_min(ctx);
    struct nk_vec2 content_max = nk_window_get_content_region_max(ctx);

    struct nk_rect r;
    r.x = content_max.x;
    r.y = content_min.y;                    // top of content (below header)
    r.w = (bounds.x + bounds.w) - r.x;      // everything to the right
    r.h = (bounds.y + bounds.h) - r.y;      // down to bottom of window
    return r;
}

// Figure out of the scrollbar is being dragged
static void update_nk_scroll_drag(struct nk_context *ctx, VIEWMISC *misc) {
    const struct nk_input *in = &ctx->input;
    const struct nk_mouse_button *mb = &in->mouse.buttons[NK_BUTTON_LEFT];

    struct nk_rect vscroll = nk_window_vscroll_rect(ctx);

    if(!misc->dragging) {
        // initial click inside the scrollbar rect
        if(mb->clicked &&
                NK_INBOX(mb->clicked_pos.x, mb->clicked_pos.y, vscroll.x, vscroll.y, vscroll.w, vscroll.y)) {
            misc->dragging = 1;
        }
    } else {
        if(!mb->down) {
            // release when button released
            misc->dragging = 0;
        }
    }
}

int unk_misc_init(VIEWMISC *viewmisc) {
    BREAKPOINT_EDIT *bpe = &viewmisc->breakpoint_edit;
    bpe->bp_under_edit.slot = 6;
    bpe->bp_under_edit.device = 0;
    bpe->string_device[0][0] = '6';
    bpe->string_device[1][0] = '0';
    bpe->string_device_len[0] = bpe->string_device_len[1] = 1;
    return A2_OK;
}

void unk_misc_show(UNK *v) {
    APPLE2 *m = v->m;
    RUNTIME *rt = v->rt;
    VIEWDASM *dv = &v->viewdasm;
    FILE_BROWSER *fb = &v->viewmisc.file_browser;
    BREAKPOINT_EDIT *bpe = &v->viewmisc.breakpoint_edit;
    static int last_state = 0;
    struct nk_context *ctx = v->ctx;

    struct nk_color ob = ctx->style.window.background;
    int x = 512;
    int w = v->layout.misc.w;
    int flags = NK_WINDOW_SCROLL_AUTO_HIDE | NK_WINDOW_TITLE | NK_WINDOW_BORDER;
    // If the breakpoint "dialog" is open, disable the misc window
    if(v->dlg_breakpoint) {
        flags |= NK_WINDOW_NO_INPUT;
    }
    if(nk_begin(ctx, "Miscellaneous", v->layout.misc, flags)) {
        // Show the slot if it has a card in it
        if(nk_tree_push(ctx, NK_TREE_TAB, "Slots", NK_MAXIMIZED)) {
            for(int i = 1; i < 8; i++) {
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
                                rt_machine_reset(rt);
                                m->cpu.pc = 0xc000 + i * 0x100;
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
                            if(!v->unk_dlg_modal) {
                                fb->slot = i;
                                fb->device = j;
                                fb->device_type = SLOT_TYPE_SMARTPORT;
                                fb->dir_contents.items = 0;
                                v->unk_dlg_modal = 1;
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
                    // The Disk II
                    DISKII_CONTROLLER *d = m->diskii_controller;
                    nk_labelf(ctx, NK_TEXT_LEFT, "Slot %d: Disk II", i);
                    for(int j = 0; j < 2; j++) {
                        nk_layout_row_begin(ctx, NK_DYNAMIC, 18, 5);
                        float w = 1.0f - 0.08f;
                        nk_layout_row_push(ctx, 0.08f);
                        if(!j) {
                            char label[4];
                            sprintf(label, "%d.%d", i, j);
                            if(nk_button_label(ctx, label)) {
                                rt_machine_reset(rt);
                                m->cpu.pc = 0xc000 + i * 0x100;
                            }
                        } else {
                            nk_labelf(ctx, NK_TEXT_CENTERED, "%d.%d", i, j);
                        }
                        w -= 0.08f;
                        nk_layout_row_push(ctx, 0.08f);
                        if(nk_button_label(ctx, "Eject")) {
                            diskii_eject(m, i, j, 1);
                        }
                        w -= 0.1f;
                        nk_layout_row_push(ctx, 0.1f);
                        if(nk_button_label(ctx, "Insert")) {
                            if(!v->unk_dlg_modal) {
                                fb->slot = i;
                                fb->device = j;
                                fb->device_type = SLOT_TYPE_DISKII;
                                fb->dir_contents.items = 0;
                                v->unk_dlg_modal = 1;
                                v->dlg_filebrowser = 1;
                            }
                        }
                        if(d[i].diskii_drive[j].images.items > 1) {
                            char low[3], high[3], title[13];
                            int index;
                            size_t items;
                            index = d[i].diskii_drive[j].image_index + 1;
                            items = d[i].diskii_drive[j].images.items;
                            snprintf(low, 3, "%02d", index);
                            snprintf(high, 3, "%02zd", items);
                            sprintf(title, "Swap (%s/%s)", low, high);
                            w -= 0.18f;
                            nk_layout_row_push(ctx, 0.18f);
                            if(nk_button_label(ctx, title)) {
                                diskii_mount_image(m, i, j, index >= items ? 0 : index);
                            }
                        }
                        nk_layout_row_push(ctx, w);
                        if(d[i].diskii_drive[j].active_image && d[i].diskii_drive[j].active_image->file.is_file_loaded) {
                            nk_label(ctx, d[i].diskii_drive[j].active_image->file.file_display_name, NK_TEXT_LEFT);
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
        // The Breakpoints tab
        if(nk_tree_push(ctx, NK_TREE_TAB, "Debugger", NK_MAXIMIZED)) {
            nk_layout_row_dynamic(ctx, 13, 1);
            nk_label(ctx, "Debug Status", NK_TEXT_LEFT);
            nk_layout_row_begin(ctx, NK_DYNAMIC, 120, 2);
            nk_layout_row_push(ctx, 0.40f);
            if(nk_group_begin(ctx, "run status group", NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BORDER)) {
                nk_layout_row_begin(ctx, NK_DYNAMIC, 18, 2);
                nk_layout_row_push(ctx, 0.49f);
                nk_option_label(ctx, "Run to PC", rt->run_to_pc);
                nk_layout_row_push(ctx, 0.49f);
                nk_labelf(ctx, NK_TEXT_LEFT, "%04X", rt->run_to_pc_address);
                nk_layout_row_end(ctx);
                nk_layout_row_dynamic(ctx, 18, 1);
                nk_option_label(ctx, "Step Out", rt->run_step_out);
                nk_layout_row_dynamic(ctx, 28, 2);
                nk_label(ctx, "Step Cycles", NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_BOTTOM);
                nk_labelf(ctx, NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_BOTTOM, "%"PRIu64, m->cpu.cycles - rt->prev_stop_cycles);
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
                // SQW  - Probably need a manual scrollbarV
                if(nk_group_begin(ctx, "Callstack", NK_WINDOW_BORDER)) {
                    char callstack_display[256];
                    uint16_t address = m->cpu.sp + 1;
                    while(address < 0x1ff) {
                        uint16_t stack_addr = read_from_memory_debug(m, address) + read_from_memory_debug(m, address + 1) * 256;
                        if(read_from_memory_debug(m, stack_addr - 2) == 0x20) {
                            nk_layout_row_begin(ctx, NK_DYNAMIC, 18, 2);
                            uint16_t dest_addr = read_from_memory_debug(m, stack_addr - 1) + read_from_memory_debug(m, stack_addr) * 256;
                            char *symbol = rt_sym_find_symbols(rt, dest_addr);
                            sprintf(callstack_display, "%04X", stack_addr - 2);
                            nk_layout_row_push(ctx, 0.1f);
                            if(nk_select_label(ctx, callstack_display, NK_TEXT_ALIGN_LEFT, 0)) {
                                dv->cursor_address = strtoul(callstack_display, NULL, 16);
                                unk_dasm_recenter_view(v, 0);
                            }
                            if(symbol) {
                                snprintf(callstack_display, 256, "JSR %04X %s", dest_addr, symbol);
                            } else {
                                snprintf(callstack_display, 256, "JSR %04X", dest_addr);
                            }
                            nk_layout_row_push(ctx, 0.9f);
                            if(nk_select_label(ctx, callstack_display, NK_TEXT_ALIGN_LEFT, 0)) {
                                dv->cursor_address = strtoul(callstack_display + 4, NULL, 16);
                                unk_dasm_recenter_view(v, 0);
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
            if(rt->breakpoints.items) {
                // nk_layout_row_dynamic(ctx, 29*(rt->breakpoints.items + 1), 2);
                nk_layout_row_begin(ctx, NK_DYNAMIC, 29 * (rt->breakpoints.items + 1), 2);
                nk_layout_row_push(ctx, 0.80f);
                if(nk_group_begin(ctx, "breakpoints group", 0)) {
                    for(int i = 0; i < rt->breakpoints.items; i++) {
                        char label[32];
                        BREAKPOINT *bp = ARRAY_GET(&rt->breakpoints, BREAKPOINT, i);
                        nk_layout_row_begin(ctx, NK_DYNAMIC, 25, 5);
                        nk_layout_row_push(ctx, 0.399f);
                        if(bp->use_pc) {
                            if(bp->use_counter) {
                                nk_labelf(ctx, NK_TEXT_CENTERED, "%04X (%d/%d)", bp->address,
                                          bp->counter_count, bp->counter_stop_value);
                            } else if(bp->action) {
                                switch(bp->action) {
                                    case ACTION_FAST:
                                        nk_labelf(ctx, NK_TEXT_CENTERED, "%04X Fast", bp->address);
                                        break;
                                    case ACTION_RESTORE:
                                        nk_labelf(ctx, NK_TEXT_CENTERED, "%04X Restore", bp->address);
                                        break;
                                    case ACTION_SLOW:
                                        nk_labelf(ctx, NK_TEXT_CENTERED, "%04X Slow", bp->address);
                                        break;
                                    case ACTION_SWAP:
                                        nk_labelf(ctx, NK_TEXT_CENTERED, "%04X Swap s%dd%d", bp->address, bp->slot, bp->device);
                                        break;
                                    case ACTION_TROFF:
                                        nk_labelf(ctx, NK_TEXT_CENTERED, "%04X Troff", bp->address);
                                        break;
                                    case ACTION_TRON:
                                        nk_labelf(ctx, NK_TEXT_CENTERED, "%04X Tron", bp->address);
                                        break;
                                    case ACTION_TYPE:
                                        nk_labelf(ctx, NK_TEXT_CENTERED, "%04X Type", bp->address);
                                        break;
                                    default:
                                        nk_labelf(ctx, NK_TEXT_CENTERED, "%04X Action", bp->address);
                                        break;
                                }
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
                            bpe->string_type_len = parse_encode_c_string(bp->type_text, bpe->string_type, 128);
                            bpe->string_address_len[0] = sprintf(bpe->string_address[0], "%04X", bp->address);
                            bpe->string_address_len[1] = sprintf(bpe->string_address[1], "%04X", bp->address_range_end);
                            bpe->string_counter_len[0] = sprintf(bpe->string_counter[0], "%d", bp->counter_stop_value);
                            bpe->string_counter_len[1] = sprintf(bpe->string_counter[1], "%d", bp->counter_reset);
                            v->unk_dlg_modal = 1;
                            v->dlg_breakpoint = 1;
                            // This fixes an issue where selecting edit passes through to the pop-up and
                            // selects Cancel as well.
                            ctx->input.mouse.buttons->down = 0;
                        }
                        nk_layout_row_push(ctx, 0.15f);
                        if(nk_button_label(ctx, bp->disabled ? "Enable" : "Disable")) {
                            bp->disabled ^= 1;
                            if(!bp->use_pc) {
                                rt_bp_apply_masks(rt);
                            }
                        }
                        if(!bp->use_pc) {
                            nk_widget_disable_begin(ctx);
                        }
                        nk_layout_row_push(ctx, 0.15f);
                        if(nk_button_label(ctx, "View PC")) {
                            dv->cursor_address = bp->address;
                            unk_dasm_recenter_view(v, 0);
                        }
                        if(!bp->use_pc) {
                            nk_widget_disable_end(ctx);
                        }
                        nk_layout_row_push(ctx, 0.15f);
                        if(nk_button_label(ctx, "Clear")) {
                            // Delete the breakpoint
                            array_remove(&rt->breakpoints, bp);
                            // And reset the mask on memory to watch for any remaining address breakpoint
                            rt_bp_apply_masks(rt);
                        }
                        nk_layout_row_end(ctx);
                    }
                    nk_group_end(ctx);
                    nk_layout_row_push(ctx, 0.19f);
                    if(nk_group_begin(ctx, "clear all group", 0)) {
                        if(rt->breakpoints.items >= 2) {
                            nk_layout_row_dynamic(ctx, 25, 1);
                            if(nk_button_label(ctx, "Clear All")) {
                                rt->breakpoints.items = 0;
                            }
                        }
                        nk_group_end(ctx);
                    }
                }
                nk_layout_row_end(ctx);
            }
            nk_tree_pop(ctx);
        }
        if(nk_tree_push(ctx, NK_TREE_TAB, "Soft Switches", NK_MAXIMIZED)) {
            nk_layout_row_dynamic(ctx, 13, 4);
            v->shadow_flags.b.store80set = nk_option_label_disabled(ctx, "C000-80STORE", v->shadow_flags.b.store80set, 1);
            v->shadow_flags.b.ramrdset   = nk_option_label_disabled(ctx, "C003-RAMRD", v->shadow_flags.b.ramrdset, 1);
            v->shadow_flags.b.ramwrtset  = nk_option_label_disabled(ctx, "C005-RAMWRT", v->shadow_flags.b.ramwrtset, 1);
            v->shadow_flags.b.cxromset   = nk_option_label_disabled(ctx, "C007-CXROM", v->shadow_flags.b.cxromset, 1);
            v->shadow_flags.b.altzpset   = nk_option_label_disabled(ctx, "C009-ALTZP", v->shadow_flags.b.altzpset, 1);
            v->shadow_flags.b.c3slotrom  = nk_option_label_disabled(ctx, "C00B-C3ROM", v->shadow_flags.b.c3slotrom, 1);
            nk_layout_row_dynamic(ctx, 13, 1);
            nk_spacer(ctx);
            v->display_override          = nk_option_label(ctx, "Display override", v->display_override);
            nk_layout_row_dynamic(ctx, 13, 4);
            v->shadow_flags.b.col80set   = nk_option_label_disabled(ctx, "C00D-80COL", v->shadow_flags.b.col80set, !v->display_override);
            v->shadow_flags.b.altcharset = nk_option_label_disabled(ctx, "C00F-ALTCHAR", v->shadow_flags.b.altcharset, !v->display_override);
            v->shadow_flags.b.text       = nk_option_label_disabled(ctx, "C051-TEXT", v->shadow_flags.b.text, !v->display_override);
            v->shadow_flags.b.mixed      = nk_option_label_disabled(ctx, "C053-MIXED", v->shadow_flags.b.mixed, !v->display_override);
            v->shadow_flags.b.page2set   = nk_option_label_disabled(ctx, "C055-PAGE2", v->shadow_flags.b.page2set, !v->display_override);
            v->shadow_flags.b.hires      = nk_option_label_disabled(ctx, "C057-HIRES", v->shadow_flags.b.hires, !v->display_override);
            v->shadow_flags.b.dhires     = nk_option_label_disabled(ctx, "C05E-DHGR", v->shadow_flags.b.dhires, !v->display_override);
            nk_layout_row_dynamic(ctx, 13, 1);
            nk_spacer(ctx);
            nk_label(ctx, "Language Card", NK_TEXT_LEFT);
            nk_layout_row_dynamic(ctx, 13, 4);
            v->shadow_flags.b.lc_bank2_enable     = nk_option_label_disabled(ctx, "LCBANK2", v->shadow_flags.b.lc_bank2_enable, 1);
            v->shadow_flags.b.lc_read_ram_enable  = nk_option_label_disabled(ctx, "LCREAD", v->shadow_flags.b.lc_read_ram_enable, 1);
            v->shadow_flags.b.lc_pre_write        = nk_option_label_disabled(ctx, "LCPREWRITE", v->shadow_flags.b.lc_pre_write, 1);
            v->shadow_flags.b.lc_write_enable     = nk_option_label_disabled(ctx, "LCWRITE", v->shadow_flags.b.lc_write_enable, 1);
            nk_tree_pop(ctx);
        }
    }
    // Set a flag if the scrollbar is being dragged (scrollbar_active in unk_view)
    update_nk_scroll_drag(ctx, &v->viewmisc);
    nk_end(ctx);

    // Pop-up windows after the main windows
    if(v->dlg_breakpoint) {
        struct nk_rect mvr = v->layout.misc;
        struct nk_rect r = nk_rect(mvr.x + 10, mvr.y + 10, 568, 180);
        int ret = unk_dlg_breakpoint_edit(ctx, r, &v->viewmisc.breakpoint_edit);
        if(ret >= 0) {
            v->dlg_breakpoint = 0;
            v->unk_dlg_modal = 0;
            if(1 == ret) {
                // Apply changes
                free(bpe->bp_original->type_text);
                *bpe->bp_original = bpe->bp_under_edit;
                bpe->bp_original->type_text = bpe->string_type_len ? parse_decode_c_string(bpe->string_type, NULL) : NULL;
                rt_bp_apply_masks(rt);
            }
            // Nov 28, 2025 - Leaving here but I think this is resolved...
            // This is necessary to keep the Misc window active. Why 193?
            // That's the value ctx->current->flags had coming in and
            // ctx->current->layout->flags is assigned to ctx->current->flags in
            // nuklear.h line 20493 inside nk_panel_end just before a comment
            // /* property garbage collector */
            // Without this, you have to mouse out of the misc window, and back in
            // for any widgets to be active in the misc window
            // ctx->current->layout->flags = 193;
        }
    }

    // Floating windows outside the main misc window
    ctx->style.window.background = ob;
    if(v->dlg_filebrowser) {
        int ret = unk_dlg_file_browser(ctx, &v->viewmisc.file_browser);
        if(ret >= 0) {
            array_free(&dv->temp_assembler_config.file_browser.dir_contents);
            v->dlg_filebrowser = 0;
            v->unk_dlg_modal = 0;
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
                    diskii_mount(m, fb->slot, fb->device, fb->dir_selected.name);
                }
            }
        }
    }
}
