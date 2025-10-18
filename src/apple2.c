// Apple ][+ and //e Emhanced emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#include "header.h"

// Set MACHINE up as an Apple II
int apple2_configure(APPLE2 *m) {

    // Clear the whole emulated machine
    memset(m, 0, sizeof(APPLE2));
    // Init the ini config storage
    ini_init(&m->ini_store);
    // Load config from ini file
    if(A2_OK != util_ini_load_file("./apple2.ini", ini_add, (void *)&m->ini_store)) {
        ini_add(&m->ini_store, "Machine", "Model", "apple2_ee ; apple2_plus | apple2_ee (//e Enhanced)");
        ini_add(&m->ini_store, "Display", "scale", "1.0 ; Uniformly scale Application Window");
        ini_add(&m->ini_store, "Display", "disk_leds", "1 ; Show disk activity LEDs");
        ini_add(&m->ini_store, "Video", ";slot", "3 ; Slot where an apple2_plus 80 col card is inserted ");
        ini_add(&m->ini_store, "Video", ";device", "Franklin Ace Display ; 80 Column Videx like card");
        ini_add(&m->ini_store, "DiskII", "slot", "6 ; This says a slot contains a disk II controller");
        ini_add(&m->ini_store, "DiskII", "disk0", "; file name of a NIB floppy image, NOT in quotes");
        ini_add(&m->ini_store, "DiskII", "disk1", "; ./disks/Apple DOS 3.3 January 1983.nib ; example usage");
        ini_add(&m->ini_store, "SmartPort", "slot", "5 ; This says a slot contains a smartport");
        ini_add(&m->ini_store, "SmartPort", "disk0", " ; file name of an image, NOT in quotes");
        ini_add(&m->ini_store, "SmartPort", "disk1", "");
        ini_add(&m->ini_store, "SmartPort", "boot", "0 ; any value other than 0 will cause a boot of disk0");
        ini_add(&m->ini_store, "SmartPort", "slot", "7 ; There can be multiple slots");
        ini_add(&m->ini_store, "SmartPort", "disk0", "");
        ini_add(&m->ini_store, "SmartPort", "disk1", "");
        ini_add(&m->ini_store, "SmartPort", "boot", "0 ; last listed non-zero boot devices' disk0 will boot");
        util_ini_save_file("apple2.ini", &m->ini_store);
    }
    // Configure the type of machine (II+ or //e Enhanced, based on ini_store)
    apple2_machine_setup(m);
    // Allocate the RAM
    m->RAM_MAIN = (uint8_t *) malloc(m->ram_size);
    memset(&m->RAM_MAIN[0xc000], 0, 0x100); // SQW Hack till I get all softswitches working
    // $E4 (at least) needs to be FF and doesn't seem to be set so I am setting all of
    // zero page to FF instead of random memory
    memset(&m->RAM_MAIN[0x0], 0xff, 0x100);
    m->RAM_WATCH = (uint8_t *) malloc(m->ram_size);
    if(!m->RAM_MAIN || !m->RAM_WATCH) {
        return A2_ERR;
    }
    // INIT the ram_card
    if(A2_OK != ram_card_init(&m->ram_card)) {
        return A2_ERR;
    }
    // RAM
    if(!memory_init(&m->ram, m->model ? 2 : 1)) {
        return A2_ERR;
    }
    memory_add(&m->ram, 0, 0x0000, BANK_SIZE, &m->RAM_MAIN[0]);
    if(m->model) {
        memory_add(&m->ram, 1, 0x0000, BANK_SIZE, &m->RAM_MAIN[BANK_SIZE]);
    }
    // ROMS
    if(!memory_init(&m->roms, ROM_NUM_ROMS)) {
        return A2_ERR;
    }
    if(!m->model) {
        memory_add(&m->roms, ROM_APPLE2, 0xD000, a2p_rom_size, a2p_rom);
        memory_add(&m->roms, ROM_APPLE2_CHARACTER, 0x0000, a2p_character_rom_size, a2p_character_rom);
    } else {
        memory_add(&m->roms, ROM_APPLE2, 0xD000, 0x3000, &a2ee_rom[0X1000]);
        memory_add(&m->roms, ROM_APPLE2_SLOTS, 0xC100, 0xF00, &a2ee_rom[0X100]);
        memory_add(&m->roms, ROM_APPLE2_CHARACTER, 0x0000, a2ee_character_rom_size, a2ee_character_rom);
    }
    memory_add(&m->roms, ROM_DISKII_13SECTOR, 0xC000, 256, disk2_rom[DSK_ENCODING_13SECTOR]);
    memory_add(&m->roms, ROM_DISKII_16SECTOR, 0xC000, 256, disk2_rom[DSK_ENCODING_16SECTOR]);
    memory_add(&m->roms, ROM_SMARTPORT, 0xC000, smartport_rom_size, smartport_rom);
    memory_add(&m->roms, ROM_FRANKLIN_ACE_DISPLAY, 0x0000, franklin_ace_display_rom_size, franklin_ace_display_rom);
    memory_add(&m->roms, ROM_FRANKLIN_ACE_CHARACTER, 0x0000, franklin_ace_character_rom_size, franklin_ace_character_rom);

    // PAGES
    if(!pages_init(&m->read_pages, BANK_SIZE / PAGE_SIZE)) {
        return A2_ERR;
    }
    if(!pages_init(&m->write_pages, BANK_SIZE / PAGE_SIZE)) {
        return A2_ERR;
    }
    if(!pages_init(&m->watch_pages, BANK_SIZE / PAGE_SIZE)) {
        return A2_ERR;
    }
    // Map main write ram
    pages_map(&m->write_pages, 0, BANK_SIZE / PAGE_SIZE, m->ram.blocks[MAIN_RAM].bytes);

    // Map read ram (same as write ram in this case)
    pages_map(&m->read_pages, 0, BANK_SIZE / PAGE_SIZE, m->ram.blocks[MAIN_RAM].bytes);

    // Map the rom ($D000-$FFFF) as read pages
    pages_map_memory_block(&m->read_pages, &m->roms.blocks[ROM_APPLE2]);

    // Install the watch callbacks
    m->callback_write = apple2_softswitch_write_callback;
    m->callback_read = apple2_softswitch_read_callback;
    m->callback_breakpoint = breakpoint_callback;

    // Map watch area checks - start with no watch
    memset(m->RAM_WATCH, 0, BANK_SIZE);

    // Add the IO ports by flagging them as 1 in the RAM watch
    memset(m->RAM_WATCH + 0xC001, 1, 0xFE);                 // Skip KBD
    // Watch the location to clear slot rom
    m->RAM_WATCH[CLRROM] = 1;

    // Set up the Watch Pages (RAM_WATCH can be changed without re-doing the map)
    pages_map(&m->watch_pages, 0, BANK_SIZE / PAGE_SIZE, m->RAM_WATCH);

    // Configure the LC using the same function the soft switches would, so the
    // same way, meaning call 2x to enable ROM and WRITE
    ram_card(m, 0xC081, 0x100);
    ram_card(m, 0xC081, 0x100);

    // Init the CPU to cold-start by jumping to ROM address at 0xfffc
    cpu_init(m);

    // Configure the slots (based on ini_store)
    apple2_slot_setup(m);

    return A2_OK;
}

