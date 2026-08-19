#include "debugger_layout.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>

static float debugger_layout_clampf(float value, float min, float max)
{
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

void debugger_layout_init(debugger_layout *layout)
{
    if (layout == NULL) {
        return;
    }

    layout->split_display_right = 0.62f;
    layout->split_top_bottom = 0.58f;
    layout->split_memory_misc = 0.55f;
    layout->display_px_w = 352;
    layout->display_px_h = 248;
    layout->display_aspect = 352.0f / 248.0f;
    layout->drag_active = DEBUGGER_LAYOUT_DRAG_NONE;
    layout->drag_last_x = 0.0f;
    layout->drag_last_y = 0.0f;
    layout->drag_moved = 0;
}

void debugger_layout_compute(debugger_layout *layout, struct nk_rect parent, const debugger_layout_limits *limits)
{
    float split_min;
    float split_max;
    float split;
    float top_h;
    float display_w;
    float memory_w;
    int registers_h;
    int gutter;
    int corner;
    struct nk_rect top;
    struct nk_rect bottom;
    struct nk_rect right;
    struct nk_rect *rects[5];
    int i;

    if (layout == NULL || limits == NULL || parent.w <= 0.0f || parent.h <= 0.0f) {
        return;
    }

    split_min = (float)(limits->registers_h_px + limits->min_disassembly_h_px) / parent.h;
    split_max = 1.0f - (float)limits->min_bottom_h_px / parent.h;
    split = debugger_layout_clampf(layout->split_top_bottom, split_min, split_max);
    layout->split_top_bottom = split;
    top_h = roundf(parent.h * split);

    top = nk_rect(parent.x, parent.y, parent.w, top_h);
    bottom = nk_rect(parent.x, parent.y + top_h, parent.w, parent.h - top_h);

    split_min = (float)limits->min_display_w_px / parent.w;
    split_max = 1.0f - (float)limits->min_right_w_px / parent.w;
    split = debugger_layout_clampf(layout->split_display_right, split_min, split_max);
    layout->split_display_right = split;
    display_w = roundf(parent.w * split);

    layout->display = nk_rect(top.x, top.y, display_w, top.h);
    right = nk_rect(top.x + display_w, top.y, top.w - display_w, top.h);

    registers_h = limits->registers_h_px;
    if (registers_h > (int)right.h - limits->min_disassembly_h_px) {
        registers_h = (int)right.h - limits->min_disassembly_h_px;
    }
    if (registers_h < 0) {
        registers_h = 0;
    }

    layout->registers = nk_rect(right.x, right.y, right.w, (float)registers_h);
    layout->disassembly = nk_rect(right.x, right.y + registers_h, right.w, right.h - registers_h);

    split_min = (float)limits->min_memory_w_px / bottom.w;
    split_max = 1.0f - (float)limits->min_misc_w_px / bottom.w;
    split = debugger_layout_clampf(layout->split_memory_misc, split_min, split_max);
    layout->split_memory_misc = split;
    memory_w = roundf(bottom.w * split);

    layout->memory = nk_rect(bottom.x, bottom.y, memory_w, bottom.h);
    layout->misc = nk_rect(bottom.x + memory_w, bottom.y, bottom.w - memory_w, bottom.h);

    rects[0] = &layout->display;
    rects[1] = &layout->registers;
    rects[2] = &layout->disassembly;
    rects[3] = &layout->memory;
    rects[4] = &layout->misc;
    for (i = 0; i < 5; ++i) {
        rects[i]->x = roundf(rects[i]->x);
        rects[i]->y = roundf(rects[i]->y);
        rects[i]->w = roundf(rects[i]->w);
        rects[i]->h = roundf(rects[i]->h);
    }

    gutter = limits->gutter_px;
    layout->hit_split_display = nk_rect(
        roundf(layout->display.x + layout->display.w - (float)gutter * 0.5f),
        top.y,
        (float)gutter,
        top.h);
    layout->hit_split_top_bottom = nk_rect(
        parent.x,
        roundf(top.y + top.h - (float)gutter * 0.5f),
        parent.w,
        (float)gutter);
    layout->hit_split_memory_misc = nk_rect(
        roundf(layout->memory.x + layout->memory.w - (float)gutter * 0.5f),
        bottom.y,
        (float)gutter,
        bottom.h);
    corner = limits->corner_px;
    layout->hit_display_corner = nk_rect(
        layout->display.x + layout->display.w - (float)corner,
        layout->display.y + layout->display.h - (float)corner,
        (float)corner,
        (float)corner);
}

static void debugger_layout_set_display_pixels(debugger_layout *layout, int width, int height, struct nk_rect parent, const debugger_layout_limits *limits)
{
    int min_top_h;
    int max_top_h;

    if (width < limits->min_display_w_px) {
        width = limits->min_display_w_px;
    }
    if (width > (int)(parent.w - limits->min_right_w_px)) {
        width = (int)(parent.w - limits->min_right_w_px);
    }

    min_top_h = limits->registers_h_px + limits->min_disassembly_h_px;
    max_top_h = (int)(parent.h - limits->min_bottom_h_px);
    if (height < min_top_h) {
        height = min_top_h;
    }
    if (height > max_top_h) {
        height = max_top_h;
    }

    layout->display_px_w = width;
    layout->display_px_h = height;
    layout->split_display_right = (float)width / parent.w;
    layout->split_top_bottom = (float)height / parent.h;
}

static void debugger_layout_snap_display_aspect(debugger_layout *layout, struct nk_rect parent, const debugger_layout_limits *limits)
{
    int width = layout->display_px_w;
    int height = layout->display_px_h;
    int width_from_height = (int)roundf((float)height * layout->display_aspect);
    int height_from_width = (int)roundf((float)width / layout->display_aspect);

    if (abs(width_from_height - width) < abs(height_from_width - height)) {
        width = width_from_height;
    } else {
        height = height_from_width;
    }

    debugger_layout_set_display_pixels(layout, width, height, parent, limits);
}

int debugger_layout_handle_drag(debugger_layout *layout, const struct nk_input *input, struct nk_rect parent, const debugger_layout_limits *limits)
{
    int changed = 0;

    if (layout == NULL || input == NULL || limits == NULL) {
        return 0;
    }

    if (input->mouse.buttons[NK_BUTTON_LEFT].down && layout->drag_active == DEBUGGER_LAYOUT_DRAG_NONE) {
        struct nk_vec2 mouse = input->mouse.pos;

        if (nk_input_is_mouse_hovering_rect(input, layout->hit_display_corner)) {
            layout->drag_active = DEBUGGER_LAYOUT_DRAG_DISPLAY_CORNER;
            layout->display_px_w = (int)roundf(layout->display.w);
            layout->display_px_h = (int)roundf(layout->display.h);
        } else if (nk_input_is_mouse_hovering_rect(input, layout->hit_split_display)) {
            layout->drag_active = DEBUGGER_LAYOUT_DRAG_SPLIT_DISPLAY;
        } else if (nk_input_is_mouse_hovering_rect(input, layout->hit_split_top_bottom)) {
            layout->drag_active = DEBUGGER_LAYOUT_DRAG_SPLIT_TOP_BOTTOM;
        } else if (nk_input_is_mouse_hovering_rect(input, layout->hit_split_memory_misc)) {
            layout->drag_active = DEBUGGER_LAYOUT_DRAG_SPLIT_BOTTOM;
        }

        layout->drag_last_x = mouse.x;
        layout->drag_last_y = mouse.y;
        layout->drag_moved = 0;
    }

    if (layout->drag_active != DEBUGGER_LAYOUT_DRAG_NONE && input->mouse.buttons[NK_BUTTON_LEFT].down) {
        struct nk_vec2 mouse = input->mouse.pos;
        float dx = mouse.x - layout->drag_last_x;
        float dy = mouse.y - layout->drag_last_y;

        layout->drag_last_x = mouse.x;
        layout->drag_last_y = mouse.y;
        if (fabsf(dx) >= 1.0f || fabsf(dy) >= 1.0f) {
            layout->drag_moved = 1;
        }

        switch (layout->drag_active) {
            case DEBUGGER_LAYOUT_DRAG_SPLIT_DISPLAY:
                layout->split_display_right += dx / parent.w;
                layout->display_px_w = (int)roundf(layout->split_display_right * parent.w);
                changed = 1;
                break;
            case DEBUGGER_LAYOUT_DRAG_SPLIT_TOP_BOTTOM:
                layout->split_top_bottom += dy / parent.h;
                layout->display_px_h = (int)roundf(layout->split_top_bottom * parent.h);
                changed = 1;
                break;
            case DEBUGGER_LAYOUT_DRAG_SPLIT_BOTTOM:
                layout->split_memory_misc += dx / parent.w;
                changed = 1;
                break;
            case DEBUGGER_LAYOUT_DRAG_DISPLAY_CORNER: {
                int width = layout->display_px_w + (int)roundf(dx);
                int height = layout->display_px_h + (int)roundf(dy);

                debugger_layout_set_display_pixels(layout, width, height, parent, limits);
                changed = 1;
                break;
            }
            case DEBUGGER_LAYOUT_DRAG_NONE:
            default:
                break;
        }
    }

    if (!input->mouse.buttons[NK_BUTTON_LEFT].down) {
        if (layout->drag_active == DEBUGGER_LAYOUT_DRAG_DISPLAY_CORNER && !layout->drag_moved) {
            debugger_layout_snap_display_aspect(layout, parent, limits);
            changed = 1;
        }
        layout->drag_active = DEBUGGER_LAYOUT_DRAG_NONE;
        layout->drag_moved = 0;
    }

    if (changed) {
        debugger_layout_compute(layout, parent, limits);
    }

    return changed;
}
