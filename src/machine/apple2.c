#include "apple2.h"
#include "diskii_rom.h"
#include "mboard.h"
#include "rom_data.h"
#include "smartport_rom.h"
#include "smrtprt.h"
#include "util_file.h"
#include "via6522.h"

#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static uint8_t apple2_bus_read(void *user, uint16_t address);
static void apple2_bus_write(void *user, uint16_t address, uint8_t value);

void apple2_pages_map_ram(apple2_t *m, bool for_write, uint32_t host_offset, uint32_t length)
{
    uint32_t page = (host_offset & 0xFFFFu) / APPLE2_PAGE_SIZE;
    uint32_t num_pages = length / APPLE2_PAGE_SIZE;
    uint32_t i;

    assert(m != NULL);
    assert(m->ram_main != NULL);
    assert((host_offset % APPLE2_PAGE_SIZE) == 0);
    assert((length % APPLE2_PAGE_SIZE) == 0);
    assert(host_offset + length <= APPLE2_RAM_MAIN_SIZE);
    assert(page + num_pages <= m->pages.num_pages);

    for (i = 0; i < num_pages; i++) {
        uint8_t *base = m->ram_main + host_offset + (i * APPLE2_PAGE_SIZE);
        if (for_write) {
            m->pages.write_pages[page + i] = base;
        } else {
            m->pages.read_pages[page + i] = base;
        }
    }
}

void apple2_pages_map_rom(apple2_t *m, uint16_t cpu_addr, uint32_t length, uint8_t *rom_bytes)
{
    uint32_t page = cpu_addr / APPLE2_PAGE_SIZE;
    uint32_t num_pages = length / APPLE2_PAGE_SIZE;
    uint32_t i;

    assert(m != NULL);
    assert(rom_bytes != NULL);
    assert((cpu_addr % APPLE2_PAGE_SIZE) == 0);
    assert((length % APPLE2_PAGE_SIZE) == 0);

    for (i = 0; i < num_pages; i++) {
        m->pages.read_pages[page + i] = rom_bytes + (i * APPLE2_PAGE_SIZE);
    }
}

void apple2_pages_map_lc(apple2_t *m, bool for_write, uint16_t cpu_addr, uint32_t length, uint32_t lc_offset)
{
    uint32_t page = cpu_addr / APPLE2_PAGE_SIZE;
    uint32_t num_pages = length / APPLE2_PAGE_SIZE;
    uint32_t i;

    assert(m != NULL);
    assert(m->ram_lc != NULL);
    assert(lc_offset + length <= APPLE2_RAM_LC_SIZE);

    for (i = 0; i < num_pages; i++) {
        uint8_t *base = m->ram_lc + lc_offset + (i * APPLE2_PAGE_SIZE);
        if (for_write) {
            m->pages.write_pages[page + i] = base;
        } else {
            m->pages.read_pages[page + i] = base;
        }
    }
}

void apple2_map_read_host(apple2_t *machine, uint16_t cpu_addr, uint32_t length, uint8_t *host)
{
    apple2_pages_map_rom(machine, cpu_addr, length, host);
}

void apple2_map_write_host(apple2_t *machine, uint16_t cpu_addr, uint32_t length, uint8_t *host)
{
    uint32_t page = cpu_addr / APPLE2_PAGE_SIZE;
    uint32_t num_pages = length / APPLE2_PAGE_SIZE;
    uint32_t i;

    assert(machine != NULL);
    assert(host != NULL);
    for (i = 0; i < num_pages; i++) {
        machine->pages.write_pages[page + i] = host + (i * APPLE2_PAGE_SIZE);
    }
}

void apple2_map_ram_offset(apple2_t *machine, bool for_write, uint32_t host_offset, uint32_t length)
{
    apple2_pages_map_ram(machine, for_write, host_offset, length);
}

static uint8_t apple2_irq_pending(void *user)
{
    apple2_t *m = (apple2_t *)user;
    if (m == NULL || m->mb_slot == 0) {
        return 0;
    }
    return mockingboard_irq_pending(m);
}

static void apple2_record_write_history(apple2_t *m, uint16_t address)
{
    if (m == NULL || m->write_history == NULL) {
        return;
    }
    m->write_history[address] =
        (m->write_history[address] << 16) | (uint64_t)m->cpu.cpu.opcode_pc;
}

static void apple2_report_memory_access(
    apple2_t *m,
    apple2_memory_access_type access,
    uint16_t address,
    uint8_t value)
{
    if (m != NULL && access == APPLE2_MEMORY_ACCESS_WRITE) {
        apple2_record_write_history(m, address);
    }
    if (m != NULL && m->memory_access != NULL) {
        m->memory_access(m->memory_access_user, access, address, value);
    }
    if (m != NULL && m->cpu_observer.access != NULL) {
        m->cpu_observer.access(
            m->cpu_observer_user,
            m->cpu.cpu.cycles,
            address,
            value,
            m->cpu.bus_access_kind);
    }
}

uint64_t apple2_debug_read_write_history(const apple2_t *machine, uint16_t address)
{
    if (machine == NULL || machine->write_history == NULL) {
        return 0u;
    }
    return machine->write_history[address];
}

void apple2_set_cpu_observer(
    apple2_t *machine,
    const apple2_cpu_observer *observer,
    void *user)
{
    if (machine == NULL) {
        return;
    }
    if (observer != NULL) {
        machine->cpu_observer = *observer;
        machine->cpu_observer_user = user;
    } else {
        memset(&machine->cpu_observer, 0, sizeof(machine->cpu_observer));
        machine->cpu_observer_user = NULL;
    }
}

