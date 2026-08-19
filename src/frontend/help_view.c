#include "help_view.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "generated/help_content.inc"
#include "re.h"

#define HELP_INLINE_CODE_ON '\001'
#define HELP_INLINE_CODE_OFF '\002'
#define HELP_TEXT_BG nk_rgba(0x00, 0x00, 0x00, 0x00)

#define HELP_PALETTE_BLACK nk_rgb(0x00, 0x00, 0x00)
#define HELP_PALETTE_WHITE nk_rgb(0xff, 0xff, 0xff)
#define HELP_PALETTE_RED nk_rgb(0x81, 0x33, 0x38)
#define HELP_PALETTE_CYAN nk_rgb(0x75, 0xce, 0xc8)
#define HELP_PALETTE_PURPLE nk_rgb(0x8e, 0x3c, 0x97)
#define HELP_PALETTE_GREEN nk_rgb(0x56, 0xac, 0x4d)
#define HELP_PALETTE_BLUE nk_rgb(0x2e, 0x2c, 0x9b)
#define HELP_PALETTE_YELLOW nk_rgb(0xed, 0xf1, 0x71)
#define HELP_PALETTE_ORANGE nk_rgb(0x8e, 0x50, 0x29)
#define HELP_PALETTE_BROWN nk_rgb(0x55, 0x38, 0x00)
#define HELP_PALETTE_LIGHT_RED nk_rgb(0xc4, 0x6c, 0x71)
#define HELP_PALETTE_DARK_GRAY nk_rgb(0x4a, 0x4a, 0x4a)
#define HELP_PALETTE_GRAY nk_rgb(0x7b, 0x7b, 0x7b)
#define HELP_PALETTE_LIGHT_GREEN nk_rgb(0xa9, 0xff, 0x9f)
#define HELP_PALETTE_LIGHT_BLUE nk_rgb(0x70, 0x6d, 0xeb)
#define HELP_PALETTE_LIGHT_GRAY nk_rgb(0xb2, 0xb2, 0xb2)

#define HELP_COLOR_BG HELP_PALETTE_DARK_GRAY
#define HELP_COLOR_PANEL HELP_PALETTE_BLUE
#define HELP_COLOR_BORDER HELP_PALETTE_LIGHT_BLUE
#define HELP_COLOR_HEADING HELP_PALETTE_WHITE
#define HELP_COLOR_BODY HELP_PALETTE_WHITE
#define HELP_COLOR_H3 HELP_PALETTE_YELLOW
#define HELP_COLOR_BULLET HELP_PALETTE_YELLOW
#define HELP_COLOR_NUMBER HELP_PALETTE_YELLOW
#define HELP_COLOR_CODE HELP_PALETTE_LIGHT_GRAY
#define HELP_COLOR_TABLE HELP_PALETTE_CYAN
#define HELP_COLOR_TABLE_HEADER HELP_PALETTE_LIGHT_RED
#define HELP_COLOR_SECTION_ACTIVE HELP_PALETTE_PURPLE
#define HELP_COLOR_SECTION_HOVER HELP_PALETTE_LIGHT_BLUE
#define HELP_COLOR_SECTION_DISABLED HELP_PALETTE_DARK_GRAY
#define HELP_COLOR_MATCH HELP_PALETTE_YELLOW
#define HELP_COLOR_MATCH_TEXT HELP_PALETTE_BLACK

#define HELP_HL_MAX_MATCHES 32
#define HELP_HL_UNDERLINE_H 2.0f

/* One search hit inside a single drawn line, in marker-stripped coordinates. */
typedef struct help_hl_match {
    int start;
    int end;
} help_hl_match;

/* Search highlighting for one render pass. `re` is recompiled every frame by
   help_view_render: re_compile() returns a pointer into one static internal
   buffer, so a compiled pattern must never be cached across calls. */
typedef struct help_highlight {
    re_t re;         /* NULL when no search is active: highlighting is off */
    bool anchored;   /* pattern starts with '^': only the first hit is real */
    bool current;    /* span the search jumped to: inverse band, not underline */
    bool hit_seen;   /* a hit in that span has been drawn or measured */
    float hit_y;     /* screen y of the row holding it, for the scroll fix */
} help_highlight;

static void help_strip_markers(char *dst, const char *src, int max);
static void help_tolower_buf(char *buf);

void help_view_init(frontend_help_state *state)
{
    if (state == NULL) {
        return;
    }
    state->open = false;
    state->paused_by_help = false;
    state->section_index = 0;
    state->pending_scroll_y = 0;
    state->content_page_y = 400;
    state->content_max_y = 0;
    state->pending_scroll_restore = false;
    state->index_popup_open = false;
    state->index_popup_just_opened = false;
    state->search_buf[0] = '\0';
    state->search_no_match = false;
    state->search_section = 0;
    state->search_span = -1;
    state->search_scroll_pending = false;
    memset(state->section_scroll_y, 0, sizeof(state->section_scroll_y));
}

void help_view_open(frontend_help_state *state, bool paused_by_help)
{
    if (state == NULL) {
        return;
    }
    state->open = true;
    state->paused_by_help = paused_by_help;
    if (state->section_index < 0 || state->section_index >= help_section_count) {
        state->section_index = 0;
    }
    if (state->section_index < FRONTEND_HELP_MAX_SECTIONS) {
        state->pending_scroll_y = state->section_scroll_y[state->section_index];
        state->pending_scroll_restore = true;
    }
    state->search_section = state->section_index;
    state->search_span = -1;
    state->search_scroll_pending = false;
}

void help_view_close(frontend_help_state *state)
{
    if (state == NULL) {
        return;
    }
    state->open = false;
    state->paused_by_help = false;
    state->index_popup_open = false;
    state->index_popup_just_opened = false;
    state->search_no_match = false;
}

bool help_view_is_open(const frontend_help_state *state)
{
    return state != NULL && state->open;
}

