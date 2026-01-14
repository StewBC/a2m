// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "common.h"
#include "hardware_lib.h"

size_t (*machine_run_opcode)(APPLE2 *m);

// Configure a ROM
uint8_t rom_init(ROMS *rom, uint16_t num_blocks) {
    int block;
    if(!(rom->blocks = (ROM_BLOCK *) malloc(sizeof(ROM_BLOCK) * num_blocks))) {
        return A2_ERR;
    }
    rom->num_blocks = num_blocks;
    for(block = 0; block < num_blocks; block++) {
        rom->blocks[block].address = 0;
        rom->blocks[block].length = 0;
        rom->blocks[block].bytes = NULL;
    }
    return A2_OK;
}

void rom_add(ROMS *rom, uint8_t block_num, uint32_t address, uint32_t length, uint8_t *bytes) {
    assert(block_num < rom->num_blocks);
    ROM_BLOCK *b = &rom->blocks[block_num];
    b->address = address;
    b->length = length;
    b->bytes = bytes;
}

// Configure PAGES
uint8_t pages_init(PAGES *pages, uint32_t length) {
    uint16_t num_pages = length / PAGE_SIZE;
    pages->num_pages = num_pages;
    pages->read_pages = (uint8_t**)malloc(sizeof(uint8_t*) * num_pages);
    pages->write_pages = (uint8_t**)malloc(sizeof(uint8_t*) * num_pages);
    pages->watch_read_pages = (uint8_t**)malloc(sizeof(uint8_t*) * num_pages);
    pages->watch_write_pages = (uint8_t**)malloc(sizeof(uint8_t*) * num_pages);
    pages->last_write_pages = (uint64_t**)malloc(sizeof(uint64_t*) * num_pages);
    if(!pages->read_pages || !pages->write_pages || !pages->watch_read_pages ||
        !pages->watch_write_pages || !pages->last_write_pages) {
            return A2_ERR;
    }

    for(int page = 0; page < num_pages; page++) {
        pages->read_pages[page] = NULL;
        pages->write_pages[page] = NULL;
        pages->watch_read_pages[page] = NULL;
        pages->watch_write_pages[page] = NULL;
        pages->last_write_pages[page] = NULL;
    }
    return A2_OK;
}

void pages_map(PAGES *pages, PAGE_MAP_TYPE map_type, uint32_t address, uint32_t length, RAM *ram) {
    uint32_t page = (address & 0xFFFF) / PAGE_SIZE;
    uint16_t num_pages = length / PAGE_SIZE;
    assert(page + num_pages <= pages->num_pages);
    if(map_type == PAGE_MAP_READ) {
        while(num_pages) {
            pages->read_pages[page] = &ram->RAM_MAIN[address];
            pages->watch_read_pages[page] = &ram->RAM_WATCH[address];
            page++;
            address += PAGE_SIZE;
            num_pages--;
        }
    } else {
        while(num_pages) {
            pages->write_pages[page] = &ram->RAM_MAIN[address];
            pages->watch_write_pages[page] = &ram->RAM_WATCH[address];
            pages->last_write_pages[page] = &ram->RAM_LAST_WRITE[address];
            page++;
            address += PAGE_SIZE;
            num_pages--;
        }
    }
}

void pages_map_lc(PAGES *pages, PAGE_MAP_TYPE map_type, uint32_t address, uint32_t length, uint32_t from, RAM *ram) {
    uint32_t page = address / PAGE_SIZE;
    uint16_t num_pages = length / PAGE_SIZE;
    if(map_type == PAGE_MAP_READ) {
        while(num_pages) {
            pages->read_pages[page] = &ram->RAM_LC[from];
            pages->watch_read_pages[page] = &ram->RAM_LC_WATCH[from];
            page++;
            from += PAGE_SIZE;
            num_pages--;
        }
    } else {
        while(num_pages) {
            pages->write_pages[page] = &ram->RAM_LC[from];
            pages->watch_write_pages[page] = &ram->RAM_LC_WATCH[from];
            pages->last_write_pages[page] = &ram->RAM_LC_LAST_WRITE[from];
            page++;
            from += PAGE_SIZE;
            num_pages--;
        }
    }
}


void pages_map_rom(PAGES *pages, uint32_t address, uint32_t length, uint8_t *rom_bytes, RAM *ram) {
    uint16_t page = address / PAGE_SIZE;
    uint16_t num_pages = length / PAGE_SIZE;
    uint8_t *bytes = rom_bytes;
    assert(page + num_pages <= pages->num_pages);
    while(num_pages) {
        pages->read_pages[page] = bytes;
        pages->watch_read_pages[page] = &ram->RAM_WATCH[address];
        page++;
        bytes += PAGE_SIZE;
        address += PAGE_SIZE;
        num_pages--;
    }
}

void pages_map_rom_block(PAGES *pages, ROM_BLOCK *block, RAM *ram) {
    uint16_t address = block->address;
    uint16_t page = address / PAGE_SIZE;
    uint16_t num_pages = block->length / PAGE_SIZE;
    uint8_t *bytes = block->bytes;
    assert(page + num_pages <= pages->num_pages);
    while(num_pages) {
        pages->read_pages[page] = bytes;
        pages->watch_read_pages[page] = &ram->RAM_WATCH[address];
        page++;
        bytes += PAGE_SIZE;
        address += PAGE_SIZE;
        num_pages--;
    }
}

// Init the 6502
void cpu_init(APPLE2 *m) {
    if(!m->model) { // 6502
        machine_run_opcode = machine_run_opcode_6502;
        opcode_text = opcode_text_6502;
        opcode_hex_params = opcode_hex_params_6502;
        opcode_symbol_params = opcode_symbol_params_6502;
        opcode_lengths = opcode_lengths_6502;
    } else { // 65c02
        machine_run_opcode = machine_run_opcode_65c02;
        opcode_text = opcode_text_65c02;
        opcode_hex_params = opcode_hex_params_65c02;
        opcode_symbol_params = opcode_symbol_params_65c02;
        opcode_lengths = opcode_lengths_65c02;
    }

    m->cpu.pc = read_from_memory_debug(m, 0xfffc) +  256 * read_from_memory_debug(m, 0xfffd);
    m->cpu.sp = 0x100;
}
