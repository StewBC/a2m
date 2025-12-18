// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

typedef enum {
    RT_IN_NONE = 0,
    RT_IN_TEXT,     // printable char
    RT_IN_KEY       // special key (arrows, etc.)
} rt_in_type;

typedef enum {
    RTK_NONE = 0,
    RTK_BACKSPACE,
    RTK_RETURN,
    RTK_ESCAPE,
    RTK_TAB,
    RTK_UP,
    RTK_DOWN,
    RTK_LEFT,
    RTK_RIGHT,
    RTK_INSERT,
    RTK_HOME,
    RTK_PAGE_UP,
    RTK_PAGE_DOWN,
    RTK_END,
    RTK_DEL,
    RTK_TURBO
} rt_key;

typedef struct {
    rt_in_type type;
    rt_key     key;     // for KEY
    uint32_t   ch;
} rt_input_event;

typedef struct UTXT {
    int show_help;
    int ctrl;
    APPLE2 *m;
    RUNTIME *rt;
} UTXT;

extern const UI_OPS utxt_ops;

void utxt_config_ui(UTXT *v, INI_STORE *ini_store);
int utxt_init(UTXT *v, int model, INI_STORE *ini_store);
int utxt_process_events(UI *ui, APPLE2 *m);
void utxt_shutdown(UTXT *v);
void utxt_present(UTXT *v);
void utxt_render_frame(UI *ui, APPLE2 *m, int dirty);
void utxt_set_runtime(UI *ui, RUNTIME *rt);
void utxt_null(UI *ui, uint32_t cycles);