bool help_view_paused_by_help(const frontend_help_state *state)
{
    return state != NULL && state->paused_by_help;
}

static int help_view_clamp_section_index(int section_index)
{
    if (help_section_count <= 0) {
        return 0;
    }
    if (section_index < 0) {
        return 0;
    }
    if (section_index >= help_section_count) {
        return help_section_count - 1;
    }
    return section_index;
}

static void help_view_store_current_scroll(struct nk_context *ctx, frontend_help_state *state)
{
    nk_uint x = 0;
    nk_uint y = 0;

    if (ctx == NULL || state == NULL ||
        state->section_index < 0 ||
        state->section_index >= FRONTEND_HELP_MAX_SECTIONS) {
        return;
    }

    /* Reachable from the SDL event loop (arrow keys, help search) with no
       window open, where nk_group_get_scroll would assert. The render pass
       stores this section's scroll every frame, so there is nothing to save. */
    if (ctx->current == NULL) {
        return;
    }

    nk_group_get_scroll(ctx, "HelpContent", &x, &y);
    state->section_scroll_y[state->section_index] = y;
}

static void help_view_request_restore(frontend_help_state *state)
{
    if (state == NULL ||
        state->section_index < 0 ||
        state->section_index >= FRONTEND_HELP_MAX_SECTIONS) {
        return;
    }

    state->pending_scroll_y = state->section_scroll_y[state->section_index];
    state->pending_scroll_restore = true;
}

bool help_view_select_section(struct nk_context *ctx, frontend_help_state *state, int section_index)
{
    int next;

    if (ctx == NULL || state == NULL || help_section_count <= 0) {
        return false;
    }

    next = help_view_clamp_section_index(section_index);
    if (next == state->section_index) {
        return true;
    }

    help_view_store_current_scroll(ctx, state);
    state->section_index = next;
    help_view_request_restore(state);
    state->search_section = next;
    state->search_span = -1;
    state->search_scroll_pending = false;
    return true;
}

bool help_view_scroll_content(struct nk_context *ctx, frontend_help_state *state, int delta_y)
{
    nk_uint y = 0;
    nk_uint old_y = 0;

    if (ctx == NULL || state == NULL) {
        return false;
    }

    (void)ctx;
    if (state->section_index >= 0 && state->section_index < FRONTEND_HELP_MAX_SECTIONS) {
        y = state->section_scroll_y[state->section_index];
    }
    old_y = y;
    if (delta_y < 0) {
        nk_uint amount = (nk_uint)(-delta_y);
        y = amount > y ? 0 : y - amount;
    } else {
        y += (nk_uint)delta_y;
        if (y > state->content_max_y) {
            y = state->content_max_y;
        }
    }
    if (y == old_y) {
        return false;
    }
    if (state->section_index >= 0 && state->section_index < FRONTEND_HELP_MAX_SECTIONS) {
        state->section_scroll_y[state->section_index] = y;
    }
    state->pending_scroll_y = y;
    state->pending_scroll_restore = true;
    return true;
}

bool help_view_scroll_content_to(struct nk_context *ctx, frontend_help_state *state, nk_uint y)
{
    nk_uint old_y = 0;

    if (ctx == NULL || state == NULL) {
        return false;
    }

    (void)ctx;
    if (y > state->content_max_y) {
        y = state->content_max_y;
    }
    if (state->section_index >= 0 && state->section_index < FRONTEND_HELP_MAX_SECTIONS) {
        old_y = state->section_scroll_y[state->section_index];
        if (y == old_y) {
            return false;
        }
        state->section_scroll_y[state->section_index] = y;
    } else if (y == 0) {
        return false;
    }
    state->pending_scroll_y = y;
    state->pending_scroll_restore = true;
    return true;
}

static float help_row_height(struct nk_context *ctx)
{
    if (ctx != NULL && ctx->style.font != NULL) {
        return ctx->style.font->height + 2.0f;
    }
    return 12.0f;
}

static float help_text_width(struct nk_context *ctx, const char *text, int len)
{
    const struct nk_user_font *font;

    if (ctx == NULL || text == NULL || len <= 0 || ctx->style.font == NULL) {
        return 0.0f;
    }

    font = ctx->style.font;
    return font->width(font->userdata, font->height, text, len);
}

static float help_encoded_text_width(struct nk_context *ctx, const char *text)
{
    const char *run;
    const char *p;
    float width = 0.0f;

    if (ctx == NULL || text == NULL) {
        return 0.0f;
    }

    run = text;
    for (p = text; ; ++p) {
        if (*p == HELP_INLINE_CODE_ON || *p == HELP_INLINE_CODE_OFF || *p == '\0') {
            if (p > run) {
                width += help_text_width(ctx, run, (int)(p - run));
            }
            if (*p == '\0') {
                break;
            }
            run = p + 1;
        }
    }
    return width;
}

static int help_append_char(char *out, int out_size, int pos, char ch)
{
    if (out == NULL || out_size <= 0 || pos >= out_size - 1) {
        return pos;
    }
    out[pos++] = ch;
    out[pos] = '\0';
    return pos;
}

static int help_append_text(char *out, int out_size, int pos, const char *text, int len)
{
    int i;

    for (i = 0; i < len; ++i) {
        pos = help_append_char(out, out_size, pos, text[i]);
    }
    return pos;
}

static bool help_update_inline_code_state(bool code, const char *text)
{
    const char *p;

    if (text == NULL) {
        return code;
    }

    for (p = text; *p != '\0'; ++p) {
        if (*p == HELP_INLINE_CODE_ON) {
            code = true;
        } else if (*p == HELP_INLINE_CODE_OFF) {
            code = false;
        }
    }
    return code;
}

/* Collect every hit in one drawn line. Matching runs on the marker-stripped,
   lowercased copy so it agrees exactly with help_search_execute; the line is
   whatever ended up on screen, which is what makes wrapped text work. */
