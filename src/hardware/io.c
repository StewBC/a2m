// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#define C800_NONE     (-1)
#define C800_INTERNAL (8)   // 0..7 indicates a slots card has strobed

// These tables hold the handlers for c000-c0ff for each model
static A2_C0_TABLE io_c0_iiplus;
static A2_C0_TABLE io_c0_iie;

// This is an accessor that points at the table for whatever model is active
A2_C0_TABLE *io_c0_machine_table;

void io_language_card_map_memory_read(APPLE2 *m) {
    uint16_t bank_addr = tst_flags(m->state_flags, A2S_LC_BANK2) ? 0x1000 : 0x0000;
    uint16_t aux_addr  = tst_flags(m->state_flags, A2S_ALTZP) ? 0x4000 : 0x0000; // Note - offset in LC RAM_LC, not RAM_MAIN
    if(tst_flags(m->state_flags, A2S_LC_READ)) {
        pages_map_lc(&m->pages, PAGE_MAP_READ, 0xD000, 0x1000, bank_addr + aux_addr, &m->ram);
        pages_map_lc(&m->pages, PAGE_MAP_READ, 0xE000, 0x2000, 0x2000 + aux_addr, &m->ram);
    } else {
        pages_map_rom_block(&m->pages, &m->roms.blocks[ROM_APPLE2], &m->ram);
    }
}

void io_language_card_map_memory_write(APPLE2 *m) {
    uint16_t bank_addr = tst_flags(m->state_flags, A2S_LC_BANK2) ? 0x1000 : 0x0000;
    uint16_t aux_addr  = tst_flags(m->state_flags, A2S_ALTZP) ? 0x4000 : 0x0000; // Note - offset in LC RAM_LC, not RAM_MAIN
    if(tst_flags(m->state_flags, A2S_LC_WRITE)) {
        pages_map_lc(&m->pages, PAGE_MAP_WRITE, 0xD000, 0x1000, bank_addr + aux_addr, &m->ram);
        pages_map_lc(&m->pages, PAGE_MAP_WRITE, 0xE000, 0x2000, 0x2000 + aux_addr, &m->ram);
    } else {
        // Writes to ROM go here but its a bit-bucket - aux doesn't matter
        pages_map(&m->pages, PAGE_MAP_WRITE, 0xD000, 0x3000, &m->ram);
    }
}

static void io_apply_c800_latch(APPLE2 *m, A2_STATE s) {
    // - If CXROM is active, C800 is not strobed (cards/internal firmware not visible).
    // - If CXROM is inactive:
    //   - strobed_slot == C800_INTERNAL maps internal 80-col firmware ONLY when C3ROM is clear.
    //   - strobed_slot 1..7 maps that slot's C800 ROM if present.
    //   - otherwise C800 reads fall through to RAM.
    if(s & A2S_CXSLOTROM_MB_ENABLE) {
        pages_map_rom(&m->pages, 0xC800, 0x800, &m->roms.blocks[ROM_APPLE2_SLOTS].bytes[0x800], &m->ram);
        return;
    }

    if(m->strobed_slot == C800_INTERNAL) {
        pages_map_rom(&m->pages, 0xC800, 0x800, &m->roms.blocks[ROM_APPLE2_SLOTS].bytes[0x800], &m->ram);
        return;
    }

    if((m->strobed_slot >= 1 && m->strobed_slot <= 7) &&
            m->slot_cards[m->strobed_slot].slot_map_cx_rom) {
        m->slot_cards[m->strobed_slot].slot_map_cx_rom(m);
        return;
    }

    pages_map(&m->pages, PAGE_MAP_READ, 0xC800, 0x800, &m->ram);
}

static void io_map_cxrom(APPLE2 *m, A2_STATE new) {
    if(new & A2S_CXSLOTROM_MB_ENABLE) {
        pages_map_rom(&m->pages, 0xC100, 0xF00, &m->roms.blocks[ROM_APPLE2_SLOTS].bytes[0x100], &m->ram);
    } else {
        // Restore the slot mappings
        m->pages.read_pages[0xC100 / PAGE_SIZE] = m->rom_shadow_pages[1];
        m->pages.read_pages[0xC200 / PAGE_SIZE] = m->rom_shadow_pages[2];
        m->pages.read_pages[0xC400 / PAGE_SIZE] = m->rom_shadow_pages[4];
        m->pages.read_pages[0xC500 / PAGE_SIZE] = m->rom_shadow_pages[5];
        m->pages.read_pages[0xC600 / PAGE_SIZE] = m->rom_shadow_pages[6];
        m->pages.read_pages[0xC700 / PAGE_SIZE] = m->rom_shadow_pages[7];
        // Only if internal slot3 is not overriding the card rom
        if(tst_flags(m->state_flags, A2S_SLOT3ROM_MB_DISABLE)) {
            m->pages.read_pages[0xC300 / PAGE_SIZE] = m->rom_shadow_pages[3];
        }
        // If a card was mapped to c800, restore that mapping
        io_apply_c800_latch(m, new);
    }
}

