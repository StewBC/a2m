#include "crt_renderer.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

static int crt_clamp_percent(int value)
{
    if (value < 0) return 0;
    if (value > 100) return 100;
    return value;
}

static uint32_t crt_darken(uint32_t pixel, int strength)
{
    unsigned int factor = (unsigned int)(1000 - crt_clamp_percent(strength) * 8);
    unsigned int r = ((pixel >> 16) & 0xffu) * factor / 1000u;
    unsigned int g = ((pixel >> 8) & 0xffu) * factor / 1000u;
    unsigned int b = (pixel & 0xffu) * factor / 1000u;

    return (pixel & 0xff000000u) | (r << 16) | (g << 8) | b;
}

static unsigned int crt_blend_channel(
    uint32_t outside,
    uint32_t inside,
    unsigned int shift,
    unsigned int inside_weight)
{
    unsigned int outside_channel = (outside >> shift) & 0xffu;
    unsigned int inside_channel = (inside >> shift) & 0xffu;

    return (outside_channel * (256u - inside_weight) +
        inside_channel * inside_weight + 128u) >> 8;
}

static uint32_t crt_blend_edge(uint32_t outside, uint32_t inside, float coverage)
{
    unsigned int weight;

    if (coverage <= 0.0f) return outside;
    if (coverage >= 1.0f) return inside;
    weight = (unsigned int)(coverage * 256.0f + 0.5f);
    return
        (crt_blend_channel(outside, inside, 24, weight) << 24) |
        (crt_blend_channel(outside, inside, 16, weight) << 16) |
        (crt_blend_channel(outside, inside, 8, weight) << 8) |
        crt_blend_channel(outside, inside, 0, weight);
}

/* Clamp to the crop edge rather than returning black for out-of-range taps. The
   bilinear taps for the outermost output pixels legitimately sit up to one source
   pixel outside the crop (sx is -0.25 at x=0), so answering them with black bled
   a dark fringe into all four edges of the picture - visible as an outline around
   the C64 border the moment any CRT effect switched rendering onto this path.
   This is standard CLAMP_TO_EDGE and lets the border colour run to the window
   edge. It does not affect curvature's genuinely-outside region: frontend_crt_
   process range-tests sxn/syn and paints black there without sampling at all. */
static uint32_t crt_crop_pixel(
    const uint32_t *source,
    int frame_width,
    int crop_x,
    int crop_y,
    int crop_width,
    int crop_height,
    int x,
    int y)
{
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= crop_width) x = crop_width - 1;
    if (y >= crop_height) y = crop_height - 1;
    return source[(size_t)(crop_y + y) * (size_t)frame_width +
        (size_t)(crop_x + x)];
}

static unsigned int crt_bilinear_channel(
    uint32_t p00,
    uint32_t p10,
    uint32_t p01,
    uint32_t p11,
    unsigned int shift,
    float fx,
    float fy)
{
    float top = (float)((p00 >> shift) & 0xffu) * (1.0f - fx) +
        (float)((p10 >> shift) & 0xffu) * fx;
    float bottom = (float)((p01 >> shift) & 0xffu) * (1.0f - fx) +
        (float)((p11 >> shift) & 0xffu) * fx;
    return (unsigned int)(top * (1.0f - fy) + bottom * fy + 0.5f);
}

static uint32_t crt_bilinear_sample(
    const uint32_t *source,
    int frame_width,
    int crop_x,
    int crop_y,
    int crop_width,
    int crop_height,
    float x,
    float y)
{
    int x0 = (int)x;
    int y0 = (int)y;
    float fx;
    float fy;
    uint32_t p00;
    uint32_t p10;
    uint32_t p01;
    uint32_t p11;

    if (x < (float)x0) x0--;
    if (y < (float)y0) y0--;
    fx = x - (float)x0;
    fy = y - (float)y0;
    p00 = crt_crop_pixel(source, frame_width, crop_x, crop_y,
        crop_width, crop_height, x0, y0);
    p10 = crt_crop_pixel(source, frame_width, crop_x, crop_y,
        crop_width, crop_height, x0 + 1, y0);
    p01 = crt_crop_pixel(source, frame_width, crop_x, crop_y,
        crop_width, crop_height, x0, y0 + 1);
    p11 = crt_crop_pixel(source, frame_width, crop_x, crop_y,
        crop_width, crop_height, x0 + 1, y0 + 1);

    return
        (crt_bilinear_channel(p00, p10, p01, p11, 24, fx, fy) << 24) |
        (crt_bilinear_channel(p00, p10, p01, p11, 16, fx, fy) << 16) |
        (crt_bilinear_channel(p00, p10, p01, p11, 8, fx, fy) << 8) |
        crt_bilinear_channel(p00, p10, p01, p11, 0, fx, fy);
}

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
    const frontend_crt_effects *effects)
{
    int x;
    int y;
    int output_width;
    int output_height;
    int output_frame_width;
    int output_frame_height;
    int curvature;
    float curve;
    float edge_x;
    float edge_y;

    if (source == NULL || destination == NULL || effects == NULL ||
        frame_width <= 0 || frame_height <= 0 || crop_width <= 0 || crop_height <= 0 ||
        output_scale <= 0 ||
        crop_x < 0 || crop_y < 0 || crop_x + crop_width > frame_width ||
        crop_y + crop_height > frame_height) {
        return;
    }

    output_width = crop_width * output_scale;
    output_height = crop_height * output_scale;
    output_frame_width = frame_width * output_scale;
    output_frame_height = frame_height * output_scale;
    memset(destination, 0,
        (size_t)output_frame_width * (size_t)output_frame_height * sizeof(*destination));
    curvature = effects->curvature ? crt_clamp_percent(effects->curvature_amount) : 0;
    curve = (float)curvature * 0.0015f;
    edge_x = 2.0f / (float)output_width;
    edge_y = 2.0f / (float)output_height;

    for (y = 0; y < output_height; ++y) {
        float ny = ((float)y + 0.5f) * 2.0f / (float)output_height - 1.0f;
        for (x = 0; x < output_width; ++x) {
            float nx = ((float)x + 0.5f) * 2.0f / (float)output_width - 1.0f;
            float sxn = nx * (1.0f + curve * ny * ny);
            float syn = ny * (1.0f + curve * nx * nx);
            float distance_x = (1.0f - fabsf(sxn)) / edge_x;
            float distance_y = (1.0f - fabsf(syn)) / edge_y;
            float coverage = (distance_x < distance_y ? distance_x : distance_y) + 0.5f;
            uint32_t pixel = outside_pixel;

            if (coverage > 0.0f) {
                float sx = (sxn + 1.0f) * 0.5f * (float)crop_width - 0.5f;
                float sy = (syn + 1.0f) * 0.5f * (float)crop_height - 0.5f;
                uint32_t inside_pixel = crt_bilinear_sample(source, frame_width, crop_x, crop_y,
                    crop_width, crop_height, sx, sy);
                /* Darken alternate OUTPUT rows, so each C64 raster line gets a
                   lit half and a dark half - that gap between lines is what a
                   scanline actually is. Keying off (y / output_scale) instead
                   darkens every other C64 line in full, which halves the
                   picture's brightness rather than adding gaps, and turns ugly
                   by ~5% strength. */
                if (effects->scanlines && (y & 1) != 0) {
                    inside_pixel = crt_darken(inside_pixel, effects->scanline_strength);
                }
                pixel = crt_blend_edge(outside_pixel, inside_pixel, coverage);
            }
            destination[(size_t)(crop_y * output_scale + y) * (size_t)output_frame_width +
                (size_t)(crop_x * output_scale + x)] = pixel;
        }
    }
}
