#include "forensics_view.h"

#include <SDL.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    FORENSICS_SEL_NONE = UINT_MAX
};

void forensics_view_init(frontend_forensics_state *state)
{
    if (state == NULL) {
        return;
    }
    memset(state, 0, sizeof(*state));
    state->sel_logical_first = FORENSICS_SEL_NONE;
    state->sel_logical_last = FORENSICS_SEL_NONE;
    snprintf(
        state->status,
        sizeof(state->status),
        "Forensics ready — FIND requires pause (PR4 wires history RPCs)");
}

static void forensics_free_logical(frontend_forensics_state *state)
{
    unsigned i;
    if (state == NULL) {
        return;
    }
    for (i = 0u; i < state->logical_count; ++i) {
        free(state->logical[i].text);
        state->logical[i].text = NULL;
    }
    state->logical_count = 0u;
    state->display_count = 0u;
}

void forensics_view_clear_transcript(frontend_forensics_state *state)
{
    if (state == NULL) {
        return;
    }
    forensics_free_logical(state);
    state->sel_logical_first = FORENSICS_SEL_NONE;
    state->sel_logical_last = FORENSICS_SEL_NONE;
    state->has_land_selection = false;
    state->selected_cycle = 0u;
    state->selected_id = 0u;
    state->last_cursor = 0u;
    state->last_more = false;
    forensics_view_set_status(state, "transcript cleared");
}

void forensics_view_set_status(frontend_forensics_state *state, const char *text)
{
    if (state == NULL) {
        return;
    }
    if (text == NULL) {
        state->status[0] = '\0';
        return;
    }
    snprintf(state->status, sizeof(state->status), "%s", text);
}

void forensics_view_open(frontend_forensics_state *state, bool resume_on_exit)
{
    if (state == NULL) {
        return;
    }
    state->open = true;
    state->resume_on_forensics_exit = resume_on_exit;
    state->query_focus_pending = true;
    state->request_close = false;
    state->query_history_index = 0u;
    if (state->status[0] == '\0') {
        forensics_view_set_status(
            state,
            "Forensics ready — FIND requires pause (PR4 wires history RPCs)");
    }
}

bool forensics_view_close(frontend_forensics_state *state)
{
    bool resume = false;
    if (state == NULL || !state->open) {
        return false;
    }
    resume = state->resume_on_forensics_exit;
    state->open = false;
    state->resume_on_forensics_exit = false;
    state->query_focus_pending = false;
    state->request_close = false;
    return resume;
}

bool forensics_view_is_open(const frontend_forensics_state *state)
{
    return state != NULL && state->open;
}

bool forensics_view_resume_on_exit(const frontend_forensics_state *state)
{
    return state != NULL && state->resume_on_forensics_exit;
}

bool forensics_view_query_history_prev(frontend_forensics_state *state)
{
    if (state == NULL || state->query_history_count == 0u) {
        return false;
    }
    if (state->query_history_index < state->query_history_count) {
        state->query_history_index++;
    }
    snprintf(
        state->query,
        sizeof(state->query),
        "%s",
        state->query_history[state->query_history_index - 1u]);
    return true;
}

bool forensics_view_query_history_next(frontend_forensics_state *state)
{
    if (state == NULL || state->query_history_index == 0u) {
        return false;
    }
    state->query_history_index--;
    if (state->query_history_index == 0u) {
        state->query[0] = '\0';
    } else {
        snprintf(
            state->query,
            sizeof(state->query),
            "%s",
            state->query_history[state->query_history_index - 1u]);
    }
    return true;
}

void forensics_view_query_history_push(
    frontend_forensics_state *state,
    const char *text)
{
    unsigned i;
    if (state == NULL || text == NULL || text[0] == '\0') {
        return;
    }
    if (state->query_history_count > 0u &&
        strcmp(state->query_history[0], text) == 0) {
        state->query_history_index = 0u;
        return;
    }
    if (state->query_history_count < FRONTEND_FR_QUERY_HISTORY) {
        state->query_history_count++;
    }
    for (i = state->query_history_count - 1u; i > 0u; --i) {
        memcpy(
            state->query_history[i],
            state->query_history[i - 1u],
            FRONTEND_FR_QUERY_MAX);
    }
    snprintf(state->query_history[0], FRONTEND_FR_QUERY_MAX, "%s", text);
    state->query_history_index = 0u;
}

