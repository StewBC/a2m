// Apple ][+ emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#pragma once

#define RAM_SIZE        (64*1024)
#define PAGE_SIZE       (256)
#define NUM_PAGES       ((RAM_SIZE)/(PAGE_SIZE))

/* A MEMORY_BLOCK tracks a block of bytes, be that ROM, MEMORY or IO Port*/
typedef struct MEMORY_BLOCK {
    uint32_t address;
    uint32_t length;
    uint8_t  *bytes;
} MEMORY_BLOCK;

/* MEMORY contains an array of MEMORY_BLOCKs which may (or not) be mapped into the 6502's 64K*/
typedef struct MEMORY {
    MEMORY_BLOCK *blocks;
    uint16_t num_blocks;
} MEMORY;

/* PAGE points to a block of bytes, with a length of PAGE_SIZE (implied) */
typedef struct PAGE {
    uint8_t *bytes;
} PAGE;

/* PAGES is an array of PAGE structures.  These can be mapped (or not) into teh 6502 address space */
typedef struct PAGES {
    PAGE *pages;
    uint16_t num_pages;
} PAGES;

/* The 6502 internals*/
typedef struct CPU {
    uint16_t    pc;             // Program write_enable
    uint16_t    sp;             // Stack pointer
    uint8_t     A, X, Y;        // 8 bit registers
    union {
        struct {
            uint8_t     C: 1;   // carry
            uint8_t     Z: 1;   // zero
            uint8_t     I: 1;   // Interrupt Disable
            uint8_t     D: 1;   // BCD mode
            uint8_t     B: 1;   // Break
            uint8_t     E: 1;   // Extra (almost unused)
            uint8_t     V: 1;   // Overflow
            uint8_t     N: 1;   // Negative
        };
    uint8_t flags;
    };
    union {
        struct {
            uint8_t address_lo;
            uint8_t address_hi;
        } ;
        uint16_t address_16;    // For Emulation - Usually where bytes will be fetched
    };
    union {
        struct {
            uint8_t scratch_lo;
            uint8_t scratch_hi;
        } ;
        uint16_t scratch_16;    // For Emulation - Placeholder
    };
    struct {
        uint8_t page_fault: 1;  // During stages where a page-fault could happen, denotes fault
    };
    uint8_t instruction;        // Current instruction being executed
    int16_t instruction_cycle;  // Stage (value 0, instruction fetched, is cycle 1 of execution, -1 need instruction)
    size_t  cycles;            // Total count of cycles the cpu has executed
} CPU;

// Forward declarations
struct APPLE2;

// Function pointer prototype that point at the steps, each individual cycle, of a 6502 instruction
typedef void (*opcode_steps)(APPLE2 *m);

// The 256 possible opcodes
extern  opcode_steps *opcodes[256];
// The UNDEFINED step (cycle) is for the unimplemented opcodes
extern  opcode_steps UNDEFINED[];

// Configure the ram, MEMORY and bytes setup (what is mapped in)
uint8_t memory_init(MEMORY *memory, uint16_t num_blocks);
void    memory_add(MEMORY *memory, uint8_t block_num, uint32_t address, uint32_t length, uint8_t *bytes);
uint8_t pages_init(PAGES *pages, uint16_t num_pages);
void    pages_map(PAGES *pages, uint32_t start_page, uint32_t num_pages, uint8_t *bytes);
void    pages_map_memory_block(PAGES *pages, MEMORY_BLOCK *block);

// 1 time init
void    cpu_init(CPU *cpu);

// Step the apple2 a single CPU cycle
void    machine_step(APPLE2 *m);

// Helper calls that access the mapped in memory
uint8_t read_from_memory(APPLE2 *m, uint16_t address);
// This call for the debugger - doesn't trigger IO ports
uint8_t read_from_memory_debug(APPLE2 *m, uint16_t address);
void    write_to_memory(APPLE2 *m, uint16_t address, uint8_t value);
