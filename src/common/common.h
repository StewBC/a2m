// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Often used return codes
#define A2_OK   0
#define A2_ERR  1

// useful macros
#ifndef max
#define max(a,b) (( (a) > (b) ) ? (a) : (b))
#endif

#ifndef min
#define min(a,b) (( (a) < (b) ) ? (a) : (b))
#endif

#define UNUSED(x)   (void)(x)

#define tst_flags(status, flag_mask)        ((status) & (flag_mask))
#define set_flags(status, flag_mask)        ((status) |= (flag_mask))
#define clr_flags(status, flag_mask)        ((status) &= ~(flag_mask))

// The UIs use this so a2m links in common.c, where it lives
extern int apl2_txt_row_start[];

// The view flags describe what memory is visible to the user, based on user selection
// in the views of the emulator
enum{
    A2SEL_48K_SHIFT  = 0,
    A2SEL_48K_BITS   = 2,
    A2SEL_C100_SHIFT = (A2SEL_48K_SHIFT + A2SEL_48K_BITS),
    A2SEL_C100_BITS  = 1,
    A2SEL_D000_SHIFT = (A2SEL_C100_SHIFT + A2SEL_C100_BITS),
    A2SEL_D000_BITS  = 2,

    A2SEL_48K_MASK   = ((1u << A2SEL_48K_BITS) - 1u) << A2SEL_48K_SHIFT,
    A2SEL_C100_MASK  = ((1u << A2SEL_C100_BITS) - 1u) << A2SEL_C100_SHIFT,
    A2SEL_D000_MASK  = ((1u << A2SEL_D000_BITS) - 1u) << A2SEL_D000_SHIFT,
};

typedef enum A2SEL_48K {
    A2SEL48K_MAPPED = 0,
    A2SEL48K_MAIN   = 1,
    A2SEL48K_AUX    = 2,
} A2SEL_48K;

typedef enum A2SEL_C100 {
    A2SELC100_MAPPED = 0,
    A2SELC100_ROM    = 1,
} A2SEL_C100;

typedef enum A2SEL_D000 {
    A2SELD000_MAPPED = 0,
    A2SELD000_LC_B1  = 1,
    A2SELD000_LC_B2  = 2,
    A2SELD000_ROM    = 3,
} A2SEL_D000;

typedef uint32_t VIEW_FLAGS;

static inline uint32_t vf_get_field(VIEW_FLAGS f, uint32_t mask, uint32_t shift) {
    return (f & mask) >> shift;
}

static inline void vf_set_field(VIEW_FLAGS *f, uint32_t mask, uint32_t shift, uint32_t v) {
    *f = (*f & ~mask) | ((v << shift) & mask);
}

static inline A2SEL_48K vf_get_ram(VIEW_FLAGS f) {
    return (A2SEL_48K)vf_get_field(f, A2SEL_48K_MASK, A2SEL_48K_SHIFT);
}

static inline void vf_set_ram(VIEW_FLAGS *f, A2SEL_48K v) {
    vf_set_field(f, A2SEL_48K_MASK, A2SEL_48K_SHIFT, (uint32_t)v);
}

static inline A2SEL_C100 vf_get_c100(VIEW_FLAGS f) {
    return (A2SEL_C100)vf_get_field(f, A2SEL_C100_MASK, A2SEL_C100_SHIFT);
}

static inline void vf_set_c100(VIEW_FLAGS *f, A2SEL_C100 v) {
    vf_set_field(f, A2SEL_C100_MASK, A2SEL_C100_SHIFT, (uint32_t)v);
}

static inline A2SEL_D000 vf_get_d000(VIEW_FLAGS f) {
    return (A2SEL_D000)vf_get_field(f, A2SEL_D000_MASK, A2SEL_D000_SHIFT);
}

static inline void vf_set_d000(VIEW_FLAGS *f, A2SEL_D000 v) {
    vf_set_field(f, A2SEL_D000_MASK, A2SEL_D000_SHIFT, (uint32_t)v);
}
