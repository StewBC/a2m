#pragma once

#include "runtime_event.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* cards[0] unused; cards[1..7] are RUNTIME_SLOT_CARD_*.
   Prefer classic home slot if that card type is present, else scan 7..1.
   Returns 1..7, or 0 if none. */

int runtime_resolve_diskii_slot(
    const runtime_slot_card_type cards[RUNTIME_APPLE_SLOT_COUNT]);

int runtime_resolve_smartport_slot(
    const runtime_slot_card_type cards[RUNTIME_APPLE_SLOT_COUNT]);

#ifdef __cplusplus
}
#endif
