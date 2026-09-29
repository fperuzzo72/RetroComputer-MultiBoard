/* mac_machine.c - the Macintosh as a Machine, for a board.
 *
 * The emulation itself is mac_core.c. What is here is only what a board
 * needs: the ROM and disc linked into the firmware, the task loop that runs
 * it, the framebuffer handed to display_mono.h, and the Machine table.
 *
 * What it boots from is an entry, like a cartridge on the MSX: entry 0 is
 * the disc built into the firmware, copied into PSRAM (what the Mac writes
 * lasts until the power goes), and the rest are the images on the board's
 * card (media.h), read and written in place, so that what is saved stays.
 * The choice is remembered by name, the card's order being the card's.
 */
#include "machine.h"
#include "display_mono.h"
#include "mac.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

#include "umac.h"
#include "media.h"
#include "nvs.h"

/* The ROM and the boot disc, linked in from roms/mac/ by
 * tools/local_mac.py when they are present there. Neither is in the
 * repository. */
#ifdef HAVE_MAC_MEDIA
extern const uint8_t mac_rom_image[], mac_rom_image_end[];
extern const uint8_t mac_disc_image[], mac_disc_image_end[];
#endif

static volatile int running;
static volatile unsigned long vsyncs;
static volatile unsigned long steps;

/* -1 until something picks, then the entry to boot. */
static int selected = -1;

#define NVS_NS  "cyd"
#define NVS_KEY "macdisc"

static const char *entry_label(int i);

static void remember(int i)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, NVS_KEY, i == 0 ? "" : media_name(i - 1));
    nvs_commit(h);
    nvs_close(h);
}

static int recall(void)
{
    nvs_handle_t h;
    char name[48] = "";
    size_t len = sizeof name;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return 0;
    if (nvs_get_str(h, NVS_KEY, name, &len) != ESP_OK) name[0] = 0;
    nvs_close(h);
    for (int i = 0; name[0] && i < media_count(); i++)
        if (!strcmp(media_name(i), name)) return i + 1;
    return 0;
}

/* umac's disc callbacks, onto media.h's. */
static int card_read(void *ctx, uint8_t *data, unsigned offset, unsigned len)
{
    return media_read(ctx, data, offset, len);
}

static int card_write(void *ctx, uint8_t *data, unsigned offset, unsigned len)
{
    return media_write(ctx, data, offset, len);
}

static void loop_forever(void)
{
    display_mono_attach(mac_framebuffer(), DISP_WIDTH, DISP_HEIGHT);
    running = 1;

    uint64_t last_vsync = 0, last_yield = 0;
    for (;;) {
        uint64_t now = (uint64_t)esp_timer_get_time();
        mac_step(now);
        steps++;
        if (now - last_vsync >= 16667) {
            last_vsync = now;
            vsyncs++;
            display_mono_vsync();
        }
        /* The loop never blocks, so it gives this core's idle task
         * one tick every 100ms or the task watchdog would. 1%. */
        if (now - last_yield >= 100000) {
            last_yield = now;
            vTaskDelay(1);
        }
    }
}

static void m_run(void)
{
#ifdef HAVE_MAC_MEDIA
    const uint8_t *rom = mac_rom_image;
    const size_t rom_len = (size_t)(mac_rom_image_end - mac_rom_image);
    if (selected < 0) selected = recall();
    printf("mac: booting from %s\n", entry_label(selected));

    if (selected > 0) {
        uint32_t size = 0;
        void *h = media_open(selected - 1, &size);
        if (h && mac_start_ops(rom, rom_len, h, card_read, card_write, size) == 0) loop_forever();
        printf("mac: %s would not open, booting the built-in disc\n", entry_label(selected));
        selected = 0;
    }
    if (mac_start(rom, rom_len, mac_disc_image,
                  (size_t)(mac_disc_image_end - mac_disc_image)) == 0)
        loop_forever();
#else
    printf("mac: no ROM or disc in this firmware. Put macplus.rom and boot.img\n"
           "     in roms/mac/ and build again; see README, \"The Macintosh\".\n");
#endif
    running = 1;   /* nothing to wait for */
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}