static void apple2_observer_begin(
    apple2_t *m,
    apple2_cpu_observer_record_kind kind)
{
    apple2_cpu_observer_begin begin;

    if (m == NULL || m->cpu_observer.begin == NULL) {
        return;
    }
    begin.kind = kind;
    begin.machine_cycle = m->cpu.cpu.cycles;
    begin.pc = m->cpu.cpu.pc;
    begin.a = m->cpu.cpu.A;
    begin.x = m->cpu.cpu.X;
    begin.y = m->cpu.cpu.Y;
    begin.sp = (uint8_t)(m->cpu.cpu.sp & 0xFFu);
    begin.p = m->cpu.cpu.flags;
    m->cpu_observer.begin(m->cpu_observer_user, &begin);
}

static void apple2_observer_complete(apple2_t *m)
{
    if (m != NULL && m->cpu_observer.complete != NULL) {
        m->cpu_observer.complete(m->cpu_observer_user);
    }
}

static uint8_t apple2_bus_read(void *user, uint16_t address)
{
    apple2_t *m = (apple2_t *)user;
    uint16_t page = (uint16_t)(address / APPLE2_PAGE_SIZE);
    uint16_t offset = (uint16_t)(address % APPLE2_PAGE_SIZE);
    uint8_t value;

    if (address >= 0xC000 && address < 0xC100) {
        value = softswitch_c0_read(m, address);
        apple2_report_memory_access(m, APPLE2_MEMORY_ACCESS_READ, address, value);
        return value;
    }
    if (address == SS_CLRROM) {
        m->strobed_slot = -1;
        softswitch_apply_full_map(m);
    }
    if (address >= 0xC100 && address < 0xC800) {
        int slot = (address >> 8) & 7;
        softswitch_slot_io_select(m, address);
        /*
         * When INTCXROM (SETCXROM / $C007) is active, internal $C100-$CFFF ROM
         * hides slot-card I/O ports — same rule as original a2m (6502_inln.h
         * clears WATCH_IO_PORT under A2S_CXSLOTROM_MB_ENABLE). Without this,
         * default Mockingboard in slot 4 makes $C4xx look like VIA regs, and
         * a2audit CXXX classifies C400-C7FF as "?" (E000B).
         */
        if (!(m->state_flags & A2S_CXSLOTROM_MB_ENABLE) &&
            m->slot_type[slot] == SLOT_TYPE_MOCKINGBOARD) {
            value = mockingboard_read_cn(
                m,
                &m->mockingboard[slot],
                slot,
                address,
                (uint8_t)(address & 0xFF));
            apple2_report_memory_access(m, APPLE2_MEMORY_ACCESS_READ, address, value);
            return value;
        }
    }

    value = m->pages.read_pages[page][offset];
    apple2_report_memory_access(m, APPLE2_MEMORY_ACCESS_READ, address, value);
    return value;
}

static void apple2_bus_write(void *user, uint16_t address, uint8_t value)
{
    apple2_t *m = (apple2_t *)user;
    uint16_t page = (uint16_t)(address / APPLE2_PAGE_SIZE);
    uint16_t offset = (uint16_t)(address % APPLE2_PAGE_SIZE);

    if (address >= 0xC000 && address < 0xC100) {
        softswitch_c0_write(m, address, value);
        apple2_report_memory_access(m, APPLE2_MEMORY_ACCESS_WRITE, address, value);
        return;
    }
    if (address == SS_CLRROM) {
        m->strobed_slot = -1;
        softswitch_apply_full_map(m);
        apple2_report_memory_access(m, APPLE2_MEMORY_ACCESS_WRITE, address, value);
        return;
    }
    if (address >= 0xC100 && address < 0xC800) {
        int slot = (address >> 8) & 7;
        softswitch_slot_io_select(m, address);
        /* INTCXROM hides slot I/O; writes fall through to underlay (ROM-like). */
        if (!(m->state_flags & A2S_CXSLOTROM_MB_ENABLE) &&
            m->slot_type[slot] == SLOT_TYPE_MOCKINGBOARD) {
            mockingboard_write_cn(
                m,
                &m->mockingboard[slot],
                slot,
                address,
                (uint8_t)(address & 0xFF),
                value);
            apple2_report_memory_access(m, APPLE2_MEMORY_ACCESS_WRITE, address, value);
            return;
        }
    }

    m->pages.write_pages[page][offset] = value;
    apple2_report_memory_access(m, APPLE2_MEMORY_ACCESS_WRITE, address, value);
}

void apple2_set_memory_access_callback(
    apple2_t *machine,
    apple2_memory_access_fn callback,
    void *user)
{
    if (machine == NULL) {
        return;
    }
    machine->memory_access = callback;
    machine->memory_access_user = user;
}

static void apple2_install_roms_for_model(apple2_t *m)
{
    if (m->model == APPLE2_MODEL_II_PLUS) {
        m->rom_d000 = a2p_rom;
        m->rom_d000_size = (size_t)a2p_rom_size;
        m->rom_c000 = NULL;
        m->rom_c000_size = 0;
        m->rom_char = a2p_character_rom;
        m->rom_char_size = (size_t)a2p_character_rom_size;
        m->cpu.cpu.class = CPU_6502;
    } else {
        /* //e Enhanced: a2ee_rom is 16K at $C000; system ROM is last 12K. */
        m->rom_c000 = a2ee_rom;
        m->rom_c000_size = (size_t)a2ee_rom_size;
        m->rom_d000 = a2ee_rom + 0x1000;
        m->rom_d000_size = 0x3000;
        m->rom_char = a2ee_character_rom;
        m->rom_char_size = (size_t)a2ee_character_rom_size;
        m->cpu.cpu.class = CPU_65c02;
    }
}

