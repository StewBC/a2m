// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

typedef struct LINE_INFO {
    uint16_t address;
    uint16_t force_byte;
} LINE_INFO;

typedef struct DECODE_ENTRY {
    uint16_t address;
    int line;
    uint8_t opcode;
} DECODE_ENTRY;

typedef struct VIEWDASM {
    int cols;                       // how many cols each line shows
    int dragging;                   // scrollbar
    int header_height;              // height of the window header portion
    int symbol_view;                // What type of symbols to show
    size_t rows;                    // how many code lines this view shows
    float grab_offset;              // scrollbar
    char *str_buf;                  // the buffer that holds a complete row of text
    uint32_t str_buf_len;           // the length of the str_buf char array
    uint16_t cursor_address;        // where cursor is - might be off-screen
    uint16_t cursor_line;           // where cursor is - might be off-screen
    RAMVIEW_FLAGS flags;            // memory to consider when decoding
    CURSOR_FIELD cursor_field;      // ASCII or ADDRESS mode
    CURSOR_DIGIT cursor_digit;      // ADDRESS mode only
    ASSEMBLER_CONFIG assembler_config;
    ASSEMBLER_CONFIG temp_assembler_config;
    ERRORLOG errorlog;
    DYNARRAY line_info;             // !NOTE! ADD/items not used - resized and used
    DYNARRAY valid_stack;           // Valid opcodes gets preference
    DYNARRAY invalid_stack;         // Invalid opcodes still used if valid didn't help
} VIEWDASM;

int unk_dasm_init(VIEWDASM *dv, int model);
void unk_dasm_process_event(UNK *v, SDL_Event *e);
void unk_dasm_resize_view(UNK *v);
void unk_dasm_show(UNK *v, int dirty);
void unk_dasm_shutdown(VIEWDASM *d);
void unk_dasm_put_address_on_line(VIEWDASM *dv, APPLE2 *m, uint16_t address, int line);
