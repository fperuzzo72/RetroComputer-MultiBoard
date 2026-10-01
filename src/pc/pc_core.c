/* pc_core.c - the PC: lib/pc8086 started the way M5PaperDOS's main.cpp
 * starts it (MIT; boot sector handling taken from there), and run in
 * slices the board calls. Plain C, no board. */
#include "pc_core.h"

#include <stdio.h>
#include <string.h>

#include "esp_timer.h"
#include "pc8086.h"

int g_c_drive_dos33_compat = 0;

#define TICK_US 54945          /* 18.2Hz, the PC's timer */
#define CYCLES_PER_SLICE 1000  /* M5PaperDOS's CPU_CYCLES_PER_TICK */

static cpu8086_t cpu;
static const char *err = "";
static int64_t next_tick_us;
static uint64_t ips_cycles;
static int64_t ips_since_us;
static uint32_t ips;
static uint64_t t_cpu, t_ports, t_irq, t_total, n_batches;

const char *pc_core_error(void) { return err; }

static uint16_t le16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

/* M5PaperDOS: a FAT boot sector, as opposed to an MBR */
static bool looks_like_fat_boot_sector(const uint8_t *s)
{
    if (s[510] != 0x55 || s[511] != 0xAA) return false;
    if (s[0] != 0xEB && s[0] != 0xE9) return false;
    const uint8_t spc = s[13];
    if (le16(s + 11) != 512 || spc == 0 || (spc & (spc - 1))) return false;
    if (le16(s + 14) == 0 || s[16] == 0 || s[16] > 2) return false;
    if (le16(s + 17) == 0) return false;
    return le16(s + 19) != 0 || le32(s + 32) != 0;
}

/* M5PaperDOS boots a hard disk's active partition directly, skipping the
 * MBR's own code; kept, it is what its images were made with. */
static bool load_boot_sector(uint8_t drive)
{
    uint8_t s[512];
    if (!disk_read_chs(drive, 0, 0, 1, 1, s)) { err = "boot sector unreadable"; return false; }
    if (!looks_like_fat_boot_sector(s) && s[510] == 0x55 && s[511] == 0xAA) {
        for (int i = 0; i < 4; i++) {
            const uint8_t *p = s + 446 + 16 * i;
            if (p[0] == 0x80 && p[4] && le32(p + 8) && le32(p + 12)) {
                uint8_t vbr[512];
                if (disk_read_lba(drive, le32(p + 8), 1, vbr) && looks_like_fat_boot_sector(vbr))
                    memcpy(s, vbr, sizeof s);
                break;
            }
        }
    }
    mem_write_block(0x7C00, s, sizeof s);
    return true;
}

bool pc_core_init(const char *c_image, const char *a_image)
{
    dos_mem_init();
    xms_init();
    ports_init();
    video_init();
    if (!mem_load_bios(embedded_8086tiny_bios, embedded_8086tiny_bios_len)) { err = "BIOS"; return false; }
    interrupts_init();
    bios_init();
    disk_init();

    const bool have_c = c_image && disk_mount(0x80, c_image, DRIVE_HDD);
    const bool have_a = a_image && disk_mount(0x00, a_image, DRIVE_FLOPPY);
    bios_set_floppy_drives(have_a ? 1 : 0);
    bios_set_hard_drives(have_c ? 1 : 0);
    if (!have_c && !have_a) { err = "no disk image"; return false; }

    const uint8_t boot = have_c ? 0x80 : 0x00;
    cpu_init(&cpu);
    if (!load_boot_sector(boot)) return false;
    cpu.regs16[REG_DX] = (cpu.regs16[REG_DX] & 0xFF00) | boot;
    cpu.sregs[SEG_CS] = cpu.sregs[SEG_DS] = cpu.sregs[SEG_ES] = cpu.sregs[SEG_SS] = 0;
    cpu.ip = 0x7C00;
    cpu.regs16[REG_SP] = 0x7C00;

    next_tick_us = esp_timer_get_time() + TICK_US;
    ips_since_us = esp_timer_get_time();
    return true;
}

void pc_core_run(uint32_t us)
{
    const int64_t until = esp_timer_get_time() + us;
    const int64_t start = esp_timer_get_time();
    do {
        const int64_t a = esp_timer_get_time();
        ips_cycles += cpu_exec_cycles(&cpu, CYCLES_PER_SLICE);
        const int64_t b = esp_timer_get_time();
        ports_tick();
        const int64_t c = esp_timer_get_time();
        if (interrupts_are_enabled()) interrupt_hardware(&cpu);
        const int64_t now = esp_timer_get_time();
        t_cpu += b - a; t_ports += c - b; t_irq += now - c; n_batches++;
        if (now >= next_tick_us) {
            bios_timer_tick();
            next_tick_us += TICK_US;
            if (now - next_tick_us > 10 * TICK_US) next_tick_us = now + TICK_US;
        }
    } while (esp_timer_get_time() < until);
    t_total += esp_timer_get_time() - start;

    const int64_t now = esp_timer_get_time();
    if (now - ips_since_us >= 1000000) {
        ips = (uint32_t)(ips_cycles * 1000000 / (uint64_t)(now - ips_since_us));
        ips_cycles = 0;
        ips_since_us = now;
    }
}

uint32_t pc_core_ips(void) { return ips; }

void pc_core_profile(char *out, int n)
{
    snprintf(out, n, "batches %llu (avg %llu instr), cpu %llu%%, ports %llu%%, irq %llu%%, halted %d",
             (unsigned long long)n_batches,
             (unsigned long long)(n_batches ? cpu.cycles / n_batches : 0),
             (unsigned long long)(t_total ? t_cpu * 100 / t_total : 0),
             (unsigned long long)(t_total ? t_ports * 100 / t_total : 0),
             (unsigned long long)(t_total ? t_irq * 100 / t_total : 0), (int)cpu.halted);
    t_cpu = t_ports = t_irq = t_total = n_batches = 0;
    cpu.cycles = 0;
}

void pc_core_screen(pc_screen *s)
{
    const uint8_t mode = video_get_mode() & 0x7F;
    uint8_t r, c;
    video_get_cursor_pos(&r, &c);
    s->mode = mode;
    s->graphics = !(mode <= 3 || mode == 7);
    s->cols = mem_read_byte(0x44A) ? mem_read_byte(0x44A) : 80;
    if (s->cols > 80) s->cols = 80;
    s->rows = 25;
    s->cursor_row = r;
    s->cursor_col = c;
    s->cursor_visible = true;
    s->cells = video_get_buffer();
}

void pc_core_key(uint8_t scancode, uint8_t ascii, bool extended, bool pressed)
{
    if (pressed) bios_key_enqueue(scancode, ascii, extended);
    kb_set_scancode_ext(pressed ? scancode : (uint8_t)(scancode | 0x80), extended);
}
