#include "apple2.h"
#include "diskii.h"

#include <string.h>

#define C800_NONE     (-1)
#define C800_INTERNAL (8)

#define FLAG(m, f) (((m)->state_flags & (f)) != 0)
#define SETF(m, f) ((m)->state_flags |= (f))
#define CLRF(m, f) ((m)->state_flags &= ~(uint32_t)(f))

static uint8_t floating_bus(apple2_t *m)
{
    return apple2_video_floating_bus(m);
}

/*
 * Paddle RC timer (a2m-compatible): after PTRIG, bit7 of PDL* stays high until
 * elapsed cycles map past the axis threshold. Full scale ≈ 3 ms of CPU time
 * (paddl_normalized = (APPLE2_CPU_FREQUENCY_HZ/1000)*3 ≈ 3061 cycles).
 */
enum { SOFTSWITCH_PADDLE_RANGE_CYCLES = 3061u };

static uint8_t softswitch_paddle_read(apple2_t *m, int axis)
{
    uint64_t delta;
    uint32_t timer;

    if (axis < 0 || axis > 3) {
        return floating_bus(m);
    }
    delta = m->cpu.cpu.cycles - m->gameport_ptrig_cycle;
    if (delta >= SOFTSWITCH_PADDLE_RANGE_CYCLES) {
        timer = 255u;
    } else {
        timer = (uint32_t)((delta * 255u) / SOFTSWITCH_PADDLE_RANGE_CYCLES);
    }
    /* High while timer is still below resistance threshold. */
    return (timer > (uint32_t)m->gameport_axis[axis]) ? 0x00u : 0x80u;
}

static uint32_t offset_main(uint16_t a)
{
    return a;
}

static uint32_t offset_aux(uint16_t a)
{
    return 0x10000u + a;
}

static uint32_t off_zp(uint32_t s, uint16_t a)
{
    return (s & A2S_ALTZP) ? offset_aux(a) : offset_main(a);
}

static uint32_t off_rd(uint32_t s, uint16_t a)
{
    return (s & A2S_RAMRD) ? offset_aux(a) : offset_main(a);
}

static uint32_t off_wr(uint32_t s, uint16_t a)
{
    return (s & A2S_RAMWRT) ? offset_aux(a) : offset_main(a);
}

static uint32_t off_vid(uint32_t s, uint16_t a)
{
    return (s & A2S_PAGE2) ? offset_aux(a) : offset_main(a);
}

static void map_lc_read(apple2_t *m)
{
    uint16_t bank = FLAG(m, A2S_LC_BANK2) ? 0x1000u : 0x0000u;
    uint16_t aux = FLAG(m, A2S_ALTZP) ? 0x4000u : 0x0000u;

    if (FLAG(m, A2S_LC_READ)) {
        apple2_pages_map_lc(m, false, 0xD000, 0x1000, bank + aux);
        apple2_pages_map_lc(m, false, 0xE000, 0x2000, 0x2000u + aux);
    } else if (m->rom_d000 != NULL) {
        apple2_pages_map_rom(m, 0xD000, 0x3000, m->rom_d000);
    }
}

static void map_lc_write(apple2_t *m)
{
    uint16_t bank = FLAG(m, A2S_LC_BANK2) ? 0x1000u : 0x0000u;
    uint16_t aux = FLAG(m, A2S_ALTZP) ? 0x4000u : 0x0000u;

    if (FLAG(m, A2S_LC_WRITE)) {
        apple2_pages_map_lc(m, true, 0xD000, 0x1000, bank + aux);
        apple2_pages_map_lc(m, true, 0xE000, 0x2000, 0x2000u + aux);
    } else if (m->rom_sink != NULL) {
        apple2_map_write_host(m, 0xD000, 0x3000, m->rom_sink);
    }
}

