// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "unk_lib.h"

char global_entry_buffer[256] = {0};
int global_entry_length = 0;
const char *str_actions[] = { "BREAK", "FAST", "RESTORE", "SLOW", "SWAP", "TROFF", "TRON", "TYPE" };
const int num_actions = sizeof(str_actions) / sizeof(str_actions[0]);

int unk_dlg_assembler_config(struct nk_context *ctx, struct nk_rect r, ASSEMBLER_CONFIG *ac) {
    int ret = 0;
    FILE_BROWSER *fb = &ac->file_browser;
    if(nk_popup_begin(ctx, NK_POPUP_STATIC, "Assembler Config", 0, r)) {
        // 1) Path row with BROWSE button
        nk_layout_row_begin(ctx, NK_DYNAMIC, 28, 3);
        {
            nk_layout_row_push(ctx, 0.20f);
            nk_label(ctx, "Path", NK_TEXT_LEFT);
            nk_layout_row_push(ctx, 0.60f);
            ctx->current->edit.mode = NK_TEXT_EDIT_MODE_REPLACE;
            if(NK_EDIT_COMMITED &
                    nk_edit_string(ctx, NK_EDIT_SELECTABLE | NK_EDIT_CLIPBOARD | NK_EDIT_SIG_ENTER, fb->dir_selected.name,
                                   &fb->dir_selected.name_length, PATH_MAX - 1, nk_filter_default)) {
                UTIL_FILE file;
                memset(&file, 0, sizeof(file));
                fb->dir_selected.name[fb->dir_selected.name_length] = '\0';
                if(A2_OK == util_file_open(&file, fb->dir_selected.name, "r")) {
                    // If the file could open, well, it's a file that was selected, so use it
                    strcpy(fb->file_selected.name, file.file_display_name);
                    // fb->dir_selected.name[file.file_display_name - file.file_path] = '\0';
                    util_file_discard(&file);
                    ret = 1;
                } else {
                    // If it wasn't a file, assume it was a folder and just ignore any errors here
                    util_dir_change(fb->dir_selected.name);
                    // rescan the current, which if it was typed correctly, will be what was typed,
                    // else rescan what was already "current"
                    fb->dir_contents.items = 0;
                    util_dir_get_current(fb->dir_selected.name, PATH_MAX);
                    fb->dir_selected.name_length = strnlen(fb->dir_selected.name, PATH_MAX);
                    // This keeps the cursor active in the edit box, and places it at the end
                    ctx->current->edit.cursor = ctx->current->edit.sel_start = ctx->current->edit.sel_end =
                                                    strnlen(fb->dir_selected.name, PATH_MAX);
                    // ctx->current->edit.active = 1;
                }
            }
            nk_layout_row_push(ctx, 0.20f);
            if(nk_button_label(ctx, "Browse") && !ac->dlg_asm_filebrowser) {
                ac->dlg_asm_filebrowser = 1;
            }
        }
        nk_layout_row_end(ctx);

        // 2) Memory bank access row
        unk_bank_view_selector(ctx, ac->model, &ac->flags, NULL);

        // 3)  Toggles and address
        nk_layout_row_begin(ctx, NK_DYNAMIC, 28, 3);
        {
            nk_layout_row_push(ctx, 0.30f);
            nk_checkbox_label_align(ctx, "Reset Stack", &ac->reset_stack, 0, NK_TEXT_LEFT);
            nk_layout_row_push(ctx, 0.29f);
            nk_checkbox_label_align(ctx, "Auto Run", &ac->auto_run_after_assemble, 0, NK_TEXT_LEFT);
            nk_layout_row_push(ctx, 0.20f);
            nk_label(ctx, "Address", NK_TEXT_RIGHT);
            nk_layout_row_push(ctx, 0.3f);
            if(NK_EDIT_COMMITED &
                    nk_edit_string(ctx, NK_EDIT_SELECTABLE | NK_EDIT_CLIPBOARD | NK_EDIT_SIG_ENTER,
                                   ac->start_address_text, &ac->start_address_text_len, 5, nk_filter_hex)) {
                ac->start_address = strtol(ac->start_address_text, NULL, 16);
            }
        }
        nk_layout_row_end(ctx);

        // 4) Cancel / OK buttons
        nk_layout_row_begin(ctx, NK_DYNAMIC, 28, 3);
        {
            nk_layout_row_push(ctx, 0.40f);
            nk_spacer(ctx);
            nk_layout_row_push(ctx, 0.30f);
            if(nk_button_label(ctx, "Cancel")) {
                ret = -1;
            }
            nk_layout_row_push(ctx, 0.30f);
            if(nk_button_label(ctx, "OK")) {
                ac->start_address = strtol(ac->start_address_text, NULL, 16);
                ret = 1;
            }
        }
        nk_layout_row_end(ctx);
    }
    nk_popup_end(ctx);
    return ret;
}

