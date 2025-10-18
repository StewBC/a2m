// Apple ][+ and //e Emhanced emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#pragma once

#define MEMSHOW_ROWS                16

typedef struct MEMLINE {
    uint16_t address;
    uint16_t id;                                            // This split ID
    uint16_t first_line;                                    // First line of this split
    uint16_t last_line;                                     // Last line of this split
    char *line_text;
} MEMLINE;

typedef struct MEMSHOW {
    DYNARRAY *lines;
    int num_lines;
    int line_length;
    int cursor_x;
    int cursor_y;
    uint16_t cursor_address;
    uint8_t *find_string;
    int find_string_len;
    int last_found_address;
    int edit_mode_ascii: 1;
} MEMSHOW;

void viewmem_active_range(APPLE2 *m, int id);
void viewmem_cursor_down(APPLE2 *m);
void viewmem_cursor_left(APPLE2 *m);
void viewmem_cursor_right(APPLE2 *m);
void viewmem_cursor_up(APPLE2 *m);
void viewmem_find_string(APPLE2 *m);
void viewmem_find_string_reverse(APPLE2 *m);
int viewmem_init(MEMSHOW *ms, int num_lines);
void viewmem_join_range(APPLE2 *m, int direction);
void viewmem_new_range(APPLE2 *m);
void viewmem_page_down(APPLE2 *m);
void viewmem_page_up(APPLE2 *m);
int viewmem_process_event(APPLE2 *m, SDL_Event *e, int window);
void viewmem_set_range_pc(APPLE2 *m, uint16_t address);
void viewmem_show(APPLE2 *m);
void viewmem_update(APPLE2 *m);