static void apply_c800(apple2_t *m)
{
    /* CXROM active: C800 is always internal firmware (not card-strobed). */
    if (FLAG(m, A2S_CXSLOTROM_MB_ENABLE) && m->rom_c000 != NULL) {
        apple2_pages_map_rom(m, 0xC800, 0x800, m->rom_c000 + 0x800);
        return;
    }
    /*
     * Once $C3xx has strobed the internal C800 latch (C800_INTERNAL), the
     * 80-col firmware stays mapped until $CFFF — even after SETC3ROM turns
     * $C300 over to the slot. Matches original a2m io_apply_c800_latch and
     * a2audit E000B (LDA $C300 / STA $C00B → C800 still ROM).
     */
    if (m->strobed_slot == C800_INTERNAL && m->rom_c000 != NULL) {
        apple2_pages_map_rom(m, 0xC800, 0x800, m->rom_c000 + 0x800);
        return;
    }
    /* Card-owned C800 (Franklin / etc.). SmartPort has no expansion ROM;
       the host trap is a calling-convention intercept, not a C800 map. */
    if (m->strobed_slot >= 1 && m->strobed_slot <= 7) {
        /* Fall through to RAM underlay. */
    }
    apple2_pages_map_ram(m, false, 0xC800, 0x800);
}

/*
 * I/O SELECT ($Cnxx): a C800 owner (internal 80-col, or a card with
 * expansion ROM) sets a sticky flip-flop; $CFFF is the only release
 * (IIe TRM / Sather). Not last-wins. SmartPort has no C800 ROM, so
 * $Csxx must not take the map — record last_io_select_slot only, so
 * the host trap can still see a slot-ROM JMP $C800.
 * SETC3ROM ($C00B) is not I/O SELECT; a prior $C3xx latch stays
 * until $CFFF (a2audit E000B).
 */
void softswitch_slot_io_select(apple2_t *m, uint16_t address)
{
    int slot;

    if (m == NULL || address < 0xC100 || address >= 0xC800) {
        return;
    }
    if (FLAG(m, A2S_CXSLOTROM_MB_ENABLE)) {
        return; /* INTCXROM: cards do not own C800 */
    }

    slot = (address >> 8) & 0x7;
    m->last_io_select_slot = slot;

    if (m->strobed_slot != C800_NONE) {
        return; /* already latched until $CFFF */
    }

    if (slot == 3 && !FLAG(m, A2S_SLOT3ROM_MB_DISABLE)) {
        m->strobed_slot = C800_INTERNAL;
        apply_c800(m);
    }
    /* SmartPort / Disk II / MB: no expansion ROM at $C800. */
}

static void map_cxrom(apple2_t *m)
{
    int i;

    if (FLAG(m, A2S_CXSLOTROM_MB_ENABLE) && m->rom_c000 != NULL) {
        apple2_pages_map_rom(m, 0xC100, 0xF00, m->rom_c000 + 0x100);
        return;
    }

    for (i = 1; i <= 7; i++) {
        if (i == 3 && !FLAG(m, A2S_SLOT3ROM_MB_DISABLE)) {
            continue;
        }
        if (m->rom_shadow_pages[i] != NULL) {
            m->pages.read_pages[0xC0 + i] = m->rom_shadow_pages[i];
        } else {
            apple2_pages_map_ram(m, false, (uint32_t)(0xC000 + i * 0x100), 0x100);
        }
    }
    if (FLAG(m, A2S_SLOT3ROM_MB_DISABLE)) {
        if (m->rom_shadow_pages[3] != NULL) {
            m->pages.read_pages[0xC3] = m->rom_shadow_pages[3];
        }
    } else if (m->rom_c000 != NULL) {
        apple2_pages_map_rom(m, 0xC300, 0x100, m->rom_c000 + 0x300);
    }
    apply_c800(m);
}

