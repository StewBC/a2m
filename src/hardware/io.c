// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

// These tables hold the handlers for c000-c0ff for each model
static A2_C0_TABLE c0_iiplus;
static A2_C0_TABLE c0_iie;

// This is an accessor that points at the table for whatever model is active
A2_C0_TABLE *c0_machine_table;

// This points at the handler for any reads/writes to c100-c1ff
void (*io_slot_handler)(APPLE2 *m, uint16_t address);

static void io_slot_iiplus(APPLE2 *m, uint16_t address) {
    int slot = (address >> 8) & 0x7;
    // Only if slot isn't mapped
    if(m->mapped_slot != slot) {
        // Map the C800 ROM based on access to Cs00, if card provides a C800 ROM
        if(!m->slot_cards[slot].cx_rom_mapped && m->slot_cards[slot].slot_map_cx_rom) {
            m->slot_cards[slot].slot_map_cx_rom(m, address);
            m->slot_cards[slot].cx_rom_mapped = 1;
        }
        m->mapped_slot = slot;
    }
}

static void io_slot_iie(APPLE2 *m, uint16_t address) {
    // IO Select, which can "strobe" c800-cfff
    int slot = (address >> 8) & 0x7;
    // slot 3 and not m->c3slotrom overrides m->strobed
    if(slot == 3 && !m->c3slotrom) {
        // Map 80 col firmware
        pages_map_rom(&m->pages, 0xC800, 0x800, &m->roms.blocks[ROM_APPLE2_SLOTS].bytes[0x800], &m->ram);
        m->strobed = 1;
        m->mapped_slot = 0;
    } else if(!m->strobed && !m->cxromset && m->slot_cards[slot].slot_map_cx_rom) {
        // If nothing is strobed, and the rom is not active and the card has rom, map the rom
        m->slot_cards[slot].slot_map_cx_rom(m, address);
        m->mapped_slot = slot;
        m->strobed = 1;
    }
}

static void io_c8_free(APPLE2 *m) {
    m->mapped_slot = 0;
    for(int i = 1; i < 8; i++) {
        m->slot_cards[i].cx_rom_mapped = 0;
    }
    // On a ][+ this C800-CFFF becomes "nothing" (RAM in my case)
    pages_map(&m->pages, PAGE_MAP_READ, 0xC800, 0x800, &m->ram);
}

static inline void slot_clrrom(APPLE2 *m, uint16_t address) {
    // cxromset overriders all - do nothing if cxromset
    if(m->strobed && !m->cxromset) {
        if(!m->mapped_slot) {
            m->slot_cards[m->mapped_slot].cx_rom_mapped = 0;
            m->mapped_slot = 0;
        }
        pages_map(&m->pages, PAGE_MAP_READ, 0xC800, 0x800, &m->ram);
        m->strobed = 0;
    }
}

static inline void set_memory_map(APPLE2 *m) {
    // Start by restoring everything
    pages_map(&m->pages, PAGE_MAP_READ,  0x0000, 0xC000, &m->ram);
    pages_map(&m->pages, PAGE_MAP_WRITE, 0x0000, 0xC000, &m->ram);
    language_card_map_memory(m);

    // SETALTZP
    if(m->altzpset) {
        pages_map(&m->pages, PAGE_MAP_READ,  0x10000, 0x0200, &m->ram);
        pages_map(&m->pages, PAGE_MAP_WRITE, 0x10000, 0x0200, &m->ram);
    }

    //SETRAMRD
    if(m->ramrdset) {
        pages_map(&m->pages, PAGE_MAP_READ,  0x10200, 0xBE00, &m->ram);
    }

    // SETRAMWRT
    if(m->ramwrtset) {
        pages_map(&m->pages, PAGE_MAP_WRITE, 0x10200, 0xBE00, &m->ram);
    }

    // SET80
    if(m->store80set) {
        pages_map(&m->pages, PAGE_MAP_READ,  0x0400, 0x0400, &m->ram);
        pages_map(&m->pages, PAGE_MAP_WRITE, 0x0400, 0x0400, &m->ram);
        if(m->hires) {
            pages_map(&m->pages, PAGE_MAP_READ,  0x2000, 0x2000, &m->ram);
            pages_map(&m->pages, PAGE_MAP_WRITE, 0x2000, 0x2000, &m->ram);
        }
        if(m->page2set) {
            pages_map(&m->pages, PAGE_MAP_READ,  0x10400, 0x0400, &m->ram);
            pages_map(&m->pages, PAGE_MAP_WRITE, 0x10400, 0x0400, &m->ram);
            if(m->hires) {
                pages_map(&m->pages, PAGE_MAP_READ,  0x12000, 0x2000, &m->ram);
                pages_map(&m->pages, PAGE_MAP_WRITE, 0x12000, 0x2000, &m->ram);
            }
        }
    }
}


