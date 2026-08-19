#pragma once

/*
 * Apple II debug memory *areas* (VIEW_FLAGS) — a2m memdst semantics.
 *
 * Three independent fields:
 *   48K plane:  Map | Main | Aux
 *   $C100:      Map | ROM   (//e internal ROM force)
 *   $D000-$FFFF: Map | LC bank1 | LC bank2 | ROM
 *
 * UI Opt+M / Source menu typically cycle named presets (helpers below).
 * Right-click can expose full multi-field selection later.
 */

#include <stddef.h>
#include <stdint.h>

enum {
    A2SEL_48K_SHIFT = 0,
    A2SEL_48K_BITS = 2,
    A2SEL_C100_SHIFT = (A2SEL_48K_SHIFT + A2SEL_48K_BITS),
    A2SEL_C100_BITS = 1,
    A2SEL_D000_SHIFT = (A2SEL_C100_SHIFT + A2SEL_C100_BITS),
    A2SEL_D000_BITS = 2,

    A2SEL_48K_MASK = ((1u << A2SEL_48K_BITS) - 1u) << A2SEL_48K_SHIFT,
    A2SEL_C100_MASK = ((1u << A2SEL_C100_BITS) - 1u) << A2SEL_C100_SHIFT,
    A2SEL_D000_MASK = ((1u << A2SEL_D000_BITS) - 1u) << A2SEL_D000_SHIFT
};

typedef enum a2sel_48k {
    A2SEL48K_MAPPED = 0,
    A2SEL48K_MAIN = 1,
    A2SEL48K_AUX = 2
} a2sel_48k;

typedef enum a2sel_c100 {
    A2SELC100_MAPPED = 0,
    A2SELC100_ROM = 1
} a2sel_c100;

typedef enum a2sel_d000 {
    A2SELD000_MAPPED = 0,
    A2SELD000_LC_B1 = 1,
    A2SELD000_LC_B2 = 2,
    A2SELD000_ROM = 3
} a2sel_d000;

typedef uint32_t view_flags_t;

static inline uint32_t vf_get_field(view_flags_t f, uint32_t mask, uint32_t shift)
{
    return (f & mask) >> shift;
}

static inline void vf_set_field(view_flags_t *f, uint32_t mask, uint32_t shift, uint32_t v)
{
    *f = (*f & ~mask) | ((v << shift) & mask);
}

static inline a2sel_48k vf_get_ram(view_flags_t f)
{
    return (a2sel_48k)vf_get_field(f, A2SEL_48K_MASK, A2SEL_48K_SHIFT);
}

static inline void vf_set_ram(view_flags_t *f, a2sel_48k v)
{
    vf_set_field(f, A2SEL_48K_MASK, A2SEL_48K_SHIFT, (uint32_t)v);
}

static inline a2sel_c100 vf_get_c100(view_flags_t f)
{
    return (a2sel_c100)vf_get_field(f, A2SEL_C100_MASK, A2SEL_C100_SHIFT);
}

static inline void vf_set_c100(view_flags_t *f, a2sel_c100 v)
{
    vf_set_field(f, A2SEL_C100_MASK, A2SEL_C100_SHIFT, (uint32_t)v);
}

static inline a2sel_d000 vf_get_d000(view_flags_t f)
{
    return (a2sel_d000)vf_get_field(f, A2SEL_D000_MASK, A2SEL_D000_SHIFT);
}

static inline void vf_set_d000(view_flags_t *f, a2sel_d000 v)
{
    vf_set_field(f, A2SEL_D000_MASK, A2SEL_D000_SHIFT, (uint32_t)v);
}

/* Named UI presets for Opt+M / Source menu (encode as VIEW_FLAGS). */
typedef enum runtime_view_area {
    RUNTIME_VIEW_AREA_MAP = 0, /* CPU map / soft-switch reality */
    RUNTIME_VIEW_AREA_MAIN,    /* force main 48K */
    RUNTIME_VIEW_AREA_AUX,     /* force aux 48K (//e) */
    RUNTIME_VIEW_AREA_LC1,     /* force LC bank 1 ($D000) + main LC plane */
    RUNTIME_VIEW_AREA_LC2,     /* force LC bank 2 */
    RUNTIME_VIEW_AREA_ROM,     /* force system ROM $D000-$FFFF (+ C100 ROM on //e) */
    RUNTIME_VIEW_AREA_COUNT
} runtime_view_area;