static void io_map_c3rom(APPLE2 *m, A2_STATE new) {
    if(new & A2S_SLOT3ROM_MB_DISABLE) {
        m->pages.read_pages[0xC300 / PAGE_SIZE] = m->rom_shadow_pages[3];
    } else {
        pages_map_rom(&m->pages, 0xC300, 0x100, &m->roms.blocks[ROM_APPLE2_SLOTS].bytes[0x300], &m->ram);
    }
    io_apply_c800_latch(m, new);
}

static inline uint32_t offset_main(uint16_t a) {
    return a;
}

static inline uint32_t offset_aux(uint16_t a) {
    return 0x10000 + a;
}

static inline uint32_t off_zp(const APPLE2 *m, A2_STATE s, uint16_t a) {
    return (s & A2S_ALTZP) ? offset_aux(a) : offset_main(a);
}

static inline uint32_t off_rd(const APPLE2 *m, A2_STATE s, uint16_t a) {
    return (s & A2S_RAMRD) ? offset_aux(a) : offset_main(a);
}

static inline uint32_t off_wr(const APPLE2 *m, A2_STATE s, uint16_t a) {
    return (s & A2S_RAMWRT) ? offset_aux(a) : offset_main(a);
}

static inline uint32_t off_vid(const APPLE2 *m, A2_STATE s, uint16_t a) {
    // 80STORE video bank is selected by PAGE2
    return (s & A2S_PAGE2) ? offset_aux(a) : offset_main(a);
}

// Keep the memory banking state up to date
void io_apply_mem_state(APPLE2 *m, A2_STATE old, A2_STATE new) {
    A2_STATE chg = old ^ new;

    if(chg & A2S_ALTZP) {
        uint32_t base = off_zp(m, new, 0x0000);
        pages_map(&m->pages, PAGE_MAP_READ,  base, 0x0200, &m->ram);
        pages_map(&m->pages, PAGE_MAP_WRITE, base, 0x0200, &m->ram);
    }

    if(chg & (A2S_RAMRD | A2S_RAMWRT | A2S_80STORE | A2S_PAGE2 | A2S_HIRES)) {
        // Base map $0200-$BFFF
        pages_map(&m->pages, PAGE_MAP_READ,  off_rd(m, new, 0x0200), 0xBE00, &m->ram);
        pages_map(&m->pages, PAGE_MAP_WRITE, off_wr(m, new, 0x0200), 0xBE00, &m->ram);

        // If 80STORE, override the video-selected regions
        if(new & A2S_80STORE) {
            // Text page 1/2: $0400-$07FF
            pages_map(&m->pages, PAGE_MAP_READ,  off_vid(m, new, 0x0400), 0x0400, &m->ram);
            pages_map(&m->pages, PAGE_MAP_WRITE, off_vid(m, new, 0x0400), 0x0400, &m->ram);

            // Hires page 1/2: $2000-$3FFF (only if HIRES mode is active)
            if(new & A2S_HIRES) {
                pages_map(&m->pages, PAGE_MAP_READ,  off_vid(m, new, 0x2000), 0x2000, &m->ram);
                pages_map(&m->pages, PAGE_MAP_WRITE, off_vid(m, new, 0x2000), 0x2000, &m->ram);
            }
        }
    }

    if(chg & (A2S_LC_READ | A2S_LC_BANK2 | A2S_ALTZP)) {
        io_language_card_map_memory_read(m);
    }
    if(chg & (A2S_LC_WRITE | A2S_LC_BANK2 | A2S_ALTZP)) {
        io_language_card_map_memory_write(m);
    }

    if(chg & A2S_CXSLOTROM_MB_ENABLE) {
        io_map_cxrom(m, new);
    }
    if(chg & A2S_SLOT3ROM_MB_DISABLE) {
        io_map_c3rom(m, new);
    }
}

// Use a2_bank_state_* to set the bits that affect the state of BANKING, so that means:
// A2S_80STORE, A2S_RAMRD               , A2S_RAMWRT  , A2S_CXSLOTROM_MB_ENABLE
// A2S_ALTZP  , A2S_SLOT3ROM_MB_DISABLE , A2S_PAGE2   , A2S_HIRES
// A2S_LC_READ, A2S_LC_WRITE            , A2S_LC_BANK2
static inline void io_mem_bank_state_set(APPLE2 *m, A2_STATE bank_state_bits) {
    A2_STATE old = m->state_flags & A2S_BANK_MASK;
    A2_STATE new = old | bank_state_bits;

    if(new != old) {
        m->state_flags = (m->state_flags & ~A2S_BANK_MASK) | new;
        io_apply_mem_state(m, old, new);
    }
}