bool apple2_init(apple2_t *machine)
{
    uint32_t i;

    if (machine == NULL) {
        return false;
    }

    memset(machine, 0, sizeof(*machine));
    softswitch_init_tables();

    machine->ram_main = (uint8_t *)calloc(APPLE2_RAM_MAIN_SIZE, 1);
    machine->ram_lc = (uint8_t *)calloc(APPLE2_RAM_LC_SIZE, 1);
    machine->rom_sink = (uint8_t *)calloc(0x3000, 1);
    machine->pages.read_pages =
        (uint8_t **)calloc(APPLE2_NUM_PAGES, sizeof(uint8_t *));
    machine->pages.write_pages =
        (uint8_t **)calloc(APPLE2_NUM_PAGES, sizeof(uint8_t *));
    machine->write_history = (uint64_t *)calloc(APPLE2_ADDR_SPACE, sizeof(uint64_t));

    if (!machine->ram_main || !machine->ram_lc || !machine->rom_sink ||
        !machine->pages.read_pages || !machine->pages.write_pages ||
        !machine->write_history) {
        apple2_shutdown(machine);
        return false;
    }

    machine->pages.num_pages = APPLE2_NUM_PAGES;
    machine->model = APPLE2_MODEL_IIE_ENHANCED;
    machine->strobed_slot = -1;

    /* Pattern underlay like a2m floating IO area. */
    memset(machine->ram_main + 0xC001, 0xA0, 0x0FFE);
    memset(machine->ram_main + 0x10000 + 0xC001, 0xA0, 0x0FFE);

    for (i = 0; i < APPLE2_NUM_PAGES; i++) {
        machine->pages.read_pages[i] = machine->ram_main + (i * APPLE2_PAGE_SIZE);
        machine->pages.write_pages[i] = machine->ram_main + (i * APPLE2_PAGE_SIZE);
    }

    cpu65_init(&machine->cpu, machine, apple2_bus_read, apple2_bus_write);
    cpu65_set_irq_pending_callback(&machine->cpu, apple2_irq_pending);
    apple2_install_roms_for_model(machine); /* sets CPU class after cpu65_init */
    apple2_video_init(machine);

    machine->ready = true;
    machine->instruction_complete = true;
    machine->mb_slot = 0;

    /* Unconnected paddles read near center until host input arrives. */
    machine->gameport_axis[0] = 128u;
    machine->gameport_axis[1] = 128u;
    machine->gameport_axis[2] = 128u;
    machine->gameport_axis[3] = 128u;
    machine->gameport_buttons = 0;
    machine->gameport_ptrig_cycle = 0;

    /*
     * Empty-slot Cn shadows must be RAM underlay, captured BEFORE bank apply
     * maps //e internal C3 ROM onto $C300. SETC3ROM restores this shadow;
     * capturing after map_c3rom made SETC3ROM a no-op (a2audit CXXX).
     */
    for (i = 0; i < 8; i++) {
        machine->rom_shadow_pages[i] =
            machine->ram_main + (size_t)(0xC000u + i * APPLE2_PAGE_SIZE);
        diskii_controller_init(&machine->diskii_controller[i]);
        machine->diskii_present[i] = 0;
        machine->slot_type[i] = SLOT_TYPE_EMPTY;
        memset(&machine->sp_device[i], 0, sizeof(machine->sp_device[i]));
        mockingboard_reset(&machine->mockingboard[i], 0);
    }

    softswitch_setup_after_reset(machine);
    softswitch_apply_full_map(machine);

    /* Default Disk II in slot 6 (16-sector PROM until media selects encoding). */
    apple2_attach_diskii(machine, 6);
    /* Default Mockingboard in slot 4 (common configuration). */
    apple2_attach_mockingboard(machine, 4);

    cpu65_reset(&machine->cpu);
    machine->prng = 0xA2A2A2A2u;
    return true;
}

uint32_t apple2_rand_u32(apple2_t *machine)
{
    uint32_t x;

    if (machine == NULL) {
        return 0u;
    }
    x = machine->prng;
    if (x == 0u) {
        x = 0xA2A2A2A2u;
    }
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    machine->prng = x;
    return x;
}

void apple2_set_replay_sealed(apple2_t *machine, bool sealed)
{
    if (machine == NULL) {
        return;
    }
    machine->replay_sealed = sealed;
}

void apple2_set_media_event_callback(
    apple2_t *machine,
    void (*callback)(void *user, uint64_t cycle, int slot, int device, int kind),
    void *user)
{
    if (machine == NULL) {
        return;
    }
    machine->media_event = callback;
    machine->media_event_user = user;
}

void apple2_set_input_event_callback(
    apple2_t *machine,
    void (*callback)(
        void *user, uint64_t cycle, int kind, uint32_t a, uint32_t b, uint32_t c),
    void *user)
{
    if (machine == NULL) {
        return;
    }
    machine->input_event = callback;
    machine->input_event_user = user;
}

void apple2_note_media_event(apple2_t *machine, int slot, int device, int kind)
{
    if (machine == NULL || machine->replay_sealed || machine->media_event == NULL) {
        return;
    }
    machine->media_event(
        machine->media_event_user,
        apple2_cycles(machine),
        slot,
        device,
        kind);
}

void apple2_install_diskii_rom(apple2_t *m, int slot, int encoding)
{
    uint8_t *src;

    if (m == NULL || slot < 1 || slot > 7) {
        return;
    }
    src = (encoding == DSK_ENCODING_13SECTOR) ? diskii_rom_13sector()
                                              : diskii_rom_16sector();
    memcpy(m->diskii_rom_bytes[slot], src, 256);
    apple2_pages_map_rom(m, (uint16_t)(0xC000 + slot * 0x100), 0x100,
                         m->diskii_rom_bytes[slot]);
    m->rom_shadow_pages[slot] = m->diskii_rom_bytes[slot];
    softswitch_apply_full_map(m);
}

bool apple2_attach_diskii(apple2_t *m, int slot)
{
    if (m == NULL || slot < 1 || slot > 7) {
        return false;
    }
    if (m->slot_type[slot] != SLOT_TYPE_EMPTY &&
        m->slot_type[slot] != SLOT_TYPE_DISKII) {
        apple2_detach_slot_card(m, slot);
    }
    m->diskii_present[slot] = 1;
    m->slot_type[slot] = SLOT_TYPE_DISKII;
    apple2_install_diskii_rom(m, slot, DSK_ENCODING_16SECTOR);
    return true;
}