void apple2_machine_setup(APPLE2 *m) {
    INI_SECTION *s;
    int slot_number = -1;

    m->model = MODEL_APPLE_IIEE;
    m->cpu.class = CPU_65c02;
    m->ram_size = 128 * 1024;

    s = ini_find_section(&m->ini_store, "machine");
    if(s) {
        for(int i = 0; i < s->kv.items; i++) {
            INI_KV *kv = ARRAY_GET(&s->kv, INI_KV, i);
            const char *key = kv->key;
            const char *val = kv->val;
            if(0 == stricmp(key, "model")) {
                if(0 == stricmp(val, "apple2_plus")) {
                    m->model = MODEL_APPLE_II_PLUS;
                    m->cpu.class = CPU_6502;
                    m->ram_size = 64 * 1024;
                }
            }
        }
    }
}

void apple2_slot_setup(APPLE2 *m) {
    INI_SECTION *s;
    int slot_number = -1;

    s = ini_find_section(&m->ini_store, "diskii");
    if(s) {
        for(int i = 0; i < s->kv.items; i++) {
            INI_KV *kv = ARRAY_GET(&s->kv, INI_KV, i);
            const char *key = kv->key;
            const char *val = kv->val;
            if(0 == stricmp(key, "slot")) {
                sscanf(val, "%d", &slot_number);
                if(slot_number >= 1 && slot_number < 8) {
                    slot_add_card(m, slot_number, SLOT_TYPE_DISKII, &m->diskii_controller[slot_number],
                                  m->roms.blocks[ROM_DISKII_16SECTOR].bytes, NULL);
                    m->diskii_controller[slot_number].diskii_drive[0].quarter_track_pos = rand() % DISKII_QUATERTRACKS;
                    m->diskii_controller[slot_number].diskii_drive[1].quarter_track_pos = rand() % DISKII_QUATERTRACKS;
                }
            } else if(slot_number >= 0 && slot_number < 8) {
                if(0 == stricmp(key, "disk0")) {
                    diskii_mount(m, slot_number, 0, val);
                } else if(0 == stricmp(key, "disk1")) {
                    diskii_mount(m, slot_number, 1, val);
                }
            }
        }
    }

    s = ini_find_section(&m->ini_store, "smartport");
    if(s) {
        for(int i = 0; i < s->kv.items; i++) {
            INI_KV *kv = ARRAY_GET(&s->kv, INI_KV, i);
            const char *key = kv->key;
            const char *val = kv->val;
            if(0 == stricmp(key, "slot")) {
                sscanf(val, "%d", &slot_number);
                if(slot_number >= 1 && slot_number < 8) {
                    slot_add_card(m, slot_number, SLOT_TYPE_SMARTPORT, &m->sp_device[slot_number],
                                  &m->roms.blocks[ROM_SMARTPORT].bytes[slot_number * 0x100], NULL);
                }
            } else if(slot_number >= 0 && slot_number < 8) {
                if(0 == stricmp(key, "disk0")) {
                    sp_mount(m, slot_number, 0, val);
                } else if(0 == stricmp(key, "disk1")) {
                    sp_mount(m, slot_number, 1, val);
                } else if(0 == stricmp(key, "boot")) {
                    if(0 != strcmp(val, "0") && m->sp_device[slot_number].sp_files[0].is_file_open) {
                        // The rom doesn't get a chance to run but fortunately ProDOS does just fine
                        m->cpu.pc = 0xc000 + slot_number * 0x100;
                    }
                }
            }
        }
    }

    s = ini_find_section(&m->ini_store, "smartport");
    if(s) {
        for(int i = 0; i < s->kv.items; i++) {
            INI_KV *kv = ARRAY_GET(&s->kv, INI_KV, i);
            const char *key = kv->key;
            const char *val = kv->val;
            if(0 == stricmp(key, "slot")) {
                sscanf(val, "%d", &slot_number);
            }
            if(0 == stricmp(key, "device")) {
                if(slot_number >= 1 && slot_number < 8) {
                    if(A2_OK == franklin_display_init(&m->franklin_display)) {
                        slot_add_card(m, slot_number, SLOT_TYPE_VIDEX_API, &m->franklin_display,
                                      &m->roms.blocks[ROM_FRANKLIN_ACE_DISPLAY].bytes[0x600], franklin_display_map_cx_rom);
                        memset(m->RAM_WATCH + 0xCC00, 1, 0x200);
                    }
                }
            }
        }
    }
}