static inline void io_mem_bank_state_clear(APPLE2 *m, A2_STATE bank_state_bits) {
    A2_STATE old = m->state_flags & A2S_BANK_MASK;
    A2_STATE new = old & ~bank_state_bits;

    if(new != old) {
        m->state_flags = (m->state_flags & ~A2S_BANK_MASK) | new;
        io_apply_mem_state(m, old, new);
    }
}

void io_language_card(APPLE2 *m, uint16_t address, int write_access) {
    int odd_access = address & 1;

    // BANK2 (A3)
    if((address & 0b1000) == 0) {
        io_mem_bank_state_set(m, A2S_LC_BANK2);
    } else {
        io_mem_bank_state_clear(m, A2S_LC_BANK2);
    }

    // LC_READ (A1/A0)
    int bits2 = address & 0b11;
    if(bits2 == 0 || bits2 == 3) {
        io_mem_bank_state_set(m, A2S_LC_READ);
    } else {
        io_mem_bank_state_clear(m, A2S_LC_READ);
    }

    if(!odd_access) {
        clr_flags(m->state_flags, A2S_LC_PRE_WRITE);
        io_mem_bank_state_clear(m, A2S_LC_WRITE);
    } else if(write_access) {
        // Odd write
        clr_flags(m->state_flags, A2S_LC_PRE_WRITE);
    } else {
        // Odd Read
        if(tst_flags(m->state_flags, A2S_LC_PRE_WRITE)) {
            io_mem_bank_state_set(m, A2S_LC_WRITE);
        }
        set_flags(m->state_flags, A2S_LC_PRE_WRITE);
    }
}

static void io_slot_iiplus(APPLE2 *m, uint16_t address) {
    int slot = (address >> 8) & 0x7;
    // Does the Slot have ROM
    if(m->slot_cards[slot].slot_map_cx_rom) {
        // Install the rom and latch it
        m->strobed_slot = slot;
        io_apply_c800_latch(m, m->state_flags);
    }
}

static void io_slot_handler(APPLE2 *m, uint16_t address) {
    // IO Select, which can "strobe" c800-cfff
    int slot = (address >> 8) & 0x7;
    if(slot == 3 && !tst_flags(m->state_flags, A2S_SLOT3ROM_MB_DISABLE)) {
        // Map 80 col firmware
        m->strobed_slot = C800_INTERNAL;
        io_apply_c800_latch(m, m->state_flags);
    } else if(!tst_flags(m->state_flags, A2S_CXSLOTROM_MB_ENABLE) && m->slot_cards[slot].slot_map_cx_rom) {
        // If nothing is strobed, and the rom is not active and the card has rom, map the rom
        m->strobed_slot = slot;
        io_apply_c800_latch(m, m->state_flags);
    }
}

static inline void slot_clrrom(APPLE2 *m, uint16_t address) {
    UNUSED(address);
    m->strobed_slot = C800_NONE;
    io_apply_c800_latch(m, m->state_flags);
}

/*--------------------------------------------------------------------------*/
static void io_c0_set_read_range(A2_C0_TABLE *t, int start, int length, ss_read read) {
    for(int i = start; i < start + length; i++) {
        t->r[i] = read;
    }
}

static void io_c0_set_range(A2_C0_TABLE *t, int start, int length, ss_read read_fn, ss_write write_fn) {
    for(int i = start; i < start + length; i++) {
        t->r[i] = read_fn;
        t->w[i] = write_fn;
    }
}

static inline uint8_t floating_bus(void) {
    return 0xA0;
}

static uint8_t io_c0_floating_r(APPLE2 *m, uint16_t a) {
    UNUSED(m);
    UNUSED(a);
    return floating_bus();
}

static uint8_t io_c0_through_r(APPLE2 *m, uint16_t a) {
    return m->pages.read_pages[a / PAGE_SIZE][a % PAGE_SIZE];
}

static void io_c0_noop_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(m);
    UNUSED(a);
    UNUSED(v);
}

//--------------------------------------------------------------------------
// 0xC000
uint8_t io_c0_kbd_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    return m->ram.RAM_MAIN[KBD];
}

// 0xC000
void io_c0_clr80store_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
    // This is just keeping the old system till I have table-converted the code
    io_mem_bank_state_clear(m, A2S_80STORE);
}

// 0xC001
void io_c0_set80store_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
    io_mem_bank_state_set(m, A2S_80STORE);
}

// 0xC002
void io_c0_clrramrd_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
    io_mem_bank_state_clear(m, A2S_RAMRD);
}

// 0xC0003
void io_c0_setramrd_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
    io_mem_bank_state_set(m, A2S_RAMRD);
}

// 0xC004
void io_c0_clrramwrt_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
    io_mem_bank_state_clear(m, A2S_RAMWRT);
}

// 0xC005
void io_c0_setramwrt_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
    io_mem_bank_state_set(m, A2S_RAMWRT);
}

// 0xC006
void io_c0_clrcxrom_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(v);
    io_mem_bank_state_clear(m, A2S_CXSLOTROM_MB_ENABLE);
}

