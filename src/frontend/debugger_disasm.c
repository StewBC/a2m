#include "debugger_disasm.h"

#include <stddef.h>

void debugger_disasm_view_init(debugger_disasm_view *view)
{
    if (view == NULL) {
        return;
    }
    view->top_address = 0u;
    view->cursor_address = 0u;
    view->rows = 16u;
    view->active_address_digit = 0u;
    view->address_entry = false;
    view->follow_focus = true;
    view->has_cursor = false;
}

char debugger_disasm_row_marker(bool is_focus, bool is_browse)
{
    if (is_focus) {
        return '>';
    }
    if (is_browse) {
        return '*';
    }
    return ' ';
}

const char *debugger_disasm_footer_hint(debugger_disasm_mode mode)
{
    if (mode == DEBUGGER_DISASM_MODE_INSPECT) {
        return "Right=THEN | Opt+A=goto | Opt+B=BP | Opt+Left unbound";
    }
    return "Right=PC | Opt+A=goto | Opt+B=BP | Opt+Left=set PC";
}

int debugger_disasm_hex_digit(SDL_Keycode sym)
{
    if (sym >= SDLK_0 && sym <= SDLK_9) {
        return (int)(sym - SDLK_0);
    }
    if (sym >= SDLK_a && sym <= SDLK_f) {
        return 10 + (int)(sym - SDLK_a);
    }
    if (sym >= SDLK_KP_0 && sym <= SDLK_KP_9) {
        return (int)(sym - SDLK_KP_0);
    }
    return -1;
}

static void debugger_disasm_default_detach(
    debugger_disasm_view *view,
    const debugger_disasm_ops *ops,
    uint16_t cursor)
{
    view->cursor_address = cursor;
    view->has_cursor = true;
    view->follow_focus = false;
    view->address_entry = false;
    if (ops != NULL && ops->on_detach_browse != NULL) {
        ops->on_detach_browse(ops->ctx, cursor);
    }
}

static void debugger_disasm_note_browse_moved(const debugger_disasm_ops *ops)
{
    if (ops != NULL && ops->on_browse_moved != NULL) {
        ops->on_browse_moved(ops->ctx);
    }
}

void debugger_disasm_apply_address_digit(
    debugger_disasm_view *view,
    const debugger_disasm_ops *ops,
    int digit)
{
    int shift;
    uint16_t mask;

    if (view == NULL || digit < 0 || digit > 15) {
        return;
    }

    shift = (3 - (int)view->active_address_digit) * 4;
    mask = (uint16_t)(0x0fu << shift);
    view->cursor_address = (uint16_t)(
        (view->cursor_address & (uint16_t)~mask) |
        (uint16_t)((uint16_t)digit << shift));
    view->has_cursor = true;
    view->follow_focus = false;

    if (view->active_address_digit >= 3u) {
        view->address_entry = false;
        view->active_address_digit = 0u;
        if (ops != NULL && ops->on_goto_committed != NULL) {
            ops->on_goto_committed(ops->ctx, view->cursor_address);
        } else {
            view->top_address = (uint16_t)(view->cursor_address - 16u);
        }
        debugger_disasm_note_browse_moved(ops);
    } else {
        view->active_address_digit++;
    }
}

static void debugger_disasm_default_browse_up(
    debugger_disasm_view *view,
    const debugger_disasm_ops *ops)
{
    uint8_t rows = view->rows > 0u ? view->rows : 16u;
    uint16_t cur = view->has_cursor ? view->cursor_address : view->top_address;

    cur = (uint16_t)(cur - 1u);
    if (cur < view->top_address ||
        cur >= (uint16_t)(view->top_address + rows * 3u)) {
        view->top_address = (uint16_t)(cur - 8u);
    }
    debugger_disasm_default_detach(view, ops, cur);
    debugger_disasm_note_browse_moved(ops);
}

static void debugger_disasm_default_browse_down(
    debugger_disasm_view *view,
    const debugger_disasm_ops *ops)
{
    uint8_t rows = view->rows > 0u ? view->rows : 16u;
    uint16_t cur = view->has_cursor ? view->cursor_address : view->top_address;

    cur = (uint16_t)(cur + 1u);
    debugger_disasm_default_detach(view, ops, cur);
    if (cur < view->top_address ||
        (uint16_t)(cur - view->top_address) > (uint16_t)(rows * 2u)) {
        view->top_address = (uint16_t)(cur - 8u);
    }
    debugger_disasm_note_browse_moved(ops);
}