static int help_hl_collect(
    const help_highlight *hl,
    const char *text,
    help_hl_match *out,
    int max)
{
    char lowered[2048];
    int len;
    int pos = 0;
    int count = 0;

    if (hl == NULL || hl->re == NULL || text == NULL || out == NULL) {
        return 0;
    }

    help_strip_markers(lowered, text, (int)sizeof(lowered));
    help_tolower_buf(lowered);
    len = (int)strlen(lowered);

    while (pos < len && count < max) {
        int match_len = 0;
        int idx = re_matchp(hl->re, lowered + pos, &match_len);

        if (idx < 0) {
            break;
        }
        if (match_len <= 0) {
            /* A pattern that can match nothing (`a*`) would spin here. */
            pos += idx + 1;
            continue;
        }
        out[count].start = pos + idx;
        out[count].end = pos + idx + match_len;
        pos = out[count].end;
        ++count;
        if (hl->anchored) {
            /* Rescanning from `pos` would let '^' match mid-line. */
            break;
        }
    }
    return count;
}

static bool help_hl_covers(const help_hl_match *matches, int count, int index)
{
    int i;

    for (i = 0; i < count; ++i) {
        if (index >= matches[i].start && index < matches[i].end) {
            return true;
        }
    }
    return false;
}

/* True while the row holding the current hit still has to be located. Rows
   scrolled out of view are laid out but not drawn, so they are measured (not
   painted) to keep the scroll correction exact. */
static bool help_hl_measuring(const help_highlight *hl)
{
    return hl != NULL && hl->re != NULL && hl->current && !hl->hit_seen;
}

static void help_draw_inline_at(
    struct nk_context *ctx,
    struct nk_command_buffer *canvas,
    struct nk_rect bounds,
    const char *text,
    struct nk_color base_color,
    help_highlight *hl,
    bool visible)
{
    const struct nk_user_font *font;
    help_hl_match matches[HELP_HL_MAX_MATCHES];
    int match_count;
    int stripped_pos = 0;
    bool current;
    const char *run;
    const char *p;
    float x;
    bool code = false;

    if (ctx == NULL || canvas == NULL || text == NULL || ctx->style.font == NULL) {
        return;
    }

    current = hl != NULL && hl->current;
    match_count = help_hl_collect(hl, text, matches, HELP_HL_MAX_MATCHES);
    if (match_count > 0 && current && !hl->hit_seen) {
        hl->hit_seen = true;
        hl->hit_y = bounds.y;
    }
    if (!visible) {
        return;
    }

    font = ctx->style.font;
    x = bounds.x;
    p = text;
    run = p;
    while (true) {
        if (*p == HELP_INLINE_CODE_ON || *p == HELP_INLINE_CODE_OFF || *p == '\0') {
            int len = (int)(p - run);
            if (len > 0) {
                struct nk_color run_color = code ? HELP_COLOR_CODE : base_color;
                int i = 0;

                /* Split the run again wherever a hit starts or ends. */
                while (i < len) {
                    bool hit = help_hl_covers(matches, match_count, stripped_pos + i);
                    int j = i + 1;
                    float w;

                    while (j < len &&
                           help_hl_covers(matches, match_count, stripped_pos + j) == hit) {
                        ++j;
                    }
                    w = help_text_width(ctx, run + i, j - i);
                    if (hit) {
                        /* nk_convert forwards only the foreground colour, so
                           nk_draw_text's background argument is dropped by this
                           backend: the band has to be a real filled rect. */
                        if (current) {
                            nk_fill_rect(
                                canvas,
                                nk_rect(x, bounds.y, w, bounds.h),
                                0.0f,
                                HELP_COLOR_MATCH);
                        } else {
                            nk_fill_rect(
                                canvas,
                                nk_rect(
                                    x,
                                    bounds.y + bounds.h - HELP_HL_UNDERLINE_H,
                                    w,
                                    HELP_HL_UNDERLINE_H),
                                0.0f,
                                HELP_COLOR_MATCH);
                        }
                    }
                    nk_draw_text(
                        canvas,
                        nk_rect(x, bounds.y, w + 1.0f, bounds.h),
                        run + i,
                        j - i,
                        font,
                        HELP_TEXT_BG,
                        (hit && current) ? HELP_COLOR_MATCH_TEXT : run_color);
                    x += w;
                    i = j;
                }
                stripped_pos += len;
            }
            if (*p == '\0') {
                break;
            }
            code = *p == HELP_INLINE_CODE_ON;
            ++p;
            run = p;
        } else {
            ++p;
        }
    }
}

static void help_inline_row(
    struct nk_context *ctx,
    const char *text,
    struct nk_color color,
    help_highlight *hl)
{
    struct nk_rect bounds;
    bool visible;

    if (ctx == NULL) {
        return;
    }

    nk_layout_row_dynamic(ctx, help_row_height(ctx), 1);
    visible = nk_widget(&bounds, ctx) != 0;
    if (!visible && !help_hl_measuring(hl)) {
        return;
    }

    help_draw_inline_at(
        ctx, nk_window_get_canvas(ctx), bounds, text != NULL ? text : "", color, hl, visible);
}

static void help_inline_row_indented(
    struct nk_context *ctx,
    const char *text,
    struct nk_color color,
    float indent,
    help_highlight *hl)
{
    struct nk_rect bounds;
    bool visible;

    if (ctx == NULL) {
        return;
    }

    nk_layout_row_dynamic(ctx, help_row_height(ctx), 1);
    visible = nk_widget(&bounds, ctx) != 0;
    if (!visible && !help_hl_measuring(hl)) {
        return;
    }

    bounds.x += indent;
    bounds.w -= indent;
    help_draw_inline_at(
        ctx, nk_window_get_canvas(ctx), bounds, text != NULL ? text : "", color, hl, visible);
}