int unk_dlg_assembler_errors(UNK *v, struct nk_context *ctx, struct nk_rect r) {
    int ret = 0;
    VIEWDASM *dv = &v->viewdasm;
    static nk_uint x_offset = 0, y_offset = 0;
    if(nk_popup_begin(ctx, NK_POPUP_STATIC, "Assembler errors", 0, r)) {
        nk_layout_row_dynamic(ctx, 360, 1);
        {
            if(nk_group_scrolled_offset_begin(ctx, &x_offset, &y_offset, "Error Messages", NK_WINDOW_BORDER)) {
                size_t i;
                nk_layout_row_static(ctx, 13, 8 * dv->errorlog.longest_error_message_length, 1);
                for(i = 0; i < dv->errorlog.log_array.items; i++) {
                    char *text = ARRAY_GET(&dv->errorlog.log_array, ERROR_ENTRY, i)->err_str;
                    nk_label(ctx, text, NK_TEXT_ALIGN_LEFT);
                }
                nk_group_end(ctx);
            }
        }
        nk_layout_row_dynamic(ctx, 28, 1);
        {
            if(nk_button_label(ctx, "OK")) {
                ret = 1;
            }
        }
    }
    nk_popup_end(ctx);
    return ret;
}

int unk_dlg_breakpoint_edit(struct nk_context *ctx, struct nk_rect r, BREAKPOINT_EDIT *bpe) {
    int ret = -1;

    // This is a window, not a pop-up because combo boxes uses popups and you can't have popups in popups
    // use same thickness as popups
    nk_style_push_float(ctx, &ctx->style.window.border, ctx->style.window.popup_border);
    // use same red as popups
    nk_style_push_color(ctx, &ctx->style.window.border_color, ctx->style.window.popup_border_color);

    if(nk_begin_titled(ctx, "EditBreakpoint", "Edit Breakpoint", r, NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
        nk_layout_row_dynamic(ctx, 22, 4);
        {
            // 1)
            nk_label(ctx, "On access", NK_TEXT_LEFT);
            // 2)
            bpe->bp_under_edit.break_on_exec = nk_check_label(ctx, "Execute", bpe->bp_under_edit.break_on_exec);
            // 3)
            bpe->bp_under_edit.break_on_read = nk_check_label(ctx, "Read", bpe->bp_under_edit.break_on_read);
            // 4)
            bpe->bp_under_edit.break_on_write = nk_check_label(ctx, "Write", bpe->bp_under_edit.break_on_write);
            // One has to be true
            if(!bpe->bp_under_edit.break_on_write && !bpe->bp_under_edit.break_on_read && !bpe->bp_under_edit.break_on_exec) {
                bpe->bp_under_edit.break_on_exec = 1;
            }
        }

        // B) Address range
        nk_layout_row_dynamic(ctx, 22, 4);
        {
            int value;
            // 1)
            nk_label(ctx, "At address", NK_TEXT_LEFT);
            // 2)
            if(NK_EDIT_COMMITED &
                    nk_edit_string(ctx, NK_EDIT_SELECTABLE | NK_EDIT_CLIPBOARD | NK_EDIT_SIG_ENTER, bpe->string_address[0],
                                   &bpe->string_address_len[0], 5, nk_filter_hex)) {
                    bpe->string_address[0][bpe->string_address_len[0]] = '\0';
                    if(1 == sscanf(bpe->string_address[0], "%x", &value) && value >= 0 && value <= 65535) {
                        bpe->bp_under_edit.address = value;
                        ctx->current->edit.active = 0;
                    }
            }
            // 3)
            bpe->bp_under_edit.use_range = nk_check_label(ctx, "Range End", bpe->bp_under_edit.use_range);
            // 4)
            if(!bpe->bp_under_edit.use_range) {
                nk_widget_disable_begin(ctx);
            }
            if(NK_EDIT_COMMITED &
                    nk_edit_string(ctx, NK_EDIT_SELECTABLE | NK_EDIT_CLIPBOARD | NK_EDIT_SIG_ENTER, bpe->string_address[1],
                                   &bpe->string_address_len[1], 5, nk_filter_hex)) {
                    bpe->string_address[1][bpe->string_address_len[1]] = '\0';
                    if(1 == sscanf(bpe->string_address[1], "%x", &value) && value >= 0 && value <= 65535 && value >= bpe->bp_under_edit.address) {
                        bpe->bp_under_edit.address_range_end = value;
                        ctx->current->edit.active = 0;
                    }
            }
            if(!bpe->bp_under_edit.use_range) {
                nk_widget_disable_end(ctx);
            }
        }

        // C) Memory bank access
        nk_layout_row_begin(ctx, NK_DYNAMIC, 22, 5);
        {
            // // 1)
            // nk_layout_row_push(ctx, 0.25f);
            // nk_label(ctx, "In Bank", NK_TEXT_LEFT);
            // // 2)
            // nk_layout_row_push(ctx, 0.17f);
            // if(nk_option_label(ctx, "6502", !tst_flags(bpe->bp_under_edit.selected_bank, (MEM_MAIN | MEM_AUX))) && tst_flags(bpe->bp_under_edit.selected_bank, (MEM_MAIN | MEM_AUX))) {
            //     clr_flags(bpe->bp_under_edit.selected_bank, MEM_MAIN);
            //     clr_flags(bpe->bp_under_edit.selected_bank, MEM_AUX);
            //     clr_flags(bpe->bp_under_edit.selected_bank, MEM_LC_BANK2);
            // }
            // // 3)
            // nk_layout_row_push(ctx, 0.17f);
            // if(nk_option_label(ctx, "64K", tst_flags(bpe->bp_under_edit.selected_bank, MEM_MAIN)) && !tst_flags(bpe->bp_under_edit.selected_bank, MEM_MAIN)) {
            //     clr_flags(bpe->bp_under_edit.selected_bank, MEM_AUX);
            //     set_flags(bpe->bp_under_edit.selected_bank, MEM_MAIN);
            // }
            // // 4)
            // nk_layout_row_push(ctx, 0.16f);
            // if(nk_option_label_disabled(ctx, "128K", tst_flags(bpe->bp_under_edit.selected_bank, MEM_AUX), !bpe->bp_under_edit.model) && !tst_flags(bpe->bp_under_edit.selected_bank, MEM_AUX)) {
            //     clr_flags(bpe->bp_under_edit.selected_bank, MEM_MAIN);
            //     set_flags(bpe->bp_under_edit.selected_bank, MEM_AUX);
            // }
            // // 5)
            // nk_layout_row_push(ctx, 0.25f);
            // int before = tst_flags(bpe->bp_under_edit.selected_bank, MEM_LC_BANK2);
            // int after = nk_option_label_disabled(ctx, "LC Bank2", before, !tst_flags(bpe->bp_under_edit.selected_bank, MEM_MAIN | MEM_AUX));
            // if(after != before) {
            //     if(after) {
            //         set_flags(bpe->bp_under_edit.selected_bank, MEM_LC_BANK2);
            //     } else {
            //         clr_flags(bpe->bp_under_edit.selected_bank, MEM_LC_BANK2);
            //     }
            // }
        }

        // D) Counters
        nk_layout_row_begin(ctx, NK_DYNAMIC, 22, 5);
        {
            // 1)
            nk_layout_row_push(ctx, 0.25f);
            bpe->bp_under_edit.use_counter = nk_check_label(ctx, "Use counter", bpe->bp_under_edit.use_counter ? 1 : 0) ? 1 : 0;
            if(!bpe->bp_under_edit.use_counter) {
                nk_widget_disable_begin(ctx);
            }
            // 2)
            nk_layout_row_push(ctx, 0.17f);
            nk_label(ctx, "Initial", NK_TEXT_LEFT);
            // 3)
            nk_layout_row_push(ctx, 0.17f);
            if(NK_EDIT_COMMITED &
                    nk_edit_string(ctx, NK_EDIT_SELECTABLE | NK_EDIT_CLIPBOARD | NK_EDIT_SIG_ENTER, bpe->string_counter[0],
                                   &bpe->string_counter_len[0], 8, nk_filter_decimal)) {
                ctx->current->edit.active = 0;
            }
            // 4)
            nk_layout_row_push(ctx, 0.16f);
            nk_label(ctx, "at reset", NK_TEXT_LEFT);
            // 5)
            nk_layout_row_push(ctx, 0.17f);
            if(NK_EDIT_COMMITED &
                    nk_edit_string(ctx, NK_EDIT_SELECTABLE | NK_EDIT_CLIPBOARD | NK_EDIT_SIG_ENTER, bpe->string_counter[1],
                                   &bpe->string_counter_len[1], 8, nk_filter_decimal)) {
                ctx->current->edit.active = 0;
            }
            if(!bpe->bp_under_edit.use_counter) {
                nk_widget_disable_end(ctx);
            }
        }
        nk_layout_row_end(ctx);

        // E) Actions
        nk_layout_row_begin(ctx, NK_DYNAMIC, 22, 6);
        {
            // 1)
            nk_layout_row_push(ctx, 0.25f);
            nk_text(ctx, "Action", 6, NK_TEXT_ALIGN_MIDDLE);
            // 2)
            nk_layout_row_push(ctx, 0.25f);
            // Get the space where the combo box should reside
            bpe->combo_rect = nk_widget_bounds(ctx);
            nk_spacer(ctx);
            switch(bpe->bp_under_edit.action) {
                case ACTION_BREAK:
                case ACTION_FAST:
                case ACTION_RESTORE:
                case ACTION_SLOW:
                case ACTION_TROFF:
                case ACTION_TRON:
                    break;
                case ACTION_SWAP:
                    // 3)
                    nk_layout_row_push(ctx, 0.1f);
                    nk_text(ctx, "Slot", 4, NK_TEXT_CENTERED);
                    // 4)
                    nk_layout_row_push(ctx, 0.05f);
                    if(NK_EDIT_COMMITED &
                            nk_edit_string(ctx, NK_EDIT_SELECTABLE | NK_EDIT_CLIPBOARD | NK_EDIT_SIG_ENTER,
                                           bpe->string_device[0], &bpe->string_device_len[0], 2, nk_filter_decimal)) {
                        if(!bpe->string_device_len[0]) {
                            bpe->string_device_len[0] = 1;
                        }
                        int s = strtol(bpe->string_device[0], NULL, 10);
                        if(s < 1 || s > 7) {
                            bpe->string_device[0][0] = '6';
                        }
                        ctx->current->edit.active = 0;
                    }
                    // 5)
                    nk_layout_row_push(ctx, 0.1f);
                    nk_text(ctx, "Drive", 5, NK_TEXT_CENTERED);
                    // 6)
                    nk_layout_row_push(ctx, 0.05f);
                    if(NK_EDIT_COMMITED &
                            nk_edit_string(ctx, NK_EDIT_SELECTABLE | NK_EDIT_CLIPBOARD | NK_EDIT_SIG_ENTER,
                                           bpe->string_device[1], &bpe->string_device_len[1], 2, nk_filter_decimal)) {
                        if(!bpe->string_device_len[1]) {
                            bpe->string_device_len[1] = 1;
                        }
                        int s = strtol(bpe->string_device[1], NULL, 10);
                        if(s < 0 || s > 1) {
                            bpe->string_device[1][0] = '0';
                        }
                        ctx->current->edit.active = 0;
                    }
                    break;
                case ACTION_TYPE:
                    // 3)
                    nk_layout_row_push(ctx, 0.5f);
                    if(NK_EDIT_COMMITED &
                            nk_edit_string(ctx, NK_EDIT_SELECTABLE | NK_EDIT_CLIPBOARD | NK_EDIT_SIG_ENTER,
                                           bpe->string_type, &bpe->string_type_len, 128, nk_filter_ascii)) {
                        bpe->string_type[bpe->string_type_len] = '\0';
                        ctx->current->edit.active = 0;
                    }
                    break;
            }
        }
        nk_layout_row_end(ctx);
        ctx->current->edit.mode = NK_TEXT_EDIT_MODE_REPLACE;
        nk_layout_row_dynamic(ctx, 4, 2);
        {
            nk_spacer(ctx);
        }

        // 2) Cancel / Apply buttons
        nk_layout_row_dynamic(ctx, 24, 2);
        {
            // 1)
            if(nk_button_label(ctx, "Cancel")) {
                // Ignore all edits, retain original
                ret = 0;
            }
            // 2)
            if(nk_button_label(ctx, "Apply")) {
                int value;
                // Validate that the options make sense
                ctx->current->edit.active = 0;

                // Address
                bpe->string_address[0][bpe->string_address_len[0]] = '\0';
                if(1 == sscanf(bpe->string_address[0], "%x", &value) && value >= 0 && value <= 65535) {
                    bpe->bp_under_edit.address = value;
                } else {
                    ctx->current->edit.active = 1;
                }
                if(bpe->bp_under_edit.use_range) {
                    // Range Address
                    bpe->string_address[1][bpe->string_address_len[1]] = '\0';
                    if(1 == sscanf(bpe->string_address[1], "%x", &value) && value >= 0 && value <= 65535 && value >= bpe->bp_under_edit.address) {
                        bpe->bp_under_edit.address_range_end = value;
                    } else {
                        ctx->current->edit.active = 1;
                    }
                } else {
                    bpe->bp_under_edit.address_range_end = bpe->bp_under_edit.address;
                }

                // Set the access rights
                bpe->bp_under_edit.access_mask = bpe->bp_under_edit.break_on_exec ? WATCH_EXEC_BREAKPOINT : 0;
                bpe->bp_under_edit.access_mask |= bpe->bp_under_edit.break_on_read ? WATCH_READ_BREAKPOINT : 0;
                bpe->bp_under_edit.access_mask |= bpe->bp_under_edit.break_on_write ? WATCH_WRITE_BREAKPOINT : 0;

                // Set the counters (start by resetting an active count)
                bpe->bp_under_edit.counter_count = 0;
                if(bpe->bp_under_edit.use_counter) {
                    // Counter stop
                    bpe->string_counter[0][bpe->string_counter_len[0]] = '\0';
                    if((bpe->bp_under_edit.counter_stop_value = strtol(bpe->string_counter[0], NULL, 10)) <= 0) {
                        bpe->bp_under_edit.counter_stop_value = 1;
                        bpe->string_counter_len[0] = sprintf(bpe->string_counter[0], "%d", bpe->bp_under_edit.counter_stop_value);
                        ctx->current->edit.active = 1;
                    }
                    // Counter reset
                    bpe->string_counter[1][bpe->string_counter_len[1]] = '\0';
                    if((bpe->bp_under_edit.counter_reset = strtol(bpe->string_counter[1], NULL, 10)) <= 0) {
                        bpe->bp_under_edit.counter_reset = 1;
                        bpe->string_counter_len[1] = sprintf(bpe->string_counter[1], "%d", bpe->bp_under_edit.counter_reset);
                        ctx->current->edit.active = 1;
                    }
                }

                // Some ACTIONS require more processing
                if(ACTION_SWAP == bpe->bp_under_edit.action) {
                    bpe->string_device_len[0] = bpe->string_device_len[1] = 1;
                    bpe->string_device[0][1] = '\0';
                    bpe->string_device[1][1] = '\0';
                    bpe->bp_under_edit.slot = strtol(bpe->string_device[0], NULL, 10);
                    bpe->bp_under_edit.device = strtol(bpe->string_device[1], NULL, 10);
                } else if(ACTION_TYPE == bpe->bp_under_edit.action) {
                    bpe->string_type[bpe->string_type_len] = '\0';
                }
                if(!ctx->current->edit.active) {
                    ret = 1;
                }
            }
        }
    }

    // Set the draw-space to where the combo box wants to be drawn
    // And draw the combo-box last, so the drop down draws over everything "below" it as well.
    nk_layout_space_begin(ctx, NK_STATIC, bpe->combo_rect.h, 1);
    {
        struct nk_rect local = nk_layout_space_rect_to_local(ctx, bpe->combo_rect);
        nk_layout_space_push(ctx, local);
        // The combo box itself (tuned to just fit inside the window)
        if(nk_combo_begin_label(ctx, str_actions[bpe->bp_under_edit.action], nk_vec2(bpe->combo_rect.w, -2 + 4 * 25))) {
            nk_layout_row_dynamic(ctx, 20, 1);
            {
                nk_bool selected = bpe->bp_under_edit.action;
                for(int i = 0; i < num_actions; ++i) {
                    if(nk_selectable_label(ctx, str_actions[i], NK_TEXT_LEFT, &selected)) {
                        bpe->bp_under_edit.action = i;
                        nk_popup_close(ctx);
                        break;
                    }
                }
            }
            nk_combo_end(ctx);
        }
    }
    nk_layout_space_end(ctx);
    nk_end(ctx);

    // Restore previous style and thickness for windows, after nk_end(ctx) otherwise it doesn't apply
    nk_style_pop_color(ctx);
    nk_style_pop_float(ctx);

    return ret;
}

int unk_dlg_file_browser(struct nk_context *ctx, FILE_BROWSER *fb) {
    int ret = -1;
    // The current folder is not loaded, so load it
    if(!fb->dir_contents.items) {
        // fb->file_selected.name_length = strlen(fb->file_selected.name);
        // Get the full path to the active folder
        if(A2_OK != util_dir_get_current(fb->dir_selected.name, PATH_MAX)) {
            return 0;
        }
        fb->dir_selected.name_length = strlen(fb->dir_selected.name);
        if(A2_OK != util_dir_load_contents(&fb->dir_contents)) {
            return 0;
        }
        qsort(fb->dir_contents.data, fb->dir_contents.items, fb->dir_contents.element_size, util_file_info_qsort_cmp);
    }

    if(nk_begin(ctx, "File Browser", nk_rect(0, 0, 600, 600),
                NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_TITLE | NK_WINDOW_BORDER)) {
        // 1) Row with Path
        nk_layout_row_begin(ctx, NK_DYNAMIC, 28, 2);
        {
            nk_layout_row_push(ctx, 0.10f);
            nk_label(ctx, "Path", NK_TEXT_LEFT);
            nk_layout_row_push(ctx, 0.90f);
            if(NK_EDIT_COMMITED &
                    nk_edit_string(ctx, NK_EDIT_SELECTABLE | NK_EDIT_CLIPBOARD | NK_EDIT_SIG_ENTER, fb->dir_selected.name,
                                   &fb->dir_selected.name_length, PATH_MAX - 1, nk_filter_default)) {
                UTIL_FILE file;
                memset(&file, 0, sizeof(file));
                fb->dir_selected.name[fb->dir_selected.name_length] = '\0';
                if(A2_OK == util_file_open(&file, fb->dir_selected.name, "r")) {
                    // If the file could open, well, it's a file that was selected, so use it
                    strcpy(fb->file_selected.name, file.file_display_name);
                    fb->dir_selected.name[file.file_display_name - file.file_path] = '\0';
                    util_file_discard(&file);
                    ret = 1;
                } else {
                    // If it wasn't a file, assume it was a folder and just ignore any errors here
                    util_dir_change(fb->dir_selected.name);
                    // rescan the current, which if it was typed correctly, will be what was typed,
                    // else rescan what was already "current"
                    fb->dir_contents.items = 0;
                    util_dir_get_current(fb->dir_selected.name, PATH_MAX);
                    fb->dir_selected.name_length = strnlen(fb->dir_selected.name, PATH_MAX);
                }
            }
            ctx->current->edit.mode = NK_TEXT_EDIT_MODE_INSERT;
        }
        nk_layout_row_end(ctx);

        // 2) Dir/File list
        nk_layout_row_dynamic(ctx, 500, 1);
        {
            if(nk_group_begin(ctx, "files group", NK_WINDOW_BORDER)) {
                for(int i = 0; i < fb->dir_contents.items; i++) {
                    FILE_INFO *fi = ARRAY_GET(&fb->dir_contents, FILE_INFO, i);

                    // A) The row with the file/dir name details
                    nk_layout_row_begin(ctx, NK_DYNAMIC, 18, 3);
                    {
                        nk_layout_row_push(ctx, 0.8f);
                        if(nk_select_label(ctx, fi->name, NK_TEXT_ALIGN_LEFT, 0)) {
                            if(!fi->is_directory) {
                                // A file to load has been selected
                                fb->file_selected = *fi;
                                ret = 1;
                            } else {
                                // A folder was selected, so dump current and change to selected
                                fb->dir_contents.items = 0;
                                // Not checking for success as the options are to close the dialog (bad) or ignore error.
                                util_dir_change(fi->name);
                            }
                            break;
                        }
                        nk_layout_row_push(ctx, 0.1f);
                        nk_labelf(ctx, NK_TEXT_LEFT, "%zd", fi->size);
                        nk_layout_row_push(ctx, 0.09f);
                        nk_label(ctx, fi->is_directory ? "Dir" : "File", NK_TEXT_LEFT);
                    }
                    nk_layout_row_end(ctx);
                }
                nk_group_end(ctx);
            }
        }

        // 3) Cancel button
        nk_layout_row_dynamic(ctx, 20, 1);
        {
            if(nk_button_label(ctx, "Cancel")) {
                ret = 0;
            }
        }
    }
    nk_end(ctx);

    return ret;
}

// This code works but it's honestly not great
// The find_mode is STATIC so it remembers between calls
// It is 3 bit fields:
// 1 - Sting search, 2 - Hex search, 4 - Case insensitive string search
// A return value of all bits set (7) means cancel.  0 means no action yet and
// 1, 2, or 5 means str, hex, stri search to start
int unk_dlg_find(struct nk_context *ctx, struct nk_rect r, uint8_t *data, int *data_length, int max_len) {
    int ret = 0;
    static int find_mode = 1;
    static int error_status = 0;
    int find_mode_setting;

    if(nk_popup_begin(ctx, NK_POPUP_STATIC, "Find", 0, r)) {
        int edit_state = NK_EDIT_INACTIVE;

        // 1) Search mode (string/hex/case)
        nk_layout_row_dynamic(ctx, 20, 3);
        {
            find_mode_setting = nk_option_label(ctx, "String", (find_mode & 1)) ? (find_mode & ~2) | 1 : find_mode;
            find_mode_setting = nk_option_label(ctx, "Hex", find_mode_setting & 2) ? ((find_mode & ~1) | 2) : find_mode_setting;
            int find_case_setting = nk_option_label_disabled(ctx, "Ignore Case", find_mode_setting & 4, !(find_mode_setting & 1));
            if(find_case_setting) {
                find_mode_setting |= 4;
            }
            if(find_mode_setting != find_mode) {
                if((find_mode_setting & 3) != (find_mode & 3)) {
                    *data_length = 0;
                }
                find_mode = find_mode_setting;
            }
        }

        // 2) Serach string edit box and label
        nk_layout_row_begin(ctx, NK_DYNAMIC, 28, 2);
        {
            nk_layout_row_push(ctx, 0.20f);
            nk_label(ctx, (find_mode & 1) ? "String" : "HEX", NK_TEXT_CENTERED);
            nk_layout_row_push(ctx, 0.80f);
            edit_state = nk_edit_string(ctx, NK_EDIT_SELECTABLE | NK_EDIT_CLIPBOARD | NK_EDIT_SIG_ENTER, (char *)data, data_length, max_len,
                                        (find_mode & 1) ? nk_filter_default : nk_filter_hex);
        }
        nk_layout_row_end(ctx);
        if(!ctx->active->edit.active) {
            ctx->current->edit.active = 1;
            ctx->current->edit.mode = NK_TEXT_EDIT_MODE_REPLACE;
        }

        // 3) OK / Cancel buttons
        nk_layout_row_dynamic(ctx, 28, 2);
        {
            if(edit_state & NK_EDIT_COMMITED || nk_button_label(ctx, "OK")) {
                if((find_mode & 2) && *data_length & 1) {
                    error_status = 1;
                } else {
                    nk_popup_close(ctx);
                    ret = find_mode;
                }
            }
            if(nk_button_label(ctx, "Cancel")) {
                nk_popup_close(ctx);
                ret = 7;
            }
        }

        // 4) Status message
        nk_layout_row_dynamic(ctx, 28, 2);
        {
            nk_label(ctx, error_status ? "Uneven # of hex digits" : "Okay", NK_TEXT_ALIGN_LEFT);
        }
    }
    nk_popup_end(ctx);

    if(ret) {
        error_status = 0;
    }
    return ret;
}

int unk_dlg_symbol_lookup(struct nk_context *ctx, struct nk_rect r, DYNARRAY *symbols_search, char *name, int *name_length, uint16_t *pc) {
    int ret = 0;

    if(nk_popup_begin(ctx, NK_POPUP_STATIC, "Enter a symbol name", 0, r)) {
        int edit_state = NK_EDIT_INACTIVE;

        // 1) Symbol to serach for edit box
        nk_layout_row_dynamic(ctx, 28, 2);
        {
            nk_label(ctx, "Symbol Serach:", NK_TEXT_CENTERED);
            edit_state = nk_edit_string(ctx, NK_EDIT_CLIPBOARD | NK_EDIT_SIG_ENTER, name, name_length, 256, 0);
            if(!ctx->active->edit.active) {
                ctx->current->edit.active = 1;
                ctx->current->edit.mode = NK_TEXT_EDIT_MODE_REPLACE;
            }
        }

        // Symbols that match seach criteria
        nk_layout_row_dynamic(ctx, r.h - 85, 1);
        {
            if(nk_group_begin(ctx, "symbols group", NK_WINDOW_BORDER)) {
                int i;
                for(i = 0; i < symbols_search->items; i++) {
                    int insert = 1;
                    SYMBOL *s = *ARRAY_GET(symbols_search, SYMBOL *, i);
                    if(*name_length) {
                        if(NULL == util_strinstr(s->symbol_name, name, *name_length) && NULL == util_strinstr(s->symbol_source, name, *name_length)) {
                            insert = 0;
                        }
                    }
                    if(insert) {
                        nk_layout_row_begin(ctx, NK_DYNAMIC, 18, 3);
                        {
                            nk_layout_row_push(ctx, 0.64f);
                            if(nk_select_label(ctx, s->symbol_name, NK_TEXT_ALIGN_LEFT, 0)) {
                                *pc = s->pc;
                                ret = 1;
                            }
                            nk_layout_row_push(ctx, 0.15f);
                            nk_labelf(ctx, NK_TEXT_LEFT, "$%04X", s->pc);
                            nk_layout_row_push(ctx, 0.21f);
                            nk_label(ctx, s->symbol_source, NK_TEXT_LEFT);
                        }
                        nk_layout_row_end(ctx);
                    }
                }
                nk_group_end(ctx);
            }
        }

        // 3) OK / Cancel buttons
        nk_layout_row_dynamic(ctx, 28, 2);
        {
            if(edit_state & NK_EDIT_COMMITED || nk_button_label(ctx, "OK")) {
                nk_popup_close(ctx);
                ret = 2;
            }
            if(nk_button_label(ctx, "Cancel")) {
                nk_popup_close(ctx);
                ret = 2;
            }
        }
    }
    nk_popup_end(ctx);

    return ret;
}