// 0xC007
void io_c0_setcxrom_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
    io_mem_bank_state_set(m, A2S_CXSLOTROM_MB_ENABLE);
}

// 0xC008
void io_c0_clraltzp_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
    io_mem_bank_state_clear(m, A2S_ALTZP);
}

// 0xC009
void io_c0_setaltzp_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
    io_mem_bank_state_set(m, A2S_ALTZP);
}

// 0xC00A
void io_c0_clrc3rom_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
    // C300-C3FF
    // Turn internal rom on, slot rom off
    io_mem_bank_state_clear(m, A2S_SLOT3ROM_MB_DISABLE);
}

// 0xC00B
void io_c0_setc3rom_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
    // Turn internal rom off, slot rom on
    io_mem_bank_state_set(m, A2S_SLOT3ROM_MB_DISABLE);
}

// 0xC00C
void io_c0_clr80col_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
    clr_flags(m->state_flags, A2S_COL80);
}

// 0xC00D
void io_c0_set80col_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
    set_flags(m->state_flags, A2S_COL80);
}

// 0xC00E
void io_c0_clraltchar_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
    clr_flags(m->state_flags, A2S_ALTCHARSET);
}

// 0xC00F
void io_c0_setaltchar_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
    set_flags(m->state_flags, A2S_ALTCHARSET);
}

static inline void kbdstrb_helper(APPLE2 *m) {
    if(m->a2out_cb.cb_clipboard_ctx.cb_clipboard &&
            !m->a2out_cb.cb_clipboard_ctx.cb_clipboard(m->a2out_cb.cb_clipboard_ctx.user)) {
        m->ram.RAM_MAIN[KBD] &= 0x7F;
    }
}

// 0xC010
uint8_t io_c0_kbdstrb_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    kbdstrb_helper(m);
    return floating_bus();
}

void io_c0_kbdstrb_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
    kbdstrb_helper(m);
}

// 0xC011
uint8_t io_c0_hramrd_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    kbdstrb_helper(m);
    return tst_flags(m->state_flags, A2S_LC_BANK2) ? 0x80 : 0x00;;
}

// 0xC012
uint8_t io_c0_hramwrt_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    kbdstrb_helper(m);
    return tst_flags(m->state_flags, A2S_LC_READ) ? 0x80 : 0x00;;
}

// 0xC013
uint8_t io_c0_rdramrd_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    kbdstrb_helper(m);
    return tst_flags(m->state_flags, A2S_RAMRD) ? 0x80 : 0x00;;
}

// 0xC014
uint8_t io_c0_rdramwrt_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    kbdstrb_helper(m);
    return tst_flags(m->state_flags, A2S_RAMWRT) ? 0x80 : 0x00;;
}

// 0xC015
uint8_t io_c0_rdcxrom_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    kbdstrb_helper(m);
    return tst_flags(m->state_flags, A2S_CXSLOTROM_MB_ENABLE) ? 0x80 : 0x00;;
}

// 0xC016
uint8_t io_c0_rdaltzp_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    kbdstrb_helper(m);
    return tst_flags(m->state_flags, A2S_ALTZP) ? 0x80 : 0x00;;
}

// 0xc017
uint8_t io_c0_rdc3rom_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    kbdstrb_helper(m);
    return tst_flags(m->state_flags, A2S_SLOT3ROM_MB_DISABLE) ? 0x80 : 0x00;;
}

// 0xC018
uint8_t io_c0_rd80store_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    kbdstrb_helper(m);
    return tst_flags(m->state_flags, A2S_80STORE) ? 0x80 : 0x00;;
}

// 0xC019
uint8_t io_c0_rdvbl_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    kbdstrb_helper(m);
    return (m->cpu.cycles % 17030) >= 15665 ? 0x80 : 0;
}

// 0xC01A
uint8_t io_c0_rdtext_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    kbdstrb_helper(m);
    return tst_flags(m->state_flags, A2S_TEXT) ? 0x80 : 0x00;;
}

// 0xC01B
uint8_t io_c0_rdmixed_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    kbdstrb_helper(m);
    return tst_flags(m->state_flags, A2S_MIXED) ? 0x80 : 0x00;;
}

// 0xC01C
uint8_t io_c0_rdpage2_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    kbdstrb_helper(m);
    return tst_flags(m->state_flags, A2S_PAGE2) ? 0x80 : 0x00;;
}

// 0xC01D
uint8_t io_c0_rdhires_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    kbdstrb_helper(m);
    return tst_flags(m->state_flags, A2S_HIRES) ? 0x80 : 0x00;;
}

// 0xC01E
uint8_t io_c0_rdaltchar_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    kbdstrb_helper(m);
    return tst_flags(m->state_flags, A2S_ALTCHARSET) ? 0x80 : 0x00;;
}

// 0xC01F
uint8_t io_c0_rd80col_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    kbdstrb_helper(m);
    return tst_flags(m->state_flags, A2S_COL80) ? 0x80 : 0x00;;
}