static void help_marker_text_row(
    struct nk_context *ctx,
    const char *marker,
    const char *text,
    struct nk_color marker_color,
    struct nk_color text_color,
    float text_indent,
    help_highlight *hl)
{
    struct nk_rect bounds;
    struct nk_command_buffer *canvas;
    bool visible;

    if (ctx == NULL) {
        return;
    }

    nk_layout_row_dynamic(ctx, help_row_height(ctx), 1);
    visible = nk_widget(&bounds, ctx) != 0;
    if (!visible && !help_hl_measuring(hl)) {
        return;
    }

    canvas = nk_window_get_canvas(ctx);
    if (visible && marker != NULL && marker[0] != '\0') {
        nk_draw_text(
            canvas,
            nk_rect(bounds.x, bounds.y, text_indent + 1.0f, bounds.h),
            marker,
            (int)strlen(marker),
            ctx->style.font,
            HELP_TEXT_BG,
            marker_color);
    }
    bounds.x += text_indent;
    bounds.w -= text_indent;
    help_draw_inline_at(ctx, canvas, bounds, text != NULL ? text : "", text_color, hl, visible);
}

static void help_wrap_text(
    struct nk_context *ctx,
    const char *text,
    struct nk_color color,
    float first_indent,
    float rest_indent,
    const char *marker,
    struct nk_color marker_color,
    help_highlight *hl)
{
    char line[2048];
    char candidate[2048];
    char token[512];
    const char *p;
    int line_len = 0;
    int row = 0;
    bool code = false;

    if (ctx == NULL || text == NULL) {
        return;
    }

    p = text;
    line[0] = '\0';
    while (*p != '\0') {
        int token_len = 0;
        int candidate_len = 0;
        float indent;
        float available;

        while (*p == ' ') {
            ++p;
        }
        while (*p != '\0' && *p != ' ' && token_len < (int)sizeof(token) - 1) {
            token[token_len++] = *p++;
        }
        token[token_len] = '\0';
        if (token_len == 0) {
            break;
        }

        candidate_len = help_append_text(candidate, sizeof(candidate), 0, line, line_len);
        if (candidate_len > 0) {
            candidate_len = help_append_char(candidate, sizeof(candidate), candidate_len, ' ');
        } else if (code) {
            candidate_len = help_append_char(candidate, sizeof(candidate), candidate_len, HELP_INLINE_CODE_ON);
        }
        candidate_len = help_append_text(candidate, sizeof(candidate), candidate_len, token, token_len);
        candidate[candidate_len] = '\0';

        indent = row == 0 ? first_indent : rest_indent;
        available = nk_window_get_content_region(ctx).w - indent - 8.0f;
        if (line_len > 0 && help_encoded_text_width(ctx, candidate) > available) {
            if (row == 0 && marker != NULL) {
                help_marker_text_row(ctx, marker, line, marker_color, color, first_indent, hl);
            } else {
                help_inline_row_indented(ctx, line, color, indent, hl);
            }
            ++row;
            line_len = 0;
            line[0] = '\0';
            candidate_len = 0;
            if (code) {
                candidate_len = help_append_char(candidate, sizeof(candidate), candidate_len, HELP_INLINE_CODE_ON);
            }
            candidate_len = help_append_text(candidate, sizeof(candidate), candidate_len, token, token_len);
            candidate[candidate_len] = '\0';
            indent = rest_indent;
            available = nk_window_get_content_region(ctx).w - indent - 8.0f;
        }

        if (help_encoded_text_width(ctx, candidate) > available) {
            /* Token alone overflows — split character by character. */
            const char *cp = candidate;
            char piece[2048];
            int piece_len = 0;
            while (*cp != '\0') {
                float p_indent = row == 0 ? first_indent : rest_indent;
                float p_avail = nk_window_get_content_region(ctx).w - p_indent - 8.0f;
                while (*cp != '\0') {
                    char test[2048];
                    int tlen;
                    if (*cp == HELP_INLINE_CODE_ON || *cp == HELP_INLINE_CODE_OFF) {
                        piece_len = help_append_char(piece, sizeof(piece), piece_len, *cp++);
                        continue;
                    }
                    tlen = help_append_text(test, sizeof(test), 0, piece, piece_len);
                    tlen = help_append_char(test, sizeof(test), tlen, *cp);
                    test[tlen] = '\0';
                    if (help_encoded_text_width(ctx, test) > p_avail) {
                        break;
                    }
                    piece_len = help_append_char(piece, sizeof(piece), piece_len, *cp++);
                }
                piece[piece_len] = '\0';
                if (piece_len > 0) {
                    if (row == 0 && marker != NULL) {
                        help_marker_text_row(ctx, marker, piece, marker_color, color, first_indent, hl);
                    } else {
                        help_inline_row_indented(ctx, piece, color, p_indent, hl);
                    }
                    ++row;
                    piece_len = 0;
                    piece[0] = '\0';
                } else if (*cp != '\0') {
                    ++cp;
                }
            }
            line_len = 0;
            line[0] = '\0';
        } else {
            memcpy(line, candidate, (size_t)candidate_len + 1u);
            line_len = candidate_len;
        }
        code = help_update_inline_code_state(code, token);
    }

    if (row == 0 && marker != NULL) {
        help_marker_text_row(ctx, marker, line, marker_color, color, first_indent, hl);
    } else {
        help_inline_row_indented(ctx, line, color, row == 0 ? first_indent : rest_indent, hl);
    }
}

static void help_inline_wrap_if_needed(
    struct nk_context *ctx,
    const char *text,
    struct nk_color color,
    help_highlight *hl)
{
    float available;

    if (ctx == NULL || text == NULL) {
        return;
    }

    available = nk_window_get_content_region(ctx).w - 8.0f;
    if (help_encoded_text_width(ctx, text) <= available) {
        help_inline_row(ctx, text, color, hl);
        return;
    }
    help_wrap_text(ctx, text, color, 0.0f, 0.0f, NULL, color, hl);
}

