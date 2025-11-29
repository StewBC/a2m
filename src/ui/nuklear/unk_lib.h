// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

#include "common.h"
#include "utils_lib.h"
#include "asm_lib.h"
#include "hardware_lib.h"
#include "runtime_lib.h"

// SDL related
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

// nuklear GUI includes
#define NK_UINT_DRAW_INDEX
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:5287)  // different enum types
#endif

// #define NK_IMPLEMENTATION
#include "nuklear.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

typedef struct UNK UNK;

typedef enum CURSOR_FIELD {
    CURSOR_HEX,
    CURSOR_ASCII,
    CURSOR_ADDRESS,
} CURSOR_FIELD;

typedef enum CURSOR_DIGIT {
    CURSOR_DIGIT0,
    CURSOR_DIGIT1,
    CURSOR_DIGIT2,
    CURSOR_DIGIT3,
} CURSOR_DIGIT;

#define set_mem_flag(status, flag_mask)       ((status) |= (flag_mask))
#define tst_mem_flag(status, flag_mask)       ((status) & (flag_mask))
#define clr_mem_flag(status, flag_mask)       ((status) &= ~(flag_mask))

#define VIEWCPU_NAME_HASH   0XE5EA0BC3
#define VIEWDASM_NAME_HASH  0X81101438
#define VIEWMEM_NAME_HASH   0XB9A77E29
#define VIEWMISC_NAME_HASH  0X97446A22

#include "breakpoint.h"
#include "unk_layout.h"
#include "unk_leds.h"
#include "nuklrsdl.h"
#include "unk_apl2.h"
#include "unk_audio.h"
#include "unk_cpu.h"
#include "unk_dlg.h"
#include "unk_dasm.h"
#include "unk_help.h"
#include "unk_mem.h"
#include "unk_misc.h"
#include "unk_view.h"