void forensics_view_render(
    struct nk_context *ctx,
    frontend_forensics_state *state,
    int width,
    int height)
{
    struct nk_rect bounds;
    float margin;
    float toolbar_h = 56.0f;
    float status_h = 22.0f;
    float query_h = 28.0f;
    float content_h;
    unsigned i;

    if (ctx == NULL || state == NULL || !state->open || width <= 0 || height <= 0) {
        return;
    }

    margin = width < 760 ? 10.0f : 24.0f;
    bounds = nk_rect(
        margin,
        margin,
        (float)width - margin * 2.0f,
        (float)height - margin * 2.0f);
    if (bounds.w < 120.0f || bounds.h < 140.0f) {
        return;
    }
    content_h = bounds.h - toolbar_h - status_h - query_h - 18.0f;
    if (content_h < 60.0f) {
        content_h = 60.0f;
    }

    if (nk_begin(
            ctx,
            "Forensics",
            bounds,
            NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_NO_SCROLLBAR)) {
        /* Toolbar row 1 */
        nk_layout_row_begin(ctx, NK_STATIC, 24.0f, 5);
        nk_layout_row_push(ctx, 70.0f);
        if (nk_button_label(ctx, "Close")) {
            state->request_close = true;
        }
        nk_layout_row_push(ctx, 90.0f);
        if (nk_button_label(ctx, "Clear view")) {
            forensics_view_clear_transcript(state);
        }
        nk_layout_row_push(ctx, 70.0f);
        if (nk_button_label(ctx, "Copy")) {
            /* PR4 fills transcript; stub copies a note when empty. */
            if (state->sel_logical_first != FORENSICS_SEL_NONE &&
                state->sel_logical_first < state->logical_count &&
                state->logical[state->sel_logical_first].text != NULL) {
                (void)SDL_SetClipboardText(
                    state->logical[state->sel_logical_first].text);
                forensics_view_set_status(state, "copied selection");
            } else {
                forensics_view_set_status(state, "nothing to copy");
            }
        }
        nk_layout_row_push(ctx, 120.0f);
        if (state->has_land_selection) {
            if (nk_button_label(ctx, "Land at cycle")) {
                forensics_view_set_status(
                    state, "Land at cycle — wired in PR5");
            }
        } else {
            nk_widget_disable_begin(ctx);
            (void)nk_button_label(ctx, "Land at cycle");
            nk_widget_disable_end(ctx);
        }
        nk_layout_row_end(ctx);

        nk_layout_row_dynamic(ctx, 18.0f, 1);
        nk_label(
            ctx,
            "Query FIND/NEXT/READ here — results append below (PR4).",
            NK_TEXT_LEFT);

        /* Transcript */
        nk_layout_row_dynamic(ctx, content_h, 1);
        if (nk_group_begin(ctx, "ForensicsTranscript", NK_WINDOW_BORDER)) {
            if (state->display_count == 0u && state->logical_count == 0u) {
                nk_layout_row_dynamic(ctx, 18.0f, 1);
                nk_label_colored(
                    ctx,
                    "(empty transcript)",
                    NK_TEXT_LEFT,
                    nk_rgb(140, 140, 150));
            } else {
                for (i = 0u; i < state->display_count; ++i) {
                    unsigned li = state->display_logical_index[i];
                    const frontend_fr_logical_entry *entry;
                    char line[FRONTEND_FR_DISPLAY_COLS + 4u];
                    unsigned off;
                    unsigned len;
                    bool selected;

                    if (li >= state->logical_count) {
                        continue;
                    }
                    entry = &state->logical[li];
                    if (entry->text == NULL) {
                        continue;
                    }
                    off = state->display_off[i];
                    len = state->display_len[i];
                    if (off >= strlen(entry->text)) {
                        continue;
                    }
                    if (len > FRONTEND_FR_DISPLAY_COLS) {
                        len = FRONTEND_FR_DISPLAY_COLS;
                    }
                    if (off + len > strlen(entry->text)) {
                        len = (unsigned)strlen(entry->text) - off;
                    }
                    memcpy(line, entry->text + off, len);
                    line[len] = '\0';
                    selected =
                        state->sel_logical_first != FORENSICS_SEL_NONE &&
                        li >= state->sel_logical_first &&
                        li <= state->sel_logical_last;
                    nk_layout_row_dynamic(ctx, 16.0f, 1);
                    if (selected) {
                        nk_label_colored(
                            ctx, line, NK_TEXT_LEFT, nk_rgb(220, 220, 120));
                    } else {
                        nk_label(ctx, line, NK_TEXT_LEFT);
                    }
                    if (nk_widget_is_mouse_clicked(ctx, NK_BUTTON_LEFT)) {
                        state->sel_logical_first = li;
                        state->sel_logical_last = li;
                        if (entry->is_record && entry->has_cycle) {
                            state->has_land_selection = true;
                            state->selected_cycle = entry->cycle;
                            state->selected_id = entry->id;
                        } else {
                            state->has_land_selection = false;
                        }
                    }
                }
            }
            nk_group_end(ctx);
        }

        /* Status + query */
        nk_layout_row_dynamic(ctx, status_h, 1);
        nk_label(ctx, state->status, NK_TEXT_LEFT);

        nk_layout_row_begin(ctx, NK_DYNAMIC, query_h, 2);
        nk_layout_row_push(ctx, 0.12f);
        nk_label(ctx, "Query", NK_TEXT_LEFT);
        nk_layout_row_push(ctx, 0.88f);
        {
            nk_flags edit_flags =
                NK_EDIT_FIELD | NK_EDIT_SIG_ENTER | NK_EDIT_GOTO_END_ON_ACTIVATE;
            nk_flags result;
            if (state->query_focus_pending) {
                nk_edit_focus(ctx, 0);
                state->query_focus_pending = false;
            }
            result = nk_edit_string_zero_terminated(
                ctx,
                edit_flags,
                state->query,
                (int)sizeof(state->query),
                nk_filter_default);
            if (result & NK_EDIT_COMMITED) {
                if (state->query[0] != '\0') {
                    forensics_view_query_history_push(state, state->query);
                    forensics_view_set_status(
                        state,
                        "query entered — FIND dispatch lands in PR4");
                }
            }
        }
        nk_layout_row_end(ctx);
    }
    nk_end(ctx);
}