void apple2_detach_slot_card(apple2_t *m, int slot)
{
    if (m == NULL || slot < 1 || slot > 7) {
        return;
    }
    if (m->slot_type[slot] == SLOT_TYPE_SMARTPORT) {
        (void)sp_eject(m, slot, 0);
        (void)sp_eject(m, slot, 1);
    }
    if (m->slot_type[slot] == SLOT_TYPE_MOCKINGBOARD && m->mb_slot == (uint8_t)slot) {
        m->mb_slot = 0;
    }
    m->slot_type[slot] = SLOT_TYPE_EMPTY;
    m->diskii_present[slot] = 0;
    apple2_pages_map_ram(m, false, (uint32_t)(0xC000 + slot * 0x100), 0x100);
    m->rom_shadow_pages[slot] = m->pages.read_pages[0xC0 + slot];
    softswitch_apply_full_map(m);
}

bool apple2_attach_mockingboard(apple2_t *m, int slot)
{
    if (m == NULL || slot < 1 || slot > 7) {
        return false;
    }
    if (m->mb_slot != 0 && m->mb_slot != (uint8_t)slot) {
        apple2_detach_slot_card(m, m->mb_slot);
    }
    if (m->slot_type[slot] != SLOT_TYPE_EMPTY &&
        m->slot_type[slot] != SLOT_TYPE_MOCKINGBOARD) {
        apple2_detach_slot_card(m, slot);
    }
    m->diskii_present[slot] = 0;
    m->slot_type[slot] = SLOT_TYPE_MOCKINGBOARD;
    mockingboard_reset(&m->mockingboard[slot], 1);
    m->mb_slot = (uint8_t)slot;
    /* No Cn ROM — registers only; leave shadow as RAM. */
    softswitch_apply_full_map(m);
    return true;
}

bool apple2_attach_smartport(apple2_t *m, int slot)
{
    uint8_t *rom_src;
    size_t offset;

    if (m == NULL || slot < 1 || slot > 7) {
        return false;
    }
    if (m->slot_type[slot] != SLOT_TYPE_EMPTY &&
        m->slot_type[slot] != SLOT_TYPE_SMARTPORT) {
        apple2_detach_slot_card(m, slot);
    }
    m->diskii_present[slot] = 0;
    m->slot_type[slot] = SLOT_TYPE_SMARTPORT;
    memset(&m->sp_device[slot], 0, sizeof(m->sp_device[slot]));

    /* 2K SmartPort image: 256 bytes per slot area. */
    offset = (size_t)slot * 0x100u;
    if (offset + 0x100u > (size_t)smartport_rom_size) {
        offset = 0;
    }
    rom_src = (uint8_t *)smartport_rom + offset;
    memcpy(m->smartport_rom_bytes[slot], rom_src, 256);
    /* Model ID byte (a2m): //e = 0xFF, ][+ = 0x3C at offset 7. */
    m->smartport_rom_bytes[slot][0x07] =
        (m->model == APPLE2_MODEL_IIE_ENHANCED) ? 0xFFu : 0x3Cu;
    apple2_pages_map_rom(
        m, (uint16_t)(0xC000 + slot * 0x100), 0x100, m->smartport_rom_bytes[slot]);
    m->rom_shadow_pages[slot] = m->smartport_rom_bytes[slot];
    softswitch_apply_full_map(m);
    return true;
}

int apple2_smartport_mount(apple2_t *m, int slot, int device, const char *path)
{
    if (m == NULL || path == NULL || slot < 1 || slot > 7 || device < 0 || device > 1) {
        return -1;
    }
    if (m->slot_type[slot] != SLOT_TYPE_SMARTPORT) {
        if (!apple2_attach_smartport(m, slot)) {
            return -1;
        }
    }
    return (sp_mount(m, slot, device, path) == A2_OK) ? 0 : -1;
}

int apple2_smartport_eject(apple2_t *m, int slot, int device)
{
    if (m == NULL || slot < 1 || slot > 7 || device < 0 || device > 1 ||
        m->slot_type[slot] != SLOT_TYPE_SMARTPORT) {
        return -1;
    }
    return sp_eject(m, slot, device) == A2_OK ? 0 : -1;
}

void apple2_peripherals_step(apple2_t *m, uint32_t cycles)
{
    int slot;

    if (m == NULL || cycles == 0) {
        return;
    }
    for (slot = 1; slot <= 7; slot++) {
        if (m->slot_type[slot] == SLOT_TYPE_MOCKINGBOARD) {
            MOCKINGBOARD *mb = &m->mockingboard[slot];
            via6522_step_cycles(&mb->via[0], cycles);
            via6522_step_cycles(&mb->via[1], cycles);
            mockingboard_queue_ay_cycles(mb, cycles);
        }
    }
}

int apple2_disk_mount(apple2_t *m, int slot, int drive, const char *path)
{
    if (m == NULL || path == NULL || slot < 1 || slot > 7 || drive < 0 || drive > 1) {
        return -1;
    }
    if (!m->diskii_present[slot]) {
        apple2_attach_diskii(m, slot);
    }
    return diskii_mount(m, slot, drive, path);
}

int apple2_disk_eject(apple2_t *m, int slot, int drive)
{
    if (m == NULL || slot < 1 || slot > 7 || drive < 0 || drive > 1 ||
        !m->diskii_present[slot]) {
        return -1;
    }
    return diskii_eject(m, slot, drive, 1) == A2_OK ? 0 : -1;
}

int apple2_disk_select_image(apple2_t *m, int slot, int drive, int index)
{
    if (m == NULL || slot < 1 || slot > 7 || drive < 0 || drive > 1) {
        return -1;
    }
    if (!m->diskii_present[slot]) {
        return -1;
    }
    return diskii_mount_image(m, slot, drive, index) == A2_OK ? 0 : -1;
}

