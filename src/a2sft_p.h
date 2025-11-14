// Apple ][+ and //e Emhanced emulator and assembler
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

static inline uint8_t apple2_softswitch_read_callback_IIplus(APPLE2 *m, uint16_t address) {
    uint8_t byte = m->read_pages.pages[address / PAGE_SIZE].bytes[address % PAGE_SIZE];

    if(address >= 0xc080 && address < 0xC090) {
        language_card(m, address, 0x100);
    } else if(address >= 0xc090 && address < 0xC100) {
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
    } else if(address >= 0xc100 && address < 0xCFFE) {
        // IO Select
        int slot = (address >> 8) & 0x7;
        // Only if slot isn't mapped, and only if ROM isn't active
        if(m->mapped_slot != slot && !(m->cxromset || (slot == 3 && m->c3slotrom))) {
            // Map the C800 ROM based on access to Cs00, if card provides a C800 ROM
            if(!m->slot_cards[slot].cx_rom_mapped && m->slot_cards[slot].slot_map_cx_rom) {
                m->slot_cards[slot].slot_map_cx_rom(m, address);
                m->slot_cards[slot].cx_rom_mapped = 1;
            }
            m->mapped_slot = slot;
        }
    } else if(address == CLRROM) {
        m->mapped_slot = 0;
        for(int i = 1; i < 8; i++) {
            m->slot_cards[i].cx_rom_mapped = 0;
        }
        // On a ][+ this C800-CFFF becomes "nothing" (RAM in my case)
        pages_map(&m->read_pages, 0xC800 / PAGE_SIZE, 0x800 / PAGE_SIZE, &m->RAM_MAIN[0XC800]);
    } else {
        switch(address) {
            case KBD:
                byte = m->RAM_MAIN[KBD];
                break;
            case KBDSTRB:
                if(m->clipboard_text) {
                    viewapl2_feed_clipboard_key(m);
                } else {
                    // I believe you get the key, pre-stobe-reset back
                    m->write_pages.pages[KBD / PAGE_SIZE].bytes[KBD % PAGE_SIZE] &= 0x7F;
                }
                break;
            case A2SPEAKER:
                speaker_toggle(&m->speaker);
                break;
            case TXTCLR:
                m->text = 0;
                break;
            case TXTSET:
                m->text = 1;
                break;
            case MIXCLR:
                m->mixed = 0;
                break;
            case MIXSET:
                m->mixed = 1;
                break;
            case CLRPAGE2:
                m->page2set = 0;
                break;
            case SETPAGE2:
                m->page2set = 1;
                break;
            case CLRHIRES:
                m->hires = 0;
                break;
            case SETHIRES:
                m->hires = 1;
                break;
            case BUTN0: {
                    uint8_t button = m->open_apple ? 0x80 : 0x00;
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
                    uint8_t button = m->closed_apple ? 0x80 : 0x00;
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
                        uint8_t val = clamp_u8(cycle_delta * 255 / paddl_normalized, 0, 255);
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
                        uint8_t val = clamp_u8(cycle_delta * 255 / paddl_normalized, 0, 255);
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
                        uint8_t val = clamp_u8(cycle_delta * 255 / paddl_normalized, 0, 255);
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
                        uint8_t val = clamp_u8(cycle_delta * 255 / paddl_normalized, 0, 255);
                        if(val >= v->axis_left_y[1]) {
                            byte = 0x0;
                            break;
                        }
                    }
                    byte = 0x80;
                }
            case PTRIG: {
                    VIEWPORT *v = m->viewport;
                    if(v && v->num_controllers) {
                        v->ptrig_cycle = m->cpu.cycles;
                    }
                }
                break;
        }
    }
    return byte;
}

static inline void apple2_softswitch_write_callback_IIplus(APPLE2 *m, uint16_t address, uint8_t value) {
    uint8_t byte = m->read_pages.pages[address / PAGE_SIZE].bytes[address % PAGE_SIZE];
    if(address >= 0xc080 && address < 0xC090) {
        language_card(m, address, value);
    } else if(address >= 0xc090 && address < 0xC100) {
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
    } else if(address >= 0xc100 && address < 0xC800) {
        // IO Select
        int slot = (address >> 8) & 0x7;
        // Only if slot isn't mapped, and only if ROM isn't active
        if(m->mapped_slot != slot) {
            // Map the C800 ROM based on access to Cs00, if card provides a C800 ROM
            if(!m->slot_cards[slot].cx_rom_mapped && m->slot_cards[slot].slot_map_cx_rom) {
                m->slot_cards[slot].slot_map_cx_rom(m, address);
                m->slot_cards[slot].cx_rom_mapped = 1;
            }
            m->mapped_slot = slot;
        }
    } else if(m->franklin80installed && address >= 0xCC00 && address < 0xCE00) {
        m->franklin_display.display_ram[(address & 0x01ff) + m->franklin_display.bank * 0x200] = value;
    } else if(address == CLRROM) {
        m->mapped_slot = 0;
        for(int i = 1; i < 8; i++) {
            m->slot_cards[i].cx_rom_mapped = 0;
        }
        // On a ][+ this C800-CFFF becomes "nothing" (RAM in my case)
        pages_map(&m->read_pages, 0xC800 / PAGE_SIZE, 0x800 / PAGE_SIZE, &m->RAM_MAIN[0XC800]);
    } else {
        // All other addresses
        switch(address) {
            case KBDSTRB:
                if(m->clipboard_text) {
                    viewapl2_feed_clipboard_key(m);
                } else {
                    m->write_pages.pages[KBD / PAGE_SIZE].bytes[KBD % PAGE_SIZE] &= 0x7F;
                }
                break;
            case A2SPEAKER:
                speaker_toggle(&m->speaker);
                break;
            case TXTCLR:
                m->text = 0;
                break;
            case TXTSET:
                m->text = 1;
                break;
            case MIXCLR:
                m->mixed = 0;
                break;
            case MIXSET:
                m->mixed = 1;
                break;
            case CLRPAGE2:
                m->page2set = 0;
                break;
            case SETPAGE2:
                m->page2set = 1;
                break;
            case CLRHIRES:
                m->hires = 0;
                break;
            case SETHIRES:
                m->hires = 1;
                break;
        }
    }
}
