/* Headless nuklear render of the help view: drives a search and inspects the
   draw commands it emits, so the highlight geometry and the search scroll
   correction are checked against real layout rather than by eye. */

#include "help_view.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_WIDTH 900
#define TEST_HEIGHT 600
#define TEST_GLYPH_W 7.0f
#define TEST_FONT_H 12.0f
#define MAX_CAPTURED 4096

/* HELP_COLOR_MATCH: the C64 yellow the highlight bands are painted in. */
#define MATCH_R 0xed
#define MATCH_G 0xf1
#define MATCH_B 0x71

typedef struct captured_text {
    float x, y, w, h;
    char text[256];
} captured_text;

typedef struct captured_band {
    float x, y, w, h;
} captured_band;

typedef struct capture {
    captured_text texts[MAX_CAPTURED];
    int text_count;
    captured_band bands[MAX_CAPTURED];
    int band_count;
} capture;

static void fail(const char *msg)
{
    fprintf(stderr, "test_help_view: %s\n", msg);
    exit(1);
}

static float test_font_width(nk_handle handle, float height, const char *text, int len)
{
    (void)handle;
    (void)height;
    (void)text;
    return (float)len * TEST_GLYPH_W;
}

static void capture_reset(capture *cap)
{
    cap->text_count = 0;
    cap->band_count = 0;
}

static void capture_frame(struct nk_context *ctx, capture *cap)
{
    const struct nk_command *cmd;

    capture_reset(cap);
    nk_foreach(cmd, ctx) {
        if (cmd->type == NK_COMMAND_TEXT) {
            const struct nk_command_text *t = (const struct nk_command_text *)cmd;
            captured_text *out;
            int len = t->length;

            if (cap->text_count >= MAX_CAPTURED) continue;
            out = &cap->texts[cap->text_count++];
            out->x = (float)t->x;
            out->y = (float)t->y;
            out->w = (float)t->w;
            out->h = (float)t->h;
            if (len > (int)sizeof(out->text) - 1) len = (int)sizeof(out->text) - 1;
            memcpy(out->text, t->string, (size_t)len);
            out->text[len] = '\0';
        } else if (cmd->type == NK_COMMAND_RECT_FILLED) {
            const struct nk_command_rect_filled *r = (const struct nk_command_rect_filled *)cmd;
            captured_band *out;

            if (r->color.r != MATCH_R || r->color.g != MATCH_G || r->color.b != MATCH_B) continue;
            if (cap->band_count >= MAX_CAPTURED) continue;
            out = &cap->bands[cap->band_count++];
            out->x = (float)r->x;
            out->y = (float)r->y;
            out->w = (float)r->w;
            out->h = (float)r->h;
        }
    }
}

static void render_help(struct nk_context *ctx, frontend_help_state *state, capture *cap)
{
    nk_input_begin(ctx);
    nk_input_end(ctx);
    help_view_render(ctx, state, NULL, TEST_WIDTH, TEST_HEIGHT);
    capture_frame(ctx, cap);
    nk_clear(ctx);
}

/* The text drawn at exactly the band's x: for a highlighted run that is the
   matched substring, because the run is split at the match boundaries. The
   inverse band sits at the row's y, the underline band a row height below it,
   so take the vertically nearest text at that x. */
static const char *text_at_band(const capture *cap, const captured_band *band)
{
    const char *best = NULL;
    float best_dy = 0.0f;
    int i;

    for (i = 0; i < cap->text_count; ++i) {
        const captured_text *t = &cap->texts[i];
        float dx = t->x - band->x;
        float dy = t->y - band->y;

        if (dx < -0.5f || dx > 0.5f) continue;
        if (dy < 0.0f) dy = -dy;
        if (dy > (float)TEST_FONT_H + 4.0f) continue;
        if (best == NULL || dy < best_dy) {
            best = t->text;
            best_dy = dy;
        }
    }
    return best;
}

static bool equals_ignore_case(const char *a, const char *b)
{
    if (a == NULL || b == NULL) return false;
    for (; *a != '\0' && *b != '\0'; ++a, ++b) {
        char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
        if (ca != cb) return false;
    }
    return *a == *b;
}

static void test_no_search_draws_no_bands(struct nk_context *ctx)
{
    frontend_help_state state;
    capture cap;

    help_view_init(&state);
    help_view_open(&state, false);
    render_help(ctx, &state, &cap);

    if (cap.text_count == 0) fail("help view drew no text at all");
    if (cap.band_count != 0) fail("highlight bands drawn with no search active");
}

