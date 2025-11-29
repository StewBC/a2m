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

// The flags that tell memory functions where to read/write
// These are not used by hardware, but exist so external friends can ask hardware for
// values from banks other than RAM_MAIN
typedef enum RAMVIEW_FLAGS {
    MEM_MAPPED_6502     = (1 << 0),
    MEM_MAIN            = (1 << 1),
    MEM_AUX             = (1 << 2),
    MEM_LC_BANK2        = (1 << 3),
} RAMVIEW_FLAGS;