// 0xC030
uint8_t io_c0_a2speaker_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    if(m->a2out_cb.cb_speaker_ctx.cb_speaker) {
        m->a2out_cb.cb_speaker_ctx.cb_speaker(m->a2out_cb.cb_speaker_ctx.user);
    }
    return floating_bus();
}

void io_c0_a2speaker_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
    if(m->a2out_cb.cb_speaker_ctx.cb_speaker) {
        m->a2out_cb.cb_speaker_ctx.cb_speaker(m->a2out_cb.cb_speaker_ctx.user);
    }
}

// 0xC036
uint8_t io_c0_cyareg_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    return floating_bus();
}

void io_c0_cyareg_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
}

// 0xC041
uint8_t io_c0_rdvblmsk_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    return floating_bus();
}

void io_c0_rdvblmsk_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
}

// 0xC050
uint8_t io_c0_txtclr_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    clr_flags(m->state_flags, A2S_TEXT);
    return floating_bus();
}

void io_c0_txtclr_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
    clr_flags(m->state_flags, A2S_TEXT);
}

// 0xC051
uint8_t io_c0_txtset_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    set_flags(m->state_flags, A2S_TEXT);
    return floating_bus();
}

void io_c0_txtset_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
    set_flags(m->state_flags, A2S_TEXT);
}

// 0xC052
uint8_t io_c0_mixclr_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    clr_flags(m->state_flags, A2S_MIXED);
    return floating_bus();
}

void io_c0_mixclr_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
    clr_flags(m->state_flags, A2S_MIXED);
}

// 0xC053
uint8_t io_c0_mixset_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    set_flags(m->state_flags, A2S_MIXED);
    return floating_bus();
}

void io_c0_mixset_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
    set_flags(m->state_flags, A2S_MIXED);
}

// 0xC054
uint8_t io_c0_clrpage2_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    io_mem_bank_state_clear(m, A2S_PAGE2);
    return floating_bus();
}

void io_c0_clrpage2_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
    io_mem_bank_state_clear(m, A2S_PAGE2);
}

// 0xC055
uint8_t io_c0_setpage2_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    io_mem_bank_state_set(m, A2S_PAGE2);
    return floating_bus();
}

void io_c0_setpage2_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
    io_mem_bank_state_set(m, A2S_PAGE2);
}

// 0xC056
uint8_t io_c0_clrhires_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    io_mem_bank_state_clear(m, A2S_HIRES);
    return floating_bus();
}

void io_c0_clrhires_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
    io_mem_bank_state_clear(m, A2S_HIRES);
}

// 0xC057
uint8_t io_c0_sethires_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    io_mem_bank_state_set(m, A2S_HIRES);
    return floating_bus();
}

void io_c0_sethires_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
    io_mem_bank_state_set(m, A2S_HIRES);
}

// 0xC05E
uint8_t io_c0_clran3_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    set_flags(m->state_flags, A2S_DHIRES);
    return floating_bus();
}

void io_c0_clran3_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
    set_flags(m->state_flags, A2S_DHIRES);
}

// 0xC05F
uint8_t io_c0_setan3_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    clr_flags(m->state_flags, A2S_DHIRES);
    return floating_bus();
}

void io_c0_setan3_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
    clr_flags(m->state_flags, A2S_DHIRES);
}

// 0xC061
uint8_t io_c0_butn0_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    uint8_t button = tst_flags(m->state_flags, A2S_OPEN_APPLE) ? 0x80 : 0x00;
    cb_read_button rb = m->a2out_cb.cb_inputdevice_ctx.cb_read_button;
    if(rb) {
        for(int i = 0; i < 2; i++) {
            button |= rb(m->a2out_cb.cb_inputdevice_ctx.user, i, APPLE2_BUTTON_0);
        }
    }
    return button ? 0x80 : 0;
}

// 0xC062
uint8_t io_c0_butn1_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    uint8_t button = tst_flags(m->state_flags, A2S_CLOSED_APPLE) ? 0x80 : 0x00;
    cb_read_button rb = m->a2out_cb.cb_inputdevice_ctx.cb_read_button;
    if(rb) {
        for(int i = 0; i < 2; i++) {
            button |= rb(m->a2out_cb.cb_inputdevice_ctx.user, i, APPLE2_BUTTON_1);
        }
    }
    return button ? 0x80 : 0;
}

// 0xC063
uint8_t io_c0_butn2_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    uint8_t button = 0;
    cb_read_button rb = m->a2out_cb.cb_inputdevice_ctx.cb_read_button;
    if(rb) {
        for(int i = 0; i < 2; i++) {
            button = rb(m->a2out_cb.cb_inputdevice_ctx.user, i, APPLE2_BUTTON_2);
        }
    }
    return button ? 0x80 : 0;
}

