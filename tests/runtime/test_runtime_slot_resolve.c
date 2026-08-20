#include "runtime_slot_resolve.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    exit(1);
}

static void expect_int(const char *name, int got, int want)
{
    if (got != want) {
        fprintf(stderr, "FAIL: %s: got %d want %d\n", name, got, want);
        exit(1);
    }
}

static void clear_cards(runtime_slot_card_type cards[RUNTIME_APPLE_SLOT_COUNT])
{
    memset(cards, 0, sizeof(runtime_slot_card_type) * (size_t)RUNTIME_APPLE_SLOT_COUNT);
}

int main(void)
{
    runtime_slot_card_type cards[RUNTIME_APPLE_SLOT_COUNT];

    clear_cards(cards);
    expect_int("diskii empty", runtime_resolve_diskii_slot(cards), 0);
    expect_int("smartport empty", runtime_resolve_smartport_slot(cards), 0);

    clear_cards(cards);
    cards[6] = RUNTIME_SLOT_CARD_DISKII;
    expect_int("diskii only 6", runtime_resolve_diskii_slot(cards), 6);

    clear_cards(cards);
    cards[5] = RUNTIME_SLOT_CARD_DISKII;
    expect_int("diskii only 5", runtime_resolve_diskii_slot(cards), 5);

    clear_cards(cards);
    cards[5] = RUNTIME_SLOT_CARD_DISKII;
    cards[6] = RUNTIME_SLOT_CARD_DISKII;
    expect_int("diskii prefer 6", runtime_resolve_diskii_slot(cards), 6);

    clear_cards(cards);
    cards[7] = RUNTIME_SLOT_CARD_DISKII;
    expect_int("diskii only 7", runtime_resolve_diskii_slot(cards), 7);

    clear_cards(cards);
    cards[7] = RUNTIME_SLOT_CARD_SMARTPORT;
    expect_int("smartport only 7", runtime_resolve_smartport_slot(cards), 7);

    clear_cards(cards);
    cards[4] = RUNTIME_SLOT_CARD_SMARTPORT;
    expect_int("smartport only 4", runtime_resolve_smartport_slot(cards), 4);

    clear_cards(cards);
    cards[4] = RUNTIME_SLOT_CARD_SMARTPORT;
    cards[7] = RUNTIME_SLOT_CARD_SMARTPORT;
    expect_int("smartport prefer 7", runtime_resolve_smartport_slot(cards), 7);

    clear_cards(cards);
    cards[6] = RUNTIME_SLOT_CARD_SMARTPORT;
    cards[7] = RUNTIME_SLOT_CARD_DISKII;
    expect_int("diskii ignores smartport 6", runtime_resolve_diskii_slot(cards), 7);
    expect_int("smartport ignores diskii 7", runtime_resolve_smartport_slot(cards), 6);

    expect_int("null diskii", runtime_resolve_diskii_slot(NULL), 0);
    expect_int("null smartport", runtime_resolve_smartport_slot(NULL), 0);

    printf("ok\n");
    return 0;
}