static void help_bullet_row(struct nk_context *ctx, const char *text, help_highlight *hl)
{
    struct nk_rect bounds;
    struct nk_command_buffer *canvas;
    float marker_w;
    bool visible;

    if (ctx == NULL) {
        return;
    }

    nk_layout_row_dynamic(ctx, help_row_height(ctx), 1);
    visible = nk_widget(&bounds, ctx) != 0;
    if (!visible && !help_hl_measuring(hl)) {
        return;
    }

    canvas = nk_window_get_canvas(ctx);
    marker_w = help_text_width(ctx, "- ", 2);
    if (visible) {
        nk_draw_text(
            canvas,
            nk_rect(bounds.x, bounds.y, marker_w + 1.0f, bounds.h),
            "- ",
            2,
            ctx->style.font,
            HELP_TEXT_BG,
            HELP_COLOR_BULLET);
    }
    bounds.x += marker_w;
    bounds.w -= marker_w;
    help_draw_inline_at(ctx, canvas, bounds, text != NULL ? text : "", HELP_COLOR_BODY, hl, visible);
}

static void help_bullet_wrap_if_needed(struct nk_context *ctx, const char *text, help_highlight *hl)
{
    float marker_w;
    float available;

    if (ctx == NULL || text == NULL) {
        return;
    }

    marker_w = help_text_width(ctx, "- ", 2);
    available = nk_window_get_content_region(ctx).w - marker_w - 8.0f;
    if (help_encoded_text_width(ctx, text) <= available) {
        help_bullet_row(ctx, text, hl);
        return;
    }
    help_wrap_text(ctx, text, HELP_COLOR_BODY, marker_w, marker_w, "- ", HELP_COLOR_BULLET, hl);
}

static void help_render_span(struct nk_context *ctx, const help_span *span, help_highlight *hl)
{
    if (ctx == NULL || span == NULL) {
        return;
    }

    switch (span->kind) {
        case HELP_SPAN_BLANK:
            nk_layout_row_dynamic(ctx, 4.0f, 1);
            nk_spacing(ctx, 1);
            break;

        case HELP_SPAN_H3:
            help_inline_wrap_if_needed(ctx, span->text, HELP_COLOR_H3, hl);
            break;

        case HELP_SPAN_BULLET:
            help_bullet_wrap_if_needed(ctx, span->text, hl);
            break;

        case HELP_SPAN_NUMBER:
            help_inline_wrap_if_needed(ctx, span->text, HELP_COLOR_NUMBER, hl);
            break;

        case HELP_SPAN_CODE_BLOCK:
            help_inline_row(ctx, span->text, HELP_COLOR_CODE, hl);
            break;

        case HELP_SPAN_TABLE:
            help_inline_row(ctx, span->text, HELP_COLOR_TABLE, hl);
            break;

        case HELP_SPAN_TABLE_HEADER:
            help_inline_row(ctx, span->text, HELP_COLOR_TABLE_HEADER, hl);
            break;

        case HELP_SPAN_TEXT:
        default:
            help_inline_wrap_if_needed(ctx, span->text, HELP_COLOR_BODY, hl);
            break;
    }
}

static void help_render_section(
    struct nk_context *ctx,
    const help_section *section,
    help_highlight *hl,
    int current_span)
{
    int i;

    if (ctx == NULL || section == NULL) {
        return;
    }

    for (i = 0; i < section->span_count; ++i) {
        if (hl != NULL) {
            hl->current = i == current_span;
        }
        help_render_span(ctx, &section->spans[i], hl);
    }
    if (hl != NULL) {
        hl->current = false;
    }
}

static void help_strip_markers(char *dst, const char *src, int max)
{
    int j = 0;
    while (*src && j < max - 1) {
        unsigned char c = (unsigned char)*src++;
        if (c != HELP_INLINE_CODE_ON && c != HELP_INLINE_CODE_OFF)
            dst[j++] = (char)c;
    }
    dst[j] = '\0';
}

static void help_tolower_buf(char *buf)
{
    for (; *buf; buf++)
        *buf = (char)tolower((unsigned char)*buf);
}

static float help_estimate_span_y(struct nk_context *ctx, int sec, int target_span)
{
    float row_h = help_row_height(ctx);
    float y = 0.0f;
    int i;

    if (sec < 0 || sec >= help_section_count) return 0.0f;
    for (i = 0; i < target_span && i < help_sections[sec].span_count; i++) {
        y += (help_sections[sec].spans[i].kind == HELP_SPAN_BLANK) ? 6.0f : (row_h + 2.0f);
    }
    return y;
}

