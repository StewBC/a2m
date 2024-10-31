// Apple ][+ emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#pragma once

typedef struct FILE_BROWSER {
    FILE_INFO   file_selected;
    FILE_INFO   dir_selected;
    int         slot;
    int         device;
    DYNARRAY    dir_contents;
} FILE_BROWSER;

int viewdlg_file_browser(struct nk_context *ctx, FILE_BROWSER *fb);
int viewdlg_find(struct nk_context *ctx, struct nk_rect r, char *address, int *address_length, int max_len);
int viewdlg_hex_address(struct nk_context *ctx, struct nk_rect r, char *address, int *address_length);