int apple2_disk_swap(
    apple2_t *m,
    int slot,
    int drive,
    int32_t param,
    bool relative)
{
    if (m == NULL || slot < 1 || slot > 7 || drive < 0 || drive > 1) {
        return -1;
    }
    if (!m->diskii_present[slot]) {
        return -1;
    }
    return diskii_swap_image(m, slot, drive, param, relative ? 1 : 0);
}

int apple2_disk_set_writable(apple2_t *m, int slot, int drive, bool writable)
{
    DISKII_DRIVE *dd;

    if (m == NULL || slot < 1 || slot > 7 || drive < 0 || drive > 1) {
        return -1;
    }
    if (!m->diskii_present[slot]) {
        return -1;
    }
    dd = &m->diskii_controller[slot].diskii_drive[drive];
    /* sensor_protect: 0 = writes enabled, 1 = notch protected. */
    dd->sensor_protect = writable ? 0u : 1u;
    return 0;
}

bool apple2_flush_media(apple2_t *m)
{
    return m != NULL && diskii_flush_all(m) == A2_OK && sp_flush_all(m) == A2_OK;
}

void apple2_shutdown(apple2_t *machine)
{
    if (machine == NULL) {
        return;
    }

    sp_shutdown(machine);
    diskii_shutdown(machine);
    apple2_video_shutdown(machine);
    apple2_paste_cancel(machine);
    free(machine->pages.read_pages);
    free(machine->pages.write_pages);
    free(machine->ram_main);
    free(machine->ram_lc);
    free(machine->rom_sink);
    free(machine->write_history);
    memset(machine, 0, sizeof(*machine));
}

static void apple2_reset_common(apple2_t *machine, bool cold)
{
    int slot;

    if (machine == NULL || !machine->ready) {
        return;
    }

    if (cold) {
        /* Power-on style: wipe main + language-card RAM, restore floating-IO
           underlay pattern used at init. Open-Apple stays asserted so Autostart
           takes the cold-start path (real CTRL+Open-Apple+RESET). */
        memset(machine->ram_main, 0, APPLE2_RAM_MAIN_SIZE);
        if (machine->ram_lc != NULL) {
            memset(machine->ram_lc, 0, APPLE2_RAM_LC_SIZE);
        }
        memset(machine->ram_main + 0xC001, 0xA0, 0x0FFE);
        memset(machine->ram_main + 0x10000 + 0xC001, 0xA0, 0x0FFE);
        machine->state_flags |= A2S_OPEN_APPLE;
        machine->state_flags &= ~A2S_CLOSED_APPLE;
    } else {
        /* Warm CTRL+RESET: clear text page; leave most RAM intact. */
        memset(machine->ram_main + 0x0400, 0xA0, 0x400);
        machine->state_flags &= ~(A2S_OPEN_APPLE | A2S_CLOSED_APPLE);
    }

    softswitch_setup_after_reset(machine);
    diskii_reset(machine);
    apple2_video_reset(machine);
    apple2_paste_cancel(machine);
    for (slot = 1; slot <= 7; slot++) {
        if (machine->slot_type[slot] == SLOT_TYPE_MOCKINGBOARD) {
            mockingboard_reset(&machine->mockingboard[slot], 1);
        }
    }
    cpu65_reset(&machine->cpu);
    machine->instruction_complete = true;
}

void apple2_reset(apple2_t *machine)
{
    apple2_reset_common(machine, false);
}

void apple2_cold_reset(apple2_t *machine)
{
    apple2_reset_common(machine, true);
}

void apple2_set_cpu_class(apple2_t *machine, uint32_t cpu_class)
{
    if (machine == NULL) {
        return;
    }
    machine->cpu.cpu.class = cpu_class;
}

void apple2_set_model(apple2_t *machine, apple2_model model)
{
    int slot;

    if (machine == NULL || !machine->ready) {
        return;
    }
    machine->model = model;
    apple2_install_roms_for_model(machine);
    /* SmartPort's slot ROM advertises the host model in byte 7. Preserve
       mounted volumes while refreshing that byte for a live model change. */
    for (slot = 1; slot <= 7; ++slot) {
        if (machine->slot_type[slot] == SLOT_TYPE_SMARTPORT) {
            machine->smartport_rom_bytes[slot][0x07] =
                model == APPLE2_MODEL_IIE_ENHANCED ? 0xFFu : 0x3Cu;
        }
    }
    softswitch_setup_after_reset(machine);
    cpu65_reset(&machine->cpu);
}

uint8_t apple2_debug_read(const apple2_t *machine, uint16_t address)
{
    uint16_t page = (uint16_t)(address / APPLE2_PAGE_SIZE);
    uint16_t offset = (uint16_t)(address % APPLE2_PAGE_SIZE);

    assert(machine != NULL);
    return machine->pages.read_pages[page][offset];
}

void apple2_debug_write(apple2_t *machine, uint16_t address, uint8_t value)
{
    uint16_t page = (uint16_t)(address / APPLE2_PAGE_SIZE);
    uint16_t offset = (uint16_t)(address % APPLE2_PAGE_SIZE);

    assert(machine != NULL);
    machine->pages.write_pages[page][offset] = value;
}

uint8_t apple2_debug_call_stack(
    const apple2_t *machine,
    apple2_call_stack_entry *out,
    uint8_t max_entries)
{
    uint16_t address;
    uint8_t count = 0;

    if (machine == NULL || out == NULL || max_entries == 0u) {
        return 0u;
    }

    /* 6502 stack grows down in $0100–$01FF; words above SP are candidates. */
    address = (uint16_t)(machine->cpu.cpu.sp + 1u);
    while (address < 0x01FFu && count < max_entries) {
        uint16_t return_addr = (uint16_t)(
            apple2_debug_read(machine, address) |
            ((uint16_t)apple2_debug_read(machine, (uint16_t)(address + 1u)) << 8));
        /* JSR pushes address of its last operand byte; opcode sits two back. */
        if (apple2_debug_read(machine, (uint16_t)(return_addr - 2u)) == 0x20u) {
            out[count].jsr_address = (uint16_t)(return_addr - 2u);
            out[count].dest_address = (uint16_t)(
                apple2_debug_read(machine, (uint16_t)(return_addr - 1u)) |
                ((uint16_t)apple2_debug_read(machine, return_addr) << 8));
            count++;
            address = (uint16_t)(address + 2u);
        } else {
            address = (uint16_t)(address + 1u);
        }
    }
    return count;
}

