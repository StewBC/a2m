// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "unk_lib.h"

// The help lives in unk_help_text.c
extern const int unk_help_page_count;
extern const char *unk_page_titles[];
extern const char *unk_help_text[];

#define HELP_DEFAULT_LINE_HEIGHT 14
#define HELP_MAX_LINE            512

struct unk_help_color_tag {
    const char *name;          // e.g. "H1"
    struct nk_color color;     // color for the tag
};

// Index 0 is "Normal" and is used as a fall-back
static const struct unk_help_color_tag unk_help_color_tags[] = {
    { "NM", {0xD0, 0xD0, 0xD0, 0xFF}},
    { "BG", {0xD2, 0xD2, 0xD2, 0xFF}},
    { "H0", {0x00, 0xB7, 0xFF, 0xFF}},
    { "H1", {0x00, 0xE5, 0xA8, 0xFF}},
    { "H2", {0xFF, 0xD8, 0x4D, 0xFF}},
    { "H3", {0xB3, 0x88, 0xFF, 0xFF}},
    { "BO", {0xFF, 0x9F, 0x43, 0xFF}},
    { "QU", {0x7C, 0xFF, 0x00, 0xFF}},
    { "TB", {0xFF, 0x6B, 0x6B, 0xFF}},
};

static int unk_help_hex_digit(char c) {
    if(c >= '0' && c <= '9') {
        return c - '0';
    }
    if(c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    if(c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    return 0;
}

static struct nk_color unk_help_parse_hex_color(const char *s) {
    // expects "RRGGBB" (no '#')
    if(!s || strlen(s) < 6) {
        return unk_help_color_tags[0].color;
    }
    int r = (unk_help_hex_digit(s[0]) << 4) | unk_help_hex_digit(s[1]);
    int g = (unk_help_hex_digit(s[2]) << 4) | unk_help_hex_digit(s[3]);
    int b = (unk_help_hex_digit(s[4]) << 4) | unk_help_hex_digit(s[5]);
    return nk_rgb(r, g, b);
}

static int unk_help_is_hex6(const char *s) {
    if(!s) {
        return 0;
    }
    for(int i = 0; i < 6; i++) {
        char c = s[i];
        if(!c) {
            return 0;
        }
        if(!((c >= '0' && c <= '9') ||
                (c >= 'a' && c <= 'f') ||
                (c >= 'A' && c <= 'F'))) {
            return 0;
        }
    }
    return s[6] == '\0';
}

static int unk_help_lookup_tag_color(const char *name, struct nk_color *out) {
    if(!name || !out) {
        return 0;
    }
    for(size_t i = 0; i < (sizeof(unk_help_color_tags) / sizeof(unk_help_color_tags[0])); i++) {
        if(strcmp(unk_help_color_tags[i].name, name) == 0) {
            *out = unk_help_color_tags[i].color;
            return 1;
        }
    }
    return 0;
}

// Look up by name, fall back to literal, and fall back to a default
static struct nk_color unk_help_resolve_tag_payload(const char *payload) {
    struct nk_color c;
    if(unk_help_lookup_tag_color(payload, &c)) {
        return c;
    }
    if(unk_help_is_hex6(payload)) {
        return unk_help_parse_hex_color(payload);
    }
    return unk_help_color_tags[0].color;
}

static void unk_help_consume_color_tags(const char *s, int len, struct nk_color *cur_color) {
    const char *p = s;
    const char *end = s + len;

    while(p < end) {
        int tlen = 0;
        if(*p == '<' && unk_help_is_inline_color_tag(p, &tlen) && p + tlen <= end) {
            const char *payload = p + 2;
            int plen = tlen - 3;

            char tmp[64];
            if(plen > 0) {
                if(plen >= (int)sizeof(tmp)) {
                    plen = sizeof(tmp) - 1;
                }
                memcpy(tmp, payload, plen);
                tmp[plen] = '\0';
                *cur_color = unk_help_resolve_tag_payload(tmp);
            }
            p += tlen;
        } else {
            p++;
        }
    }
}

// <#...> is a color tag, where ... ends at '>' and is non-empty.
static int unk_help_is_inline_color_tag(const char *p, int *tag_chars) {
    if(!p || p[0] != '<' || p[1] != '#') {
        return 0;
    }

    const char *gt = strchr(p, '>');
    if(!gt) {
        return 0;
    }

    int payload_len = (int)(gt - (p + 2));
    if(payload_len <= 0) {
        return 0;
    }

    if(tag_chars) {
        *tag_chars = (int)((gt - p) + 1);    // include '>'
    }
    return 1;
}

// Draw one visual line segment [s, s+len) and UPDATE *cur_color when tags appear.
static void unk_help_draw_rich_segment(struct nk_context *ctx, const char *s, int len, int line_height, nk_flags align, struct nk_color *cur_color) {
    if(len <= 0 || !cur_color) {
        return;
    }

    const struct nk_user_font *font = ctx->style.font;
    struct nk_command_buffer *out = nk_window_get_canvas(ctx);

    nk_layout_row_dynamic(ctx, line_height, 1);

    struct nk_rect bounds;
    if(!nk_widget(&bounds, ctx)) {
        // Not visible: still advance THE color through this segment
        unk_help_consume_color_tags(s, len, cur_color);
        return;
    }

    const char *p   = s;
    const char *end = s + len;

    // Measure visible width for centering (tags have no width)
    float total_w = 0.0f;
    while(p < end) {
        int tlen = 0;

        if(*p == '<' && unk_help_is_inline_color_tag(p, &tlen) && (p + tlen) <= end) {
            p += tlen;
            continue;
        }

        const char *run = p;
        while(p < end) {
            if(*p == '<' && unk_help_is_inline_color_tag(p, &tlen) && (p + tlen) <= end) {
                break;
            }
            p++;
        }

        int run_len = (int)(p - run);
        if(run_len > 0) {
            total_w += font->width(font->userdata, font->height, run, run_len);
        }
    }

    float x = bounds.x;
    if(align == NK_TEXT_CENTERED && total_w < bounds.w) {
        x = bounds.x + (bounds.w - total_w) * 0.5f;
    }

    // Push scissor for this widget, remember previous clip to restore
    struct nk_rect prev_clip = out->clip;
    nk_push_scissor(out, bounds);

    // Draw runs; when a tag is encountered, advance THE color via consume helper
    p = s;
    while(p < end) {
        int tlen = 0;

        if(*p == '<' && unk_help_is_inline_color_tag(p, &tlen) && (p + tlen) <= end) {
            // Update THE color (tag has no width)
            unk_help_consume_color_tags(p, tlen, cur_color);
            p += tlen;
            continue;
        }

        const char *run = p;
        while(p < end) {
            if(*p == '<' && unk_help_is_inline_color_tag(p, &tlen) && (p + tlen) <= end) {
                break;
            }
            p++;
        }

        int run_len = (int)(p - run);
        if(run_len > 0) {
            float w = font->width(font->userdata, font->height, run, run_len);

            struct nk_rect r = bounds;
            r.x = x;
            r.w = w;

            nk_draw_text(out, r, run, run_len, font, ctx->style.window.background, *cur_color);
            x += w;
        }
    }

    // Restore previous scissor
    nk_push_scissor(out, prev_clip);
}

static void unk_help_draw_wrapped_rich_line(struct nk_context *ctx, const char *s, int max_visible_chars, int line_height, nk_flags align, struct nk_color *cur_color) {
    const char *p = s;

    while(*p) {
        int vis = 0;
        int last_space_vis = -1;
        const char *last_space_ptr = NULL;

        const char *q = p;
        while(*q && vis < max_visible_chars) {
            int tlen = 0;
            if(*q == '<' && unk_help_is_inline_color_tag(q, &tlen)) {
                q += tlen;
                continue;
            }
            if(*q == ' ') {
                last_space_vis = vis;
                last_space_ptr = q;
            }
            q++;
            vis++;
        }

        const char *line_end = q;
        if(*line_end && last_space_ptr && last_space_vis > 0) {
            line_end = last_space_ptr;
        }

        int seg_len = (int)(line_end - p);
        if(seg_len <= 0) {
            const char *r = p;
            int advanced = 0;
            while(*r && advanced < 1) {
                int tlen = 0;
                if(*r == '<' && unk_help_is_inline_color_tag(r, &tlen)) {
                    r += tlen;
                    continue;
                }
                r++;
                advanced++;
            }
            seg_len = (int)(r - p);
            line_end = r;
        }

        // IMPORTANT: cur_color is shared across segments (wraps)
        unk_help_draw_rich_segment(ctx, p, seg_len, line_height, align, cur_color);

        p = line_end;
        while(*p == ' ') {
            p++;
        }
    }
}


static void unk_help_render_text(struct nk_context *ctx, const char *help_text, int max_chars_per_line) {
    // current render state
    nk_flags align = NK_TEXT_LEFT;
    int line_height = HELP_DEFAULT_LINE_HEIGHT;
    struct nk_command_buffer *out = nk_window_get_canvas(ctx);
    struct nk_rect old = out->clip;
    const char *p = help_text;
    char line[HELP_MAX_LINE];
    int line_len = 0;
    struct nk_color cur_color = unk_help_color_tags[0].color;

    while(*p) {
        char c = *p++;

        if(c != '\n' && c != '\r' && line_len < HELP_MAX_LINE - 1) {
            line[line_len++] = c;
            continue;
        }

        // End of line
        line[line_len] = '\0';
        line_len = 0;

        // Skip \r act on \n
        if(c == '\r' && *p == '\n') {
            continue;
        }

        const char *s = line;

        // Preserve indentation, but allow tags to appear after leading spaces.
        const char *indent_end = s;
        while(*indent_end == ' ' || *indent_end == '\t') {
            indent_end++;
        }

        const char *t = indent_end;

        // Handle tags at start of "content" (after indentation)
        for(;;) {
            if(*t != '<') {
                break;
            }

            const char *tag_start = t + 1;
            const char *gt = strchr(tag_start, '>');
            if(!gt) {
                break;
            }

            size_t tag_len = (size_t)(gt - tag_start);
            char tag[64];
            if(tag_len >= sizeof(tag)) {
                tag_len = sizeof(tag) - 1;
            }
            memcpy(tag, tag_start, tag_len);
            tag[tag_len] = '\0';

            // Advance past this tag
            t = gt + 1;

            // Interpret tag
            if(strcmp(tag, "c") == 0) {
                align = NK_TEXT_CENTERED;
            } else if(strcmp(tag, "l") == 0) {
                align = NK_TEXT_LEFT;
            } else if(tag[0] == '#') {
                // <#TAGNAME> or <#RRGGBB>
                cur_color = unk_help_resolve_tag_payload(tag + 1);
            } else if(strncmp(tag, "lh", 2) == 0) {
                // <lh N>
                const char *num = tag + 2;
                while(*num == ' ') {
                    num++;
                }
                int h = atoi(num);
                if(h > 4 && h < 200) {
                    line_height = h;
                }
            } else {
                // Ignore unknown tags
            }
        }

        // If remainder is empty, still emit a blank line.
        char visible[HELP_MAX_LINE];
        size_t ind_len = (size_t)(indent_end - s);
        if(ind_len >= sizeof(visible)) {
            ind_len = sizeof(visible) - 1;
        }

        size_t rem_len = strlen(t);
        if(ind_len + rem_len >= sizeof(visible)) {
            rem_len = sizeof(visible) - 1 - ind_len;
        }

        memcpy(visible, s, ind_len);               // keep indentation
        memcpy(visible + ind_len, t, rem_len);     // visible text
        visible[ind_len + rem_len] = '\0';

        if(visible[0] == '\0') {
            nk_layout_row_dynamic(ctx, line_height, 1);
            nk_label(ctx, " ", NK_TEXT_LEFT);
            continue;
        }

        // Single-column only, wrap + rich inline tags
        unk_help_draw_wrapped_rich_line(ctx, visible, max_chars_per_line, line_height, align, &cur_color);
        nk_push_scissor(out, old);
    }
}

void unk_show_help(UNK *v) {
    struct nk_context *ctx = v->ctx;
    SDL_Rect r = v->sdl_os_rect;
    char label[8];

    const struct nk_user_font *font = ctx->style.font;
    float char_w = font->width(font->userdata, font->height, "0", 1);
    int max_chars_per_line = (int)((r.w - 2 * ctx->style.window.padding.x) / char_w);

    // clamp to the buffer size
    if(max_chars_per_line > HELP_MAX_LINE - 1) {
        max_chars_per_line = HELP_MAX_LINE - 1;
    }
    if(max_chars_per_line < 10) {
        // Should never have a window so small, but just for sanity's sake
        max_chars_per_line = 10;
    }

    if(nk_begin(ctx, "Help", nk_rect(r.x, r.y, r.w, r.h), NK_WINDOW_NO_SCROLLBAR)) {
        nk_layout_row_dynamic(ctx, 30, 1);
        nk_label_colored(ctx, "Apple ][+ and //e Enhanced emulator by Stefan Wessels, 2025.", NK_TEXT_CENTERED, nk_rgb(0, 255, 255));
        // main help area
        nk_layout_row_dynamic(ctx, r.h - 55, 1);
        struct nk_scroll *scr = &v->help_scroll[v->help_page];

        if(nk_group_scrolled_begin(ctx, scr, "Help Pages", 0)) {
            unk_help_render_text(ctx, unk_help_text[v->help_page], max_chars_per_line);
            nk_group_scrolled_end(ctx);
        }

        // page selector row (Page: [1][2] etc)
        const struct nk_color active = {0xff, 0xff, 0x00, 0xff};

        // one label + N buttons
        nk_layout_row_begin(ctx, NK_STATIC, 13, 1 + unk_help_page_count);

        // label
        nk_layout_row_push(ctx, 40.0f);
        nk_label(ctx, "Page:", NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_MIDDLE);

        // buttons
        for(int i = 0; i < unk_help_page_count; ++i) {
            nk_layout_row_push(ctx, 80.0f); // button width

            struct nk_style_button style = ctx->style.button;  // base style
            if(v->help_page == i) {
                style.text_normal = style.text_hover = style.text_active = active;
            }

            if(nk_button_label_styled(ctx, &style, unk_page_titles[i])) {
                v->help_page = i;
            }
        }

        nk_layout_row_end(ctx);
    }

    nk_end(ctx);
}