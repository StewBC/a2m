// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "unk_lib.h"

// The help lives in unk_help_text.c
extern const int unk_help_page_count;
extern const char *unk_help_text;

#define HELP_DEFAULT_LINE_HEIGHT 14
#define HELP_MAX_LINE            170

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
        return nk_rgb(200, 200, 200);
    }
    int r = (unk_help_hex_digit(s[0]) << 4) | unk_help_hex_digit(s[1]);
    int g = (unk_help_hex_digit(s[2]) << 4) | unk_help_hex_digit(s[3]);
    int b = (unk_help_hex_digit(s[4]) << 4) | unk_help_hex_digit(s[5]);
    return nk_rgb(r, g, b);
}

static void unk_help_render_text(struct nk_context *ctx, int requested_page, const char *help_text) {
    int current_page = 0;
    int page_active = 0;

    // current render state
    nk_flags align = NK_TEXT_LEFT;
    struct nk_color color = nk_rgb(200, 200, 200);
    int cols = 1;
    int line_height = HELP_DEFAULT_LINE_HEIGHT;

    const char *p = help_text;
    char line[HELP_MAX_LINE];
    int line_len = 0;

    while(*p) {
        char c = *p++;

        if(c != '\n' && c != '\r' && line_len < HELP_MAX_LINE - 1) {
            line[line_len++] = c;
            continue;
        }

        // End of line (or buffer full)
        line[line_len] = '\0';
        line_len = 0;

        // Normalize CRLF: skip bare '\r' lines, handle on '\n'
        if(c == '\r' && *p == '\n') {
            continue; // we'll process on '\n'
        }

        // Process this line
        const char *s = line;

        // Skip leading spaces
        while(*s == ' ' || *s == '\t') {
            s++;
        }

        // Handle tags at start of line
        int control_only = 0;
        for(;;) {
            if(*s != '<') {
                break;
            }

            const char *tag_start = s + 1;
            const char *gt = strchr(tag_start, '>');
            if(!gt) {
                break;    // malformed, bail out
            }

            size_t tag_len = (size_t)(gt - tag_start);
            char tag[64];
            if(tag_len >= sizeof(tag)) {
                tag_len = sizeof(tag) - 1;
            }
            memcpy(tag, tag_start, tag_len);
            tag[tag_len] = '\0';

            // Advance s past this tag
            s = gt + 1;

            // Interpret tag
            if(strncmp(tag, "page", 4) == 0) {
                // <page N>
                const char *num = tag + 4;
                while(*num == ' ') {
                    num++;
                }
                int n = atoi(num);
                current_page = n;
                page_active = (current_page == requested_page);

                // New page - reset formatting to defaults
                align = NK_TEXT_LEFT;
                color = nk_rgb(200, 200, 200);
                cols = 1;
                line_height = HELP_DEFAULT_LINE_HEIGHT;

                control_only = 1;
            } else if(strcmp(tag, "c") == 0) {
                align = NK_TEXT_CENTERED;
            } else if(strcmp(tag, "l") == 0) {
                align = NK_TEXT_LEFT;
            } else if(tag[0] == '#') {
                // <#RRGGBB>
                color = unk_help_parse_hex_color(tag + 1);
            } else if(strncmp(tag, "cols", 4) == 0) {
                const char *num = tag + 4;
                while(*num == ' ') {
                    num++;
                }
                int n = atoi(num);
                if(n < 1) {
                    n = 1;
                }
                cols = n;
                control_only = 1;
            } else if(strncmp(tag, "lh", 2) == 0) {
                const char *num = tag + 2;
                while(*num == ' ') {
                    num++;
                }
                int h = atoi(num);
                if(h > 4 && h < 200) {
                    line_height = h;
                }
            } else {
                // SQW unknown tag - ignore for now
            }

            // loop again as there could be more tags, eg <c><#ff0000>
        }

        // Anything left to render as text for this line?
        if(!*s && control_only) {
            // Tag-only line = optional blank line on active page
            if(page_active) {
                nk_layout_row_dynamic(ctx, line_height, 1);
                nk_label(ctx, " ", NK_TEXT_LEFT);
            }
            continue;
        }

        if(!page_active) {
            continue; // not on the selected page
        }

        // Split by '\t' into columns (up to 'cols')
        const char *col_start = s;
        int col_index = 0;

        nk_layout_row_dynamic(ctx, line_height, cols);

        while(col_index < cols) {
            const char *tab = strchr(col_start, '\t');
            char col_text[HELP_MAX_LINE];

            if(tab && col_index < cols - 1) {
                size_t len = (size_t)(tab - col_start);
                if(len >= sizeof(col_text)) {
                    len = sizeof(col_text) - 1;
                }
                memcpy(col_text, col_start, len);
                col_text[len] = '\0';
                col_start = tab + 1;
            } else {
                // last column or no more tabs
                strncpy(col_text, col_start, sizeof(col_text) - 1);
                col_text[sizeof(col_text) - 1] = '\0';
                col_start += strlen(col_start);
            }

            nk_label_colored(ctx, col_text, align, color);
            col_index++;
        }
    }

    // Handle final line if file doesn't end with newline
    if(line_len > 0) {
        line[line_len] = '\0';
        const char *s = line;
        while(*s == ' ' || *s == '\t') {
            s++;
        }
        if(*s && page_active) {
            nk_layout_row_dynamic(ctx, line_height, cols);
            nk_label_colored(ctx, s, align, color);
        }
    }
}


void unk_show_help(UNK *v) {
    struct nk_context *ctx = v->ctx;
    SDL_Rect r = v->sdl_os_rect;
    char label[8];

    if(nk_begin(ctx, "Help", nk_rect(r.x, r.y, r.w, r.h), NK_WINDOW_NO_SCROLLBAR)) {
        nk_layout_row_dynamic(ctx, 30, 1);
        nk_label_colored(ctx, "Apple ][+ emulator by Stefan Wessels, 2025.", NK_TEXT_CENTERED, nk_rgb(0, 255, 255));
        // main help area
        nk_layout_row_dynamic(ctx, r.h - 55, 1);
        if(nk_group_begin(ctx, "Help Pages", 0)) {
            // Display pages are 1-based but v->help_page is 0-based
            unk_help_render_text(ctx, v->help_page + 1, unk_help_text);
            nk_group_end(ctx);
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
            nk_layout_row_push(ctx, 25.0f); // button width

            struct nk_style_button style = ctx->style.button;  // base style
            if(v->help_page == i) {
                style.text_normal = style.text_hover = style.text_active = active;
            }

            snprintf(label, sizeof(label), "%d", i + 1);
            if(nk_button_label_styled(ctx, &style, label)) {
                v->help_page = i;
            }
        }

        nk_layout_row_end(ctx);
    }

    nk_end(ctx);
}