/*--------------------------------------------------------------------------*/

static void c0_set_read_range(A2_C0_TABLE *t, int start, int end, ss_read read) {
    for(int i = start; i <= end; i++) {
        t->r[i] = read;
    }
}

// static void c0_set_write_range(A2_C0_TABLE *t, int start, int end, ss_write write) {
//     for(int i = start; i <= end; i++) {
//         t->w[i] = write;
//     }
// }

static void c0_set_range(A2_C0_TABLE *t, int start, int end, ss_read read_fn, ss_write write_fn) {
    for(int i = start; i <= end; i++) {
        t->r[i] = read_fn;
        t->w[i] = write_fn;
    }
}

static inline uint8_t floating_bus(void) {
    return 0xA0;
}

static uint8_t c0_floating_r(APPLE2 *m, uint16_t a) {
    UNUSED(m);
    UNUSED(a);
    return floating_bus();
}

static uint8_t c0_through_r(APPLE2 *m, uint16_t a) {
    return m->pages.read_pages[a / PAGE_SIZE][a % PAGE_SIZE];
}

static void c0_noop_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(m);
    UNUSED(a);
    UNUSED(v);
}

//--------------------------------------------------------------------------
// 0xC000
uint8_t c0_kbd_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    return m->ram.RAM_MAIN[KBD];
}

// 0xC000
void c0_clr80store_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
    // This is just keeping the old system till I have table-converted the code
    m->store80set = 0;
    set_memory_map(m);
}

// 0xC001
void c0_set80store_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
    // This is just keeping the old system till I have table-converted the code
    m->store80set = 1;
    set_memory_map(m);
}

// All of these will get filled out
// 0xC002
void c0_clrramrd_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a); UNUSED(v);
    m->ramrdset = 0;
    set_memory_map(m);
}

// 0xC0003
void c0_setramrd_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a); UNUSED(v);
    m->ramrdset = 1;
    set_memory_map(m);
}

// 0xC004
void c0_clrramwrt_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a); UNUSED(v);
    m->ramwrtset = 0;
    set_memory_map(m);    
}

// 0xC005
void c0_setramwrt_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a); UNUSED(v);
    m->ramwrtset = 1;
    set_memory_map(m);
}

// 0xC006
void c0_clrcxrom_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(v);
    m->cxromset = 0;
    // Restore the slot mappings
    m->pages.read_pages[0xC100 / PAGE_SIZE] = m->rom_shadow_pages[1];
    m->pages.read_pages[0xC200 / PAGE_SIZE] = m->rom_shadow_pages[2];
    m->pages.read_pages[0xC400 / PAGE_SIZE] = m->rom_shadow_pages[4];
    m->pages.read_pages[0xC500 / PAGE_SIZE] = m->rom_shadow_pages[5];
    m->pages.read_pages[0xC600 / PAGE_SIZE] = m->rom_shadow_pages[6];
    m->pages.read_pages[0xC700 / PAGE_SIZE] = m->rom_shadow_pages[7];
    // Only if internal slot3 is not overriding the card rom
    if(m->c3slotrom) {
        m->pages.read_pages[0xC300 / PAGE_SIZE] = m->rom_shadow_pages[3];
    }
    // If a card was mapped to c800, restore that mapping
    if(m->strobed) {
        if(m->mapped_slot) {
            m->slot_cards[m->mapped_slot].slot_map_cx_rom(m, a);
        }
    } else {
        pages_map(&m->pages, PAGE_MAP_READ, 0xC800, 0x800, &m->ram);
    }
}

