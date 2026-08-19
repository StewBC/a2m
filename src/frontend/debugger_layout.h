#pragma once

#include "nuklear_config.h"

typedef enum debugger_layout_drag_kind {
    DEBUGGER_LAYOUT_DRAG_NONE = 0,
    DEBUGGER_LAYOUT_DRAG_SPLIT_DISPLAY,
    DEBUGGER_LAYOUT_DRAG_SPLIT_TOP_BOTTOM,
    DEBUGGER_LAYOUT_DRAG_SPLIT_BOTTOM,
    DEBUGGER_LAYOUT_DRAG_DISPLAY_CORNER
} debugger_layout_drag_kind;

typedef struct debugger_layout_limits {
    int registers_h_px;
    int min_display_w_px;
    int min_right_w_px;
    int min_disassembly_h_px;
    int min_bottom_h_px;
    int min_memory_w_px;
    int min_misc_w_px;
    int gutter_px;
    int corner_px;
} debugger_layout_limits;

typedef struct debugger_layout {
    float split_display_right;
    float split_top_bottom;
    float split_memory_misc;
    int display_px_w;
    int display_px_h;
    float display_aspect;

    struct nk_rect display;
    struct nk_rect registers;
    struct nk_rect disassembly;
    struct nk_rect memory;
    struct nk_rect misc;

    struct nk_rect hit_split_display;
    struct nk_rect hit_split_top_bottom;
    struct nk_rect hit_split_memory_misc;
    struct nk_rect hit_display_corner;

    debugger_layout_drag_kind drag_active;
    float drag_last_x;
    float drag_last_y;
    int drag_moved;
} debugger_layout;

void debugger_layout_init(debugger_layout *layout);
void debugger_layout_compute(debugger_layout *layout, struct nk_rect parent, const debugger_layout_limits *limits);
int debugger_layout_handle_drag(debugger_layout *layout, const struct nk_input *input, struct nk_rect parent, const debugger_layout_limits *limits);
