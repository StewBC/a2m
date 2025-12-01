// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "unk_lib.h"


static float clampf(float v, float lo, float hi) {
    if(v < lo) {
        v = lo;
    } else if(v > hi) {
        v = hi;
    }
    return v;
}

void unk_layout_init(LAYOUT *l, float A, float B, float C, int a2w, int a2h) {
    l->split_top_left_right = clampf(A, 0.05f, 0.95f);
    l->split_top_bottom = clampf(B, 0.10f, 0.90f);
    l->split_bottom_left_right = clampf(C, 0.10f, 0.90f);
    l->apple2_px_w = (a2w > 0) ? a2w : 560;
    l->apple2_px_h = (a2h > 0) ? a2h : 384;
    l->apple2_aspect = (l->apple2_px_h > 0) ? (float)l->apple2_px_w / (float)l->apple2_px_h : 1.0f;
    l->drag_active = LAYOUT_DRAG_NONE;
}

void unk_layout_init_apple2(LAYOUT *l, struct nk_rect parent, int a2w, int a2h, float mem_ratio) {
    l->apple2_px_w = (int)roundf((float)a2w);
    l->apple2_px_h = (int)roundf((float)a2h);
    l->apple2_aspect = (l->apple2_px_h > 0) ? (float)l->apple2_px_w / (float)l->apple2_px_h : 1.0f;

    // A and B come directly from Apple II pixel seed
    l->split_top_left_right = clampf((float)l->apple2_px_w / parent.w, 0.05f, 0.95f);
    l->split_top_bottom = clampf((float)l->apple2_px_h / parent.h, 0.10f, 0.90f);

    // C from caller mem_ratio
    l->split_bottom_left_right = clampf(mem_ratio, 0.10f, 0.90f);

    l->drag_active = LAYOUT_DRAG_NONE;
}

// Core compute: build rects, then hit rects
void unk_layout_compute(LAYOUT *l, struct nk_rect parent, const LAYOUT_LIMITS *lim) {
    // 1) Top vs Bottom by B (clamped for min sizes)
    float B_min = (float)(lim->cpu_h_px + lim->min_dis_h_px) / parent.h;
    float B_max = 1.0f - (float)lim->min_bot_h_px / parent.h;
    float B = clampf(l->split_top_bottom, B_min, B_max);
    float top_h = parent.h * B;
    struct nk_rect TOP = { parent.x, parent.y, parent.w, top_h };
    struct nk_rect BOT = { parent.x, parent.y + top_h, parent.w, parent.h - top_h };

    // 2) Apple II vs Right by A (clamped for min widths)
    float A_min = (float)lim->min_a2_w_px / parent.w;
    float A_max = 1.0f - (float)lim->min_right_w_px / parent.w;
    float A = clampf(l->split_top_left_right, A_min, A_max);
    float a2_w = parent.w * A;
    l->apple2 = (struct nk_rect) {
        TOP.x, TOP.y, a2_w, TOP.h
    };
    struct nk_rect RIGHT = { TOP.x + a2_w, TOP.y, TOP.w - a2_w, TOP.h };

    // 3) Right column: fixed CPU height, remainder Disasm (clamp min disasm)
    int cpu_h = lim->cpu_h_px;
    if(cpu_h > (int)RIGHT.h - lim->min_dis_h_px) {
        cpu_h = (int)RIGHT.h - lim->min_dis_h_px;
    }
    l->cpu    = (struct nk_rect) {
        RIGHT.x, RIGHT.y, RIGHT.w, (float)cpu_h
    };
    l->dasm = (struct nk_rect) {
        RIGHT.x, RIGHT.y + cpu_h, RIGHT.w, RIGHT.h - cpu_h
    };

    // 4) Bottom row split C: Mem | Misc
    float C_min = (float)lim->min_mem_w_px / BOT.w;
    float C_max = 1.0f - (float)lim->min_misc_w_px / BOT.w;
    float C = clampf(l->split_bottom_left_right, C_min, C_max);
    float mem_w = BOT.w * C;
    l->mem  = (struct nk_rect) {
        BOT.x, BOT.y, mem_w, BOT.h
    };
    l->misc = (struct nk_rect) {
        BOT.x + mem_w, BOT.y, BOT.w - mem_w, BOT.h
    };

    // Snap to pixel grid for crisp borders
    struct nk_rect *R[] = { &l->apple2, &l->cpu, &l->dasm, &l->mem, &l->misc };
    for(int i = 0; i < 5; ++i) {
        R[i]->x = roundf(R[i]->x);
        R[i]->y = roundf(R[i]->y);
        R[i]->w = roundf(R[i]->w);
        R[i]->h = roundf(R[i]->h);
    }

    // 5) Hit gutters: overlay, not subtracting space
    const int g = lim->gutter_px;
    l->hit_split_A = (struct nk_rect) {
        roundf(l->apple2.x + l->apple2.w - g * 0.5f), TOP.y, (float)g, TOP.h
    };
    l->hit_split_B = (struct nk_rect) {
        parent.x, roundf(TOP.y + TOP.h - g * 0.5f), parent.w, (float)g
    };
    l->hit_split_C = (struct nk_rect) {
        roundf(l->mem.x + l->mem.w - g * 0.5f), BOT.y, (float)g, BOT.h
    };

    // 6) Apple II corner handles (square)
    const int c = lim->corner_px;
    float ax = l->apple2.x, ay = l->apple2.y, aw = l->apple2.w, ah = l->apple2.h;
    l->hit_a2_tl = (struct nk_rect) {
        ax, ay, (float)c, (float)c
    };
    l->hit_a2_tr = (struct nk_rect) {
        ax + aw - c, ay, (float)c, (float)c
    };
    l->hit_a2_bl = (struct nk_rect) {
        ax, ay + ah - c, (float)c, (float)c
    };
    l->hit_a2_br = (struct nk_rect) {
        ax + aw - c, ay + ah - c, (float)c, (float)c
    };
}

