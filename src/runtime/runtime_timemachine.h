#pragma once

/*
 * TimeMachine: runtime-owned forensic engine.
 *
 * TM0: master enable + recorder arming (HST1 + frame ring). Checkpoint ring,
 * queries, and materialize land in later phases.
 */

#include <stdbool.h>
#include <stdint.h>

typedef struct runtime runtime;

void runtime_tm_set_enabled(runtime *rt, bool enabled);
bool runtime_tm_enabled(const runtime *rt);
uint32_t runtime_tm_memory_mb(const runtime *rt);