// 0xC064
uint8_t io_c0_paddl0_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    cb_read_axis ra = m->a2out_cb.cb_inputdevice_ctx.cb_read_axis;
    if(ra) {
        return ra(m->a2out_cb.cb_inputdevice_ctx.user, 0, APPLE2_AXIS_X, m->cpu.cycles);
    }
    return 0x80;
}

// 0xC065
uint8_t io_c0_paddl1_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    cb_read_axis ra = m->a2out_cb.cb_inputdevice_ctx.cb_read_axis;
    if(ra) {
        return ra(m->a2out_cb.cb_inputdevice_ctx.user, 0, APPLE2_AXIS_Y, m->cpu.cycles);
    }
    return 0x80;
}

// 0xC066
uint8_t io_c0_paddl2_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    cb_read_axis ra = m->a2out_cb.cb_inputdevice_ctx.cb_read_axis;
    if(ra) {
        return ra(m->a2out_cb.cb_inputdevice_ctx.user, 1, APPLE2_AXIS_X, m->cpu.cycles);
    }
    return 0x80;
}

// 0xC067
uint8_t io_c0_paddl3_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    cb_read_axis ra = m->a2out_cb.cb_inputdevice_ctx.cb_read_axis;
    if(ra) {
        return ra(m->a2out_cb.cb_inputdevice_ctx.user, 1, APPLE2_AXIS_Y, m->cpu.cycles);
    }
    return 0x80;
}

// 0xC070
static inline void ptrig_helper(APPLE2 *m) {
    cb_ptrig cb_ptrig = m->a2out_cb.cb_inputdevice_ctx.cb_ptrig;
    if(cb_ptrig) {
        cb_ptrig(m->a2out_cb.cb_inputdevice_ctx.user, m->cpu.cycles);
    }
}

uint8_t io_c0_ptrig_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    ptrig_helper(m);
    return floating_bus();
}

void io_c0_ptrig_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    ptrig_helper(m);
}


// 0xC07F
uint8_t io_c0_rdhgr_wsetioud_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    return floating_bus();
}

// 0xC080-0xC08F
uint8_t io_c0_read_lc(APPLE2 *m, uint16_t a) {
    io_language_card(m, a, 0);
    return floating_bus();
}

void io_c0_write_lc(APPLE2 *m, uint16_t a, uint8_t v) {
    io_language_card(m, a, 1);
}

// 0xC90-0xCFF
static inline uint8_t io_c0_diskii_helper(APPLE2 *m, uint16_t a, uint8_t slot) {
    uint8_t soft_switch = a & 0x0f;
    if(soft_switch <= IWM_PH3_ON) {
        diskii_step_head(m, slot, soft_switch);
    } else {
        switch(soft_switch) {
            case IWM_MOTOR_ON:
            case IWM_MOTOR_OFF:
                diskii_motor(m, slot, soft_switch);
                break;

            case IWM_SEL_DRIVE_1:
            case IWM_SEL_DRIVE_2:
                diskii_drive_select(m, slot, soft_switch);
                break;

            case IWM_Q6_OFF:
            case IWM_Q6_ON:
                return diskii_q6_access(m, slot, soft_switch & 1);

            case IWM_Q7_OFF:
            case IWM_Q7_ON:
                return diskii_q7_access(m, slot, soft_switch & 1);
        }
    }
    return floating_bus();
}

uint8_t io_c0_read_slot(APPLE2 *m, uint16_t a) {
    // Device Select
    int slot = (a >> 4) & 0x7;
    switch(m->slot_cards[slot].slot_type) {
        case SLOT_TYPE_DISKII:
            return io_c0_diskii_helper(m, a, slot);

        case SLOT_TYPE_SMARTPORT:
            switch(a & 0x0f) {
                case SP_DATA:
                    return m->sp_device[slot].sp_buffer[m->sp_device[slot].sp_read_offset++];
                    break;

                case SP_STATUS:
                    return m->sp_device[slot].sp_status;
                    break;
            }
            break;

        case SLOT_TYPE_VIDEX_API: {
                FRANKLIN_DISPLAY *fd80 = &m->franklin_display;
                fd80->bank = (a & 0x0C) >> 2;
                return fd80->registers[a & 0x0F];
            }
            break;
    }
    return floating_bus();
}

void io_c0_write_slot(APPLE2 *m, uint16_t a, uint8_t v) {
    int slot = (a >> 4) & 0x7;
    switch(m->slot_cards[slot].slot_type) {
        case SLOT_TYPE_DISKII:
            io_c0_diskii_helper(m, a, slot);
            break;

        case SLOT_TYPE_SMARTPORT:
            switch(a & 0x0F) {
                case SP_DATA:
                    m->sp_device[slot].sp_buffer[m->sp_device[slot].sp_write_offset++] = v;
                    return;

                case SP_STATUS:
                    m->sp_device[slot].sp_read_offset = 0;
                    m->sp_device[slot].sp_write_offset = 0;

                    switch(m->sp_device[slot].sp_buffer[0]) {
                        case 0:
                            sp_status(m, slot);
                            break;
                        case 1:
                            sp_read(m, slot);
                            break;
                        case 2:
                            sp_write(m, slot);
                            break;
                    }
                    m->sp_device[slot].sp_status = 0x80;
                    return;
            }
            break;

        case SLOT_TYPE_VIDEX_API:
            franklin_display_set(m, a, v);
            break;
    }
}

