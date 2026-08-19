#pragma once

#include <stdbool.h>
#include <stdint.h>

struct apple2;

/* Banking + display flags (names match a2m). */
typedef enum {
    A2S_80STORE             = (1u << 0),
    A2S_RAMRD               = (1u << 1),
    A2S_RAMWRT              = (1u << 2),
    A2S_CXSLOTROM_MB_ENABLE = (1u << 3),
    A2S_ALTZP               = (1u << 4),
    A2S_SLOT3ROM_MB_DISABLE = (1u << 5),
    A2S_PAGE2               = (1u << 6),
    A2S_HIRES               = (1u << 7),
    A2S_LC_READ             = (1u << 8),
    A2S_LC_WRITE            = (1u << 9),
    A2S_LC_BANK2            = (1u << 10),
    A2S_BANK_MASK           = (1u << 11) - 1,

    A2S_COL80               = (1u << 11),
    A2S_ALTCHARSET          = (1u << 12),
    A2S_TEXT                = (1u << 13),
    A2S_MIXED               = (1u << 14),
    A2S_DHIRES              = (1u << 15),

    A2S_LC_PRE_WRITE        = (1u << 16),

    A2S_RESET_MASK          = (1u << 18) - 1,
    A2S_OPEN_APPLE          = (1u << 18),
    A2S_CLOSED_APPLE        = (1u << 19),
    A2S_KEY_HELD            = (1u << 20)
} a2_state_flags;

enum {
    SS_KBD     = 0xC000,
    SS_KBDSTRB = 0xC010,
    SS_SPEAKER = 0xC030,
    SS_CLRROM  = 0xCFFF
};

void softswitch_init_tables(void);
void softswitch_setup_after_reset(struct apple2 *m);
void softswitch_apply_full_map(struct apple2 *m);

uint8_t softswitch_c0_read(struct apple2 *m, uint16_t address);
void softswitch_c0_write(struct apple2 *m, uint16_t address, uint8_t value);
void softswitch_language_card(struct apple2 *m, uint16_t address, int write_access);

/* First $Cnxx access may latch $C800 expansion ROM (//e C3 / card). */
void softswitch_slot_io_select(struct apple2 *m, uint16_t address);

void softswitch_bank_set(struct apple2 *m, uint32_t bits);
void softswitch_bank_clear(struct apple2 *m, uint32_t bits);

uint8_t softswitch_diskii(struct apple2 *m, uint16_t address, int write_access,
                          uint8_t write_value);
