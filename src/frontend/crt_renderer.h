#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct frontend_crt_effects {
    bool scanlines;
    int scanline_strength;
    bool curvature;
    int curvature_amount;
} frontend_crt_effects;

/* Barrel coefficient matching frontend_crt_process (curvature_amount * 0.0015). */
float frontend_crt_curve_amount(const frontend_crt_effects *effects);

/*
 * CRT output (screen) UV in [-1,1] -> source UV (same pull map as process).
 * Returns false when the output sample falls outside the curved glass.
 */
bool frontend_crt_output_to_source(
    float nx,
    float ny,
    float curve,
    float *out_sxn,
    float *out_syn);

/*
 * Approximate inverse (source UV -> output UV) via Newton iteration.
 * Used to keep paint/probe math testable; hover uses output_to_source.
 */
bool frontend_crt_source_to_output(
    float sxn,
    float syn,
    float curve,
    float *out_nx,
    float *out_ny);

/*
 * Map a point inside image_bounds to an Apple host pixel (0..crop_w-1,
 * 0..crop_h-1). When effects->curvature is on, applies the CRT output->source
 * barrel; scanlines do not affect addressing. Returns false if outside the
 * image rect, outside the curved glass, or out of the crop.
 */
bool frontend_crt_mouse_to_pixel(
    float mouse_x,
    float mouse_y,
    float image_x,
    float image_y,
    float image_w,
    float image_h,
    int crop_w,
    int crop_h,
    const frontend_crt_effects *effects,
    uint16_t *out_px,
    uint16_t *out_py);

/* Produces an output_scale-times ARGB8888 frame, applying effects only inside
   the displayed crop. Destination must hold
   (frame_width * output_scale) * (frame_height * output_scale) pixels.
   Pixels outside the curved screen use outside_pixel. Source and destination
   must not overlap. */
void frontend_crt_process(
    const uint32_t *source,
    uint32_t *destination,
    int frame_width,
    int frame_height,
    int crop_x,
    int crop_y,
    int crop_width,
    int crop_height,
    int output_scale,
    uint32_t outside_pixel,
    const frontend_crt_effects *effects);
