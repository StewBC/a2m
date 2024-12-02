// Apple ][+ emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#include "header.h"

// Set MACHINE up as an Apple II
int apple2_configure(APPLE2 *m) {

    // Clear the whole emulated machine
    memset(m, 0, sizeof(APPLE2));

    // Allocate the RAM
    m->RAM_MAIN = (uint8_t *) malloc(RAM_SIZE);
    m->RAM_WATCH = (uint8_t *) malloc(RAM_SIZE);
    if(!m->RAM_MAIN || !m->RAM_WATCH) {
        return A2_ERR;
    }
    // INIT the ram_card
    if(A2_OK != ram_card_init(&m->ram_card)) {
        return A2_ERR;
    }
    // RAM
    if(!memory_init(&m->ram, 1)) {
        return A2_ERR;
    }
    memory_add(&m->ram, 0, 0x0000, RAM_SIZE, m->RAM_MAIN);

    // ROMS
    if(!memory_init(&m->roms, ROM_NUM_ROMS)) {
        return A2_ERR;
    }
    memory_add(&m->roms, ROM_APPLE, 0xD000, apple2_rom_size, apple2_rom);
    memory_add(&m->roms, ROM_CHARACTER, 0x0000, apple_character_rom_size, apple_character_rom);
    memory_add(&m->roms, ROM_SMARTPORT, 0xC000, smartport_rom_size, smartport_rom);
    memory_add(&m->roms, ROM_FRANKLIN_ACE_DISPLAY, 0x0000, franklin_ace_display_rom_size, franklin_ace_display_rom);
    memory_add(&m->roms, ROM_FRANKLIN_ACE_CHARACTER, 0x0000, franklin_ace_character_rom_size, franklin_ace_character_rom);

    // PAGES
    if(!pages_init(&m->read_pages, RAM_SIZE / PAGE_SIZE)) {
        return A2_ERR;
    }
    if(!pages_init(&m->write_pages, RAM_SIZE / PAGE_SIZE)) {
        return A2_ERR;
    }
    if(!pages_init(&m->watch_pages, RAM_SIZE / PAGE_SIZE)) {
        return A2_ERR;
    }
    // Map main write ram
    pages_map_memory_block(&m->write_pages, &m->ram.blocks[MAIN_RAM]);

    // Map read ram (same as write ram in this case)
    pages_map_memory_block(&m->read_pages, &m->ram.blocks[MAIN_RAM]);

    // Map the roms as read pages
    pages_map_memory_block(&m->read_pages, &m->roms.blocks[ROM_APPLE]);

    // Install the watch callbacks
    m->callback_write = apple2_softswitch_write_callback;
    m->callback_read = apple2_softswitch_read_callback;
    m->callback_breakpoint = breakpoint_callback;

    // Map watch area checks - start with no watch
    memset(m->RAM_WATCH, 0, RAM_SIZE);

    // Add the IO ports by flagging them as 1 in the RAM watch
    memset(m->RAM_WATCH + 0xC001, 1, 0xFE);                 // Skip KBD
    // Watch the location to clear slot rom
    m->RAM_WATCH[CLRROM] = 1;

    // Set up the Watch Pages (RAM_WATCH can be changed without re-doing the map)
    pages_map(&m->watch_pages, 0, RAM_SIZE / PAGE_SIZE, m->RAM_WATCH);

    // Init the CPU to cold-start by jumping to ROM at 0xfffc
    cpu_init(&m->cpu);

    // Should be obtained_spec.freq instead of SAMPLE_RATE, I think, but that doesn't work on macOS
    // The 1.5 is a fudge number to make sure the audio doesn't lag
    m->speaker.sample_rate = (CPU_FREQUENCY / SAMPLE_RATE) + 1.5f;
    m->speaker.current_rate = m->speaker.sample_rate;

    // Configure the LC using the same function the soft switches would, so the
    // same way, meaning call 2x to enable ROM and WRITE
    ram_card(m, 0xC081, 0x100);
    ram_card(m, 0xC081, 0x100);

    // Configure slots from the ini file
    if(A2_OK != util_ini_load_file("./apple2.ini", apple2_ini_load_callback, (void *) m)) {
        // If apple2.ini doesn't succesfully load, just add a smartport in slot 7
        slot_add_card(m, 7, SLOT_TYPE_SMARTPORT, &m->sp_device[7], &m->roms.blocks[ROM_SMARTPORT].bytes[0x700], NULL);
    }

    return A2_OK;
}

