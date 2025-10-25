// Apple ][+ and //e Emhanced emulator and assembler
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

static inline uint8_t apple2_softswitch_read_callback_IIe(APPLE2 *m, uint16_t address) {
    // This would ideally not be neccesary
    uint8_t byte = m->read_pages.pages[address / PAGE_SIZE].bytes[address % PAGE_SIZE];

    if(address >= 0xc100 && address < 0xCFFE) {
        // IO Select
        int slot = (address >> 8) & 0x7;
        // Only if slot isn't mapped, and only if ROM isn't active
        if(m->mapped_slot != slot && !(m->cxromset || (slot == 3 && m->c3romset))) {
            // Map the C800 ROM based on access to Cs00, if card provides a C800 ROM
            if(!m->slot_cards[slot].cx_rom_mapped && m->slot_cards[slot].slot_map_cx_rom) {
                m->slot_cards[slot].slot_map_cx_rom(m, address);
                m->slot_cards[slot].cx_rom_mapped = 1;
            }
            m->mapped_slot = slot;
        }
    } else if(address == CLRROM) {
        m->mapped_slot = -1;
        for(int i = 1; i < 8; i++) {
            m->slot_cards[i].cx_rom_mapped = 0;
        }
        // On a //e this C800-CFFF becomes ROM
        pages_map(&m->read_pages, 0xC800 / PAGE_SIZE, 0x800 / PAGE_SIZE, &m->roms.blocks[ROM_APPLE2_SLOTS].bytes[0X800]);
    } else {
        // Split into 16-byte "chunks"
        switch(address & 0xC0F0) {
            case 0xC000:
                byte = m->RAM_MAIN[KBD];
                break;
            case 0xC010:
                byte = m->RAM_MAIN[KBD] & 0x7F;
                switch(address) {
                    case KBDSTRB:
                        if(m->clipboard_text) {
                            viewapl2_feed_clipboard_key(m);
                        } else {
                            // I believe you get the key, pre-stobe-reset back
                            m->write_pages.pages[KBD / PAGE_SIZE].bytes[KBD % PAGE_SIZE] &= 0x7F;
                        }
                        break;
                    case HRAMRD:      //e
                        byte |= m->ram_card[m->ramrdset].bank2_enable << 7;
                        break;
                    case HRAMWRT:     //e
                        byte |= m->ram_card[m->ramrdset].read_ram_enable << 7;
                        break;
                    case RDRAMRD:     //e
                        byte |= m->ramrdset << 7;
                        break;
                    case RDRAMWRT:    //e
                        byte |= m->ramwrtset << 7;
                        break;
                    case RDCXROM:     //e
                        byte |= m->cxromset << 7;
                        break;
                    case RDALTZP:
                        byte |= m->altzpset << 7;
                        break;
                    case RDC3ROM:     //e
                        byte |= m->c3romset << 7;
                        break;
                    case RD80STORE:   //e
                        byte |= m->store80set << 7;
                        break;
                    case RDVBL: {     //e
                            // SQW is that what this VBL is about?
                            // Assume a frame (scanline to scanline) is 17030 cycles
                            // something lke 40 + 25 = 65 for a horizontal line
                            // and 65 * 262 = 17030; 262 = 525 NTSC / 2 (interlaced)
                            byte |= (m->cpu.cycles % 17030); // 17063 if 262.5 is used
                        }
                        break;
                    case RDTEXT:      //e
                        byte |= (m->screen_mode & SCREEN_MODE_GRAPHICS) ? 0 : 128;
                    case RDMIXED:     //e
                        byte |= (m->screen_mode & SCREEN_MODE_MIXED) ? 128 : 0;
                        break;
                    case RDPAGE2:     //e
                        byte |= m->page2set << 7;
                        break;
                    case RDHIRES:     //e
                        byte |= (m->screen_mode & SCREEN_MODE_GRAPHICS) ? 128 : 0;
                        break;
                    case RDALTCHAR:   //e
                        byte |= m->altcharset << 7;
                        break;
                    case RD80COL:     //e
                        byte |= m->col80set << 7;
                        break;
                }
                break;
            case 0xC020:
                // Casette stuff
                break;
            case 0xC030:
                speaker_toggle(&m->speaker);
                break;
            case 0xC040:
                // C40 strobe - don't know what that is
                break;
            case 0xC050:
                if (!m->ioudclr && address & 0x0008) {
                    // if ioudclr == 1, ie ioud not set (0 == set)
                    // Enable IOU access to 0xC058-0xC05F
                    break;
                }
                switch (address) {
                    case TXTCLR:
                        m->screen_mode |= SCREEN_MODE_GRAPHICS;
                        break;
                    case TXTSET:
                        m->screen_mode &= ~SCREEN_MODE_GRAPHICS;
                        break;
                    case MIXCLR:
                        m->screen_mode &= ~SCREEN_MODE_MIXED;
                        break;
                    case MIXSET:
                        m->screen_mode |= SCREEN_MODE_MIXED;
                        break;
                    case CLRPAGE2:
                        if(m->store80set) {
                            pages_map(&m->read_pages, 0x0400 / PAGE_SIZE, 0x0400 / PAGE_SIZE, &m->RAM_MAIN[0x0400]);
                            pages_map(&m->write_pages, 0x0400 / PAGE_SIZE, 0x0400 / PAGE_SIZE, &m->RAM_MAIN[0x0400]);
                            if(m->screen_mode & SCREEN_MODE_HIRES) {
                                pages_map(&m->read_pages, 0x2000 / PAGE_SIZE, 0x2000 / PAGE_SIZE, &m->RAM_MAIN[0x2000]);
                                pages_map(&m->write_pages, 0x2000 / PAGE_SIZE, 0x2000 / PAGE_SIZE, &m->RAM_MAIN[0x2000]);
                            }
                        }
                        m->page2set = 0;
                        break;
                    case SETPAGE2:
                        if(m->store80set) {
                            pages_map(&m->read_pages, 0x0400 / PAGE_SIZE, 0x0400 / PAGE_SIZE, &m->RAM_MAIN[0x10400]);
                            pages_map(&m->write_pages, 0x0400 / PAGE_SIZE, 0x0400 / PAGE_SIZE, &m->RAM_MAIN[0x10400]);
                            if(m->screen_mode & SCREEN_MODE_HIRES) {
                                pages_map(&m->read_pages, 0x2000 / PAGE_SIZE, 0x2000 / PAGE_SIZE, &m->RAM_MAIN[0x12000]);
                                pages_map(&m->write_pages, 0x2000 / PAGE_SIZE, 0x2000 / PAGE_SIZE, &m->RAM_MAIN[0x12000]);
                            }
                        }
                        m->page2set = 1;
                        break;
                    case LORES: // SQW - rename to CLRHIRES
                        m->screen_mode &= ~SCREEN_MODE_HIRES;
                        if(m->store80set) {
                            pages_map(&m->read_pages, 0x2000 / PAGE_SIZE, 0x2000 / PAGE_SIZE, &m->RAM_MAIN[0x2000]);
                            pages_map(&m->write_pages, 0x2000 / PAGE_SIZE, 0x2000 / PAGE_SIZE, &m->RAM_MAIN[0x2000]);
                        }
                        break;
                    case HIRES: // SQW - rename to SETHIRES
                        m->screen_mode |= SCREEN_MODE_HIRES;
                        if(m->store80set) {
                            pages_map(&m->read_pages, 0x2000 / PAGE_SIZE, 0x2000 / PAGE_SIZE, &m->RAM_MAIN[0x12000]);
                            pages_map(&m->write_pages, 0x2000 / PAGE_SIZE, 0x2000 / PAGE_SIZE, &m->RAM_MAIN[0x12000]);
                        }
                        break;
                    case CLRAN0: // SQW - hires or dhires if text off 
                        break;
                    case SETAN0: // SQW
                        break;
                    case CLRAN1: // SQW
                        break;
                    case SETAN1: // SQW
                        break;
                    case CLRAN2: // SQW
                        break;
                    case SETAN2: // SQW
                        break;
                    case CLRDHGR: // SQW - dhires on if IOUD is on
                        break;
                    case SETDHGR: // SQW - dhires off if IOUD is on
                        break;
                }
                break;
            case 0xC060:
                switch(address & 0xFFF7) {   
                    // c060 & c068, etc, all map to the same thing
                    case TAPEIN:
                        break;
                    case BUTN0: {
                        uint8_t button = m->open_apple;
                        VIEWPORT *v = m->viewport;
                        if(v) {
                            for(int i = 0; i < v->num_controllers; i++) {
                                button |= v->button_a[i];
                            }
                        }
                        byte = button ? 0x80 : 0;
                    }
                    break;
                    case BUTN1: {
                        uint8_t button = m->closed_apple;
                        VIEWPORT *v = m->viewport;
                        if(v) {
                            for(int i = 0; i < v->num_controllers; i++) {
                                button |= v->button_b[i];
                            }
                        }
                        byte = button ? 0x80 : 0;
                    }
                    break;
                    case BUTN2: {
                        uint8_t button = 0;
                        VIEWPORT *v = m->viewport;
                        if(v) {
                            for(int i = 0; i < v->num_controllers; i++) {
                                button |= v->button_x[i];
                            }
                        }
                        byte = button ? 0x80 : 0;
                    }
                    break;
                    case PADDL0: {
                        VIEWPORT *v = m->viewport;
                        if(v && v->game_controller[0]) {
                            uint64_t cycle_delta = m->cpu.cycles - v->ptrig_cycle;
                            uint8_t val = clamp_u8(cycle_delta * 255 / paddl_normalized, 0, 255 );
                            if(val >= v->axis_left_x[0]) {
                                byte = 0x0;
                                break;
                            }
                        }
                        byte = 0x80;
                    }
                    break;
                    case PADDL1: {
                        VIEWPORT *v = m->viewport;
                        if(v && v->game_controller[0]) {
                            uint64_t cycle_delta = m->cpu.cycles - v->ptrig_cycle;
                            uint8_t val = clamp_u8(cycle_delta * 255 / paddl_normalized, 0, 255 );
                            if(val >= v->axis_left_y[0]) {
                                byte = 0x0;
                                break;
                            }
                        }
                        byte = 0x80;
                    }
                    break;
                    case PADDL2: {
                        VIEWPORT *v = m->viewport;
                        if(v && v->game_controller[1]) {
                            uint64_t cycle_delta = m->cpu.cycles - v->ptrig_cycle;
                            uint8_t val = clamp_u8(cycle_delta * 255 / paddl_normalized, 0, 255 );
                            if(val >= v->axis_left_x[1]) {
                                byte = 0x0;
                                break;
                            }
                        }
                        byte = 0x80;
                    }
                    break;
                    case PADDL3: {
                        VIEWPORT *v = m->viewport;
                        if(v && v->game_controller[1]) {
                            uint64_t cycle_delta = m->cpu.cycles - v->ptrig_cycle;
                            uint8_t val = clamp_u8(cycle_delta * 255 / paddl_normalized, 0, 255 );
                            if(val >= v->axis_left_y[1]) {
                                byte = 0x0;
                                break;
                            }
                        }
                        byte = 0x80;
                    }
                }
                break;
            case 0xC070: {
                    VIEWPORT *v = m->viewport;
                    if(v && v->num_controllers) {
                        v->ptrig_cycle = m->cpu.cycles;
                    }
                    // Should also reset the VBL interrup flag...
                    switch(address) {
                        case RDWCLRIOUD: // e
                            byte |= 0; // m->ioudclr << 7; // Notice 0 == ON
                            break;
                        case RDHGR_WSETIOUD: // e
                            byte |= 0; // m->dhiresclr << 7; // Notice 0 == ON
                            break;
                    }
                }
                break;
            case 0xC080:
                ram_card(m, m->ramrdset, address, 0x100);
                break;
            case 0xC090:
            case 0xC0A0:
            case 0xC0B0:
            case 0xC0C0:
            case 0xC0D0:
            case 0xC0E0:
            case 0xC0F0: {
                // Device Select
                int slot = (address >> 4) & 0x7;
                switch(m->slot_cards[slot].slot_type) {
                    case SLOT_TYPE_DISKII: {
                            uint8_t soft_switch = address & 0x0f;
                            if(soft_switch <= IWM_PH3_ON) {
                                diskii_step_head(m, slot, soft_switch);
                                break;
                            }
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
                        break;

                    case SLOT_TYPE_SMARTPORT:
                        switch(address & 0x0f) {
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
                            fd80->bank = (address & 0x0C) >> 2;
                            return fd80->registers[address & 0x0F];
                        }
                        break;
                }
                break;
            }
        }
    }
    return byte;
}

static inline void apple2_softswitch_write_callback_IIe(APPLE2 *m, uint16_t address, uint8_t value) {
    uint8_t byte = m->read_pages.pages[address / PAGE_SIZE].bytes[address % PAGE_SIZE];

    // All other addresses
    switch(address & 0xC0F0) {
        case 0xC000:
            switch(address) {
                case CLR80STORE:
                    if(m->model) {
                        m->store80set = 0;
                        pages_map(&m->read_pages, 0x0400 / PAGE_SIZE, 0x0400 / PAGE_SIZE, &m->RAM_MAIN[0x0400]);
                        pages_map(&m->read_pages, 0x2000 / PAGE_SIZE, 0x2000 / PAGE_SIZE, &m->RAM_MAIN[0x2000]);
                        pages_map(&m->write_pages, 0x0400 / PAGE_SIZE, 0x0400 / PAGE_SIZE, &m->RAM_MAIN[0x0400]);
                        pages_map(&m->write_pages, 0x2000 / PAGE_SIZE, 0x2000 / PAGE_SIZE, &m->RAM_MAIN[0x2000]);
                    }
                    break;
                case SET80STORE:  //e
                    if(m->model) {
                        m->store80set = 1;
                        if(m->page2set) {
                            pages_map(&m->read_pages, 0x0400 / PAGE_SIZE, 0x0400 / PAGE_SIZE, &m->RAM_MAIN[0x10400]);
                            pages_map(&m->read_pages, 0x2000 / PAGE_SIZE, 0x2000 / PAGE_SIZE, &m->RAM_MAIN[0x12000]);
                            pages_map(&m->write_pages, 0x0400 / PAGE_SIZE, 0x0400 / PAGE_SIZE, &m->RAM_MAIN[0x10400]);
                            pages_map(&m->write_pages, 0x2000 / PAGE_SIZE, 0x2000 / PAGE_SIZE, &m->RAM_MAIN[0x12000]);
                        }
                    }
                    break;
                case CLRRAMRD: //e
                    if(m->model) {
                        m->ramrdset = 0;
                        if(m->store80set) {
                            pages_map(&m->read_pages, 0x0200 / PAGE_SIZE, 0x0200 / PAGE_SIZE, &m->RAM_MAIN[0x0200]);
                            if(m->screen_mode & SCREEN_MODE_HIRES) {
                                pages_map(&m->read_pages, 0x0800 / PAGE_SIZE, 0x1800 / PAGE_SIZE, &m->RAM_MAIN[0x0800]);
                                pages_map(&m->read_pages, 0x4000 / PAGE_SIZE, 0x8000 / PAGE_SIZE, &m->RAM_MAIN[0x2000]);
                            } else {
                                pages_map(&m->read_pages, 0x0800 / PAGE_SIZE, 0xB800 / PAGE_SIZE, &m->RAM_MAIN[0x0200]);
                            }
                        } else {
                            pages_map(&m->read_pages, 0x0200 / PAGE_SIZE, 0xBE00 / PAGE_SIZE, &m->RAM_MAIN[0x0200]);
                        }
                    }
                    break;
                case SETRAMRD: //e
                    if(m->model) {
                        m->ramrdset = 1;
                        if(m->store80set) {
                            pages_map(&m->read_pages, 0x0200 / PAGE_SIZE, 0x0200 / PAGE_SIZE, &m->RAM_MAIN[0x10200]);
                            if(m->screen_mode & SCREEN_MODE_HIRES) {
                                pages_map(&m->read_pages, 0x0800 / PAGE_SIZE, 0x1800 / PAGE_SIZE, &m->RAM_MAIN[0x10800]);
                                pages_map(&m->read_pages, 0x4000 / PAGE_SIZE, 0x8000 / PAGE_SIZE, &m->RAM_MAIN[0x12000]);
                            } else {
                                pages_map(&m->read_pages, 0x0800 / PAGE_SIZE, 0xB800 / PAGE_SIZE, &m->RAM_MAIN[0x10200]);
                            }
                        } else {
                            pages_map(&m->read_pages, 0x0200 / PAGE_SIZE, 0xBE00 / PAGE_SIZE, &m->RAM_MAIN[0x10200]);
                        }
                    }
                    break;
                case CLRRAMWRT: //e
                    if(m->model) {
                        m->ramwrtset = 0;
                        if(m->store80set) {
                            pages_map(&m->write_pages, 0x0200 / PAGE_SIZE, 0x0200 / PAGE_SIZE, &m->RAM_MAIN[0x0200]);
                            if(m->screen_mode & SCREEN_MODE_HIRES) {
                                pages_map(&m->write_pages, 0x0800 / PAGE_SIZE, 0x1800 / PAGE_SIZE, &m->RAM_MAIN[0x0800]);
                                pages_map(&m->write_pages, 0x4000 / PAGE_SIZE, 0x8000 / PAGE_SIZE, &m->RAM_MAIN[0x2000]);
                            } else {
                                pages_map(&m->write_pages, 0x0800 / PAGE_SIZE, 0xB800 / PAGE_SIZE, &m->RAM_MAIN[0x0200]);
                            }
                        } else {
                            pages_map(&m->write_pages, 0x0200 / PAGE_SIZE, 0xBE00 / PAGE_SIZE, &m->RAM_MAIN[0x0200]);
                        }
                    }
                    break;
                case SETRAMWRT: //e
                    if(m->model) {
                        m->ramwrtset = 1;
                        if(m->store80set) {
                            pages_map(&m->write_pages, 0x0200 / PAGE_SIZE, 0x0200 / PAGE_SIZE, &m->RAM_MAIN[0x10200]);
                            if(m->screen_mode & SCREEN_MODE_HIRES) {
                                pages_map(&m->write_pages, 0x0800 / PAGE_SIZE, 0x1800 / PAGE_SIZE, &m->RAM_MAIN[0x10800]);
                                pages_map(&m->write_pages, 0x4000 / PAGE_SIZE, 0x8000 / PAGE_SIZE, &m->RAM_MAIN[0x12000]);
                            } else {
                                pages_map(&m->write_pages, 0x0800 / PAGE_SIZE, 0xB800 / PAGE_SIZE, &m->RAM_MAIN[0x10200]);
                            }
                        } else {
                            pages_map(&m->write_pages, 0x0200 / PAGE_SIZE, 0xBE00 / PAGE_SIZE, &m->RAM_MAIN[0x10200]);
                        }
                    }
                    break;
                case CLRCXROM:  //e
                    if(m->model) {
                        m->cxromset = 0;
                        m->read_pages.pages[0xC100 / PAGE_SIZE].bytes = m->rom_shadow_pages[1];
                        m->read_pages.pages[0xC200 / PAGE_SIZE].bytes = m->rom_shadow_pages[2];
                        m->read_pages.pages[0xC400 / PAGE_SIZE].bytes = m->rom_shadow_pages[4];
                        m->read_pages.pages[0xC500 / PAGE_SIZE].bytes = m->rom_shadow_pages[5];
                        m->read_pages.pages[0xC600 / PAGE_SIZE].bytes = m->rom_shadow_pages[6];
                        m->read_pages.pages[0xC700 / PAGE_SIZE].bytes = m->rom_shadow_pages[7];
                        if(!m->c3romset) {
                            m->read_pages.pages[0xC300 / PAGE_SIZE].bytes = m->rom_shadow_pages[3];
                        }
                    }
                    break;
                case SETCXROM:  //e
                    if(m->model) {
                        m->cxromset = 1;
                        // C100-C2FF
                        pages_map(&m->read_pages, 0xC100 / PAGE_SIZE, 0x200 / PAGE_SIZE, &m->roms.blocks[ROM_APPLE2_SLOTS].bytes[0x100]);
                        // C400-CFFF
                        pages_map(&m->read_pages, 0xC400 / PAGE_SIZE, 0xC00 / PAGE_SIZE, &m->roms.blocks[ROM_APPLE2_SLOTS].bytes[0x400]);
                    }
                    break;
                case CLRALTZP:
                    if(m->model) {
                        m->altzpset = 0;
                        // 0000-01ff
                        pages_map(&m->write_pages, 0x0000 / PAGE_SIZE, 0x0200 / PAGE_SIZE, &m->RAM_MAIN[0x0000]);
                    }
                    break;
                case SETALTZP:
                    if(m->model) {
                        m->altzpset = 1;
                        // 10000-101ff
                        pages_map(&m->write_pages, 0x0000 / PAGE_SIZE, 0x0200 / PAGE_SIZE, &m->RAM_MAIN[0x10000]);
                    }
                    break;
                case CLRC3ROM: //e
                    if(m->model) {
                        m->c3romset = 0;
                        // C300-C3FF
                        m->read_pages.pages[0xC300 / PAGE_SIZE].bytes = m->rom_shadow_pages[3];
                    }
                    break;
                case SETC3ROM: // e
                    if(m->model) {
                        m->c3romset = 1;
                        // C300-C3FF
                        pages_map(&m->read_pages, 0xC300 / PAGE_SIZE, 0x100 / PAGE_SIZE, &m->roms.blocks[ROM_APPLE2_SLOTS].bytes[0x300]);
                        // SQW - Map C800 as well?
                        // pages_map(&m->read_pages, 0xC800 / PAGE_SIZE, 0x800 / PAGE_SIZE, &m->roms.blocks[ROM_APPLE2_SLOTS].bytes[0x800]);
                    }
                    break;
                case CLR80COL: //e
                    if(m->model) {
                        m->col80set = 0;
                    }
                    break;
                case SET80COL: //e
                    if(m->model) {
                        m->col80set = 1;
                    }
                    break;
                case CLRALTCHAR: // e
                    if(m->model) {
                        m->altcharset = 0;
                    }
                    break;
                case SETALTCHAR: // e
                    if(m->model) {
                        m->altcharset = 1;
                    }
                    break;
            }
            break;
        case 0xC010:
                if(m->clipboard_text) {
                    viewapl2_feed_clipboard_key(m);
                } else {
                    m->write_pages.pages[KBD / PAGE_SIZE].bytes[KBD % PAGE_SIZE] &= 0x7F;
                }
            break;
        case 0xC020:
            break;
        case 0xC030:
            speaker_toggle(&m->speaker);
            break;
        case 0xC040:
            // C40 strobe - don't know what that is
            break;
        case 0xC050:
            switch (address) {
                case TXTCLR:
                    m->screen_mode |= SCREEN_MODE_GRAPHICS;
                    break;
                case TXTSET:
                    m->screen_mode &= ~SCREEN_MODE_GRAPHICS;
                    break;
                case MIXCLR:
                    m->screen_mode &= ~SCREEN_MODE_MIXED;
                    break;
                case MIXSET:
                    m->screen_mode |= SCREEN_MODE_MIXED;
                    break;
                case CLRPAGE2:
                    if(m->store80set) {
                        pages_map(&m->read_pages, 0x0400 / PAGE_SIZE, 0x0400 / PAGE_SIZE, &m->RAM_MAIN[0x0400]);
                        pages_map(&m->write_pages, 0x0400 / PAGE_SIZE, 0x0400 / PAGE_SIZE, &m->RAM_MAIN[0x0400]);
                        if(m->screen_mode & SCREEN_MODE_HIRES) {
                            pages_map(&m->read_pages, 0x2000 / PAGE_SIZE, 0x2000 / PAGE_SIZE, &m->RAM_MAIN[0x2000]);
                            pages_map(&m->write_pages, 0x2000 / PAGE_SIZE, 0x2000 / PAGE_SIZE, &m->RAM_MAIN[0x2000]);
                        }
                    }
                    m->page2set = 0;
                    break;
                case SETPAGE2:
                    if(m->store80set) {
                        pages_map(&m->read_pages, 0x0400 / PAGE_SIZE, 0x0400 / PAGE_SIZE, &m->RAM_MAIN[0x10400]);
                        pages_map(&m->write_pages, 0x0400 / PAGE_SIZE, 0x0400 / PAGE_SIZE, &m->RAM_MAIN[0x10400]);
                        if(m->screen_mode & SCREEN_MODE_HIRES) {
                            pages_map(&m->read_pages, 0x2000 / PAGE_SIZE, 0x2000 / PAGE_SIZE, &m->RAM_MAIN[0x12000]);
                            pages_map(&m->write_pages, 0x2000 / PAGE_SIZE, 0x2000 / PAGE_SIZE, &m->RAM_MAIN[0x12000]);
                        }
                    }
                    m->page2set = 1;
                    break;
                case LORES: // SQW - rename to CLRHIRES
                    m->screen_mode &= ~SCREEN_MODE_HIRES;
                    if(m->store80set) {
                        pages_map(&m->read_pages, 0x2000 / PAGE_SIZE, 0x2000 / PAGE_SIZE, &m->RAM_MAIN[0x2000]);
                        pages_map(&m->write_pages, 0x2000 / PAGE_SIZE, 0x2000 / PAGE_SIZE, &m->RAM_MAIN[0x2000]);
                    }
                    break;
                case HIRES: // SQW - rename to SETHIRES
                    m->screen_mode |= SCREEN_MODE_HIRES;
                    if(m->store80set) {
                        pages_map(&m->read_pages, 0x2000 / PAGE_SIZE, 0x2000 / PAGE_SIZE, &m->RAM_MAIN[0x12000]);
                        pages_map(&m->write_pages, 0x2000 / PAGE_SIZE, 0x2000 / PAGE_SIZE, &m->RAM_MAIN[0x12000]);
                    }
                    break;
                case CLRAN0: // SQW - hires or dhires if text off 
                    break;
                case SETAN0: // SQW
                    break;
                case CLRAN1: // SQW
                    break;
                case SETAN1: // SQW
                    break;
                case CLRAN2: // SQW
                    break;
                case SETAN2: // SQW
                    break;
                case CLRDHGR: // SQW - dhires on if IOUD is on
                    break;
                case SETDHGR: // SQW - dhires off if IOUD is on
                    break;
            }
            break;
        case 0xC060:
            break;
        case 0xC070: 
            break;
        case 0xC080:
            ram_card(m, m->ramrdset, address, value);
            break;
        case 0xC090:
        case 0xC0A0:
        case 0xC0B0:
        case 0xC0C0:
        case 0xC0D0:
        case 0xC0E0:
        case 0xC0F0: {
            if(address == CLRROM) {
                m->mapped_slot = -1;
                for(int i = 1; i < 8; i++) {
                    m->slot_cards[i].cx_rom_mapped = 0;
                }
                // On a //e this C800-CFFF becomes rom, on a II+ it's floating bus (ram in my case)
                pages_map(&m->read_pages, 0xC800 / PAGE_SIZE, 0x800 / PAGE_SIZE, m->model ? &m->roms.blocks[ROM_APPLE2_SLOTS].bytes[0X800] : &m->RAM_MAIN[0xC800]);
            } else {
                int slot = (address >> 4) & 0x7;
                switch(m->slot_cards[slot].slot_type) {
                    case SLOT_TYPE_DISKII: {
                            uint8_t soft_switch = address & 0x0f;
                            if(soft_switch <= IWM_PH3_ON) {
                                diskii_step_head(m, slot, soft_switch);
                                break;
                            }
                            switch(soft_switch) {
                                case IWM_MOTOR_ON:
                                case IWM_MOTOR_OFF:
                                    diskii_motor(m, slot, soft_switch);
                                    break;

                                case IWM_SEL_DRIVE_1:
                                case IWM_SEL_DRIVE_2:
                                    diskii_drive_select(m, slot, soft_switch);
                                    break;

                                // SQW - Do these trigger with write?
                                case IWM_Q6_OFF:
                                case IWM_Q6_ON:
                                    diskii_q6_access(m, slot, soft_switch & 1);

                                case IWM_Q7_OFF:
                                case IWM_Q7_ON:
                                    diskii_q7_access(m, slot, soft_switch & 1);
                            }
                        }
                        break;

                        case SLOT_TYPE_SMARTPORT:
                        switch(address & 0x0F) {
                            case SP_DATA:
                                m->sp_device[slot].sp_buffer[m->sp_device[slot].sp_write_offset++] = value;
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
                        franklin_display_set(m, address, value);
                        break;
                    break;
                }
            }
        }
    }
}