void unk_layout_on_window_resize(LAYOUT *l, struct nk_rect parent, const LAYOUT_LIMITS *lim) {
    l->apple2_px_w = l->split_top_left_right * parent.w;
    l->apple2_px_h = l->split_top_bottom * parent.h;
}

// Splitter drags
void unk_layout_on_drag_split_A(LAYOUT *l, float dx, struct nk_rect parent, const LAYOUT_LIMITS *lim) {
    float A = l->split_top_left_right + dx / parent.w;
    float A_min = (float)lim->min_a2_w_px / parent.w;
    float A_max = 1.0f - (float)lim->min_right_w_px / parent.w;
    l->split_top_left_right = clampf(A, A_min, A_max);

    // Update remembered Apple II px width for future resizes
    l->apple2_px_w = (int)roundf(parent.w * l->split_top_left_right);
    // Height follows B; remembered separately via B or corner/slider paths
}

void unk_layout_on_drag_split_B(LAYOUT *l, float dy, struct nk_rect parent, const LAYOUT_LIMITS *lim) {
    float B = l->split_top_bottom + dy / parent.h;
    float B_min = (float)(lim->cpu_h_px + lim->min_dis_h_px) / parent.h;
    float B_max = 1.0f - (float)lim->min_bot_h_px / parent.h;
    l->split_top_bottom = clampf(B, B_min, B_max);

    // Update remembered Apple II px height for future resizes
    l->apple2_px_h = (int)roundf(parent.h * l->split_top_bottom);
}

void unk_layout_on_drag_split_C(LAYOUT *l, float dx, struct nk_rect parent, const LAYOUT_LIMITS *lim) {
    // Bottom region is full width of parent in this layout
    float C = l->split_bottom_left_right + dx / parent.w;
    float C_min = (float)lim->min_mem_w_px / parent.w;
    float C_max = 1.0f - (float)lim->min_misc_w_px / parent.w;
    l->split_bottom_left_right = clampf(C, C_min, C_max);
}

