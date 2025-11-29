// Apple ][+ and //e Emhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

/* Splitters:
   A: Top AppleII | top Right-column
   B: Top block | Bottom block
   C: Bottom Mem | Bottom Misc
*/

typedef enum {
    LAYOUT_DRAG_NONE = 0,
    LAYOUT_DRAG_SPLIT_A,
    LAYOUT_DRAG_SPLIT_B,
    LAYOUT_DRAG_SPLIT_C,
    LAYOUT_DRAG_APPLE2_CORNER           //  proportional by default
} LAYOUT_DRAG_KIND;

typedef struct LAYOUT_LIMITS {
    int cpu_h_px;                       //  fixed CPU height
    int min_a2_w_px;                    //  Apple II min width
    int min_right_w_px;                 //  minimum width for right column
    int min_dis_h_px;                   //  minimum Disasm height
    int min_bot_h_px;                   //  minimum bottom row height
    int min_mem_w_px;                   //  minimum Mem width
    int min_misc_w_px;                  //  minimum Misc width
    int gutter_px;                      //  hit gutter thickness for splitters
    int corner_px;                      //  hit square size for Apple II corners
    int dpi_scale;                      //  for scaling borders/gutters if desired
} LAYOUT_LIMITS;

typedef struct LAYOUT {
    //  Fractions that drive compute()
    float split_top_left_right;         //  A: Apple II | right (0..1 of parent W)
    float split_top_bottom;             //  B: Top | Bottom (0..1 of parent H)
    float split_bottom_left_right;      //  C: Mem | Misc (0..1 of bottom W)

    //  Policy anchors: keep Apple II pixel size on window resize
    int   apple2_px_w;                  //  remembered Apple II width in px
    int   apple2_px_h;                  //  remembered Apple II height in px
    float apple2_aspect;                //  w/h for proportional corner resize

    //  Computed rects (screen space, in pixels)
    struct nk_rect apple2;
    struct nk_rect cpu;
    struct nk_rect dasm;
    struct nk_rect mem;
    struct nk_rect misc;

    //  Hit rects (screen space) for your input/hover logic
    struct nk_rect hit_split_A;         //  vertical gutter between Apple2 | Right
    struct nk_rect hit_split_B;         //  horizontal gutter between Top | Bottom
    struct nk_rect hit_split_C;         //  vertical gutter between Mem | Misc
    struct nk_rect hit_a2_tl, hit_a2_tr, hit_a2_bl, hit_a2_br; //  Apple II corners

    //  Drag state (instance-local; safe for multiple layouts)
    LAYOUT_DRAG_KIND drag_active;
} LAYOUT;

//  ---------- Init ----------

//  Ratio-based init (kept for reuse; you won't use it now)
void unk_layout_init(LAYOUT *l, float split_top_left_right, float split_top_bottom, float split_bottom_left_right, int apple2_px_w, int apple2_px_h);

//  Pixel-seeded init (use this): Seeds Apple II pixel size; derives A and B from parent size; C from mem_ratio
void unk_layout_init_apple2(LAYOUT *l, struct nk_rect parent, int apple2_px_w, int apple2_px_h, float mem_ratio); //  0..1 for Mem within bottom

//  ---------- Policy hooks ----------

//  Call when OS window size changes: Keeps Apple II pixel size; recomputes A and B from apple2_px_w/h; clamps.
void unk_layout_on_window_resize(LAYOUT *l, struct nk_rect parent, const LAYOUT_LIMITS *lim);

//  Splitter drags (update fractions and the remembered Apple II size as needed)
void unk_layout_on_drag_split_A(LAYOUT *l, float dx_pixels, struct nk_rect parent, const LAYOUT_LIMITS *lim);
void unk_layout_on_drag_split_B(LAYOUT *l, float dy_pixels, struct nk_rect parent, const LAYOUT_LIMITS *lim);
void unk_layout_on_drag_split_C(LAYOUT *l, float dx_pixels, struct nk_rect parent, const LAYOUT_LIMITS *lim);

//  Apple II corner drag; proportional by default. If preserve_aspect=false, applies dx/dy independently (non-uniform).
void unk_layout_on_drag_a2_corner(LAYOUT *l, float dx_pixels, float dy_pixels, int preserve_aspect, struct nk_rect parent, const LAYOUT_LIMITS *lim);

//  ---------- Dragging layout handler ----------
int unk_layout_handle_drag(LAYOUT *l, const struct nk_input *in, struct nk_rect parent, const LAYOUT_LIMITS *lim);

//  If you drive Apple II size from a slider:
void unk_layout_set_apple2_pixels(LAYOUT *l, int new_w, int new_h, struct nk_rect parent, const LAYOUT_LIMITS *lim);

//  ---------- Compute ----------

//  Recompute pane rects & hit rects from current splits and limits.
void unk_layout_compute(LAYOUT *l, struct nk_rect parent, const LAYOUT_LIMITS *lim);

//  Optional helper to clear drag state when mouse released.
static inline void unk_layout_clear_drag(LAYOUT *l) {
    l->drag_active = LAYOUT_DRAG_NONE;
}
