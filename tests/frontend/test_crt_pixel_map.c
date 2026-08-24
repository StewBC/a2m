#include "crt_renderer.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static void expect_true(const char *name, int v)
{
    if (!v) {
        fprintf(stderr, "FAIL: %s\n", name);
        exit(1);
    }
}

int main(void)
{
    frontend_crt_effects off = {0};
    frontend_crt_effects curved = {
        .scanlines = false,
        .scanline_strength = 0,
        .curvature = true,
        .curvature_amount = 40
    };
    uint16_t px = 0;
    uint16_t py = 0;
    float curve;
    float sxn;
    float syn;
    float nx;
    float ny;
    int ix;
    int iy;

    /* Flat: center of 560x192 image -> mid pixel. */
    expect_true("flat center",
        frontend_crt_mouse_to_pixel(
            140.0f, 96.0f,
            0.0f, 0.0f, 280.0f, 192.0f,
            560, 192, &off, &px, &py));
    expect_true("flat center px", px == 280u);
    expect_true("flat center py", py == 96u);

    /* Outside image rect. */
    expect_true("outside",
        !frontend_crt_mouse_to_pixel(
            -1.0f, 0.0f,
            0.0f, 0.0f, 280.0f, 192.0f,
            560, 192, &off, &px, &py));

    /* Fill-style bounds (stretch): left-top corner. */
    expect_true("fill corner",
        frontend_crt_mouse_to_pixel(
            10.0f, 20.0f,
            10.0f, 20.0f, 400.0f, 300.0f,
            560, 192, &off, &px, &py));
    expect_true("fill corner px", px == 0u);
    expect_true("fill corner py", py == 0u);

    curve = frontend_crt_curve_amount(&curved);
    expect_true("curve > 0", curve > 0.0f);

    /* Round-trip a grid through forward + inverse. */
    for (iy = -8; iy <= 8; iy++) {
        for (ix = -8; ix <= 8; ix++) {
            float in_nx = (float)ix / 10.0f;
            float in_ny = (float)iy / 10.0f;
            char name[64];

            if (!frontend_crt_output_to_source(in_nx, in_ny, curve, &sxn, &syn)) {
                continue; /* near rim / outside */
            }
            expect_true("inverse",
                frontend_crt_source_to_output(sxn, syn, curve, &nx, &ny));
            snprintf(name, sizeof(name), "rt nx %d,%d", ix, iy);
            expect_true(name, fabsf(nx - in_nx) < 1.0e-4f);
            snprintf(name, sizeof(name), "rt ny %d,%d", ix, iy);
            expect_true(name, fabsf(ny - in_ny) < 1.0e-4f);
        }
    }

    /* Curved mouse map still resolves near center. */
    expect_true("curved center",
        frontend_crt_mouse_to_pixel(
            140.0f, 96.0f,
            0.0f, 0.0f, 280.0f, 192.0f,
            560, 192, &curved, &px, &py));
    expect_true("curved center near", px > 250u && px < 310u && py > 80u && py < 112u);

    printf("ok\n");
    return 0;
}