// Apple II corner drag
void unk_layout_on_drag_a2_corner(LAYOUT *l, float dx, float dy, int preserve_aspect, struct nk_rect parent, const LAYOUT_LIMITS *lim) {
    int new_w = l->apple2_px_w;
    int new_h = l->apple2_px_h;

    if(preserve_aspect) {
        // Use the dominant axis for proportional scale
        float delta = fabsf(dx) > fabsf(dy) ? dx : dy;
        new_w = (int)roundf((float)l->apple2_px_w + delta);
        new_h = (int)roundf((float)new_w / fmaxf(l->apple2_aspect, 1e-6f));
    } else {
        new_w = (int)roundf((float)l->apple2_px_w + dx);
        new_h = (int)roundf((float)l->apple2_px_h + dy);
    }

    // Clamp to structural minimums and available space
    if(new_w < lim->min_a2_w_px) {
        new_w = lim->min_a2_w_px;
    }
    // Ensure right column has room
    if(new_w > (int)(parent.w - lim->min_right_w_px)) {
        new_w = (int)(parent.w - lim->min_right_w_px);
    }

    // Top height must leave room for CPU+Disasm minimums and bottom minimum
    int top_min_h = lim->cpu_h_px + lim->min_dis_h_px;
    int top_max_h = (int)(parent.h - lim->min_bot_h_px);
    if(new_h < top_min_h) {
        new_h = top_min_h;
    }
    if(new_h > top_max_h) {
        new_h = top_max_h;
    }

    unk_layout_set_apple2_pixels(l, new_w, new_h, parent, lim);
}

// Dragging layout handler
int unk_layout_handle_drag(LAYOUT *l, const struct nk_input *in, struct nk_rect parent, const LAYOUT_LIMITS *lim) {
    static float last_x = 0, last_y = 0;
    int changed = 0;

    // On mouse down — select what’s active
    if(in->mouse.buttons[NK_BUTTON_LEFT].down && !l->drag_active) {
        struct nk_vec2 m = in->mouse.pos;
        // Priority: Apple2 corner > split A > split B > split C because corner hit-rect overlaps A and B.
        if(nk_input_is_mouse_hovering_rect(in, l->hit_a2_br)) {
            l->drag_active = LAYOUT_DRAG_APPLE2_CORNER;
        } else if(nk_input_is_mouse_hovering_rect(in, l->hit_split_A)) {
            l->drag_active = LAYOUT_DRAG_SPLIT_A;
        } else if(nk_input_is_mouse_hovering_rect(in, l->hit_split_B)) {
            l->drag_active = LAYOUT_DRAG_SPLIT_B;
        } else if(nk_input_is_mouse_hovering_rect(in, l->hit_split_C)) {
            l->drag_active = LAYOUT_DRAG_SPLIT_C;
        }
        last_x = m.x;
        last_y = m.y;
    }

    // On mouse move while active — apply deltas
    if(l->drag_active && in->mouse.buttons[NK_BUTTON_LEFT].down) {
        struct nk_vec2 m = in->mouse.pos;
        float dx = m.x - last_x;
        float dy = m.y - last_y;
        last_x = m.x;
        last_y = m.y;

        switch(l->drag_active) {
            case LAYOUT_DRAG_SPLIT_A:
                unk_layout_on_drag_split_A(l, dx, parent, lim);
                changed = 1;
                break;
            case LAYOUT_DRAG_SPLIT_B:
                unk_layout_on_drag_split_B(l, dy, parent, lim);
                changed = 1;
                break;
            case LAYOUT_DRAG_SPLIT_C:
                unk_layout_on_drag_split_C(l, dx, parent, lim);
                changed = 1;
                break;
            case LAYOUT_DRAG_APPLE2_CORNER:
                unk_layout_on_drag_a2_corner(l, dx, dy, 1, parent, lim);
                changed = 1;
                break;
            default:
                break;
        }
    }

    // On mouse release — clear drag
    if(!in->mouse.buttons[NK_BUTTON_LEFT].down) {
        l->drag_active = LAYOUT_DRAG_NONE;
    }

    if(changed) {
        unk_layout_compute(l, parent, lim);
    }

    return changed;
}


// Slider/API: set Apple II px size explicitly
void unk_layout_set_apple2_pixels(LAYOUT *l, int new_w, int new_h, struct nk_rect parent, const LAYOUT_LIMITS *lim) {
    l->apple2_px_w = new_w;
    l->apple2_px_h = new_h;

    // Re-derive A and B from pixels; clamp
    float A_min = (float)lim->min_a2_w_px / parent.w;
    float A_max = 1.0f - (float)lim->min_right_w_px / parent.w;
    float A = (float)l->apple2_px_w / parent.w;
    l->split_top_left_right = clampf(A, A_min, A_max);

    float B_min = (float)(lim->cpu_h_px + lim->min_dis_h_px) / parent.h;
    float B_max = 1.0f - (float)lim->min_bot_h_px / parent.h;
    float B = (float)l->apple2_px_h / parent.h;
    l->split_top_bottom = clampf(B, B_min, B_max);
}
