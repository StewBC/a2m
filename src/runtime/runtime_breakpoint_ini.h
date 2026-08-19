#pragma once

#include "runtime_internal.h"

#include <stdbool.h>

bool runtime_load_breakpoints_from_ini(runtime *rt);
bool runtime_save_breakpoints_to_ini(runtime *rt);