static void map_c3rom(apple2_t *m)
{
    if (FLAG(m, A2S_SLOT3ROM_MB_DISABLE)) {
        if (m->rom_shadow_pages[3] != NULL) {
            m->pages.read_pages[0xC3] = m->rom_shadow_pages[3];
        } else {
            apple2_pages_map_ram(m, false, 0xC300, 0x100);
        }
    } else if (m->rom_c000 != NULL) {
        apple2_pages_map_rom(m, 0xC300, 0x100, m->rom_c000 + 0x300);
    }
    apply_c800(m);
}

static void apply_mem_state(apple2_t *m, uint32_t old, uint32_t new_flags)
{
    uint32_t chg = old ^ new_flags;

    if (chg & A2S_ALTZP) {
        uint32_t base = off_zp(new_flags, 0x0000);
        apple2_pages_map_ram(m, false, base, 0x0200);
        apple2_pages_map_ram(m, true, base, 0x0200);
    }

    if (chg & (A2S_RAMRD | A2S_RAMWRT | A2S_80STORE | A2S_PAGE2 | A2S_HIRES)) {
        apple2_pages_map_ram(m, false, off_rd(new_flags, 0x0200), 0xBE00);
        apple2_pages_map_ram(m, true, off_wr(new_flags, 0x0200), 0xBE00);

        if (new_flags & A2S_80STORE) {
            apple2_pages_map_ram(m, false, off_vid(new_flags, 0x0400), 0x0400);
            apple2_pages_map_ram(m, true, off_vid(new_flags, 0x0400), 0x0400);
            if (new_flags & A2S_HIRES) {
                apple2_pages_map_ram(m, false, off_vid(new_flags, 0x2000), 0x2000);
                apple2_pages_map_ram(m, true, off_vid(new_flags, 0x2000), 0x2000);
            }
        }
    }

    if (chg & (A2S_LC_READ | A2S_LC_BANK2 | A2S_ALTZP)) {
        map_lc_read(m);
    }
    if (chg & (A2S_LC_WRITE | A2S_LC_BANK2 | A2S_ALTZP)) {
        map_lc_write(m);
    }
    if (chg & A2S_CXSLOTROM_MB_ENABLE) {
        map_cxrom(m);
    }
    if (chg & A2S_SLOT3ROM_MB_DISABLE) {
        map_c3rom(m);
    }
}

void softswitch_bank_set(apple2_t *m, uint32_t bits)
{
    uint32_t old = m->state_flags & A2S_BANK_MASK;
    uint32_t new_flags = old | bits;

    if (new_flags != old) {
        m->state_flags = (m->state_flags & ~A2S_BANK_MASK) | new_flags;
        apply_mem_state(m, old, new_flags);
    }
}

void softswitch_bank_clear(apple2_t *m, uint32_t bits)
{
    uint32_t old = m->state_flags & A2S_BANK_MASK;
    uint32_t new_flags = old & ~bits;

    if (new_flags != old) {
        m->state_flags = (m->state_flags & ~A2S_BANK_MASK) | new_flags;
        apply_mem_state(m, old, new_flags);
    }
}

void softswitch_language_card(apple2_t *m, uint16_t address, int write_access)
{
    int odd = address & 1;
    int bits2 = address & 0x3;

    if ((address & 0x8) == 0) {
        softswitch_bank_set(m, A2S_LC_BANK2);
    } else {
        softswitch_bank_clear(m, A2S_LC_BANK2);
    }

    if (bits2 == 0 || bits2 == 3) {
        softswitch_bank_set(m, A2S_LC_READ);
    } else {
        softswitch_bank_clear(m, A2S_LC_READ);
    }

    if (!odd) {
        CLRF(m, A2S_LC_PRE_WRITE);
        softswitch_bank_clear(m, A2S_LC_WRITE);
    } else if (write_access) {
        CLRF(m, A2S_LC_PRE_WRITE);
    } else {
        if (FLAG(m, A2S_LC_PRE_WRITE)) {
            softswitch_bank_set(m, A2S_LC_WRITE);
        }
        SETF(m, A2S_LC_PRE_WRITE);
    }
}

