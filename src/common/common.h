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

// The flags that tell memory functions where to read/write
// These are not used by hardware, but exist so external friends can ask hardware for
// values from banks other than whatever is mapped into the 6502 space (MEM_MAPPED_6502)
typedef enum {
    MEM_MAPPED_6502     = 0,
    MEM_MAIN            = (1u << 0),
    MEM_AUX             = (1u << 1),
    MEM_LC_BANK1        = (1u << 2),
    MEM_LC_BANK2        = (1u << 3),
    MEM_LC_E000_8K      = (1u << 4),  // LC fixed 8KB at $E000-$FFFF when LC RAM enabled
    MEM_ROM             = (1u << 5),
    MEM_IO              = (1u << 6),
    MEM_MASK_ANY        = MEM_MAIN | MEM_AUX |
                          MEM_LC_BANK1 | MEM_LC_BANK2 | MEM_LC_E000_8K |
                          MEM_ROM | MEM_IO
} RAMVIEW_FLAGS;

// The UIs use this so a2m links in common.c, where it lives
extern int apl2_txt_row_start[];
