#include "softswitch.h"
#include "video.h"

#include <stdio.h>
#include <stdlib.h>

static void expect_true(const char *name, int v)
{
    if (!v) {
        fprintf(stderr, "FAIL: %s\n", name);
        exit(1);
    }
}

static void expect_addr(
    const char *name,
    uint32_t flags,
    uint16_t px,
    uint16_t py,
    uint16_t bank,
    uint16_t ofs,
    uint32_t host,
    int from_aux)
{
    apple2_video_pixel_addr a;
    char buf[128];

    expect_true(name, apple2_video_pixel_address(flags, px, py, &a));
    if (a.bank_base != bank || a.offset != ofs || a.host_addr != host ||
        (a.from_aux ? 1 : 0) != from_aux) {
        snprintf(buf, sizeof(buf),
            "%s: got bank=$%04X ofs=$%04X adr=$%06X aux=%d "
            "(want bank=$%04X ofs=$%04X adr=$%06X aux=%d)",
            name,
            (unsigned)a.bank_base, (unsigned)a.offset, (unsigned)a.host_addr,
            a.from_aux ? 1 : 0,
            (unsigned)bank, (unsigned)ofs, (unsigned)host, from_aux);
        fprintf(stderr, "FAIL: %s\n", buf);
        exit(1);
    }
}

int main(void)
{
    uint16_t text0 = apple2_video_text_line_base(0);
    uint16_t hgr0 = apple2_video_hgr_line_offset(0);
    uint16_t text20 = apple2_video_text_line_base(20); /* MIXED y=160 -> row 20 */

    {
        apple2_video_pixel_addr scratch;
        expect_true("reject null", !apple2_video_pixel_address(A2S_TEXT, 0, 0, NULL));
        expect_true("reject px",
            !apple2_video_pixel_address(A2S_TEXT, 560, 0, &scratch));
        expect_true("reject py",
            !apple2_video_pixel_address(A2S_TEXT, 0, 192, &scratch));
    }

    /* TEXT 40 page1: scanner col 0, any px in 0..13 */
    expect_addr("text40 p1", A2S_TEXT, 0, 0,
        0x0400u, text0, 0x0400u + text0, 0);
    expect_addr("text40 p1 col1", A2S_TEXT, 14, 0,
        0x0400u, (uint16_t)(text0 + 1u), 0x0400u + text0 + 1u, 0);

    /* TEXT 40 PAGE2 (no 80STORE) */
    expect_addr("text40 p2", A2S_TEXT | A2S_PAGE2, 0, 0,
        0x0800u, text0, 0x0800u + text0, 0);

    /* TEXT 40 80STORE+PAGE2 -> aux $400 */
    expect_addr("text40 80store p2", A2S_TEXT | A2S_80STORE | A2S_PAGE2, 0, 0,
        0x0400u, text0, 0x10400u + text0, 1);

    /* TEXT 80: left half aux, right half main */
    expect_addr("text80 aux", A2S_TEXT | A2S_COL80, 0, 0,
        0x0400u, text0, 0x10400u + text0, 1);
    expect_addr("text80 main", A2S_TEXT | A2S_COL80, 7, 0,
        0x0400u, text0, 0x0400u + text0, 0);
    expect_addr("text80 col1 aux", A2S_TEXT | A2S_COL80, 14, 0,
        0x0400u, (uint16_t)(text0 + 1u), 0x10400u + text0 + 1u, 1);

    /* LORES page1 */
    expect_addr("lores", 0u, 0, 0,
        0x0400u, text0, 0x0400u + text0, 0);

    /* DLORES: aux then main half-columns; PAGE2 without 80STORE -> $800 */
    expect_addr("dlores aux", A2S_COL80, 0, 0,
        0x0400u, text0, 0x10400u + text0, 1);
    expect_addr("dlores main", A2S_COL80, 7, 0,
        0x0400u, text0, 0x0400u + text0, 0);
    expect_addr("dlores p2 aux", A2S_COL80 | A2S_PAGE2, 0, 0,
        0x0800u, text0, 0x10800u + text0, 1);

    /* HGR page1 / page2 */
    expect_addr("hgr p1", A2S_HIRES, 0, 0,
        0x2000u, hgr0, 0x2000u + hgr0, 0);
    expect_addr("hgr p1 col1", A2S_HIRES, 14, 0,
        0x2000u, (uint16_t)(hgr0 + 1u), 0x2000u + hgr0 + 1u, 0);
    expect_addr("hgr p2", A2S_HIRES | A2S_PAGE2, 0, 0,
        0x4000u, hgr0, 0x4000u + hgr0, 0);
    expect_addr("hgr 80store p2", A2S_HIRES | A2S_80STORE | A2S_PAGE2, 0, 0,
        0x2000u, hgr0, 0x12000u + hgr0, 1);

    /* Illustrative design example: bank $2000 ofs $0202 */
    {
        uint16_t want_ofs = 0x0202u;
        uint8_t row;
        uint16_t col = 0;
        int found = 0;
        for (row = 0; row < 192u; row++) {
            uint16_t base = apple2_video_hgr_line_offset(row);
            if (want_ofs >= base && want_ofs < (uint16_t)(base + 40u)) {
                col = (uint16_t)(want_ofs - base);
                expect_addr("hgr ofs 0202", A2S_HIRES,
                    (uint16_t)(col * 14u), row,
                    0x2000u, want_ofs, 0x2202u, 0);
                found = 1;
                break;
            }
        }
        expect_true("hgr ofs 0202 exists", found);
    }

    /* DHGR: px 0..6 aux col0, 7..13 main col0, 14..20 aux col1, 21..27 main col1 */
    expect_addr("dhgr aux0", A2S_HIRES | A2S_COL80, 0, 0,
        0x2000u, hgr0, 0x12000u + hgr0, 1);
    expect_addr("dhgr main0", A2S_HIRES | A2S_COL80, 7, 0,
        0x2000u, hgr0, 0x2000u + hgr0, 0);
    expect_addr("dhgr aux1", A2S_HIRES | A2S_COL80, 14, 0,
        0x2000u, (uint16_t)(hgr0 + 1u), 0x12000u + hgr0 + 1u, 1);
    expect_addr("dhgr main1", A2S_HIRES | A2S_COL80, 21, 0,
        0x2000u, (uint16_t)(hgr0 + 1u), 0x2000u + hgr0 + 1u, 0);
    expect_addr("dhgr p2 aux", A2S_HIRES | A2S_COL80 | A2S_PAGE2, 0, 0,
        0x4000u, hgr0, 0x14000u + hgr0, 1);

    /* MIXED: y=160 is text row 20; graphics above stays HGR */
    expect_addr("mixed text y160", A2S_HIRES | A2S_MIXED, 0, 160,
        0x0400u, text20, 0x0400u + text20, 0);
    expect_addr("mixed hgr y159", A2S_HIRES | A2S_MIXED, 0, 159,
        0x2000u, apple2_video_hgr_line_offset(159),
        0x2000u + apple2_video_hgr_line_offset(159), 0);
    expect_addr("mixed 80 text", A2S_HIRES | A2S_MIXED | A2S_COL80 | A2S_DHIRES,
        0, 160, 0x0400u, text20, 0x10400u + text20, 1);

    printf("ok\n");
    return 0;
}
