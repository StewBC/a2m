// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

#include <stdarg.h>
#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asm_common.h"
#include "dynarray.h"
#include "errorlog.h"

#include "define.h"
#include "file.h"
#include "gperf.h"
#include "opcode.h"
#include "token.h"
#include "scope.h"
#include "segment.h"
#include "symbol.h"
#include "expr.h"
#include "emit.h"
#include "parse.h"
#include "err.h"
#include "asm.h"