// 0xC007
void c0_setcxrom_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a); UNUSED(v);
    m->cxromset = 1;
    // C100-D000
    pages_map_rom(&m->pages, 0xC100, 0xF00, &m->roms.blocks[ROM_APPLE2_SLOTS].bytes[0x100], &m->ram);
}

// 0xC008
void c0_clraltzp_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a); UNUSED(v);
    m->altzpset = 0;
    set_memory_map(m);    
}

// 0xC009
void c0_setaltzp_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a); UNUSED(v);
    m->altzpset = 1;
    set_memory_map(m);
}

// 0xC00A
void c0_clrc3rom_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a); UNUSED(v);
    // C300-C3FF
    // Turn internal rom on, slot rom off
    m->c3slotrom = 0;
    pages_map_rom(&m->pages, 0xC300, 0x100, &m->roms.blocks[ROM_APPLE2_SLOTS].bytes[0x300], &m->ram);
}

// // 0xC00B
void c0_setc3rom_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a); UNUSED(v);
    // Turn internal rom off, slot rom on
    m->c3slotrom = 1;
    m->pages.read_pages[0xC300 / PAGE_SIZE] = m->rom_shadow_pages[3];
}

// 0xC00C
void c0_clr80col_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a); UNUSED(v);
    m->col80set = 0;
}

// 0xC00D
void c0_set80col_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a); UNUSED(v);
    m->col80set = 1;
}

// 0xC00E
void c0_clraltchar_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a); UNUSED(v);
    m->altcharset = 0;
}

// 0xC00F
void c0_setaltchar_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a); UNUSED(v);
    m->altcharset = 1;
}

static inline void kbdstrb_helper(APPLE2 *m) {
    if(m->a2out_cb.cb_clipboard_ctx.cb_clipboard &&
       !m->a2out_cb.cb_clipboard_ctx.cb_clipboard(m->a2out_cb.cb_clipboard_ctx.user)) {
        m->ram.RAM_MAIN[KBD] &= 0x7F;
    }
}

// 0xC010
uint8_t c0_kbdstrb_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    kbdstrb_helper(m);
    return floating_bus(); 
}

void c0_kbdstrb_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
    kbdstrb_helper(m); 
}
   
// 0xC011
uint8_t c0_hramrd_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    kbdstrb_helper(m); 
    return m->lc_bank2_enable << 7;
}

// 0xC012
uint8_t c0_hramwrt_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    kbdstrb_helper(m); 
    return m->lc_read_ram_enable << 7;
}

// 0xC013
uint8_t c0_rdramrd_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    kbdstrb_helper(m); 
    return m->ramrdset << 7;
}

// 0xC014
uint8_t c0_rdramwrt_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    kbdstrb_helper(m); 
    return m->ramwrtset << 7;
}

// 0xC015
uint8_t c0_rdcxrom_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    kbdstrb_helper(m); 
    return m->cxromset << 7;
}

// 0xC016
uint8_t c0_rdaltzp_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    kbdstrb_helper(m); 
    return m->altzpset << 7;
}

// 0xc017
uint8_t c0_rdc3rom_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    kbdstrb_helper(m); 
    return m->c3slotrom << 7;
}

// 0xC018
uint8_t c0_rd80store_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    kbdstrb_helper(m); 
    return m->store80set << 7;
}

// 0xC019
uint8_t c0_rdvbl_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    kbdstrb_helper(m); 
    return (m->cpu.cycles % 17030) >= 15665 ? 0x80 : 0;
}

// 0xC01A
uint8_t c0_rdtext_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    kbdstrb_helper(m); 
    return m->text << 7;
}

// 0xC01B
uint8_t c0_rdmixed_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    kbdstrb_helper(m); 
    return m->mixed << 7;
}