static void help_search_execute(struct nk_context *ctx, frontend_help_state *state, bool forward)
{
    char pattern[128];
    char text[2048];
    re_t re;
    int total = 0;
    int cur_sec, cur_span;
    int i, ml;

    if (state->search_buf[0] == '\0') return;

    /* Compile case-insensitive: lowercase both pattern and text before matching. */
    strncpy(pattern, state->search_buf, sizeof(pattern) - 1);
    pattern[sizeof(pattern) - 1] = '\0';
    help_tolower_buf(pattern);
    re = re_compile(pattern);
    if (!re) { state->search_no_match = true; return; }

    for (i = 0; i < help_section_count; i++)
        total += help_sections[i].span_count;
    if (total == 0) { state->search_no_match = true; return; }

    /* Starting position: one step past (forward) or before (backward) current. */
    cur_sec  = state->search_section;
    cur_span = state->search_span + (forward ? 1 : -1);

    /* Normalize across section boundaries. */
    if (forward) {
        if (cur_sec < 0 || cur_sec >= help_section_count)
            cur_sec = 0;
        while (cur_sec < help_section_count &&
               cur_span >= help_sections[cur_sec].span_count) {
            cur_sec = (cur_sec + 1) % help_section_count;
            cur_span = 0;
        }
    } else {
        if (cur_sec < 0 || cur_sec >= help_section_count)
            cur_sec = help_section_count - 1;
        while (cur_span < 0) {
            cur_sec = (cur_sec - 1 + help_section_count) % help_section_count;
            cur_span = help_sections[cur_sec].span_count - 1;
        }
    }

    /* Iterate over at most `total` spans (full wrap). */
    for (i = 0; i < total; i++) {
        const help_span *sp;

        if (cur_sec < 0 || cur_sec >= help_section_count ||
            cur_span < 0 || cur_span >= help_sections[cur_sec].span_count)
            break;

        sp = &help_sections[cur_sec].spans[cur_span];
        if (sp->kind != HELP_SPAN_BLANK && sp->text && sp->text[0] != '\0') {
            help_strip_markers(text, sp->text, (int)sizeof(text));
            help_tolower_buf(text);
            ml = 0;
            if (re_matchp(re, text, &ml) >= 0) {
                float span_y = help_estimate_span_y(ctx, cur_sec, cur_span);
                if (cur_sec != state->section_index)
                    help_view_select_section(ctx, state, cur_sec);
                if (cur_sec < FRONTEND_HELP_MAX_SECTIONS)
                    state->section_scroll_y[cur_sec] = (nk_uint)span_y;
                state->pending_scroll_y = (nk_uint)span_y;
                state->pending_scroll_restore = true;
                state->search_section = cur_sec;
                state->search_span = cur_span;
                state->search_no_match = false;
                /* span_y is a per-span estimate that ignores wrapped rows; the
                   render pass measures the real row and corrects the scroll. */
                state->search_scroll_pending = true;
                return;
            }
        }

        if (forward) {
            cur_span++;
            if (cur_span >= help_sections[cur_sec].span_count) {
                cur_sec = (cur_sec + 1) % help_section_count;
                cur_span = 0;
            }
        } else {
            cur_span--;
            if (cur_span < 0) {
                cur_sec = (cur_sec - 1 + help_section_count) % help_section_count;
                cur_span = help_sections[cur_sec].span_count - 1;
            }
        }
    }

    state->search_no_match = true;
}

bool help_view_search(
    struct nk_context *ctx,
    frontend_help_state *state,
    const char *needle,
    bool forward)
{
    if (ctx == NULL || state == NULL) {
        return false;
    }

    if (needle != NULL) {
        strncpy(state->search_buf, needle, sizeof(state->search_buf) - 1);
        state->search_buf[sizeof(state->search_buf) - 1] = '\0';
        state->search_no_match = false;
    }
    help_search_execute(ctx, state, forward);
    return !state->search_no_match;
}

static float help_nav_combo_width(struct nk_context *ctx)
{
    int i;
    float max_w = 0.0f;
    float padding = 32.0f;

    if (ctx == NULL || ctx->style.font == NULL) {
        return 160.0f;
    }

    for (i = 0; i < help_section_count; ++i) {
        const char *title = help_sections[i].title;
        int len = (int)strlen(title);
        float w = help_text_width(ctx, title, len);
        if (w > max_w) {
            max_w = w;
        }
    }
    return max_w + padding;
}