static uint8_t apple2_read_rom_forced(const apple2_t *m, uint16_t address)
{
    if (address >= 0xC100 && address < 0xD000) {
        if (m->rom_c000 != NULL && m->rom_c000_size > (size_t)(address - 0xC000u)) {
            return m->rom_c000[address - 0xC000u];
        }
        return 0xFFu;
    }
    if (address >= 0xD000 && m->rom_d000 != NULL) {
        uint16_t off = (uint16_t)(address - 0xD000u);
        if ((size_t)off < m->rom_d000_size) {
            return m->rom_d000[off];
        }
    }
    return 0xFFu;
}

uint8_t apple2_read_in_view(const apple2_t *m, view_flags_t vf, uint16_t address)
{
    a2sel_48k ram;
    uint16_t page;
    uint16_t offset;

    assert(m != NULL);
    ram = vf_get_ram(vf);
    page = (uint16_t)(address / APPLE2_PAGE_SIZE);
    offset = (uint16_t)(address % APPLE2_PAGE_SIZE);

    /* I/O page — keyboard latch lives in main; avoid soft-switch side effects. */
    if (address >= 0xC000 && address <= 0xC0FF) {
        return m->ram_main[address];
    }

    /* $C100-$CFFF */
    if (address >= 0xC100 && address < 0xD000) {
        if (vf_get_c100(vf) == A2SELC100_ROM) {
            return apple2_read_rom_forced(m, address);
        }
        return m->pages.read_pages[page][offset];
    }

    /* $D000-$FFFF */
    if (address >= 0xD000) {
        if (ram == A2SEL48K_MAPPED && vf_get_d000(vf) == A2SELD000_MAPPED) {
            return m->pages.read_pages[page][offset];
        }
        switch (vf_get_d000(vf)) {
        case A2SELD000_ROM:
            return apple2_read_rom_forced(m, address);
        case A2SELD000_MAPPED:
            return m->pages.read_pages[page][offset];
        case A2SELD000_LC_B1:
        case A2SELD000_LC_B2: {
            uint32_t bank_base = (vf_get_d000(vf) == A2SELD000_LC_B2) ? 0x1000u : 0u;
            uint32_t lc_base = (ram == A2SEL48K_AUX) ? 0x4000u : 0u;
            if (address < 0xE000) {
                return m->ram_lc[lc_base + bank_base + (uint32_t)(address - 0xD000u)];
            }
            return m->ram_lc[lc_base + 0x2000u + (uint32_t)(address - 0xE000u)];
        }
        default:
            return m->pages.read_pages[page][offset];
        }
    }

    /* $0000-$BFFF */
    if (ram == A2SEL48K_MAPPED) {
        return m->pages.read_pages[page][offset];
    }
    if (ram == A2SEL48K_MAIN) {
        return m->ram_main[address];
    }
    /* Aux 48K plane */
    return m->ram_main[(uint32_t)address + 0x10000u];
}

void apple2_write_in_view(apple2_t *m, view_flags_t vf, uint16_t address, uint8_t value)
{
    a2sel_48k ram;
    uint16_t page;
    uint16_t offset;

    assert(m != NULL);
    ram = vf_get_ram(vf);
    page = (uint16_t)(address / APPLE2_PAGE_SIZE);
    offset = (uint16_t)(address % APPLE2_PAGE_SIZE);

    if (address >= 0xC000 && address <= 0xC0FF) {
        if (address == 0xC000) {
            m->ram_main[address] = value;
        }
        return;
    }

    if (address >= 0xC100 && address < 0xD000) {
        if (vf_get_c100(vf) == A2SELC100_ROM) {
            return;
        }
        m->pages.write_pages[page][offset] = value;
        return;
    }

    if (address >= 0xD000) {
        if (ram == A2SEL48K_MAPPED && vf_get_d000(vf) == A2SELD000_MAPPED) {
            m->pages.write_pages[page][offset] = value;
            return;
        }
        switch (vf_get_d000(vf)) {
        case A2SELD000_MAPPED:
            m->pages.write_pages[page][offset] = value;
            return;
        case A2SELD000_ROM:
            return;
        case A2SELD000_LC_B1:
        case A2SELD000_LC_B2: {
            uint32_t bank_base = (vf_get_d000(vf) == A2SELD000_LC_B2) ? 0x1000u : 0u;
            uint32_t lc_base = (ram == A2SEL48K_AUX) ? 0x4000u : 0u;
            if (address < 0xE000) {
                m->ram_lc[lc_base + bank_base + (uint32_t)(address - 0xD000u)] = value;
                return;
            }
            m->ram_lc[lc_base + 0x2000u + (uint32_t)(address - 0xE000u)] = value;
            return;
        }
        default:
            m->pages.write_pages[page][offset] = value;
            return;
        }
    }

    if (ram == A2SEL48K_MAPPED) {
        m->pages.write_pages[page][offset] = value;
        return;
    }
    if (ram == A2SEL48K_MAIN) {
        m->ram_main[address] = value;
        return;
    }
    m->ram_main[(uint32_t)address + 0x10000u] = value;
}

void apple2_load(apple2_t *machine, uint16_t address, const uint8_t *bytes, size_t length)
{
    size_t i;
    assert(machine != NULL);
    assert(bytes != NULL || length == 0);
    for (i = 0; i < length; i++) {
        apple2_debug_write(machine, (uint16_t)(address + i), bytes[i]);
    }
}

