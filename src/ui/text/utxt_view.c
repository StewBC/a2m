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
    .set_shadow_state  = NULL,
};

static int curses_input_poll(rt_input_event *out) {
    memset(out, 0, sizeof(*out));

    int ch = getch(); // non-blocking - nodelay/timeout(0) set
    if(ch == ERR) {
        return 0;
    }

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

    // "Special" keys
#ifdef KEY_NPAGE
    if(ch == KEY_NPAGE) {
        out->type = RT_IN_KEY;
        out->key = RTK_PAGE_DOWN;
        return 1;
    }
#endif
#ifdef KEY_PPAGE
    if(ch == KEY_PPAGE) {
        out->type = RT_IN_KEY;
        out->key = RTK_PAGE_UP;
        return 1;
    }
#endif
#ifdef KEY_END
    if(ch == KEY_END) {
        out->type = RT_IN_KEY;
        out->key = RTK_END;
        return 1;
    }
#endif
#ifdef KEY_HOME
    if(ch == KEY_HOME) {
        out->type = RT_IN_KEY;
        out->key  = RTK_HOME;
        return 1;
    }
#endif
#ifdef KEY_F0
    if(ch == KEY_F(1)) {
        out->type = RT_IN_KEY;
        out->key = RTK_HELP;
        return 1;
    }
    if(ch == KEY_F(3)) {
        out->type = RT_IN_KEY;
        out->key = RTK_TURBO;
        return 1;
    }
#endif
#ifdef KEY_DC
    if(ch == KEY_DC) {
        out->type = RT_IN_KEY;
        out->key = RTK_DEL;
        return 1;
    }
#endif
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

    // // Ctrl+A..Z => 1..26
    // if(ch >= 1 && ch <= 26) {
    //     out->type = RT_IN_TEXT;
    //     out->ch   = (uint32_t)ch;
    //     out->ctrl = 1;
    //     return 1;
    // }

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
}

void utxt_present(UTXT *v) {
    UNUSED(v);
    refresh();
}

int utxt_process_events(UI *ui, APPLE2 *m) {
    UTXT *v = (UTXT *)ui->user;
    RUNTIME *rt = v->rt;
    v->m = m;

    rt_input_event e;
    int ret = 0;

    while(curses_input_poll(&e)) {
        if(e.type == RT_IN_TEXT) {
            // Apple II KBD uses high bit set.
            if(v->ctrl) {
                if(e.ch > 'Z') {
                    e.ch -= 'a';
                } else {
                    e.ch -= 'A';
                }
                m->ram.RAM_MAIN[KBD] = 0x80 | (uint8_t)(e.ch + 1);
            } else {
                m->ram.RAM_MAIN[KBD] = 0x80 | (uint8_t)(e.ch);
            }
        } else if(e.type == RT_IN_KEY) {
            switch(e.key) {
                case RTK_BACKSPACE:
                    m->ram.RAM_MAIN[KBD] = 0x88;
                    break;
                case RTK_RETURN:
                    m->ram.RAM_MAIN[KBD] = 0x8D;
                    break;
                case RTK_ESCAPE:
                    m->ram.RAM_MAIN[KBD] = 0x9B;
                    break;
                case RTK_TAB:
                    m->ram.RAM_MAIN[KBD] = 0x89;
                    break;
                case RTK_UP:
                    m->ram.RAM_MAIN[KBD] = 0x8B;
                    break;
                case RTK_DOWN:
                    m->ram.RAM_MAIN[KBD] = 0x8A;
                    break;
                case RTK_LEFT:
                    m->ram.RAM_MAIN[KBD] = 0x88;
                    break;
                case RTK_RIGHT:
                    m->ram.RAM_MAIN[KBD] = 0x95;
                    break;
                case RTK_PAGE_UP:
                    v->m->state_flags ^= A2S_OPEN_APPLE;
                    break;
                case RTK_PAGE_DOWN:
                    v->m->state_flags ^= A2S_CLOSED_APPLE;
                    break;
                case RTK_END:
                    v->ctrl ^= 1;
                    break;
                case RTK_DEL:
                    rt_machine_reset(rt);
                    break;
                case RTK_TURBO:
                    if(++rt->turbo_index >= rt->turbo_count) {
                        rt->turbo_index = 0;
                    }
                    rt->turbo_active = rt->turbo[rt->turbo_index];
                    break;
                case RTK_HELP:
                    v->show_help ^= 1;
                    if(v->show_help) {
                        rt_machine_stop(v->rt);
                    } else {
                        rt_machine_run(v->rt);
                    }
                default:
                    break;
            }
        }
    }

    return ret;
}

void utxt_show_help(UTXT *v) {
    WINDOW *rect = derwin(stdscr, 24, 40, 0, 0);
    box(rect, 0, 0);

    int y = 1;
    move(y++, 2);
    addstr("Apple ][+ and //e Enhanced emulator");
    move(y++, 8);
    addstr("by Stefan Wessels, 2025.");
    y++;
    move(y++, 1);
    addstr("Key Quick reference:");
    move(y++, 1);
#ifdef _WIN32
    addstr("CTRL+PAUSE Quit");
#else
    addstr("CTRL-C.... Quit");
#endif
    move(y++, 1);
    addstr("END....... Apple 2 Control");
    move(y++, 1);
    addstr("PAGE UP... Open Apple");
    move(y++, 1);
    addstr("PAGE DOWN. Closed Apple");
    move(y++, 1);
    addstr("DEL....... Reset");
    move(y++, 1);
    addstr("F1........ Help");
    move(y++, 1);
    addstr("F3........ Turbo");
    y++;
    move(y++, 1);
    addstr("Note that the Apple & CTRL keys");
    move(y++, 1);
    addstr("  are tap-active. Tap once to");
    move(y++, 1);
    addstr("  activate and tap again to");
    move(y++, 1);
    addstr("  de-activate.");
    y += 2;
    move(y, 1);
    addstr("There is no debugger in text mode.");
}

void utxt_render_frame(UI *ui, APPLE2 *m, int dirty) {
    (void)dirty;

    UTXT *v = (UTXT *)ui->user;
    v->m = m;

    erase();

    if(v->show_help) {
        utxt_show_help(v);
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
