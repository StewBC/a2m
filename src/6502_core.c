// Apple ][+ emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#include "header.h"

// Configure a MEMORY
uint8_t memory_init(MEMORY *memory, uint16_t num_blocks) {
    int block;
    if(!(memory->blocks = (MEMORY_BLOCK *) malloc(sizeof(MEMORY_BLOCK) * num_blocks))) {
        return (memory->num_blocks = 0);
    }
    memory->num_blocks = num_blocks;
    for(block = 0; block < num_blocks; block++) {
        memory->blocks[block].address = 0;
        memory->blocks[block].length = 0;
        memory->blocks[block].bytes = NULL;
    }
    return 1;
}

void memory_add(MEMORY *memory, uint8_t block_num, uint32_t address, uint32_t length, uint8_t *bytes) {
    assert(block_num < memory->num_blocks);
    MEMORY_BLOCK *b = &memory->blocks[block_num];
    b->address = address;
    b->length = length;
    b->bytes = bytes;
}

// Configure PAGES
uint8_t pages_init(PAGES *pages, uint16_t num_pages) {
    int page;
    if(!(pages->pages = (PAGE *) malloc(sizeof(PAGE) * num_pages))) {
        return (pages->num_pages = 0);
    }
    pages->num_pages = num_pages;
    for(page = 0; page < num_pages; page++) {
        pages->pages[page].bytes = NULL;
    }
    return 1;
}

void pages_map(PAGES *pages, uint32_t start_page, uint32_t num_pages, uint8_t *bytes) {
    assert(start_page + num_pages <= pages->num_pages);
    while(num_pages) {
        pages->pages[start_page++].bytes = bytes;
        bytes += PAGE_SIZE;
        num_pages--;
    }
}

void pages_map_memory_block(PAGES *pages, MEMORY_BLOCK *block) {
    uint16_t start_page = block->address / PAGE_SIZE;
    uint16_t num_pages = block->length / PAGE_SIZE;
    uint8_t *bytes = block->bytes;
    assert(start_page + num_pages <= pages->num_pages);
    while(num_pages) {
        pages->pages[start_page++].bytes = bytes;
        bytes += PAGE_SIZE;
        num_pages--;
    }
}

// Init the 6502
void cpu_init(APPLE2 *m) {
    memset(&m->cpu, 0, sizeof(CPU));
    m->cpu.pc = read_from_memory_debug(m, 0xfffc) +  256 * read_from_memory_debug(m, 0xfffd);
    m->cpu.sp = 0x100;
}
