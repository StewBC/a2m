// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

typedef struct VIEWDASM {
    int cols;                       // how many cols each line shows
    int dragging;					// scrollbar
    int header_height;              // height of the window header portion
    int symbol_view;                // What type of symbols to show
    size_t rows;                    // how many code lines this view shows
    float grab_offset;				// scrollbar
    char *str_buf;                  // the buffer that holds a complete row of text
    uint32_t str_buf_len;           // the length of the str_buf char array
    uint16_t cursor_address;        // where cursor is - might be off-screen
    uint16_t top_address;           // address of window at the top line
    uint16_t bottom_address;        // address of last line visible in the window
    RAMVIEW_FLAGS flags;            // memory to consider when decoding
    CURSOR_FIELD cursor_field;      // ASCII or ADDRESS mode
    CURSOR_DIGIT cursor_digit;      // ADDRESS mode only
    ASSEMBLER_CONFIG assembler_config;
    ASSEMBLER_CONFIG temp_assembler_config;
    ERRORLOG errorlog;
} VIEWDASM;

int unk_dasm_init(VIEWDASM *dv);
void unk_dasm_process_event(UNK *v, SDL_Event *e);
void unk_dasm_resize_view(UNK *v);
void unk_dasm_show(UNK *v, int dirty);
void unk_dasm_shutdown(VIEWDASM *d);
void unk_dasm_recenter_view(UNK *v, int dirty);
