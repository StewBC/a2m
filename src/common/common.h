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