void io_c0_table_init(void) {
    // Set a base state
    io_c0_set_range(&io_c0_iiplus, 0x00, 0x80, io_c0_floating_r, io_c0_noop_w);
    // 00
    io_c0_set_read_range(&io_c0_iiplus, 0x00, 0x10, io_c0_kbd_r);
    // 10
    io_c0_set_range(&io_c0_iiplus, 0x10, 0x10, io_c0_kbdstrb_r, io_c0_kbdstrb_w);
    // 20
    // 30
    io_c0_set_range(&io_c0_iiplus, 0x30, 0x10, io_c0_a2speaker_r, io_c0_a2speaker_w);
    // 40 -- VBL?
    // 50
    io_c0_iiplus.r[TXTCLR & 0xFF] = io_c0_txtclr_r;
    io_c0_iiplus.w[TXTCLR & 0xFF] = io_c0_txtclr_w;
    io_c0_iiplus.r[TXTSET & 0xFF] = io_c0_txtset_r;
    io_c0_iiplus.w[TXTSET & 0xFF] = io_c0_txtset_w;
    io_c0_iiplus.r[MIXCLR & 0xFF] = io_c0_mixclr_r;
    io_c0_iiplus.w[MIXCLR & 0xFF] = io_c0_mixclr_w;
    io_c0_iiplus.r[MIXSET & 0xFF] = io_c0_mixset_r;
    io_c0_iiplus.w[MIXSET & 0xFF] = io_c0_mixset_w;
    io_c0_iiplus.r[CLRPAGE2 & 0xFF] = io_c0_clrpage2_r;
    io_c0_iiplus.w[CLRPAGE2 & 0xFF] = io_c0_clrpage2_w;
    io_c0_iiplus.r[SETPAGE2 & 0xFF] = io_c0_setpage2_r;
    io_c0_iiplus.w[SETPAGE2 & 0xFF] = io_c0_setpage2_w;
    io_c0_iiplus.r[CLRHIRES & 0xFF] = io_c0_clrhires_r;
    io_c0_iiplus.w[CLRHIRES & 0xFF] = io_c0_clrhires_w;
    io_c0_iiplus.r[SETHIRES & 0xFF] = io_c0_sethires_r;
    io_c0_iiplus.w[SETHIRES & 0xFF] = io_c0_sethires_w;
    // 60
    io_c0_iiplus.r[BUTN0 & 0xFF] = io_c0_butn0_r;
    io_c0_iiplus.r[BUTN1 & 0xFF] = io_c0_butn1_r;
    io_c0_iiplus.r[BUTN2 & 0xFF] = io_c0_butn2_r;
    io_c0_iiplus.r[PADDL0 & 0xFF] = io_c0_paddl0_r;
    io_c0_iiplus.r[PADDL1 & 0xFF] = io_c0_paddl1_r;
    io_c0_iiplus.r[PADDL2 & 0xFF] = io_c0_paddl2_r;
    io_c0_iiplus.r[PADDL3 & 0xFF] = io_c0_paddl3_r;
    // 70
    io_c0_set_range(&io_c0_iiplus, 0x70, 0x10, io_c0_ptrig_r, io_c0_ptrig_w);
    // 80 - 8F - Language card
    io_c0_set_range(&io_c0_iiplus, 0x80, 0X10, io_c0_read_lc, io_c0_write_lc);
    // 90 - FF - Slot IO
    io_c0_set_range(&io_c0_iiplus, 0x90, 0X70, io_c0_read_slot, io_c0_write_slot);

    // Make the iie the same as the iiplus (the base set)
    memcpy(&io_c0_iie, &io_c0_iiplus, sizeof(io_c0_iie));

    // now apply deltas
    // 00 - noop to new soft switches on write
    io_c0_iie.w[CLR80STORE & 0xFF] = io_c0_clr80store_w;
    io_c0_iie.w[SET80STORE & 0xFF] = io_c0_set80store_w;
    io_c0_iie.w[CLRRAMRD & 0xFF] = io_c0_clrramrd_w;
    io_c0_iie.w[SETRAMRD & 0xFF] = io_c0_setramrd_w;
    io_c0_iie.w[CLRRAMWRT & 0xFF] = io_c0_clrramwrt_w;
    io_c0_iie.w[SETRAMWRT & 0xFF] = io_c0_setramwrt_w;
    io_c0_iie.w[CLRCXROM & 0xFF] = io_c0_clrcxrom_w;
    io_c0_iie.w[SETCXROM & 0xFF] = io_c0_setcxrom_w;
    io_c0_iie.w[CLRALTZP & 0xFF] = io_c0_clraltzp_w;
    io_c0_iie.w[SETALTZP & 0xFF] = io_c0_setaltzp_w;
    io_c0_iie.w[CLRC3ROM & 0xFF] = io_c0_clrc3rom_w;
    io_c0_iie.w[SETC3ROM & 0xFF] = io_c0_setc3rom_w;
    io_c0_iie.w[CLR80COL & 0xFF] = io_c0_clr80col_w;
    io_c0_iie.w[SET80COL & 0xFF] = io_c0_set80col_w;
    io_c0_iie.w[CLRALTCHAR & 0xFF] = io_c0_clraltchar_w;
    io_c0_iie.w[SETALTCHAR & 0xFF] = io_c0_setaltchar_w;

    // 10 - these all call kbdstrb as well, and write did already
    io_c0_iie.r[HRAMRD & 0xFF] = io_c0_hramrd_r;
    io_c0_iie.r[HRAMWRT & 0xFF] = io_c0_hramwrt_r;
    io_c0_iie.r[RDRAMRD & 0xFF] = io_c0_rdramrd_r;
    io_c0_iie.r[RDRAMWRT & 0xFF] = io_c0_rdramwrt_r;
    io_c0_iie.r[RDCXROM & 0xFF] = io_c0_rdcxrom_r;
    io_c0_iie.r[RDALTZP & 0xFF] = io_c0_rdaltzp_r;
    io_c0_iie.r[RDC3ROM & 0xFF] = io_c0_rdc3rom_r;
    io_c0_iie.r[RD80STORE & 0xFF] = io_c0_rd80store_r;
    io_c0_iie.r[RDVBL & 0xFF] = io_c0_rdvbl_r;
    io_c0_iie.r[RDTEXT & 0xFF] = io_c0_rdtext_r;
    io_c0_iie.r[RDMIXED & 0xFF] = io_c0_rdmixed_r;
    io_c0_iie.r[RDPAGE2 & 0xFF] = io_c0_rdpage2_r;
    io_c0_iie.r[RDHIRES & 0xFF] = io_c0_rdhires_r;
    io_c0_iie.r[RDALTCHAR & 0xFF] = io_c0_rdaltchar_r;
    io_c0_iie.r[RD80COL & 0xFF] = io_c0_rd80col_r;

    // 20
    // 30 - Speaker
    // 40 - ?
    // 50 - DHGR toggles
    io_c0_iie.r[CLRAN3 & 0xFF] = io_c0_clran3_r;
    io_c0_iie.w[CLRAN3 & 0xFF] = io_c0_clran3_w;
    io_c0_iie.r[SETAN3 & 0xFF] = io_c0_setan3_r;
    io_c0_iie.w[SETAN3 & 0xFF] = io_c0_setan3_w;
    // 60 - FF
}