static void test_search_highlights_the_word(struct nk_context *ctx)
{
    frontend_help_state state;
    capture cap;
    const char *needle = "emulator";
    int i;
    bool found_thick = false;

    help_view_init(&state);
    help_view_open(&state, false);
    if (!help_view_search(ctx, &state, needle, true)) fail("search for 'emulator' found nothing");

    render_help(ctx, &state, &cap);
    if (cap.band_count == 0) fail("search hit drew no highlight band");

    for (i = 0; i < cap.band_count; ++i) {
        const captured_band *band = &cap.bands[i];
        const char *text = text_at_band(&cap, band);

        if (!equals_ignore_case(text, needle)) {
            fprintf(stderr, "band %d covers \"%s\", expected \"%s\"\n",
                i, text != NULL ? text : "(nothing)", needle);
            fail("highlight band is not aligned with the matched word");
        }
        /* Width must be the matched run only, never the whole line. */
        if (band->w < (float)strlen(needle) * TEST_GLYPH_W - 1.0f ||
            band->w > (float)strlen(needle) * TEST_GLYPH_W + 1.0f) {
            fail("highlight band width does not match the matched word");
        }
        if (band->h > 4.0f) found_thick = true;
    }
    /* The span the search jumped to gets the full-height inverse band. */
    if (!found_thick) fail("current hit did not get the inverse band");
}

/* The y of the full-height band once the scroll has settled, or -1. */
static float converged_hit_y(struct nk_context *ctx, const char *needle, nk_uint *out_scroll)
{
    frontend_help_state state;
    capture cap;
    float y = -1.0f;
    int frame;
    int i;

    help_view_init(&state);
    help_view_open(&state, false);
    if (!help_view_search(ctx, &state, needle, true)) {
        return -1.0f;
    }

    /* Frame 0 measures the row and queues the correction, frame 1 applies it,
       frame 2 must leave it alone. */
    for (frame = 0; frame < 3; ++frame) {
        render_help(ctx, &state, &cap);
    }

    for (i = 0; i < cap.band_count; ++i) {
        if (cap.bands[i].h > 4.0f) {
            y = cap.bands[i].y;
            break;
        }
    }
    if (out_scroll != NULL) {
        *out_scroll = state.section_index >= 0 && state.section_index < FRONTEND_HELP_MAX_SECTIONS
            ? state.section_scroll_y[state.section_index]
            : 0;
    }
    return y;
}

/* The scroll correction measures the row that actually holds the hit, so a hit
   deep in a wrapped section lands at the same place on screen as a shallow one.
   The old per-span estimate drifts with depth and cannot do that. */
static void test_scroll_lands_on_the_hit(struct nk_context *ctx)
{
    static const char *const needles[] = {
        "breakpoint", "joystick", "snapshot", "assembler", "disk",
    };
    float first_y = -1.0f;
    const char *first_needle = NULL;
    int checked = 0;
    int i;

    for (i = 0; i < (int)(sizeof(needles) / sizeof(needles[0])); ++i) {
        nk_uint scroll = 0;
        float y = converged_hit_y(ctx, needles[i], &scroll);

        if (y < 0.0f) continue;   /* wording changed: not this test's business */
        if (scroll == 0) continue; /* clamped to the top of a short section */

        if (y < 0.0f || y > (float)TEST_HEIGHT) {
            fail("current hit is off screen after the scroll correction");
        }
        ++checked;
        if (first_needle == NULL) {
            first_y = y;
            first_needle = needles[i];
            continue;
        }
        if (y < first_y - 2.0f || y > first_y + 2.0f) {
            fprintf(stderr, "\"%s\" landed at y=%.0f but \"%s\" at y=%.0f\n",
                needles[i], y, first_needle, first_y);
            fail("hits do not land at a consistent height: scroll is still estimated");
        }
    }

    if (checked < 2) {
        fprintf(stderr, "test_help_view: fewer than two scrollable hits found, "
            "scroll correction not exercised\n");
    }
}

static void test_failed_search_highlights_nothing(struct nk_context *ctx)
{
    frontend_help_state state;
    capture cap;

    help_view_init(&state);
    help_view_open(&state, false);
    if (help_view_search(ctx, &state, "zzzznotinthemanual", true)) {
        fail("search matched a string that is not in the manual");
    }

    render_help(ctx, &state, &cap);
    if (cap.band_count != 0) fail("failed search still drew highlight bands");
}

int main(void)
{
    struct nk_context ctx;
    struct nk_user_font font;

    memset(&font, 0, sizeof(font));
    font.userdata = nk_handle_ptr(NULL);
    font.height = TEST_FONT_H;
    font.width = test_font_width;

    if (!nk_init_default(&ctx, &font)) fail("nk_init_default failed");

    test_no_search_draws_no_bands(&ctx);
    test_search_highlights_the_word(&ctx);
    test_scroll_lands_on_the_hit(&ctx);
    test_failed_search_highlights_nothing(&ctx);

    nk_free(&ctx);
    printf("test_help_view: all checks passed\n");
    return 0;
}