static int m_prealloc(void) { return 1; }   /* PSRAM: nothing to race for */
static int m_ready(void) { return running; }
static unsigned long m_frames(void) { return vsyncs; }

static void m_hid(const uint8_t report[8]) { mac_hid_report(report); }
static int m_type(const char *text) { (void)text; return 0; }
static int m_typing(void) { return 0; }

static int m_screen_row(int row, uint8_t *out, int max) { (void)row; (void)out; (void)max; return 0; }
#define STR2(x) #x
#define STR(x) STR2(x)
static const char *m_screen_mode(void) { return STR(DISP_WIDTH) "x" STR(DISP_HEIGHT) " 1-bit"; }
static int m_char_pattern(int code, uint8_t *rows8) { (void)code; (void)rows8; return 0; }

static int m_peek(int addr)
{
    uint8_t *ram = mac_ram();
    if (!ram || addr < 0 || (unsigned)addr >= mac_ram_size()) return -1;
    return ram[addr];
}

static void m_set_sound(int on) { (void)on; }
static int m_sound_on(void) { return 0; }

/* The built-in disc by its HFS volume name, read from its Master
 * Directory Block (1024 bytes in, a Pascal string at offset 36). */
static const char *entry_label(int i)
{
    static char builtin[40];
    if (i > 0) return media_name(i - 1);
#ifdef HAVE_MAC_MEDIA
    if (!builtin[0]) {
        const uint8_t *mdb = mac_disc_image + 1024;
        int n = mdb[36] > 27 ? 27 : mdb[36];
        strcpy(builtin, "Embutido: ");
        if (mdb[0] == 'B' && mdb[1] == 'D' && n > 0) memcpy(builtin + 10, mdb + 37, (size_t)n);
        else strcpy(builtin + 10, "disco");
    }
#endif
    return builtin[0] ? builtin : "Embutido";
}

static int m_entry_count(void) { return 1 + media_count(); }
static const char *m_entry_name(int i) { return entry_label(i); }
static void m_select_entry(int i)
{
    if (i < 0 || i >= m_entry_count()) return;
    selected = i;
    remember(i);
}
static int m_selected_entry(void) { return selected < 0 ? recall() : selected; }
static void m_switch_to(int entry) { (void)entry; umac_reset(); }

/* `s`: how fast the Mac runs. Each step is 5ms of emulated time, so
 * steps per wall-clock second times 5 is emulated milliseconds per second,
 * and a tenth of that is the percentage of a real Mac Plus. */
static int m_debug_command(const char *line)
{
    static unsigned long last_steps;
    static uint64_t last_us;
    if (strcmp(line, "s") != 0) return 0;
    uint64_t now = (uint64_t)esp_timer_get_time();
    unsigned long n = steps;
    if (last_us) {
        double secs = (double)(now - last_us) / 1e6;
        double pct = (double)(n - last_steps) * 5.0 / 10.0 / secs;
        int x, y;
        mac_cursor(&x, &y);
        printf("mac: %.0f%% of a Mac Plus over the last %.1fs, cursor %d,%d\n", pct, secs, x, y);
    } else {
        printf("mac: speed needs two readings, ask again\n");
    }
    last_steps = n;
    last_us = now;
    return 1;
}

static const char *m_debug_help(void) { return "s  speed and cursor\n"; }

static void m_pointer(int x, int y, int button) { mac_pointer(x, y, button); }

const Machine mac_machine = {
    "Macintosh Plus",
    1,
    m_prealloc, m_run, m_ready, m_frames,
    m_hid, m_type, m_typing,
    m_screen_row, m_screen_mode, m_char_pattern, m_peek, NULL,
    m_set_sound, m_sound_on,
    m_entry_count, m_entry_name, m_select_entry, m_selected_entry, m_switch_to,
    m_debug_command, m_debug_help,
    m_pointer,
};