static inline view_flags_t view_flags_from_area(runtime_view_area area)
{
    view_flags_t f = 0;
    switch (area) {
    case RUNTIME_VIEW_AREA_MAP:
        break;
    case RUNTIME_VIEW_AREA_MAIN:
        vf_set_ram(&f, A2SEL48K_MAIN);
        break;
    case RUNTIME_VIEW_AREA_AUX:
        vf_set_ram(&f, A2SEL48K_AUX);
        break;
    case RUNTIME_VIEW_AREA_LC1:
        vf_set_d000(&f, A2SELD000_LC_B1);
        break;
    case RUNTIME_VIEW_AREA_LC2:
        vf_set_d000(&f, A2SELD000_LC_B2);
        break;
    case RUNTIME_VIEW_AREA_ROM:
        vf_set_d000(&f, A2SELD000_ROM);
        vf_set_c100(&f, A2SELC100_ROM);
        break;
    default:
        break;
    }
    return f;
}

/* Best-effort map VIEW_FLAGS back to a named area for UI chrome. */
static inline runtime_view_area view_area_from_flags(view_flags_t f)
{
    if (f == view_flags_from_area(RUNTIME_VIEW_AREA_MAP)) {
        return RUNTIME_VIEW_AREA_MAP;
    }
    if (f == view_flags_from_area(RUNTIME_VIEW_AREA_MAIN)) {
        return RUNTIME_VIEW_AREA_MAIN;
    }
    if (f == view_flags_from_area(RUNTIME_VIEW_AREA_AUX)) {
        return RUNTIME_VIEW_AREA_AUX;
    }
    if (f == view_flags_from_area(RUNTIME_VIEW_AREA_LC1)) {
        return RUNTIME_VIEW_AREA_LC1;
    }
    if (f == view_flags_from_area(RUNTIME_VIEW_AREA_LC2)) {
        return RUNTIME_VIEW_AREA_LC2;
    }
    if (f == view_flags_from_area(RUNTIME_VIEW_AREA_ROM)) {
        return RUNTIME_VIEW_AREA_ROM;
    }
    /* Custom multi-field: still show as Map chrome if everything mapped-ish. */
    return RUNTIME_VIEW_AREA_MAP;
}

static inline runtime_view_area view_area_cycle(runtime_view_area area, int model_iie)
{
    /* ][+: Map → Main → LC1 → LC2 → ROM → Map (no Aux). */
    static const runtime_view_area plus_seq[] = {
        RUNTIME_VIEW_AREA_MAP,
        RUNTIME_VIEW_AREA_MAIN,
        RUNTIME_VIEW_AREA_LC1,
        RUNTIME_VIEW_AREA_LC2,
        RUNTIME_VIEW_AREA_ROM
    };
    static const runtime_view_area iie_seq[] = {
        RUNTIME_VIEW_AREA_MAP,
        RUNTIME_VIEW_AREA_MAIN,
        RUNTIME_VIEW_AREA_AUX,
        RUNTIME_VIEW_AREA_LC1,
        RUNTIME_VIEW_AREA_LC2,
        RUNTIME_VIEW_AREA_ROM
    };
    const runtime_view_area *seq = model_iie ? iie_seq : plus_seq;
    size_t n = model_iie ? (sizeof(iie_seq) / sizeof(iie_seq[0]))
                         : (sizeof(plus_seq) / sizeof(plus_seq[0]));
    size_t i;
    for (i = 0; i < n; i++) {
        if (seq[i] == area) {
            return seq[(i + 1u) % n];
        }
    }
    return seq[0];
}

static inline const char *view_area_name(runtime_view_area area)
{
    switch (area) {
    case RUNTIME_VIEW_AREA_MAP:
        return "Map";
    case RUNTIME_VIEW_AREA_MAIN:
        return "Main";
    case RUNTIME_VIEW_AREA_AUX:
        return "Aux";
    case RUNTIME_VIEW_AREA_LC1:
        return "LC1";
    case RUNTIME_VIEW_AREA_LC2:
        return "LC2";
    case RUNTIME_VIEW_AREA_ROM:
        return "ROM";
    default:
        return "Map";
    }
}