// 0xC01C
uint8_t c0_rdpage2_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    kbdstrb_helper(m); 
    return m->page2set << 7;
}

// 0xC01D
uint8_t c0_rdhires_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    kbdstrb_helper(m); 
    return m->hires << 7;
}

// 0xC01E
uint8_t c0_rdaltchar_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    kbdstrb_helper(m); 
    return m->altcharset << 7;
}

// 0xC01F
uint8_t c0_rd80col_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    kbdstrb_helper(m); 
    return m->col80set << 7;
}

// 0xC030
uint8_t c0_a2speaker_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    if(m->a2out_cb.cb_speaker_ctx.cb_speaker) {
        m->a2out_cb.cb_speaker_ctx.cb_speaker(m->a2out_cb.cb_speaker_ctx.user);
    }
    return floating_bus();
}

void c0_a2speaker_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
    if(m->a2out_cb.cb_speaker_ctx.cb_speaker) {
        m->a2out_cb.cb_speaker_ctx.cb_speaker(m->a2out_cb.cb_speaker_ctx.user);
    }
}

// 0xC036
uint8_t c0_cyareg_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    return floating_bus();
}

void c0_cyareg_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
}

// 0xC041
uint8_t c0_rdvblmsk_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    return floating_bus();
}

void c0_rdvblmsk_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
}

// 0xC050
uint8_t c0_txtclr_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    m->text = 0;
    return floating_bus();
}

void c0_txtclr_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
    m->text = 0;
}

// 0xC051
uint8_t c0_txtset_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    m->text = 1;
    return floating_bus();
}

void c0_txtset_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
    m->text = 1;
}

// 0xC052
uint8_t c0_mixclr_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    m->mixed = 0;
    return floating_bus();
}

void c0_mixclr_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
    m->mixed = 0;
}

// 0xC053
uint8_t c0_mixset_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    m->mixed = 1;
    return floating_bus();
}

void c0_mixset_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
    m->mixed = 1;
}

// 0xC054
uint8_t c0_clrpage2_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    m->page2set = 0;
    if(m->store80set) {
        set_memory_map(m);
    }
    return floating_bus();
}

void c0_clrpage2_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
    m->page2set = 0;
    if(m->store80set) {
        set_memory_map(m);
    }
}

// 0xC055
uint8_t c0_setpage2_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    m->page2set = 1;
    if(m->store80set) {
        set_memory_map(m);
    }
    return floating_bus();
}

void c0_setpage2_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
    m->page2set = 1;
    if(m->store80set) {
        set_memory_map(m);
    }
}

// 0xC056
uint8_t c0_clrhires_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    m->hires = 0;
    if(m->store80set) {
        set_memory_map(m);
    }
    return floating_bus();
}

void c0_clrhires_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
    m->hires = 0;
    if(m->store80set) {
        set_memory_map(m);
    }
}

// 0xC057
uint8_t c0_sethires_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    m->hires = 1;
    if(m->store80set) {
        set_memory_map(m);
    }
    return floating_bus();
}

void c0_sethires_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
    m->hires = 1;
    if(m->store80set) {
        set_memory_map(m);
    }
}

// 0xC058
uint8_t c0_clran0_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    return floating_bus();
}

void c0_clran0_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
}

// 0xC059
uint8_t c0_setan0_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    return floating_bus();
}

void c0_setan0_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
}

// 0xC05A
uint8_t c0_clran1_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    return floating_bus();
}

void c0_clran1_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
}

// 0xC05B
uint8_t c0_setan1_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    return floating_bus();
}

void c0_setan1_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
}

// 0xC05C
uint8_t c0_clran2_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    return floating_bus();
}

void c0_clran2_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
}

// 0xC05D
uint8_t c0_setan2_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    return floating_bus();
}

void c0_setan2_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
}

// 0xC05E
uint8_t c0_clran3_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    m->dhires = 1;
    return floating_bus();
}

void c0_clran3_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
    m->dhires = 1;
}

// 0xC05F
uint8_t c0_setan3_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    m->dhires = 0;
    return floating_bus();
}

