#ifndef A2M_DEBUGGER_DISASM_H
#define A2M_DEBUGGER_DISASM_H

/*
 * Shared disasm chrome for live (F9) and forensic debugger modes.
 * One view + key router; mode ops supply the per-mode verbs.
 *
 * Salvaged from the retired F7 Inspector spine. Currently unwired: the
 * forensic side lands with TimeMachine TM4 (see agents/timemachine.md D1).
 */

#include <stdbool.h>
#include <stdint.h>

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum debugger_disasm_mode {
    DEBUGGER_DISASM_MODE_LIVE = 0,
    DEBUGGER_DISASM_MODE_FORENSIC = 1
} debugger_disasm_mode;

/* Chrome / browse state — one instance per shell. */
typedef struct debugger_disasm_view {
    uint16_t top_address;
    uint16_t cursor_address;
    uint8_t rows;
    uint8_t active_address_digit;
    bool address_entry;
    bool follow_focus; /* live: follow CPU PC; forensic: follow THEN */
    bool has_cursor;
} debugger_disasm_view;

/*
 * Mode-aware accessors. NULL callbacks are skipped (time travel: Opt+Left
 * unbound; Opt+B uses the one live breakpoint list).
 */
typedef struct debugger_disasm_ops {
    void *ctx;
    debugger_disasm_mode mode;

    /* false while live machine is running (keys ignored). Forensic: always true. */
    bool (*keys_enabled)(void *ctx);

    uint16_t (*get_focus_pc)(void *ctx);
    bool (*focus_valid)(void *ctx);

    void (*on_follow_focus)(void *ctx); /* Right */
    void (*on_detach_browse)(void *ctx, uint16_t cursor);
    void (*on_goto_committed)(void *ctx, uint16_t address);
    void (*on_browse_moved)(void *ctx); /* after arrows/page/goto digit */

    /* If NULL, byte-oriented defaults are used. */
    void (*browse_up)(void *ctx, debugger_disasm_view *view);
    void (*browse_down)(void *ctx, debugger_disasm_view *view);
    void (*browse_page_up)(void *ctx, debugger_disasm_view *view);
    void (*browse_page_down)(void *ctx, debugger_disasm_view *view);
    void (*browse_home)(void *ctx, debugger_disasm_view *view, bool alt);
    void (*browse_end)(void *ctx, debugger_disasm_view *view, bool alt);

    /* Opt+B: one list in live and time travel. Opt+Left: live set-PC; NULL
     * (unbound) in time travel. */
    void (*on_toggle_execute_bp)(void *ctx); /* Opt+B */
    void (*on_set_pc)(void *ctx, uint16_t address); /* Opt+Left */
    void (*on_cycle_memory_mode)(void *ctx); /* Opt+M */
    void (*on_symbol_lookup)(void *ctx); /* Opt+S */

    /* Optional frame-step in time travel. */
    void (*on_step_prev)(void *ctx);
    void (*on_step_next)(void *ctx);
} debugger_disasm_ops;

void debugger_disasm_view_init(debugger_disasm_view *view);

/* Shared key table. Returns true if the key was consumed. */
bool debugger_disasm_handle_key(
    debugger_disasm_view *view,
    const debugger_disasm_ops *ops,
    const SDL_KeyboardEvent *key);

void debugger_disasm_apply_address_digit(
    debugger_disasm_view *view,
    const debugger_disasm_ops *ops,
    int digit);

/* Row marker: '>' = focus (live PC / THEN), '*' = browse, ' ' = neither. */
char debugger_disasm_row_marker(bool is_focus, bool is_browse);

/* ASCII footer hint for the panel. */
const char *debugger_disasm_footer_hint(debugger_disasm_mode mode);

int debugger_disasm_hex_digit(SDL_Keycode sym);

#ifdef __cplusplus
}
#endif

#endif /* A2M_DEBUGGER_DISASM_H */