void apple2_set_key(apple2_t *machine, uint8_t key_with_strobe)
{
    if (machine == NULL) {
        return;
    }
    machine->ram_main[SS_KBD] = key_with_strobe;
    machine->key_held = key_with_strobe;
    machine->state_flags |= A2S_KEY_HELD;
    if (!machine->replay_sealed && machine->input_event != NULL) {
        machine->input_event(
            machine->input_event_user,
            apple2_cycles(machine),
            APPLE2_INPUT_KEY,
            key_with_strobe,
            0u,
            0u);
    }
}

uint64_t apple2_cycles(const apple2_t *machine)
{
    if (machine == NULL) {
        return 0;
    }
    return machine->cpu.cpu.cycles;
}

uint32_t apple2_state_flags(const apple2_t *machine)
{
    if (machine == NULL) {
        return 0;
    }
    return machine->state_flags;
}

/*
 * begin_video_devices: true for beam-accurate path (video + peripherals per
 * quantum). false for max free-run (caller batches peripherals; no video).
 */
static void apple2_begin_cpu_work(apple2_t *machine, bool begin_video_devices)
{
    cpu65_interrupt_kind kind;
    uint8_t opcode;

    assert(machine != NULL);
    assert(!machine->cpu.micro_active);

    kind = cpu65_micro_poll_interrupt(&machine->cpu);
    if (kind != CPU65_INTERRUPT_NONE) {
        apple2_observer_begin(
            machine,
            kind == CPU65_INTERRUPT_NMI ?
                APPLE2_CPU_OBSERVER_NMI :
                APPLE2_CPU_OBSERVER_IRQ);
        cpu65_micro_begin_interrupt(&machine->cpu, kind);
        machine->instruction_complete = false;
        return;
    }

    /* SmartPort pure protocol: trap $C800 entries (no card expansion ROM). */
    {
        uint64_t before = machine->cpu.cpu.cycles;
        if (sp_host_trap(machine)) {
            size_t ran = (size_t)(machine->cpu.cpu.cycles - before);
            if (ran > 0u && begin_video_devices) {
                apple2_video_step_n(machine, (uint32_t)ran);
                apple2_peripherals_step(machine, (uint32_t)ran);
            }
            /* Host trap is not a 6502 instruction — no history record. */
            machine->instruction_complete = true;
            return;
        }
    }

    apple2_observer_begin(machine, APPLE2_CPU_OBSERVER_INSTRUCTION);

    /*
     * Beam path: prefer micro-step for bus-accurate R/W BP / history.
     * Max free-run: atomic opcode only (a2m-shaped) — no micro, no extra
     * debug fetch before cpu65_step.
     */
    if (begin_video_devices) {
        opcode = apple2_debug_read(machine, machine->cpu.cpu.pc);
        if (cpu65_micro_can_begin(&machine->cpu, opcode)) {
            cpu65_micro_begin(&machine->cpu);
            machine->instruction_complete = false;
            return;
        }
    }

    {
        uint64_t before = machine->cpu.cpu.cycles;
        size_t ran;
        (void)cpu65_step(&machine->cpu);
        ran = (size_t)(machine->cpu.cpu.cycles - before);
        /* Atomic multi-cycle op: beam path advances video + peripherals now. */
        if (ran > 0u && begin_video_devices) {
            apple2_video_step_n(machine, (uint32_t)ran);
            apple2_peripherals_step(machine, (uint32_t)ran);
        }
    }
    machine->instruction_complete = true;
    apple2_observer_complete(machine);
}

bool apple2_step_cycle(apple2_t *machine)
{
    if (machine == NULL || !machine->ready) {
        return false;
    }

    if (!machine->cpu.micro_active) {
        apple2_begin_cpu_work(machine, true);
        if (!machine->cpu.micro_active) {
            /* Atomic path already stepped video + peripherals. */
            return true;
        }
    }

    if (cpu65_micro_step(&machine->cpu)) {
        machine->instruction_complete = true;
        apple2_observer_complete(machine);
    }
    apple2_video_step(machine);
    apple2_peripherals_step(machine, 1);
    return true;
}

bool apple2_step_cycles(apple2_t *machine, uint32_t count, uint32_t *out_ran)
{
    uint32_t ran = 0;

    if (machine == NULL || !machine->ready) {
        if (out_ran != NULL) {
            *out_ran = 0;
        }
        return false;
    }

    while (ran < count) {
        if (!apple2_step_cycle(machine)) {
            if (out_ran != NULL) {
                *out_ran = ran;
            }
            return false;
        }
        ran++;
    }
    if (out_ran != NULL) {
        *out_ran = ran;
    }
    return true;
}

size_t apple2_step_instruction(apple2_t *machine)
{
    uint64_t start;

    if (machine == NULL || !machine->ready) {
        return 0;
    }

    start = machine->cpu.cpu.cycles;

    while (machine->cpu.micro_active) {
        if (cpu65_micro_step(&machine->cpu)) {
            machine->instruction_complete = true;
            apple2_observer_complete(machine);
        }
        apple2_video_step(machine);
        apple2_peripherals_step(machine, 1);
    }

    machine->instruction_complete = false;
    while (!machine->instruction_complete) {
        if (!apple2_step_cycle(machine)) {
            break;
        }
        if (!machine->cpu.micro_active && !machine->instruction_complete) {
            machine->instruction_complete = true;
        }
    }

    return (size_t)(machine->cpu.cpu.cycles - start);
}