void c0_setan3_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
    m->dhires = 0;
}

// 0xC060
uint8_t c0_tapein_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    return floating_bus();
}

void c0_tapein_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
}

// 0xC061
uint8_t c0_butn0_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    uint8_t button = m->open_apple ? 0x80 : 0x00;
    cb_read_button rb = m->a2out_cb.cb_inputdevice_ctx.cb_read_button;
    if(rb) {
        for(int i = 0; i < 2; i++) {
            button |= rb(m->a2out_cb.cb_inputdevice_ctx.user, i, APPLE2_BUTTON_0);
        }
    }
    return button ? 0x80 : 0;
}

// 0xC062
uint8_t c0_butn1_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    uint8_t button = m->closed_apple ? 0x80 : 0x00;
    cb_read_button rb = m->a2out_cb.cb_inputdevice_ctx.cb_read_button;
    if(rb) {
        for(int i = 0; i < 2; i++) {
            button |= rb(m->a2out_cb.cb_inputdevice_ctx.user, i, APPLE2_BUTTON_1);
        }
    }
    return button ? 0x80 : 0;
}

// 0xC063
uint8_t c0_butn2_r(APPLE2 *m, uint16_t a) {
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
uint8_t c0_paddl0_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    cb_read_axis ra = m->a2out_cb.cb_inputdevice_ctx.cb_read_axis;
    if(ra) {
        return ra(m->a2out_cb.cb_inputdevice_ctx.user, 0, APPLE2_AXIS_X, m->cpu.cycles);
    }
    return 0x80;
}

// 0xC065
uint8_t c0_paddl1_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    cb_read_axis ra = m->a2out_cb.cb_inputdevice_ctx.cb_read_axis;
    if(ra) {
        return ra(m->a2out_cb.cb_inputdevice_ctx.user, 0, APPLE2_AXIS_Y, m->cpu.cycles);
    }
    return 0x80;
}

// 0xC066
uint8_t c0_paddl2_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    cb_read_axis ra = m->a2out_cb.cb_inputdevice_ctx.cb_read_axis;
    if(ra) {
        return ra(m->a2out_cb.cb_inputdevice_ctx.user, 1, APPLE2_AXIS_X, m->cpu.cycles);
    }
    return 0x80;
}

// 0xC067
uint8_t c0_paddl3_r(APPLE2 *m, uint16_t a) {
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

uint8_t c0_ptrig_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    ptrig_helper(m);
    return floating_bus();
}

void c0_ptrig_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    ptrig_helper(m);
}

// 0xC07E
uint8_t c0_rdwclrioud_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    return floating_bus();
}

void c0_rdwclrioud_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
}

// 0xC07F
uint8_t c0_rdhgr_wsetioud_r(APPLE2 *m, uint16_t a) {
    UNUSED(a);
    return floating_bus();
}

void c0_rdhgr_wsetioud_w(APPLE2 *m, uint16_t a, uint8_t v) {
    UNUSED(a);
    UNUSED(v);
}

// 0xC080-0xC08F
uint8_t c0_read_lc(APPLE2 *m, uint16_t a) {
    language_card(m, a, 0);
    return floating_bus();
}

void c0_write_lc(APPLE2 *m, uint16_t a, uint8_t v) {
    language_card(m, a, 1);
}