static void help_render_nav_bar(struct nk_context *ctx, frontend_help_state *state, float width)
{
    int i;
    float nav_w   = 60.0f;
    float arrow_w = 32.0f;
    float combo_w;
    float label_w;
    bool at_first;
    bool at_last;
    struct nk_style_button saved_btn;

    if (ctx == NULL || state == NULL || help_section_count <= 0) {
        return;
    }

    at_first = state->section_index <= 0;
    at_last  = state->section_index >= help_section_count - 1;

    combo_w = help_nav_combo_width(ctx);
    label_w = help_text_width(ctx, "Search:", 7) + 12.0f;

    /* Clamp combo width so nav + search controls always fit. */
    {
        float min_search = label_w + arrow_w * 2.0f + 60.0f;
        float max_combo  = width - nav_w * 2.0f - min_search - 16.0f;
        if (combo_w > max_combo && max_combo > 40.0f)
            combo_w = max_combo;
    }

    nk_layout_row_template_begin(ctx, 24.0f);
    nk_layout_row_template_push_static(ctx, nav_w);    /* Prev   */
    nk_layout_row_template_push_static(ctx, combo_w);  /* Index  */
    nk_layout_row_template_push_static(ctx, nav_w);    /* Next   */
    nk_layout_row_template_push_static(ctx, label_w);  /* Search:*/
    nk_layout_row_template_push_dynamic(ctx);           /* box    */
    nk_layout_row_template_push_static(ctx, arrow_w);  /* [<-]   */
    nk_layout_row_template_push_static(ctx, arrow_w);  /* [->]   */
    nk_layout_row_template_end(ctx);

    /* [Prev] */
    saved_btn = ctx->style.button;
    if (at_first) {
        ctx->style.button.normal = nk_style_item_color(HELP_COLOR_SECTION_DISABLED);
        ctx->style.button.hover  = nk_style_item_color(HELP_COLOR_SECTION_DISABLED);
        ctx->style.button.active = nk_style_item_color(HELP_COLOR_SECTION_DISABLED);
        ctx->style.button.text_normal = HELP_PALETTE_GRAY;
        ctx->style.button.text_hover  = HELP_PALETTE_GRAY;
        ctx->style.button.text_active = HELP_PALETTE_GRAY;
    }
    if (nk_button_label(ctx, "Prev") && !at_first) {
        help_view_select_section(ctx, state, state->section_index - 1);
    }
    ctx->style.button = saved_btn;

    /* [Index button — opens popup via help_view_render] */
    {
        const char *label = (state->section_index >= 0 && state->section_index < help_section_count)
            ? help_sections[state->section_index].title
            : "Index";
        saved_btn = ctx->style.button;
        if (state->index_popup_open) {
            ctx->style.button.normal = nk_style_item_color(HELP_COLOR_SECTION_ACTIVE);
            ctx->style.button.hover  = nk_style_item_color(HELP_COLOR_SECTION_ACTIVE);
            ctx->style.button.active = nk_style_item_color(HELP_COLOR_SECTION_ACTIVE);
            ctx->style.button.text_normal = HELP_COLOR_HEADING;
            ctx->style.button.text_hover  = HELP_COLOR_HEADING;
            ctx->style.button.text_active = HELP_COLOR_HEADING;
        }
        if (nk_button_label(ctx, label)) {
            state->index_popup_open = !state->index_popup_open;
            if (state->index_popup_open)
                state->index_popup_just_opened = true;
        }
        ctx->style.button = saved_btn;
    }

    /* [Next] */
    saved_btn = ctx->style.button;
    if (at_last) {
        ctx->style.button.normal = nk_style_item_color(HELP_COLOR_SECTION_DISABLED);
        ctx->style.button.hover  = nk_style_item_color(HELP_COLOR_SECTION_DISABLED);
        ctx->style.button.active = nk_style_item_color(HELP_COLOR_SECTION_DISABLED);
        ctx->style.button.text_normal = HELP_PALETTE_GRAY;
        ctx->style.button.text_hover  = HELP_PALETTE_GRAY;
        ctx->style.button.text_active = HELP_PALETTE_GRAY;
    }
    if (nk_button_label(ctx, "Next") && !at_last) {
        help_view_select_section(ctx, state, state->section_index + 1);
    }
    ctx->style.button = saved_btn;

    /* Search: label */
    nk_label_colored(ctx, "Search:", NK_TEXT_RIGHT, HELP_COLOR_BODY);

    /* Search edit box */
    {
        char prev_buf[128];
        nk_flags edit_res;
        struct nk_style_edit saved_edit = ctx->style.edit;

        memcpy(prev_buf, state->search_buf, sizeof(prev_buf));

        if (state->search_no_match) {
            struct nk_color red = nk_rgb(220, 60, 60);
            ctx->style.edit.text_normal = red;
            ctx->style.edit.text_active = red;
            ctx->style.edit.text_hover  = red;
        }

        edit_res = nk_edit_string_zero_terminated(
            ctx,
            (nk_flags)NK_EDIT_FIELD | NK_EDIT_SIG_ENTER,
            state->search_buf, (int)sizeof(state->search_buf),
            nk_filter_default);

        if (edit_res & NK_EDIT_ACTIVE)
            ctx->current->edit.mode = NK_TEXT_EDIT_MODE_REPLACE;
        if (edit_res & NK_EDIT_COMMITED) {
            nk_edit_unfocus(ctx);
            help_search_execute(ctx, state, true);
        }
        if (memcmp(prev_buf, state->search_buf, sizeof(prev_buf)) != 0)
            state->search_no_match = false;

        ctx->style.edit = saved_edit;
    }

    /* [<-] backward search */
    if (nk_button_label(ctx, "<-"))
        help_search_execute(ctx, state, false);

    /* [->] forward search */
    if (nk_button_label(ctx, "->"))
        help_search_execute(ctx, state, true);
}

