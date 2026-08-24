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

float frontend_crt_curve_amount(const frontend_crt_effects *effects)
{
    int curvature;

    if (effects == NULL || !effects->curvature) {
        return 0.0f;
    }
    curvature = crt_clamp_percent(effects->curvature_amount);
    return (float)curvature * 0.0015f;
}

bool frontend_crt_output_to_source(
    float nx,
    float ny,
    float curve,
    float *out_sxn,
    float *out_syn)
{
    float sxn;
    float syn;

    if (out_sxn == NULL || out_syn == NULL) {
        return false;
    }
    sxn = nx * (1.0f + curve * ny * ny);
    syn = ny * (1.0f + curve * nx * nx);
    /* Inclusive rim so flat (curve=0) corners still map; curved glass
       outside uses |s| > 1 from the barrel expansion. */
    if (fabsf(sxn) > 1.0f || fabsf(syn) > 1.0f) {
        return false;
    }
    *out_sxn = sxn;
    *out_syn = syn;
    return true;
}

bool frontend_crt_source_to_output(
    float sxn,
    float syn,
    float curve,
    float *out_nx,
    float *out_ny)
{
    float nx;
    float ny;
    int iter;

    if (out_nx == NULL || out_ny == NULL) {
        return false;
    }
    if (fabsf(sxn) > 1.0f || fabsf(syn) > 1.0f) {
        return false;
    }
    if (curve <= 0.0f) {
        *out_nx = sxn;
        *out_ny = syn;
        return true;
    }

    /* Seed with identity; Newton on f(n) = n * (1 + c * other^2) - s. */
    nx = sxn;
    ny = syn;
    for (iter = 0; iter < 8; iter++) {
        float f1 = nx * (1.0f + curve * ny * ny) - sxn;
        float f2 = ny * (1.0f + curve * nx * nx) - syn;
        float a = 1.0f + curve * ny * ny;
        float b = nx * (2.0f * curve * ny);
        float c = ny * (2.0f * curve * nx);
        float d = 1.0f + curve * nx * nx;
        float det = a * d - b * c;
        float inv_det;
        float dx;
        float dy;

        if (fabsf(det) < 1.0e-8f) {
            return false;
        }
        inv_det = 1.0f / det;
        dx = (d * f1 - b * f2) * inv_det;
        dy = (-c * f1 + a * f2) * inv_det;
        nx -= dx;
        ny -= dy;
        if (fabsf(dx) < 1.0e-6f && fabsf(dy) < 1.0e-6f) {
            break;
        }
    }
    if (fabsf(nx) > 1.0f || fabsf(ny) > 1.0f) {
        return false;
    }
    *out_nx = nx;
    *out_ny = ny;
    return true;
}

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
    uint16_t *out_py)
{
    float u;
    float v;
    float nx;
    float ny;
    float sxn;
    float syn;
    float curve;
    int px;
    int py;

    if (out_px == NULL || out_py == NULL || crop_w <= 0 || crop_h <= 0 ||
        image_w <= 0.0f || image_h <= 0.0f) {
        return false;
    }
    if (mouse_x < image_x || mouse_y < image_y ||
        mouse_x >= image_x + image_w || mouse_y >= image_y + image_h) {
        return false;
    }

    u = (mouse_x - image_x) / image_w;
    v = (mouse_y - image_y) / image_h;
    nx = u * 2.0f - 1.0f;
    ny = v * 2.0f - 1.0f;
    curve = frontend_crt_curve_amount(effects);
    if (!frontend_crt_output_to_source(nx, ny, curve, &sxn, &syn)) {
        return false;
    }

    u = (sxn + 1.0f) * 0.5f;
    v = (syn + 1.0f) * 0.5f;
    if (u < 0.0f || v < 0.0f || u > 1.0f || v > 1.0f) {
        return false;
    }
    px = (int)(u * (float)crop_w);
    py = (int)(v * (float)crop_h);
    /* u/v == 1.0 maps to crop_w/h before clamp. */
    if (px < 0) {
        px = 0;
    }
    if (py < 0) {
        py = 0;
    }
    if (px >= crop_w) {
        px = crop_w - 1;
    }
    if (py >= crop_h) {
        py = crop_h - 1;
    }
    *out_px = (uint16_t)px;
    *out_py = (uint16_t)py;
    return true;
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
    curve = frontend_crt_curve_amount(effects);
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