// 0xC90-0xCFF
static inline uint8_t c0_diskii_helper(APPLE2 *m, uint16_t a, uint8_t slot) {
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

uint8_t c0_read_slot(APPLE2 *m, uint16_t a) {
    // Device Select
    int slot = (a >> 4) & 0x7;
    switch(m->slot_cards[slot].slot_type) {
        case SLOT_TYPE_DISKII:
            return c0_diskii_helper(m, a, slot);

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

void c0_write_slot(APPLE2 *m, uint16_t a, uint8_t v) {
    int slot = (a >> 4) & 0x7;
    switch(m->slot_cards[slot].slot_type) {
        case SLOT_TYPE_DISKII:
            c0_diskii_helper(m, a, slot);
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
    c0_set_range(&c0_iiplus, 0x00, 0x7F, c0_floating_r, c0_noop_w);
    // 00
    c0_set_read_range(&c0_iiplus, 0x00, 0x1F, c0_kbd_r);
    // 10
    c0_set_range(&c0_iiplus, 0x10, 0x1F, c0_kbdstrb_r, c0_kbdstrb_w);
    // 20
    // 30
    c0_set_range(&c0_iiplus, 0x30, 0x1F, c0_a2speaker_r, c0_a2speaker_w);
    // 40 -- VBL?
    // 50
    c0_iiplus.r[TXTCLR & 0xFF] = c0_txtclr_r;
    c0_iiplus.w[TXTCLR & 0xFF] = c0_txtclr_w;
    c0_iiplus.r[TXTSET & 0xFF] = c0_txtset_r;
    c0_iiplus.w[TXTSET & 0xFF] = c0_txtset_w;
    c0_iiplus.r[MIXCLR & 0xFF] = c0_mixclr_r;
    c0_iiplus.w[MIXCLR & 0xFF] = c0_mixclr_w;
    c0_iiplus.r[MIXSET & 0xFF] = c0_mixset_r;
    c0_iiplus.w[MIXSET & 0xFF] = c0_mixset_w;
    c0_iiplus.r[CLRPAGE2 & 0xFF] = c0_clrpage2_r;
    c0_iiplus.w[CLRPAGE2 & 0xFF] = c0_clrpage2_w;
    c0_iiplus.r[SETPAGE2 & 0xFF] = c0_setpage2_r;
    c0_iiplus.w[SETPAGE2 & 0xFF] = c0_setpage2_w;
    c0_iiplus.r[CLRHIRES & 0xFF] = c0_clrhires_r;
    c0_iiplus.w[CLRHIRES & 0xFF] = c0_clrhires_w;
    c0_iiplus.r[SETHIRES & 0xFF] = c0_sethires_r;
    c0_iiplus.w[SETHIRES & 0xFF] = c0_sethires_w;
    // 60
    c0_iiplus.r[BUTN0 & 0xFF] = c0_butn0_r;
    c0_iiplus.r[BUTN1 & 0xFF] = c0_butn1_r;
    c0_iiplus.r[BUTN2 & 0xFF] = c0_butn2_r;
    c0_iiplus.r[PADDL0 & 0xFF] = c0_paddl0_r;
    c0_iiplus.r[PADDL1 & 0xFF] = c0_paddl1_r;
    c0_iiplus.r[PADDL2 & 0xFF] = c0_paddl2_r;
    c0_iiplus.r[PADDL3 & 0xFF] = c0_paddl3_r;
    // 70
    c0_set_range(&c0_iiplus, 0x30, 0x1F, c0_ptrig_r, c0_ptrig_w);
    // 80 - 8F - Language card
    c0_set_range(&c0_iiplus, 0x80, 0X8F, c0_read_lc, c0_write_lc);
    // 90 - FF - Slot IO
    c0_set_range(&c0_iiplus, 0x90, 0XFF, c0_read_slot, c0_write_slot);

    // Make the iie the same as the iiplus (the base set)
    memcpy(&c0_iie, &c0_iiplus, sizeof(c0_iie));

    // now apply deltas
    // 00 - noop to new soft switches on write
    c0_iie.w[CLR80STORE & 0xFF] = c0_clr80store_w;
    c0_iie.w[SET80STORE & 0xFF] = c0_set80store_w;
    c0_iie.w[CLRRAMRD & 0xFF] = c0_clrramrd_w;
    c0_iie.w[SETRAMRD & 0xFF] = c0_setramrd_w;
    c0_iie.w[CLRRAMWRT & 0xFF] = c0_clrramwrt_w;
    c0_iie.w[SETRAMWRT & 0xFF] = c0_setramwrt_w;
    c0_iie.w[CLRCXROM & 0xFF] = c0_clrcxrom_w;
    c0_iie.w[SETCXROM & 0xFF] = c0_setcxrom_w;
    c0_iie.w[CLRALTZP & 0xFF] = c0_clraltzp_w;
    c0_iie.w[SETALTZP & 0xFF] = c0_setaltzp_w;
    c0_iie.w[CLRC3ROM & 0xFF] = c0_clrc3rom_w;
    c0_iie.w[SETC3ROM & 0xFF] = c0_setc3rom_w;
    c0_iie.w[CLR80COL & 0xFF] = c0_clr80col_w;
    c0_iie.w[SET80COL & 0xFF] = c0_set80col_w;
    c0_iie.w[CLRALTCHAR & 0xFF] = c0_clraltchar_w;
    c0_iie.w[SETALTCHAR & 0xFF] = c0_setaltchar_w;

    // 10 - these all call kbdstrb as well, and write did already
    c0_iie.r[HRAMRD & 0xFF] = c0_hramrd_r;
    c0_iie.r[HRAMWRT & 0xFF] = c0_hramwrt_r;
    c0_iie.r[RDRAMRD & 0xFF] = c0_rdramrd_r;
    c0_iie.r[RDRAMWRT & 0xFF] = c0_rdramwrt_r;
    c0_iie.r[RDCXROM & 0xFF] = c0_rdcxrom_r;
    c0_iie.r[RDALTZP & 0xFF] = c0_rdaltzp_r;
    c0_iie.r[RDC3ROM & 0xFF] = c0_rdc3rom_r;
    c0_iie.r[RD80STORE & 0xFF] = c0_rd80store_r;
    c0_iie.r[RDVBL & 0xFF] = c0_rdvbl_r;
    c0_iie.r[RDTEXT & 0xFF] = c0_rdtext_r;
    c0_iie.r[RDMIXED & 0xFF] = c0_rdmixed_r;
    c0_iie.r[RDPAGE2 & 0xFF] = c0_rdpage2_r;
    c0_iie.r[RDHIRES & 0xFF] = c0_rdhires_r;
    c0_iie.r[RDALTCHAR & 0xFF] = c0_rdaltchar_r;
    c0_iie.r[RD80COL & 0xFF] = c0_rd80col_r;

    // 20 - Tape?
    // 30 - Speaker
    // 40 - ?
    // 50 - DHGR toggles
    c0_iie.r[CLRAN3 & 0xFF] = c0_clran3_r;
    c0_iie.w[CLRAN3 & 0xFF] = c0_clran3_w;
    c0_iie.r[SETAN3 & 0xFF] = c0_setan3_r;
    c0_iie.w[SETAN3 & 0xFF] = c0_setan3_w;
    // 60
    // 70
}

// IO area reads that trigger on mask (read_from_memory)
uint8_t io_callback_r(APPLE2 *m, uint16_t address) {
    if(address >= 0xC000 && address < 0xC100) {
        return c0_machine_table->r[address & 0xFF](m, address);
    } else if(address >= 0xc100 && address < 0xC800) {
        io_slot_handler(m, address);
    } else if(address == CLRROM) {
        io_c8_free(m);
    }
    // If nothing is strobed, this is floating bus which in my case is 0xA0
    return m->pages.read_pages[address / PAGE_SIZE][address % PAGE_SIZE];
}

// IO area writes that trigger on mask (write_to_memory)
void io_callback_w(APPLE2 *m, uint16_t address, uint8_t value) {
    if(address >= 0xC000 && address < 0xC100) {
        c0_machine_table->w[address & 0xFF](m, address, value);
    } else if(address >= 0xc100 && address < 0xC800) {
        io_slot_handler(m, address);
    } else if(m->franklin80installed && address >= 0xCC00 && address < 0xCE00) {
        m->franklin_display.display_ram[(address & 0x01ff) + m->franklin_display.bank * 0x200] = value;
    } else if(address == CLRROM) {
        io_c8_free(m);
    }
}

void io_setup(APPLE2 *m) {
    c0_machine_table = &c0_iiplus;
    io_slot_handler = &io_slot_iiplus;
    if(m->model) {
        c0_machine_table = &c0_iie;
        io_slot_handler = &io_slot_iie;
        set_memory_map(m);
    }
}