void softswitch_apply_full_map(apple2_t *m)
{
    uint32_t s = m->state_flags;

    /* $0000-$01FF ZP/stack */
    apple2_pages_map_ram(m, false, off_zp(s, 0x0000), 0x0200);
    apple2_pages_map_ram(m, true, off_zp(s, 0x0000), 0x0200);

    /* $0200-$BFFF */
    apple2_pages_map_ram(m, false, off_rd(s, 0x0200), 0xBE00);
    apple2_pages_map_ram(m, true, off_wr(s, 0x0200), 0xBE00);

    if (s & A2S_80STORE) {
        apple2_pages_map_ram(m, false, off_vid(s, 0x0400), 0x0400);
        apple2_pages_map_ram(m, true, off_vid(s, 0x0400), 0x0400);
        if (s & A2S_HIRES) {
            apple2_pages_map_ram(m, false, off_vid(s, 0x2000), 0x2000);
            apple2_pages_map_ram(m, true, off_vid(s, 0x2000), 0x2000);
        }
    }

    /* $C000-$C0FF stays as RAM underlay; IO handled in bus. */
    apple2_pages_map_ram(m, false, 0xC000, 0x100);
    apple2_pages_map_ram(m, true, 0xC000, 0x100);

    /* $C100-$CFFF */
    map_cxrom(m);

    map_lc_read(m);
    map_lc_write(m);
}

void softswitch_setup_after_reset(apple2_t *m)
{
    m->state_flags &= ~A2S_RESET_MASK;
    m->strobed_slot = C800_NONE;
    m->last_io_select_slot = 0;

    softswitch_bank_clear(m, A2S_BANK_MASK);

    if (m->model == APPLE2_MODEL_II_PLUS) {
        softswitch_bank_set(m, A2S_SLOT3ROM_MB_DISABLE);
    }

    /* Power-on LC: bank 2, write enabled, pre-write armed (a2m). */
    softswitch_bank_set(m, A2S_LC_BANK2 | A2S_LC_WRITE | A2S_LC_PRE_WRITE);
    SETF(m, A2S_TEXT);

    /* Full map so //e $C300 internal ROM is live after reset. */
    softswitch_apply_full_map(m);
}

/* ---- C0xx handlers ------------------------------------------------------ */

static uint8_t rd_bit(apple2_t *m, uint32_t flag)
{
    return FLAG(m, flag) ? 0x80u : 0x00u;
}

static void kbdstrb(apple2_t *m)
{
    /* During host paste, acknowledge feeds the next character into $C000
       (a2m clipboard path) instead of only clearing the strobe. */
    if (apple2_paste_on_kbdstrb(m)) {
        return;
    }
    m->ram_main[SS_KBD] &= 0x7Fu;
}