size_t apple2_step_instruction_max(apple2_t *machine)
{
    uint64_t start;
    size_t ran;

    if (machine == NULL || !machine->ready) {
        return 0;
    }

    start = machine->cpu.cpu.cycles;

    /* Drain rare mid-insn micro (e.g. entered max mid-instruction). */
    while (machine->cpu.micro_active) {
        if (cpu65_micro_step(&machine->cpu)) {
            machine->instruction_complete = true;
            apple2_observer_complete(machine);
        }
    }

    /* Max: one atomic opcode (or SP trap / IRQ micro started+finished). */
    machine->instruction_complete = false;
    apple2_begin_cpu_work(machine, false);
    while (machine->cpu.micro_active) {
        /* IRQ/NMI still use micro; finish without video. */
        if (cpu65_micro_step(&machine->cpu)) {
            machine->instruction_complete = true;
            apple2_observer_complete(machine);
        }
    }

    ran = (size_t)(machine->cpu.cpu.cycles - start);
    /*
     * Caller may batch peripherals (runtime max loop). When ran==0, error.
     * Peripherals applied here so standalone callers stay C1-correct; runtime
     * may call apple2_peripherals_step in coarser batches after several insns
     * only if it uses a no-periph variant — for now step here once per insn.
     */
    if (ran > 0u) {
        apple2_peripherals_step(machine, (uint32_t)ran);
    }
    return ran;
}

/* PTRIG timer saturates at 255 and bit7 clears only when timer > axis.
   Cap at 254 so full-right / full-down still discharges (a2m / Penetrator). */
static uint8_t apple2_gameport_clamp_axis(uint8_t value)
{
    return value > 254u ? 254u : value;
}

void apple2_gameport_set_axis(apple2_t *m, int axis, uint8_t value)
{
    if (m == NULL || axis < 0 || axis > 3) {
        return;
    }
    m->gameport_axis[axis] = apple2_gameport_clamp_axis(value);
    if (!m->replay_sealed && m->input_event != NULL) {
        m->input_event(
            m->input_event_user,
            apple2_cycles(m),
            APPLE2_INPUT_GAMEPORT_AXIS,
            (uint32_t)axis,
            m->gameport_axis[axis],
            0u);
    }
}

void apple2_gameport_set_axes(apple2_t *m, const uint8_t axis[4])
{
    int i;

    if (m == NULL || axis == NULL) {
        return;
    }
    for (i = 0; i < 4; ++i) {
        apple2_gameport_set_axis(m, i, axis[i]);
    }
}

void apple2_gameport_set_buttons(apple2_t *m, uint8_t mask)
{
    if (m == NULL) {
        return;
    }
    m->gameport_buttons = (uint8_t)(mask & 0x07u);
    if (!m->replay_sealed && m->input_event != NULL) {
        m->input_event(
            m->input_event_user,
            apple2_cycles(m),
            APPLE2_INPUT_GAMEPORT_BUTTONS,
            m->gameport_buttons,
            0u,
            0u);
    }
}

void apple2_gameport_ptrig(apple2_t *m)
{
    if (m == NULL) {
        return;
    }
    m->gameport_ptrig_cycle = m->cpu.cpu.cycles;
}

void apple2_paste_cancel(apple2_t *m)
{
    if (m == NULL) {
        return;
    }
    free(m->paste_text);
    m->paste_text = NULL;
    m->paste_index = 0;
}

bool apple2_paste_active(const apple2_t *m)
{
    return m != NULL && m->paste_text != NULL;
}

/* Latch next paste char into $C000; return true if a key was presented. */
static bool apple2_paste_feed(apple2_t *m)
{
    if (m == NULL || m->paste_text == NULL) {
        return false;
    }

    while (m->paste_text[m->paste_index] != '\0') {
        uint8_t byte = (uint8_t)m->paste_text[m->paste_index++];
        uint8_t mapped;

        if (byte >= 0x80u) {
            /* Drop UTF-8 continuation / lead sequences. */
            while ((m->paste_text[m->paste_index] & 0xC0) == 0x80) {
                m->paste_index++;
            }
            continue;
        }

        /*
         * Newlines → Apple Return ($0D). Host clipboards are usually Unix LF
         * only; Windows uses CR+LF. Emit one Return for \n, \r, or \r\n.
         */
        if (byte == '\r') {
            if (m->paste_text[m->paste_index] == '\n') {
                m->paste_index++; /* collapse CRLF */
            }
            apple2_set_key(m, (uint8_t)(0x0Du | 0x80u));
            return true;
        }
        if (byte == '\n') {
            apple2_set_key(m, (uint8_t)(0x0Du | 0x80u));
            return true;
        }
        if (byte == '\t') {
            mapped = (uint8_t)' ';
        } else if (byte >= 0x20u && byte <= 0x7Eu) {
            /* ][+ has no lowercase; //e keeps case. */
            if (m->model == APPLE2_MODEL_II_PLUS) {
                mapped = (uint8_t)toupper((unsigned char)byte);
            } else {
                mapped = byte;
            }
        } else if (byte < 0x20u) {
            /* Other ASCII controls (ESC, BS, …) pass through. */
            mapped = byte;
        } else {
            continue;
        }

        apple2_set_key(m, (uint8_t)(mapped | 0x80u));
        return true;
    }

    apple2_paste_cancel(m);
    return false;
}

bool apple2_paste_begin(apple2_t *m, const char *text, size_t length)
{
    char *copy;

    if (m == NULL || text == NULL || length == 0) {
        return false;
    }

    apple2_paste_cancel(m);
    copy = (char *)malloc(length + 1u);
    if (copy == NULL) {
        return false;
    }
    memcpy(copy, text, length);
    copy[length] = '\0';
    m->paste_text = copy;
    m->paste_index = 0;
    /* Present first character immediately; rest on KBDSTRB. */
    if (!apple2_paste_feed(m)) {
        apple2_paste_cancel(m);
        return false;
    }
    return true;
}

bool apple2_paste_on_kbdstrb(apple2_t *m)
{
    if (m == NULL || m->paste_text == NULL) {
        return false;
    }
    if (apple2_paste_feed(m)) {
        return true; /* next key latched; leave strobe set */
    }
    /* Paste finished: clear strobe on last acknowledge. */
    return false;
}