// Clean up the Apple II
void apple2_shutdown(APPLE2 *m) {
    speaker_shutdown(&m->speaker);
    ram_card_shutdown(&m->ram_card);
    diskii_shutdown(m);
    free(m->RAM_MAIN);
    m->RAM_MAIN = 0;
    free(m->RAM_WATCH);
    m->RAM_WATCH = 0;
    m->viewport = 0;
}

// Handle the Apple II sofswitches when read
uint8_t apple2_softswitch_read_callback(APPLE2 *m, uint16_t address) {
    if(address >= 0xC080 && address <= 0xC08F) {
        // Ram card address
        ram_card(m, address, 0x100);
    } else if(address >= 0xc090 && address <= 0xc0FF) {
        int slot = (address >> 4) & 0x7;
        switch(m->slot_cards[slot].slot_type) {
            case SLOT_TYPE_DISKII: {
                    uint8_t soft_switch = address & 0x0f;
                    if(soft_switch <= IWM_PH3_ON) {
                        diskii_step_head(m, slot, soft_switch);
                        return 0xff;
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
    } else if(address >= 0xc100 && address <= 0xcFFE) {
        // Map the C800 ROM based on access to Cs00, if card provides a C800 ROM
        int slot = (address >> 8) & 0x7;
        if(!m->model || !((slot != 3 && m->cxromset) || (slot == 3 && m->c3romset))) {
            if(!m->slot_cards[slot].cx_rom_mapped && m->slot_cards[slot].slot_map_cx_rom) {
                m->slot_cards[slot].slot_map_cx_rom(m, address);
                m->slot_cards[slot].cx_rom_mapped = 1;
            }
        }
    } else {
        // Everything else
        switch(address) {
            case KBD:
                break;
            case SET80STORE:  //e
                if(m->model) {

                }
                break;
            case CLRCXROM:  //e
                if(m->model) {
                    m->cxromset = 0;
                    pages_map(&m->read_pages, 0xC100 / PAGE_SIZE, 0x200 / PAGE_SIZE, &m->RAM_MAIN[0xC100]);
                    pages_map(&m->read_pages, 0xC400 / PAGE_SIZE, 0x400 / PAGE_SIZE, &m->RAM_MAIN[0xC400]);
                }
                break;
            case SETCXROM:  //e
                if(m->model) {
                    m->cxromset = 1;
                    pages_map(&m->read_pages, 0xC100 / PAGE_SIZE, 0x200 / PAGE_SIZE, m->roms.blocks[ROM_APPLE2_SLOTS].bytes);
                    pages_map(&m->read_pages, 0xC400 / PAGE_SIZE, 0xC00 / PAGE_SIZE, &m->roms.blocks[ROM_APPLE2_SLOTS].bytes[0x300]);
                }
                break;
            case CLRC3ROM: //e
                if(m->model) {
                    m->c3romset = 0;
                    pages_map(&m->read_pages, 0xC300 / PAGE_SIZE, 0x100 / PAGE_SIZE, &m->RAM_MAIN[0xC300]);
                }
                break;
            case SETC3ROM: // e
                if(m->model) {
                    m->c3romset = 1;
                    // The slot starts at 0xc100 so + 0x200 = 0xc300
                    pages_map(&m->read_pages, 0xC300 / PAGE_SIZE, 0x100 / PAGE_SIZE, &m->roms.blocks[ROM_APPLE2_SLOTS].bytes[0x200]);
                }
                break;
            case KBDSTRB:
                m->write_pages.pages[KBD / PAGE_SIZE].bytes[KBD % PAGE_SIZE] &= 0x7F;
                break;
            case RDCXROM:   //e
                if(m->model) {
                    return (m->cxromset << 7);
                }
                break;
            case RDC3ROM:   //e
                if(m->model) {
                    return (m->c3romset << 7);
                }
                break;
            case RD80COL:   //e
                if(m->model) {
                }
                break;
            case RDPAGE2:   //e
                if(m->model) {
                }
                break;
            case A2SPEAKER:
                speaker_toggle(&m->speaker);
                break;
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
            case LOWSCR:
                m->active_page = 0;
                break;
            case HISCR:
                m->active_page = -1;
                break;
            case LORES:
                m->screen_mode &= ~SCREEN_MODE_HIRES;
                break;
            case HIRES:
                m->screen_mode |= SCREEN_MODE_HIRES;
                break;
            case SETAN0: // SQW
                break;
            case SETAN1: // SQW
                break;
            case CLRAN2: // SQW
                break;
            case CLRAN3: // SQW
                break;
            case BUTN0:
                return m->open_apple;
                break;
            case BUTN1:
                return m->closed_apple;
                break;
            case PADDL0:
            case PADDL1:
            case PADDL2:
            case PADDL3:
            case PTRIG: // read SDL joystick here and record cycle counter
                return 255;
            case CLRROM: {
                    for(int i = 1; i < 8; i++) {
                        m->slot_cards[i].cx_rom_mapped = 0;
                    }
                    pages_map(&m->read_pages, 0xC800 / PAGE_SIZE, 0x800 / PAGE_SIZE, &m->RAM_MAIN[0xC800]);
                }
                break;
            default:
                break;
        }
    }
    return m->read_pages.pages[address / PAGE_SIZE].bytes[address % PAGE_SIZE];
}

// Handle the Apple ][ softswitches when written to
void apple2_softswitch_write_callback(APPLE2 *m, uint16_t address, uint8_t value) {
    if(address >= 0xC080 && address <= 0xC08F) {
        // Ram card address
        ram_card(m, address, value);
        return;
    } else if(address >= 0xc080 && address <= 0xc0FF) {
        int slot = (address >> 4) & 0x7;
        switch(m->slot_cards[slot].slot_type) {
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
        }
    } else if(address == CLR80STORE) {
        // SQW
    } else if(address >= 0xCC00 && address < 0xCE00) {
        m->franklin_display.display_ram[(address & 0x01ff) + m->franklin_display.bank * 0x200] = value;
    }
    // Everything else
    m->read_pages.pages[address / PAGE_SIZE].bytes[address % PAGE_SIZE] = value;
    apple2_softswitch_read_callback(m, address);
}
