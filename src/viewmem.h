// Apple ][+ and //e Emhanced emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#pragma once

typedef enum MEMVIEW_FLAGS {
    mem6502     = (1<<0),
    mem64       = (1<<1),
    mem128      = (1<<2),
    memlcb2     = (1<<3),
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
    uint16_t view_address;
    uint16_t cursor_address;
    CURSOR_FIELD cursor_field;
    CURSOR_FIELD prev_field;
    CURSOR_DIGIT cursor_digit;
    CURSOR_DIGIT cursor_prev_digit;
    MEMVIEW_FLAGS flags;
} MEMVIEW;

typedef struct MEMSHOW {
    uint32_t active_view;       // the # of the view that's active
    uint32_t str_buf_len;       // the length of this char array
    uint32_t find_string_len;   // the length of this char array
    uint16_t last_found_address;
    int rows;                   // how many rows each view shows
    int cols;                   // how many cols each view shows
    DYNARRAY *mem_views;        // the array of views
    char *str_buf;              // the buffer that holds a complete row of text
    char *u8_buf;               // the buffer that holds the uint8_t's from memory (in that row)
    char *find_string;
} MEMSHOW;

int viewmem_init(MEMSHOW *ms);
int viewmem_process_event(APPLE2 *m, SDL_Event *e, int window);
void viewmem_show(APPLE2 *m);
void viewmem_resize_view(APPLE2 *m);