void apple2_ini_load_callback(void *user_data, char *section, char *key, char *value) {
    APPLE2 *m = (APPLE2 *) user_data;
    static int slot_number = -1;
    if(0 == stricmp(section, "smartport")) {
        if(0 == stricmp(key, "slot")) {
            sscanf(value, "%d", &slot_number);
            if(slot_number >= 1 && slot_number < 8) {
                slot_add_card(m, slot_number, SLOT_TYPE_SMARTPORT, &m->sp_device[slot_number],
                              &m->roms.blocks[ROM_SMARTPORT].bytes[slot_number * 0x100], NULL);
            }
        } else if(slot_number >= 0 && slot_number < 8) {
            if(0 == stricmp(key, "disk0")) {
                sp_mount(m, slot_number, 0, value);
            } else if(0 == stricmp(key, "disk1")) {
                sp_mount(m, slot_number, 1, value);
            } else if(0 == stricmp(key, "boot")) {
                if(0 != strcmp(value, "0") && m->sp_device[slot_number].sp_files[0].is_file_open) {
                    // The rom doesn't get a chance to run but fortunately ProDOS does just fine
                    m->cpu.pc = 0xc000 + slot_number * 0x100;
                    m->cpu.instruction_cycle = -1;
                }
            }
        }
    }
    // The 80 col display card
    if(0 == stricmp(section, "video")) {
        if(0 == stricmp(key, "slot")) {
            sscanf(value, "%d", &slot_number);
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

// Clean up the Apple II
void apple2_shutdown(APPLE2 *m) {
    ram_card_shutdown(&m->ram_card);
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
        switch (m->slot_cards[slot].slot_type) {
        case SLOT_TYPE_SMARTPORT:
            switch (address & 0x0f) {
            case SP_DATA:
                return m->sp_device[slot].sp_buffer[m->sp_device[slot].sp_read_offset++];
                break;
            case SP_STATUS:
                return m->sp_device[slot].sp_status;
                break;
            }
            break;

        case SLOT_TYPE_VIDEX_API:{
                FRANKLIN_DISPLAY *fd80 = &m->franklin_display;
                fd80->bank = (address & 0x0C) >> 2;
                return fd80->registers[address & 0x0F];
            }
            break;
        }
    } else if(address >= 0xc100 && address <= 0xcFFE) {
        // Map the C800 ROM based on access to Cs00, if card provides a C800 ROM
        int slot = (address >> 8) & 0x7;
        if(!m->slot_cards[slot].cx_rom_mapped && m->slot_cards[slot].slot_map_cx_rom) {
            m->slot_cards[slot].slot_map_cx_rom(m, address);
            m->slot_cards[slot].cx_rom_mapped = 1;
        }
    } else {
        // Everything else
        switch (address) {
        case KBD:
            break;
        case KBDSTRB:
            m->write_pages.pages[KBD / PAGE_SIZE].bytes[KBD % PAGE_SIZE] &= 0x7F;
            break;
        case A2SPEAKER:
            m->speaker.speaker_state = 1.0f - m->speaker.speaker_state;
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
        case PTRIG:
            return 255;
        case CLRROM:{
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
        switch (m->slot_cards[slot].slot_type) {
        case SLOT_TYPE_SMARTPORT:
            switch (address & 0x0F) {
            case SP_DATA:
                m->sp_device[slot].sp_buffer[m->sp_device[slot].sp_write_offset++] = value;
                return;
            case SP_STATUS:
                m->sp_device[slot].sp_read_offset = 0;
                m->sp_device[slot].sp_write_offset = 0;

                switch (m->sp_device[slot].sp_buffer[0]) {
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
    } else if(address >= 0xCC00 && address < 0xCE00) {
        m->franklin_display.display_ram[(address & 0x01ff) + m->franklin_display.bank * 0x200] = value;
    }
    // Everything else
    m->read_pages.pages[address / PAGE_SIZE].bytes[address % PAGE_SIZE] = value;
    apple2_softswitch_read_callback(m, address);
}
