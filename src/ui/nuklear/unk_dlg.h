// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

typedef struct {
    FILE_INFO file_selected;
    FILE_INFO dir_selected;
    int slot;
    int device;                                         // drive 0 or 1
    int device_type;                                    // SLOT_TYPE_SMARTPORT | SLOT_TYPE_DISKII
    DYNARRAY dir_contents;
    int cursor_line;
    int cursor_offset;
    int dir_select_okay;
} FILE_BROWSER;

typedef struct {
    BREAKPOINT *bp_original;
    BREAKPOINT bp_under_edit;
    struct nk_rect combo_rect;
    char string_address[2][5];
    int string_address_len[2];
    char string_counter[2][9];
    int string_counter_len[2];
    char string_device[2][2];
    int string_device_len[2];
    char string_type[128];
    int string_type_len;
} BREAKPOINT_EDIT;

typedef struct {
    FILE_BROWSER file_browser;
    nk_bool auto_run_after_assemble;
    nk_bool reset_stack;
    char start_address_text[5];                             // XXXX\0
    int start_address_text_len;
    uint16_t start_address;
    int dlg_asm_filebrowser;
    VIEW_FLAGS flags;
    int model;
} ASSEMBLER_CONFIG;

typedef struct {
    char *name;
    int *name_length;
    int cursor_line;
    int cursor_offset;
    uint16_t symbol_address;
    DYNARRAY *symbols_search;
} SYMBOL_LOOKUP;

extern char global_entry_buffer[256];
extern int global_entry_length;

int unk_dlg_assembler_config(struct nk_context *ctx, struct nk_rect r, ASSEMBLER_CONFIG *ac);
int unk_dlg_assembler_errors(UNK *v, struct nk_context *ctx, struct nk_rect r);
int unk_dlg_breakpoint_edit(struct nk_context *ctx, struct nk_rect r, BREAKPOINT_EDIT *bpe);
int unk_dlg_file_browser(struct nk_context *ctx, FILE_BROWSER *fb);
int unk_dlg_find(struct nk_context *ctx, struct nk_rect r, uint8_t *address, int *address_length, int max_len);
int unk_dlg_machine_configure(struct nk_context *ctx, struct nk_rect r, MACHINE_CONFIG *mc, FILE_BROWSER *fb);
int unk_dlg_symbol_lookup(struct nk_context *ctx, struct nk_rect r, SYMBOL_LOOKUP *sl);