static void debugger_disasm_default_browse_page_up(
    debugger_disasm_view *view,
    const debugger_disasm_ops *ops)
{
    uint8_t rows = view->rows > 0u ? view->rows : 16u;

    view->top_address =
        (uint16_t)(view->top_address - (uint16_t)(rows > 1u ? rows - 1u : 1u));
    debugger_disasm_default_detach(view, ops, view->top_address);
    debugger_disasm_note_browse_moved(ops);
}

static void debugger_disasm_default_browse_page_down(
    debugger_disasm_view *view,
    const debugger_disasm_ops *ops)
{
    uint8_t rows = view->rows > 0u ? view->rows : 16u;

    view->top_address =
        (uint16_t)(view->top_address + (uint16_t)(rows > 1u ? rows - 1u : 1u));
    debugger_disasm_default_detach(view, ops, view->top_address);
    debugger_disasm_note_browse_moved(ops);
}

static void debugger_disasm_default_browse_home(
    debugger_disasm_view *view,
    const debugger_disasm_ops *ops,
    bool alt)
{
    if (alt) {
        view->top_address = 0u;
        debugger_disasm_default_detach(view, ops, 0u);
    } else {
        debugger_disasm_default_detach(view, ops, view->top_address);
    }
    debugger_disasm_note_browse_moved(ops);
}

static void debugger_disasm_default_browse_end(
    debugger_disasm_view *view,
    const debugger_disasm_ops *ops,
    bool alt)
{
    if (alt) {
        view->top_address = 0xffe0u;
        debugger_disasm_default_detach(view, ops, 0xffffu);
        debugger_disasm_note_browse_moved(ops);
    }
}