uint8_t softswitch_c0_read(apple2_t *m, uint16_t address)
{
    uint8_t a = (uint8_t)(address & 0xFF);

    if (a < 0x10) {
        return m->ram_main[SS_KBD];
    }
    if (a < 0x20) {
        uint8_t v;
        switch (a) {
        case 0x10:
            v = FLAG(m, A2S_KEY_HELD) ? m->key_held : m->ram_main[SS_KBD];
            kbdstrb(m);
            return v;
        case 0x11: return rd_bit(m, A2S_LC_BANK2);
        case 0x12: return rd_bit(m, A2S_LC_READ);
        case 0x13: return rd_bit(m, A2S_RAMRD);
        case 0x14: return rd_bit(m, A2S_RAMWRT);
        case 0x15: return rd_bit(m, A2S_CXSLOTROM_MB_ENABLE);
        case 0x16: return rd_bit(m, A2S_ALTZP);
        case 0x17: return rd_bit(m, A2S_SLOT3ROM_MB_DISABLE);
        case 0x18: return rd_bit(m, A2S_80STORE);
        case 0x19:
            return apple2_video_in_vbl(m) ? 0x80u : 0x00u;
        case 0x1A: return rd_bit(m, A2S_TEXT);
        case 0x1B: return rd_bit(m, A2S_MIXED);
        case 0x1C: return rd_bit(m, A2S_PAGE2);
        case 0x1D: return rd_bit(m, A2S_HIRES);
        case 0x1E: return rd_bit(m, A2S_ALTCHARSET);
        case 0x1F: return rd_bit(m, A2S_COL80);
        default:
            kbdstrb(m);
            return floating_bus(m);
        }
    }
    if (a >= 0x30 && a < 0x40) {
        m->speaker_level = !m->speaker_level;
        return floating_bus(m);
    }
    if (a >= 0x50 && a < 0x58) {
        /* Soft switches also respond to reads. */
        softswitch_c0_write(m, address, 0);
        return floating_bus(m);
    }
    if (a >= 0x80 && a < 0x90) {
        softswitch_language_card(m, address, 0);
        return floating_bus(m);
    }
    if (a >= 0x90) {
        int slot = (a >> 4) & 0x7;
        switch (m->slot_type[slot]) {
        case SLOT_TYPE_DISKII:
            return softswitch_diskii(m, address, 0, 0);
        case SLOT_TYPE_SMARTPORT:
            switch (a & 0x0f) {
            case SP_DATA:
                return m->sp_device[slot].sp_buffer[m->sp_device[slot].sp_read_offset++];
            case SP_STATUS:
                return m->sp_device[slot].sp_status;
            default:
                return floating_bus(m);
            }
        case SLOT_TYPE_MOCKINGBOARD:
            return mockingboard_read(
                m, &m->mockingboard[slot], slot, address, (uint8_t)(a & 0x0F));
        default:
            break;
        }
        /* Legacy path if only diskii_present is set. */
        if (m->diskii_present[slot]) {
            return softswitch_diskii(m, address, 0, 0);
        }
    }
    /* Game port: buttons $C061–$C063, paddles $C064–$C067, PTRIG $C070–$C07F. */
    if (a == 0x61) {
        /* BUTN0 / Open-Apple: OR host fire with //e Open-Apple key. */
        if (FLAG(m, A2S_OPEN_APPLE) ||
            (m->gameport_buttons & APPLE2_GAMEPORT_BUTTON0) != 0) {
            return 0x80u;
        }
        return 0x00u;
    }
    if (a == 0x62) {
        if (FLAG(m, A2S_CLOSED_APPLE) ||
            (m->gameport_buttons & APPLE2_GAMEPORT_BUTTON1) != 0) {
            return 0x80u;
        }
        return 0x00u;
    }
    if (a == 0x63) {
        return (m->gameport_buttons & APPLE2_GAMEPORT_BUTTON2) != 0 ? 0x80u : 0x00u;
    }
    if (a >= 0x64 && a <= 0x67) {
        return softswitch_paddle_read(m, (int)(a - 0x64));
    }
    if (a >= 0x70 && a <= 0x7F) {
        apple2_gameport_ptrig(m);
        return floating_bus(m);
    }
    return floating_bus(m);
}

uint8_t softswitch_diskii(apple2_t *m, uint16_t address, int write_access, uint8_t write_value)
{
    uint8_t soft_switch = (uint8_t)(address & 0x0f);
    uint8_t slot = (uint8_t)((address >> 4) & 0x7);
    uint8_t value = floating_bus(m);

    if (soft_switch <= IWM_PH3_ON) {
        diskii_step_head(m, slot, soft_switch);
    } else {
        switch (soft_switch) {
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
            value = diskii_q6_access(m, slot, soft_switch & 1, write_access);
            break;
        case IWM_Q7_OFF:
        case IWM_Q7_ON:
            value = diskii_q7_access(m, slot, soft_switch & 1);
            break;
        default:
            break;
        }
    }
    if (write_access) {
        diskii_write_access(m, slot, write_value);
    }
    return value;
}

