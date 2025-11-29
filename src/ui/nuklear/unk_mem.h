// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

typedef struct MEMVIEW {
    uint16_t view_address;              // address at top line, left
    uint16_t cursor_address;            // where cursor is - might be off-screen
    int rows;                           // how many rows this view shows
    CURSOR_FIELD cursor_field;          // edit mode
    CURSOR_FIELD prev_field;            // when swtched to address - where from
    CURSOR_DIGIT cursor_digit;          // 01 for hex, 0123 for address
    CURSOR_DIGIT cursor_prev_digit;     // when switched to address - digit cursor was at
    RAMVIEW_FLAGS flags;                // what memory to show
} MEMVIEW;

typedef struct VIEWMEM {
    uint32_t active_view_index;         // the # of the view that's active
    uint32_t str_buf_len;               // the length of the str_buf char array
    uint32_t find_string_cap;           // the size of the find_string array
    int find_string_len;                // the length of this search term
    int header_height;                  // height of the memory window header portion
    int cols;                           // how many cols each view shows
    int dragging;					    // scrollbar
    uint16_t last_found_address;        // used for find nex/prev
    float grab_offset;				    // scrollbar
    DYNARRAY *memviews;                 // the array of views
    char *str_buf;                      // the buffer that holds a complete row of text
    uint8_t *u8_buf;                    // the buffer that holds the uint8_t's from memory (in that row)
    char *find_string;
} VIEWMEM;

int unk_mem_init(VIEWMEM *ms);
int unk_mem_process_event(UNK *v, SDL_Event *e, int window);
void unk_mem_resize_view(UNK *v);
void unk_mem_show(UNK *v);
