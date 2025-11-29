// Apple ][+ and //e Emhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

#include "common.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>                                         // For _getcwd and _chdir
#define stricmp _stricmp
#define strnicmp _strnicmp
#ifndef PATH_MAX
#define PATH_MAX 4096                                       // Match Linux defenition
#define PATH_SEPARATOR '\\'
#endif
#else
#include <unistd.h>                                         // For getcwd and chdir
#include <sys/stat.h>
#include <dirent.h>
#if defined(__linux__)                                      // for PATH_MAX
#include <linux/limits.h>
#elif defined(__APPLE__)
#include <sys/syslimits.h>
#else
#include <limits.h>
#endif
#define stricmp strcasecmp
#define strnicmp strncasecmp
#define PATH_SEPARATOR '/'
#endif

// Often used return codes
#define A2_OK   0
#define A2_ERR  1

// Backbone
#include "dynarray.h"

// Support files
#include "breakprs.h"
#include "dbgopcds.h"
#include "errorlog.h"
#include "inifiles.h"
#include "util.h"
#include "trace.h"
