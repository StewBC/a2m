// utxt_lib.c (single-file version)
// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// Public domain.

#include "utxt_lib.h"

#include <locale.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

const UI_OPS utxt_ops = {
    .disk_read_led     = NULL,
    .disk_write_led    = NULL,
    .process_events    = utxt_process_events,
    .ptrig             = NULL,
    .read_button       = NULL,
    .read_axis         = NULL,
    .speaker_toggle    = NULL,
    .speaker_on_cycles = utxt_null,
    .render            = utxt_render_frame,
    .set_runtime       = utxt_set_runtime,
    .set_shadow_flags  = NULL,
};

static int curses_input_poll(rt_input_event *out) {
    memset(out, 0, sizeof(*out));

    int ch = getch(); // non-blocking - nodelay/timeout(0) set
    if(ch == ERR) {
        return 0;
    }

    // Quit on HOME
#ifdef KEY_HOME
    if(ch == KEY_HOME) {
        out->type = RT_IN_KEY;
        out->key  = RTK_HOME;
        return 1;
    }
#endif

    // Arrows
#ifdef KEY_UP
    if(ch == KEY_UP)    {
        out->type = RT_IN_KEY;
        out->key = RTK_UP;
        return 1;
    }
#endif
#ifdef KEY_DOWN
    if(ch == KEY_DOWN)  {
        out->type = RT_IN_KEY;
        out->key = RTK_DOWN;
        return 1;
    }
#endif
#ifdef KEY_LEFT
    if(ch == KEY_LEFT)  {
        out->type = RT_IN_KEY;
        out->key = RTK_LEFT;
        return 1;
    }
#endif
#ifdef KEY_RIGHT
    if(ch == KEY_RIGHT) {
        out->type = RT_IN_KEY;
        out->key = RTK_RIGHT;
        return 1;
    }
#endif

    // Insert (may be reported as KEY_IC)
#ifdef KEY_IC
    if(ch == KEY_IC) {
        out->type = RT_IN_KEY;
        out->key = RTK_INSERT;
        return 1;
    }
#endif

    // Basic keys
    if(ch == 27)   {
        out->type = RT_IN_KEY;
        out->key = RTK_ESCAPE;
        return 1;
    }
    if(ch == '\t') {
        out->type = RT_IN_KEY;
        out->key = RTK_TAB;
        return 1;
    }

#ifdef KEY_ENTER
    if(ch == KEY_ENTER || ch == '\n') {
        out->type = RT_IN_KEY;
        out->key = RTK_RETURN;
        return 1;
    }
#else
    if(ch == '\n' || ch == '\r')      {
        out->type = RT_IN_KEY;
        out->key = RTK_RETURN;
        return 1;
    }
#endif

    // Backspace is inconsistent across terminals
#ifdef KEY_BACKSPACE
    if(ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
        out->type = RT_IN_KEY;
        out->key  = RTK_BACKSPACE;
        return 1;
    }
#else
    if(ch == 127 || ch == 8) {
        out->type = RT_IN_KEY;
        out->key  = RTK_BACKSPACE;
        return 1;
    }
#endif

    // Ctrl+A..Z => 1..26
    if(ch >= 1 && ch <= 26) {
        out->type = RT_IN_TEXT;
        out->ch   = (uint32_t)ch;
        out->ctrl = 1;
        return 1;
    }

    // Plain byte
    out->type = RT_IN_TEXT;
    out->ch   = (uint32_t)(ch & 0xFF);
    return 1;
}

void utxt_config_ui(UTXT *v, INI_STORE *ini_store) {
    (void)v;
    (void)ini_store;
}

int utxt_init(UTXT *v, int model, INI_STORE *ini_store) {
    UNUSED(v);
    UNUSED(model);
    UNUSED(ini_store);

    if(A2_OK != util_console_open_for_text_ui()) {
        return A2_ERR;
    }

    setlocale(LC_ALL, "");

    initscr();
    cbreak();
    noecho();
    curs_set(0);

    // Important for special keys (arrows, home, function keys, etc.)
    keypad(stdscr, TRUE);

    // Non-blocking input (SDL_PollEvent-like)
    nodelay(stdscr, TRUE);

    erase();
    return A2_OK;
}

void utxt_shutdown(UTXT *v) {
    UNUSED(v);
    endwin();
    util_console_close_for_text_ui();
}

void utxt_present(UTXT *v) {
    UNUSED(v);
    refresh();
}

int utxt_process_events(UI *ui, APPLE2 *m) {
    UTXT *v = (UTXT *)ui->user;
    v->m = m;

    rt_input_event e;
    int ret = 0;

    while(curses_input_poll(&e)) {
        if(e.type == RT_IN_KEY && e.key == RTK_HOME) {
            ret = 1;
            break;
        }

        if(e.type == RT_IN_TEXT) {
            // Apple II KBD uses high bit set.
            m->RAM_MAIN[KBD] = 0x80 | (uint8_t)(e.ch & 0x7F);
        } else if(e.type == RT_IN_KEY) {
            switch(e.key) {
                case RTK_BACKSPACE:
                    m->RAM_MAIN[KBD] = 0x80 | 0x08;
                    break;
                case RTK_RETURN:
                    m->RAM_MAIN[KBD] = 0x80 | 0x0D;
                    break;
                case RTK_ESCAPE:
                    m->RAM_MAIN[KBD] = 0x80 | 0x1B;
                    break;
                case RTK_TAB:
                    m->RAM_MAIN[KBD] = 0x80 | 0x09;
                    break;
                case RTK_UP:
                    m->RAM_MAIN[KBD] = 0x8B;
                    break;
                case RTK_DOWN:
                    m->RAM_MAIN[KBD] = 0x8A;
                    break;
                case RTK_LEFT:
                    m->RAM_MAIN[KBD] = 0x88;
                    break;
                case RTK_RIGHT:
                    m->RAM_MAIN[KBD] = 0x95;
                    break;
                case RTK_INSERT:
                    // no clipboard integration in terminal mode
                    break;

                default:
                    break;
            }
        }

        // In Terminal mode ALT/Open/Closed Apple are not portable.
        m->open_apple   = 0;
        m->closed_apple = 0;
    }

    return ret;
}

void utxt_render_frame(UI *ui, APPLE2 *m, int dirty) {
    (void)dirty;

    UTXT *v = (UTXT *)ui->user;
    v->m = m;

    erase();

    if(v->show_help) {
        // utxt_show_help(v);
    } else {
        utxt_apl2_screen_apple2(v);
    }

    utxt_present(v);
}

void utxt_set_runtime(UI *ui, RUNTIME *rt) {
    UTXT *v = (UTXT *)ui->user;
    v->rt = rt;
}

void utxt_null(UI *ui, uint32_t cycles) {
    UNUSED(ui);
    UNUSED(cycles);
}
