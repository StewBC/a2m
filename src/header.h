// Apple ][+ emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

// Standard Includes
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h> // For _getcwd and _chdir
#define stricmp _stricmp
#ifndef PATH_MAX
#define PATH_MAX MAX_PATH
#define PATH_SEPARATOR '\\'
#endif
#else
#include <unistd.h>  // For getcwd and chdir
#include <sys/stat.h>
#include <dirent.h>
#if defined(__linux__)  // for PATH_MAX
    #include <linux/limits.h>
#elif defined(__APPLE__)
    #include <sys/syslimits.h>
#else
    #include <limits.h>
#endif
#define stricmp strcasecmp
#define PATH_SEPARATOR '/'
#endif

// SDL related
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

// nuklear GUI includes
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
// #define NK_IMPLEMENTATION
#include "nuklear.h"

// Often used return codes
#define A2_OK   0
#define A2_ERR  1

// Forward declares
typedef struct APPLE2 APPLE2;
typedef struct UTIL_FILE UTIL_FILE;
typedef struct VIEWPORT VIEWPORT;
typedef struct DEBUGGER DEBUGGER;
typedef struct DYNARRAY DYNARRAY;

#include "util.h"
#include "dynarray.h"

// Machine
#include "6502.h"
#include "ramcard.h"
#include "roms.h"
#include "sftswtch.h"
#include "smrtprt.h"

// Debugger/Viewer
#include "breakpnt.h"
#include "viewapl2.h"
#include "viewcpu.h"
#include "viewdbg.h"
#include "viewdbg.h"
#include "viewdlg.h"
#include "viewmem.h"
#include "viewmem.h"
#include "viewmisc.h"
#include "viewport.h"

// It all comes together
#include "apple2.h"