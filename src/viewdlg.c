// Apple ][+ emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#include "header.h"

int viewdlg_file_browser(struct nk_context *ctx, FILE_BROWSER *fb) {
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
        qsort(fb->dir_contents.data, fb->dir_contents.items, fb->dir_contents.element_size, util_qsort_cmp);
    }

    if(nk_begin(ctx, "File Browser", nk_rect(0, 0, 600, 600),
        NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_TITLE | NK_WINDOW_BORDER | NK_WINDOW_MOVABLE)) {
        nk_layout_row_begin(ctx, NK_DYNAMIC, 28, 2);
        nk_layout_row_push(ctx, 0.10f);
        nk_label(ctx, "Path", NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_MIDDLE);
        nk_layout_row_push(ctx, 0.90f);
        if(NK_EDIT_COMMITED & nk_edit_string(ctx, NK_EDIT_SELECTABLE | NK_EDIT_CLIPBOARD | NK_EDIT_SIG_ENTER, fb->dir_selected.name, &fb->dir_selected.name_length, PATH_MAX-1, nk_filter_default)) {
            UTIL_FILE file;
            memset(&file, 0, sizeof(file));
            fb->dir_selected.name[fb->dir_selected.name_length] = '\0';
            if(A2_OK == util_file_open(&file, fb->dir_selected.name, "r")) {
                // If the file culd open, well, it's a file that was selected, so use it
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
            }
        }
        ctx->current->edit.mode = NK_TEXT_EDIT_MODE_INSERT;
        nk_layout_row_end(ctx);

        nk_layout_row_dynamic(ctx, 400, 1);
        if(nk_group_begin(ctx, "files group", NK_WINDOW_BORDER)) {
            for(int i = 0; i < fb->dir_contents.items; i++) {
                FILE_INFO *fi = ARRAY_GET(&fb->dir_contents, FILE_INFO, i);
                nk_layout_row_begin(ctx, NK_DYNAMIC, 18, 3);
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
                nk_labelf(ctx, NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_MIDDLE, "%zd", fi->size);
                nk_layout_row_push(ctx, 0.09f);
                nk_label(ctx, fi->is_directory ? "Dir" : "File", NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_CENTERED);
                nk_layout_row_end(ctx);
            }
            nk_group_end(ctx);
        }
        nk_layout_row_dynamic(ctx, 20, 1);
        if(nk_button_label(ctx, "Cancel")) {
            ret = 0;
        }
    }
    nk_end(ctx);

    return ret;
}

int viewdlg_find(struct nk_context *ctx, struct nk_rect r, char *data, int *data_length, int max_len) {
    int ret = 0;
    static int find_mode = 0;
    static int error_status = 0;
    int find_mode_setting;

    if(nk_popup_begin(ctx, NK_POPUP_STATIC, "Find", 0, r)) {
        nk_layout_row_dynamic(ctx, 20, 2);
        find_mode_setting = nk_option_label(ctx, "String", find_mode == 0) ? 0 : find_mode;
        find_mode_setting = nk_option_label(ctx, "Hex", find_mode_setting == 1) ? 1 : find_mode_setting;
        if(find_mode_setting != find_mode) {
            *data_length = 0;
            find_mode = find_mode_setting;
        }
        nk_layout_row_begin(ctx, NK_DYNAMIC, 28, 2);
        nk_layout_row_push(ctx, 0.20f);
        nk_label(ctx, find_mode ? "HEX" : "String" , NK_TEXT_ALIGN_CENTERED | NK_TEXT_ALIGN_MIDDLE);
        nk_layout_row_push(ctx, 0.80f);
        int edit_state = nk_edit_string(ctx, NK_EDIT_SELECTABLE | NK_EDIT_CLIPBOARD | NK_EDIT_SIG_ENTER, data, data_length, max_len, find_mode ? nk_filter_hex : nk_filter_default);
        nk_layout_row_end(ctx);
        if(!ctx->active->edit.active) {
            ctx->current->edit.active = 1;
            ctx->current->edit.mode = NK_TEXT_EDIT_MODE_REPLACE;
        }
        nk_layout_row_dynamic(ctx, 28, 2);
        if(edit_state & NK_EDIT_COMMITED || nk_button_label(ctx, "OK")) {
            if(find_mode && *data_length & 1) {
                error_status = 1;
            } else {
                nk_popup_close(ctx);
                ret = 1 + find_mode;
            }
        }
        if(nk_button_label(ctx, "Cancel")) {
            nk_popup_close(ctx);
            ret = 3;
        }
        nk_layout_row_dynamic(ctx, 28, 2);
        nk_label(ctx, error_status ? "Uneven # of hex digits" : "Okay", NK_TEXT_ALIGN_LEFT);
    }
    nk_popup_end(ctx);
    if(ret) {
        error_status = 0;;
    }
    return ret;
}

int viewdlg_hex_address(struct nk_context *ctx, struct nk_rect r, char *address, int *address_length) {
    int ret = 0;
    if(nk_popup_begin(ctx, NK_POPUP_STATIC, "Enter a HEX address", 0, r)) {
        nk_layout_row_dynamic(ctx, 28, 2);
        nk_label(ctx, "HEX Address:", NK_TEXT_ALIGN_LEFT);
        int edit_state = nk_edit_string(ctx, NK_EDIT_CLIPBOARD | NK_EDIT_SIG_ENTER, address, address_length, 5, nk_filter_hex);
        if(!ctx->active->edit.active) {
            ctx->current->edit.active = 1;
            ctx->current->edit.mode = NK_TEXT_EDIT_MODE_REPLACE;
        }
        nk_layout_row_dynamic(ctx, 28, 2);
        if(edit_state & NK_EDIT_COMMITED || nk_button_label(ctx, "OK")) {
            nk_popup_close(ctx);
            ret = 1;
        }
        if(nk_button_label(ctx, "Cancel")) {
            nk_popup_close(ctx);
            ret = 2;
        }
    }
    nk_popup_end(ctx);
    return ret;
}