bool debugger_disasm_handle_key(
    debugger_disasm_view *view,
    const debugger_disasm_ops *ops,
    const SDL_KeyboardEvent *key)
{
    SDL_Keycode sym;
    SDL_Keymod mod;
    bool alt;
    bool shift;

    if (view == NULL || ops == NULL || key == NULL || key->type != SDL_KEYDOWN) {
        return false;
    }

    if (ops->keys_enabled != NULL && !ops->keys_enabled(ops->ctx)) {
        return true;
    }

    sym = key->keysym.sym;
    mod = key->keysym.mod;
    alt = (mod & KMOD_ALT) != 0;
    shift = (mod & KMOD_SHIFT) != 0;

    if (sym == SDLK_F7 || sym == SDLK_F9 || sym == SDLK_F10 || sym == SDLK_F11 ||
        sym == SDLK_F12) {
        return false;
    }

    /* Live-only: Opt+B breakpoint. */
    if (alt && !shift && sym == SDLK_b) {
        if (ops->on_toggle_execute_bp != NULL) {
            if (!view->has_cursor && ops->focus_valid != NULL && ops->focus_valid(ops->ctx) &&
                ops->get_focus_pc != NULL) {
                view->cursor_address = ops->get_focus_pc(ops->ctx);
                view->has_cursor = true;
            }
            ops->on_toggle_execute_bp(ops->ctx);
            return true;
        }
        return false;
    }

    /* Live-only: Opt+S symbol lookup. */
    if (alt && sym == SDLK_s) {
        if (ops->on_symbol_lookup != NULL) {
            ops->on_symbol_lookup(ops->ctx);
            return true;
        }
        return false;
    }

    /* Live-only: Opt+M cycle memory source. */
    if (alt && !shift && sym == SDLK_m) {
        if (ops->on_cycle_memory_mode != NULL) {
            ops->on_cycle_memory_mode(ops->ctx);
            return true;
        }
        return false;
    }

    if (alt && !shift && sym == SDLK_a) {
        view->address_entry = !view->address_entry;
        view->active_address_digit = 0u;
        view->follow_focus = false;
        if (!view->has_cursor && ops->focus_valid != NULL && ops->focus_valid(ops->ctx) &&
            ops->get_focus_pc != NULL) {
            view->cursor_address = ops->get_focus_pc(ops->ctx);
            view->has_cursor = true;
        }
        return true;
    }

    if (view->address_entry) {
        int digit;
        if (sym == SDLK_RETURN || sym == SDLK_KP_ENTER) {
            view->address_entry = false;
            view->active_address_digit = 0u;
            if (ops->on_goto_committed != NULL) {
                ops->on_goto_committed(ops->ctx, view->cursor_address);
            } else {
                view->top_address = (uint16_t)(view->cursor_address - 16u);
            }
            debugger_disasm_note_browse_moved(ops);
            return true;
        }
        if (sym == SDLK_ESCAPE) {
            view->address_entry = false;
            view->active_address_digit = 0u;
            return true;
        }
        if (sym == SDLK_HOME) {
            view->active_address_digit = 0u;
            return true;
        }
        if (sym == SDLK_END) {
            view->active_address_digit = 3u;
            return true;
        }
        if (sym == SDLK_LEFT) {
            if (view->active_address_digit > 0u) {
                view->active_address_digit--;
            }
            return true;
        }
        if (sym == SDLK_RIGHT) {
            if (view->active_address_digit >= 3u) {
                view->address_entry = false;
                view->active_address_digit = 0u;
            } else {
                view->active_address_digit++;
            }
            return true;
        }
        digit = debugger_disasm_hex_digit(sym);
        if (digit >= 0) {
            debugger_disasm_apply_address_digit(view, ops, digit);
        }
        return true;
    }

    /* Right: snap browse to focus (live PC / THEN). */
    if (sym == SDLK_RIGHT) {
        if (ops->on_follow_focus != NULL) {
            ops->on_follow_focus(ops->ctx);
        } else if (ops->focus_valid != NULL && ops->focus_valid(ops->ctx) &&
                   ops->get_focus_pc != NULL) {
            uint16_t pc = ops->get_focus_pc(ops->ctx);
            view->cursor_address = pc;
            view->top_address = (uint16_t)(pc - 16u);
            view->has_cursor = true;
            view->follow_focus = true;
            view->address_entry = false;
        }
        return true;
    }

    /* Live-only: Opt+Left set PC. */
    if (sym == SDLK_LEFT && alt) {
        if (ops->on_set_pc != NULL) {
            if (!view->has_cursor && ops->focus_valid != NULL && ops->focus_valid(ops->ctx) &&
                ops->get_focus_pc != NULL) {
                view->cursor_address = ops->get_focus_pc(ops->ctx);
                view->has_cursor = true;
            }
            if (view->has_cursor) {
                ops->on_set_pc(ops->ctx, view->cursor_address);
            }
            return true;
        }
        return false;
    }

    if (sym == SDLK_PAGEUP) {
        if (ops->browse_page_up != NULL) {
            ops->browse_page_up(ops->ctx, view);
        } else {
            debugger_disasm_default_browse_page_up(view, ops);
        }
        return true;
    }
    if (sym == SDLK_PAGEDOWN) {
        if (ops->browse_page_down != NULL) {
            ops->browse_page_down(ops->ctx, view);
        } else {
            debugger_disasm_default_browse_page_down(view, ops);
        }
        return true;
    }
    if (sym == SDLK_UP) {
        if (ops->browse_up != NULL) {
            ops->browse_up(ops->ctx, view);
        } else {
            debugger_disasm_default_browse_up(view, ops);
        }
        return true;
    }
    if (sym == SDLK_DOWN) {
        if (ops->browse_down != NULL) {
            ops->browse_down(ops->ctx, view);
        } else {
            debugger_disasm_default_browse_down(view, ops);
        }
        return true;
    }
    if (sym == SDLK_HOME) {
        if (ops->browse_home != NULL) {
            ops->browse_home(ops->ctx, view, alt);
        } else {
            debugger_disasm_default_browse_home(view, ops, alt);
        }
        return true;
    }
    if (sym == SDLK_END) {
        if (ops->browse_end != NULL) {
            ops->browse_end(ops->ctx, view, alt);
        } else {
            debugger_disasm_default_browse_end(view, ops, alt);
        }
        return true;
    }

    return false;
}
