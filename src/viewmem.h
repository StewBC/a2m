// Apple ][+ and //e Emhanced emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#pragma once

typedef enum MEMVIEW_FLAGS {
    MEM_MAPPED_6502     = (1 << 0),
    MEM_MAIN            = (1 << 1),
    MEM_AUX             = (1 << 2),
    MEM_LC_BANK2        = (1 << 3),
} MEMVIEW_FLAGS;

typedef enum CURSOR_FIELD {
    CURSOR_HEX,
    CURSOR_ASCII,
    CURSOR_ADDRESS,
} CURSOR_FIELD;

typedef enum CURSOR_DIGIT {
    CURSOR_DIGIT0,
    CURSOR_DIGIT1,
    CURSOR_DIGIT2,
    CURSOR_DIGIT3,
} CURSOR_DIGIT;

#define set_mem_flag(status, flag_mask)       ((status) |= (flag_mask))
#define tst_mem_flag(status, flag_mask)       ((status) & (flag_mask))
#define clr_mem_flag(status, flag_mask)       ((status) &= ~(flag_mask))

typedef struct MEMVIEW {
    uint16_t view_address;              // address at top line, left
    uint16_t cursor_address;            // where cursor is - might be off-screen
    int rows;                           // how many rows this view shows
    CURSOR_FIELD cursor_field;          // edit mode
    CURSOR_FIELD prev_field;            // when swtched to address - where from
    CURSOR_DIGIT cursor_digit;          // 01 for hex, 0123 for address
    CURSOR_DIGIT cursor_prev_digit;     // when switched to address - digit cursor was at
    MEMVIEW_FLAGS flags;                // what memory to show
} MEMVIEW;

typedef struct MEMSHOW {
    uint32_t active_view_index;     // the # of the view that's active
    uint32_t str_buf_len;           // the length of this char array
    uint32_t find_string_len;       // the length of this char array
    uint16_t last_found_address;    // used for find nex/prev
    int header_height;              // height of the memory window header portion
    int cols;                       // how many cols each view shows
    int dragging;					// scrollbar
    float grab_offset;				// scrollbar
    DYNARRAY *mem_views;            // the array of views
    char *str_buf;                  // the buffer that holds a complete row of text
    char *u8_buf;                   // the buffer that holds the uint8_t's from memory (in that row)
    char *find_string;
} MEMSHOW;

int viewmem_init(MEMSHOW *ms);
int viewmem_process_event(APPLE2 *m, SDL_Event *e, int window);
void viewmem_show(APPLE2 *m);
void viewmem_resize_view(APPLE2 *m);