void help_view_render(struct nk_context *ctx, frontend_help_state *state, struct nk_font *help_font, int width, int height)
{
    struct nk_rect bounds;
    struct nk_style_window saved_window;
    help_highlight hl;
    char hl_pattern[sizeof(state->search_buf)];
    nk_uint content_scroll_x = 0;
    nk_uint content_scroll_y = 0;
    float margin;
    float heading_h = 34.0f;
    float footer_h = 46.0f;
    float content_h;
    bool font_pushed = false;

    if (ctx == NULL || state == NULL || !state->open || width <= 0 || height <= 0) {
        return;
    }

    if (help_font != NULL) {
        nk_style_push_font(ctx, &help_font->handle);
        font_pushed = true;
    }

    if (state->section_index < 0 || state->section_index >= help_section_count) {
        state->section_index = 0;
    }

    /* Compile the highlight pattern fresh each frame: re_compile() hands back a
       pointer into one static buffer, and the search buttons in the nav bar
       (drawn after the content below) compile over it. */
    hl.re = NULL;
    hl.anchored = false;
    hl.current = false;
    hl.hit_seen = false;
    hl.hit_y = 0.0f;
    if (state->search_buf[0] != '\0' && !state->search_no_match) {
        strncpy(hl_pattern, state->search_buf, sizeof(hl_pattern) - 1);
        hl_pattern[sizeof(hl_pattern) - 1] = '\0';
        help_tolower_buf(hl_pattern);
        hl.anchored = hl_pattern[0] == '^';
        hl.re = re_compile(hl_pattern);
    }

    margin = width < 760 ? 12.0f : 36.0f;
    bounds = nk_rect(
        margin,
        margin,
        (float)width - margin * 2.0f,
        (float)height - margin * 2.0f);
    if (bounds.w < 100.0f || bounds.h < 100.0f) {
        if (font_pushed) {
            nk_style_pop_font(ctx);
        }
        return;
    }
    content_h = bounds.h - heading_h - footer_h - 34.0f;
    if (content_h < 80.0f) {
        content_h = 80.0f;
    }
    state->content_page_y = (nk_uint)(content_h > 40.0f ? content_h - 24.0f : 80.0f);

    saved_window = ctx->style.window;
    ctx->style.window.fixed_background = nk_style_item_color(HELP_COLOR_BG);
    ctx->style.window.border_color = HELP_COLOR_BORDER;
    ctx->style.window.padding = nk_vec2(14.0f, 10.0f);
    ctx->style.window.spacing = nk_vec2(8.0f, 2.0f);
    ctx->style.window.group_padding = nk_vec2(10.0f, 4.0f);

    if (nk_begin(
            ctx,
            "Help",
            bounds,
            NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_NO_SCROLLBAR)) {
        struct nk_style_window group_window = ctx->style.window;

        nk_layout_row_dynamic(ctx, heading_h, 1);
        nk_label_colored(ctx, help_heading, NK_TEXT_LEFT, HELP_COLOR_HEADING);

        ctx->style.window.fixed_background = nk_style_item_color(HELP_COLOR_PANEL);
        nk_layout_row_dynamic(ctx, content_h, 1);
        if (nk_group_begin(ctx, "HelpContent", NK_WINDOW_BORDER)) {
            struct nk_panel *content_layout = ctx->current != NULL ? ctx->current->layout : NULL;
            nk_uint layout_scroll_y = 0;
            if (state->pending_scroll_restore) {
                nk_group_set_scroll(ctx, "HelpContent", 0, state->pending_scroll_y);
                state->pending_scroll_restore = false;
            }
            /* The offset is dereferenced per widget, so a set_scroll above lands
               this frame: this is the scroll the rows below are laid out with,
               unclamped, which is what the hit measurement has to be read against. */
            nk_group_get_scroll(ctx, "HelpContent", &content_scroll_x, &layout_scroll_y);
            help_render_section(
                ctx,
                &help_sections[state->section_index],
                &hl,
                state->search_section == state->section_index ? state->search_span : -1);
            if (content_layout != NULL) {
                float max_y = content_layout->at_y - content_layout->bounds.y - content_layout->bounds.h;
                state->content_max_y = max_y > 0.0f ? (nk_uint)max_y : 0;
            } else {
                state->content_max_y = 0;
            }
            nk_group_get_scroll(ctx, "HelpContent", &content_scroll_x, &content_scroll_y);
            if (content_scroll_y > state->content_max_y) {
                content_scroll_y = state->content_max_y;
            }
            if (state->section_index >= 0 && state->section_index < FRONTEND_HELP_MAX_SECTIONS) {
                state->section_scroll_y[state->section_index] = content_scroll_y;
            }
            if (state->search_scroll_pending) {
                if (hl.hit_seen) {
                    /* The row was laid out this frame whether or not it was
                       visible, so this is exact even for wrapped spans. */
                    struct nk_rect region = nk_window_get_content_region(ctx);
                    float row_y = hl.hit_y - region.y + (float)layout_scroll_y;
                    float target = row_y - region.h * 0.33f;
                    nk_uint want;

                    if (target < 0.0f) {
                        target = 0.0f;
                    }
                    want = (nk_uint)target;
                    if (want > state->content_max_y) {
                        want = state->content_max_y;
                    }
                    if (want != layout_scroll_y) {
                        nk_group_set_scroll(ctx, "HelpContent", 0, want);
                        if (state->section_index >= 0 &&
                            state->section_index < FRONTEND_HELP_MAX_SECTIONS) {
                            state->section_scroll_y[state->section_index] = want;
                        }
                        state->pending_scroll_y = want;
                    }
                }
                state->search_scroll_pending = false;
            }
            nk_group_end(ctx);
        }

        ctx->style.window = group_window;
        nk_layout_row_dynamic(ctx, footer_h, 1);
        if (nk_group_begin(ctx, "HelpSections", NK_WINDOW_NO_SCROLLBAR)) {
            help_render_nav_bar(ctx, state, bounds.w - 28.0f);
            nk_group_end(ctx);
        }
    }
    nk_end(ctx);

    if (state->index_popup_open) {
        int i;
        bool just_opened = state->index_popup_just_opened;
        float nav_w = 60.0f;
        float combo_w = help_nav_combo_width(ctx);
        float item_h = 20.0f;
        float list_h = (float)help_section_count * item_h + 16.0f;
        float max_list_h = bounds.h - footer_h - heading_h - 8.0f;
        struct nk_rect popup_bounds;
        struct nk_style_window saved_popup_window;

        state->index_popup_just_opened = false;

        if (list_h > max_list_h) {
            list_h = max_list_h;
        }
        popup_bounds = nk_rect(
            bounds.x + 14.0f + nav_w + 8.0f,
            bounds.y + bounds.h - footer_h - list_h - 2.0f,
            combo_w,
            list_h);

        saved_popup_window = ctx->style.window;
        ctx->style.window.fixed_background = nk_style_item_color(HELP_COLOR_BG);
        ctx->style.window.border_color = HELP_COLOR_BORDER;
        ctx->style.window.padding = nk_vec2(4.0f, 4.0f);
        ctx->style.window.spacing = nk_vec2(4.0f, 1.0f);
        ctx->style.window.scrollbar_size = nk_vec2(8.0f, 8.0f);

        if (nk_begin(ctx, "HelpIndexPopup", popup_bounds, NK_WINDOW_BORDER)) {
            for (i = 0; i < help_section_count; ++i) {
                struct nk_style_button sbtn = ctx->style.button;
                nk_layout_row_dynamic(ctx, item_h, 1);
                if (i == state->section_index) {
                    ctx->style.button.normal = nk_style_item_color(HELP_COLOR_SECTION_ACTIVE);
                    ctx->style.button.hover  = nk_style_item_color(HELP_COLOR_SECTION_HOVER);
                    ctx->style.button.active = nk_style_item_color(HELP_COLOR_SECTION_ACTIVE);
                    ctx->style.button.text_normal = HELP_COLOR_HEADING;
                    ctx->style.button.text_hover  = HELP_COLOR_HEADING;
                    ctx->style.button.text_active = HELP_COLOR_HEADING;
                }
                if (nk_button_label(ctx, help_sections[i].title)) {
                    help_view_select_section(ctx, state, i);
                    state->index_popup_open = false;
                }
                ctx->style.button = sbtn;
            }
        }
        nk_end(ctx);
        ctx->style.window = saved_popup_window;

        if (!just_opened
            && nk_input_is_mouse_pressed(&ctx->input, NK_BUTTON_LEFT)
            && !nk_input_is_mouse_hovering_rect(&ctx->input, popup_bounds)) {
            state->index_popup_open = false;
        }
    }

    ctx->style.window = saved_window;
    if (font_pushed) {
        nk_style_pop_font(ctx);
    }
}
