#include "runtime_slot_resolve.h"

static int resolve_card_slot(
    const runtime_slot_card_type cards[RUNTIME_APPLE_SLOT_COUNT],
    runtime_slot_card_type want,
    int prefer)
{
    int slot;

    if (cards == NULL) {
        return 0;
    }
    if (prefer >= 1 && prefer <= 7 && cards[prefer] == want) {
        return prefer;
    }
    for (slot = 7; slot >= 1; --slot) {
        if (slot == prefer) {
            continue;
        }
        if (cards[slot] == want) {
            return slot;
        }
    }
    return 0;
}

int runtime_resolve_diskii_slot(
    const runtime_slot_card_type cards[RUNTIME_APPLE_SLOT_COUNT])
{
    return resolve_card_slot(cards, RUNTIME_SLOT_CARD_DISKII, 6);
}

int runtime_resolve_smartport_slot(
    const runtime_slot_card_type cards[RUNTIME_APPLE_SLOT_COUNT])
{
    return resolve_card_slot(cards, RUNTIME_SLOT_CARD_SMARTPORT, 7);
}