void softswitch_c0_write(apple2_t *m, uint16_t address, uint8_t value)
{
    uint8_t a = (uint8_t)(address & 0xFF);
    (void)value;

    if (m->model == APPLE2_MODEL_IIE_ENHANCED && a < 0x10) {
        switch (a) {
        case 0x00: softswitch_bank_clear(m, A2S_80STORE); return;
        case 0x01: softswitch_bank_set(m, A2S_80STORE); return;
        case 0x02: softswitch_bank_clear(m, A2S_RAMRD); return;
        case 0x03: softswitch_bank_set(m, A2S_RAMRD); return;
        case 0x04: softswitch_bank_clear(m, A2S_RAMWRT); return;
        case 0x05: softswitch_bank_set(m, A2S_RAMWRT); return;
        case 0x06: softswitch_bank_clear(m, A2S_CXSLOTROM_MB_ENABLE); return;
        case 0x07: softswitch_bank_set(m, A2S_CXSLOTROM_MB_ENABLE); return;
        case 0x08: softswitch_bank_clear(m, A2S_ALTZP); return;
        case 0x09: softswitch_bank_set(m, A2S_ALTZP); return;
        case 0x0A: softswitch_bank_clear(m, A2S_SLOT3ROM_MB_DISABLE); return;
        case 0x0B: softswitch_bank_set(m, A2S_SLOT3ROM_MB_DISABLE); return;
        case 0x0C: CLRF(m, A2S_COL80); return;
        case 0x0D: SETF(m, A2S_COL80); return;
        case 0x0E: CLRF(m, A2S_ALTCHARSET); return;
        case 0x0F: SETF(m, A2S_ALTCHARSET); return;
        default: break;
        }
    }

    if (a >= 0x10 && a < 0x20) {
        kbdstrb(m);
        return;
    }
    if (a >= 0x30 && a < 0x40) {
        m->speaker_level = !m->speaker_level;
        return;
    }
    if (a >= 0x50 && a < 0x60) {
        switch (a) {
        case 0x50: CLRF(m, A2S_TEXT); return;
        case 0x51: SETF(m, A2S_TEXT); return;
        case 0x52: CLRF(m, A2S_MIXED); return;
        case 0x53: SETF(m, A2S_MIXED); return;
        case 0x54: softswitch_bank_clear(m, A2S_PAGE2); return;
        case 0x55: softswitch_bank_set(m, A2S_PAGE2); return;
        case 0x56: softswitch_bank_clear(m, A2S_HIRES); return;
        case 0x57: softswitch_bank_set(m, A2S_HIRES); return;
        case 0x5E: SETF(m, A2S_DHIRES); return;
        case 0x5F: CLRF(m, A2S_DHIRES); return;
        default: return;
        }
    }
    if (a >= 0x70 && a <= 0x7F) {
        apple2_gameport_ptrig(m);
        return;
    }
    if (a >= 0x80 && a < 0x90) {
        softswitch_language_card(m, address, 1);
        return;
    }
    if (a >= 0x90) {
        int slot = (a >> 4) & 0x7;
        switch (m->slot_type[slot]) {
        case SLOT_TYPE_DISKII:
            (void)softswitch_diskii(m, address, 1, value);
            break;
        case SLOT_TYPE_SMARTPORT:
            switch (a & 0x0F) {
            case SP_DATA:
                m->sp_device[slot].sp_buffer[m->sp_device[slot].sp_write_offset++] = value;
                break;
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
                default:
                    break;
                }
                m->sp_device[slot].sp_status = 0x80;
                break;
            default:
                break;
            }
            break;
        case SLOT_TYPE_MOCKINGBOARD:
            mockingboard_write(
                m, &m->mockingboard[slot], slot, address, (uint8_t)(a & 0x0F), value);
            break;
        default:
            if (m->diskii_present[slot]) {
                (void)softswitch_diskii(m, address, 1, value);
            }
            break;
        }
    }
}

void softswitch_init_tables(void)
{
    /* Handlers are code-driven; nothing to build. */
}
