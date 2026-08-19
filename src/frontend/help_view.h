#pragma once

#include "nuklear_config.h"

#include <stdbool.h>

#define FRONTEND_HELP_MAX_SECTIONS 128

typedef struct frontend_help_state {
    bool open;
    bool paused_by_help;
    int section_index;
    nk_uint section_scroll_y[FRONTEND_HELP_MAX_SECTIONS];
    nk_uint pending_scroll_y;
    nk_uint content_page_y;
    nk_uint content_max_y;
    bool pending_scroll_restore;
    bool index_popup_open;
    bool index_popup_just_opened;
    char search_buf[128];
    bool search_no_match;
    int  search_section;
    int  search_span;
    bool search_scroll_pending;
} frontend_help_state;

void help_view_init(frontend_help_state *state);
void help_view_open(frontend_help_state *state, bool paused_by_help);
void help_view_close(frontend_help_state *state);
bool help_view_is_open(const frontend_help_state *state);
bool help_view_paused_by_help(const frontend_help_state *state);
bool help_view_select_section(struct nk_context *ctx, frontend_help_state *state, int section_index);
bool help_view_scroll_content(struct nk_context *ctx, frontend_help_state *state, int delta_y);
bool help_view_scroll_content_to(struct nk_context *ctx, frontend_help_state *state, nk_uint y);
/* Run the same search the nav bar's arrows do. Returns false when the pattern
   matches nothing; on success the next render highlights the hit and corrects
   the scroll so the row holding it is on screen. */
bool help_view_search(struct nk_context *ctx, frontend_help_state *state, const char *needle, bool forward);
void help_view_render(struct nk_context *ctx, frontend_help_state *state, struct nk_font *help_font, int width, int height);