// IO area reads that trigger on mask (read_from_memory)
uint8_t io_callback_r(APPLE2 *m, uint16_t address) {
    if(address >= 0xC000 && address < 0xC100) {
        return io_c0_machine_table->r[address & 0xFF](m, address);
    } else if(address >= 0xc100 && address < 0xC800) {
        if(m->strobed_slot == C800_NONE) {
            io_slot_handler(m, address);
        }
    } else if(address == CLRROM) {
        slot_clrrom(m, address);
    }
    // If nothing is strobed, this is floating bus which in my case is 0xA0
    return m->pages.read_pages[address / PAGE_SIZE][address % PAGE_SIZE];
}

// IO area writes that trigger on mask (write_to_memory)
void io_callback_w(APPLE2 *m, uint16_t address, uint8_t value) {
    if(address >= 0xC000 && address < 0xC100) {
        io_c0_machine_table->w[address & 0xFF](m, address, value);
    } else if(address >= 0xc100 && address < 0xC800) {
        if(m->strobed_slot == C800_NONE) {
            io_slot_handler(m, address);
        }
    } else if(tst_flags(m->state_flags, A2S_FRANKLIN80INSTALLED) && address >= 0xCC00 && address < 0xCE00) {
        m->franklin_display.display_ram[(address & 0x01ff) + m->franklin_display.bank * 0x200] = value;
    } else if(address == CLRROM) {
        slot_clrrom(m, address);
    }
}

void io_setup(APPLE2 *m) {
    io_c0_machine_table = &io_c0_iiplus;
    io_mem_bank_state_clear(m, A2S_RESET_MASK);
    if(m->model == MODEL_APPLE_IIEE) {
        io_c0_machine_table = &io_c0_iie;
    } else {
        // This allows the same io_slot handler for both models
        io_mem_bank_state_set(m, A2S_SLOT3ROM_MB_DISABLE);
    }
    io_mem_bank_state_set(m, A2S_LC_BANK2 | A2S_LC_WRITE | A2S_LC_PRE_WRITE);
    m->strobed_slot = C800_NONE;
}

