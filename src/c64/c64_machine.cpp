/* c64_machine.cpp - the Commodore 64 as a Machine, for a board.
 *
 * lib/c64 is retroelec's T-HMI-C64 core with this project's drivers
 * (retro_c64_drivers.h). What is here is what a board needs from it:
 *
 *   - the 6502 runs in the machine's task (C64Sys::run, paced to 50 PAL
 *     frames a second, never returns); a task on the other core takes the
 *     VIC's picture to the panel through display.h, and stands the 6502
 *     still while the selector is open;
 *   - the picture is 320x200 with the border colour beside it, drawn by
 *     the board's display8 at 2x, tones by contrast with the border, as
 *     the MSX's are;
 *   - the SID's frames of samples go to the board's buzzer as PCM
 *     (audio_start_push), the keyboard to c64_keys.cpp's matrix;
 *   - what it starts with: entry 0 is BASIC, the rest the .prg and .d64
 *     files in /c64/ on the card. A .d64 goes in drive 8; then, once BASIC
 *     is up, LOAD and RUN are typed for you, as a person would.
 */
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "machine.h"
#include "display.h"
#include "selector.h"

#include "C64Sys.h"
#include "platform/PlatformFactory.h"
#include "platform/PlatformManager.h"
#include "roms/charset.h"

#include "c64_keys.h"

#ifdef ARDUINO
#include <Arduino.h>
#include <SDCardManager.h>
#include "audio.h"
#include "esp_heap_caps.h"
#include "nvs.h"
#endif

/* Pepto's palette as RGB565, for display8's tones. VIC colour indices are
 * 0-15; a 256-entry table lets display8 index it with whatever byte it
 * finds. */
static uint16_t pal565[256];
static void make_palette(void)
{
    static const uint8_t pepto[16][3] = {
        {0, 0, 0}, {255, 255, 255}, {104, 55, 43}, {112, 164, 178}, {111, 61, 134}, {88, 141, 67},
        {53, 40, 121}, {184, 199, 111}, {111, 79, 37}, {67, 57, 0}, {154, 103, 89}, {68, 68, 68},
        {108, 108, 108}, {154, 210, 132}, {108, 94, 181}, {149, 149, 149}};
    for (int i = 0; i < 256; i++) {
        const uint8_t *c = pepto[i & 15];
        pal565[i] = (uint16_t)(((c[0] >> 3) << 11) | ((c[1] >> 2) << 5) | (c[2] >> 3));
    }
}

static C64Sys *cpu;
static uint8_t *ram;
static volatile int running;
static volatile unsigned long frames;

/* --- what goes to the board --------------------------------------------- */

void retro_c64_picture(const uint8_t *bitmap, uint8_t border)
{
    /* 200 lines in display8's 216, centred */
    display_write_picture(0, 8, 320, 200, bitmap, pal565[border & 15], pal565);
}

void retro_c64_audio(const int16_t *samples, size_t n)
{
    frames++;
#ifdef ARDUINO
    audio_write(samples, (unsigned)n);
#else
    (void)samples; (void)n;
#endif
}

/* --- entries: BASIC, then the card's /c64/ ------------------------------ */

static std::vector<std::string> files;   /* names in /c64/, extension kept */
static int selected = -1;

static bool wanted(const char *n)
{
    const char *dot = strrchr(n, '.');
    return dot && n[0] != '.' && (!strcasecmp(dot, ".prg") || !strcasecmp(dot, ".d64"));
}

static void list_files(void)
{
    static bool done;
    if (done) return;
    done = true;
#ifdef ARDUINO
    FsFile dir = SDCardManager::getInstance().open("/c64");
    if (!dir || !dir.isDirectory()) return;
    FsFile f;
    char n[64];
    while (files.size() < 64 && f.openNext(&dir, O_RDONLY)) {
        f.getName(n, sizeof n);
        if (!f.isDirectory() && wanted(n)) files.push_back(n);
        f.close();
    }
    dir.close();
#endif
}

#ifdef ARDUINO
#define NVS_NS  "cyd"
#define NVS_KEY "c64entry"

static void remember(int i)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, NVS_KEY, i > 0 && i - 1 < (int)files.size() ? files[i - 1].c_str() : "");
    nvs_commit(h);
    nvs_close(h);
}

static int recall(void)
{
    nvs_handle_t h;
    char name[64] = "";
    size_t len = sizeof name;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return 0;
    if (nvs_get_str(h, NVS_KEY, name, &len) != ESP_OK) name[0] = 0;
    nvs_close(h);
    list_files();
    for (size_t i = 0; name[0] && i < files.size(); i++)
        if (files[i] == name) return (int)i + 1;
    return 0;
}
#else
static void remember(int i) { (void)i; }
static int recall(void) { return 0; }
#endif

static int m_entry_count(void) { list_files(); return 1 + (int)files.size(); }

static const char *m_entry_name(int i)
{
    static std::string label;
    list_files();
    if (i <= 0 || i - 1 >= (int)files.size()) return "BASIC";
    label = files[i - 1];
    const size_t dot = label.rfind('.');
    if (dot != std::string::npos) label.erase(dot);
    return label.c_str();
}

static void m_select_entry(int i) { selected = i; remember(i); }
static int  m_selected_entry(void) { return selected < 0 ? recall() : selected; }

/* Another game while one runs: the 6502 cannot be put back to the moment
 * of power-on from here, so the board restarts straight into it. */
static void m_switch_to(int entry)
{
    m_select_entry(entry);
    selector_restart_into_choice();
}

/* --- typing LOAD and RUN, as a person would ---------------------------- */

static std::string to_type;
static size_t typed;
static unsigned long type_after;

static uint8_t usage_of(char ch, bool *shift)
{
    static const char plain[] = "abcdefghijklmnopqrstuvwxyz1234567890\n";
    static const char shifted[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*()\n";
    const char *p;
    *shift = false;
    if ((p = strchr(plain, ch))) return (uint8_t)(0x04 + (p - plain));
    if ((p = strchr(shifted, ch))) { *shift = true; return (uint8_t)(0x04 + (p - shifted)); }
    if (ch == '"') { *shift = true; return 0x34; }
    if (ch == ',') return 0x36;
    if (ch == '*') { *shift = true; return 0x25; }
    if (ch == '.') return 0x37;
    if (ch == ' ') return 0x2c;
    return 0;
}

/* One step every call, a call every refresh: a key down for two, up for
 * two. */
static void type_step(void)
{
    static int phase;
    if (typed >= to_type.size() || frames < type_after) return;
    uint8_t r[8] = {0};
    if (phase < 2) {
        bool shift;
        const char ch = to_type[typed];
        r[2] = usage_of(ch == '\n' ? '\n' : (char)tolower(ch), &shift);
        if (ch == '"' || ch == '*') r[2] = usage_of(ch, &shift);
        if (shift) r[0] = 0x02;
    }
    retro_c64_keys_report(r);
    if (++phase >= 4) { phase = 0; typed++; }
}

/* --- the tasks ---------------------------------------------------------- */

static void refresh_task(void *arg)
{
    (void)arg;
    /* As often as the board's panel wants pictures: display.h's share of
     * frames, the MSX's knob, at the C64's 50 frames a second. */
#ifdef DISPLAY_FRAME_PERCENT
    const unsigned ms = 2000 / DISPLAY_FRAME_PERCENT;
#else
    const unsigned ms = 20;
#endif
    for (;;) {
        if (selector_active()) {
            cpu->cpuhalted = true;
            int chosen = -1;
            while (selector_active()) {
                const int e = selector_frame();
                if (e >= 0) chosen = e;
                PlatformManager::getInstance().waitMS(20);
            }
            display_fill_panel(0);
            cpu->cpuhalted = false;
            if (chosen >= 0) m_switch_to(chosen);
        }
        if (display_take_repaint()) display8_request_repaint();
        PlatformManager::getInstance().lock();
        cpu->vic.refresh();
        PlatformManager::getInstance().unlock();
        type_step();
        PlatformManager::getInstance().waitMS(ms);
    }
}

static void m_run(void)
{
    PlatformManager::initialize(PlatformNS::create());
#ifndef HAVE_C64_ROMS
    printf("c64: no ROMs in this firmware. Put basic.bin, kernal.bin, chargen.bin\n"
           "     and 1541.bin in roms/c64/, run tools/make_c64_roms.py, build again.\n");
    running = 1;
    for (;;) PlatformManager::getInstance().waitMS(1000);
#else
    make_palette();
#ifdef ARDUINO
    ram = (uint8_t *)heap_caps_malloc(65536, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
    if (!ram) ram = new uint8_t[65536];
    cpu = new C64Sys();
    cpu->init(ram, charset_rom);
    retro_c64_keys_attach(cpu);

    if (selected < 0) selected = recall();
    list_files();
    if (selected > 0 && selected - 1 < (int)files.size()) {
        const std::string &f = files[selected - 1];
        const size_t dot = f.rfind('.');
        std::string base = f.substr(0, dot);
        for (auto &c : base) c = (char)tolower(c);
        if (!strcasecmp(f.c_str() + dot, ".d64")) {
            printf("c64: %s in drive 8: %s\n", f.c_str(), cpu->floppy.attach(f) ? "ok" : "FAILED");
            to_type = "load\"*\",8,1\nrun\n";
        } else {
            to_type = "load\"" + base + "\",8,1\nrun\n";
        }
        type_after = 150;   /* three seconds: BASIC is up long before */
        printf("c64: starting %s\n", f.c_str());
    } else {
        printf("c64: BASIC\n");
    }

    PlatformManager::getInstance().startIntervalTimer([]() { cpu->scanKeyboard(); }, 8000);
    PlatformManager::getInstance().startIntervalTimer([]() { cpu->cia1.updateTOD(); cpu->cia2.updateTOD(); }, 100000);
#ifdef ARDUINO
    const unsigned rate = audio_start_push(AUDIO_SAMPLE_RATE);
    printf("c64: sound %s\n", rate ? "on" : "off");
#endif
    PlatformManager::getInstance().startTask([](void *) { refresh_task(nullptr); }, 0, 2);
    running = 1;
    cpu->run();
#endif
}

static int m_prealloc(void) { return 1; }   /* PSRAM: nothing to race for */
static int m_ready(void) { return running; }
static unsigned long m_frames(void) { return frames; }

static void m_hid(const uint8_t report[8]) { retro_c64_keys_report(report); }
static int m_type(const char *text) { (void)text; return 0; }
static int m_typing(void) { return typed < to_type.size(); }

static int m_screen_row(int row, uint8_t *out, int max)
{
    /* the text screen at $0400, as screen codes */
    if (!ram || row < 0 || row >= 25) return 0;
    const int n = max < 40 ? max : 40;
    memcpy(out, ram + 0x400 + row * 40, (size_t)n);
    return n;
}
static const char *m_screen_mode(void) { return "VIC-II 320x200"; }
static int m_char_pattern(int code, uint8_t *rows8)
{
    if (code < 0 || code > 255) return 0;
    memcpy(rows8, charset_rom + code * 8, 8);
    return 1;
}
static int m_peek(int addr) { return ram && addr >= 0 && addr < 65536 ? ram[addr] : -1; }

static void m_set_sound(int on) { (void)on; }
static int m_sound_on(void) { return 1; }

static int m_debug_command(const char *line)
{
    if (!strcmp(line, "j")) {
        const int p = retro_c64_joystick_on();
        printf("c64: keyboard joystick %s%s\n", p ? "on port " : "off", p ? (p == 1 ? "1" : "2") : "");
        return 1;
    }
    return 0;
}
static const char *m_debug_help(void) { return "j  keyboard joystick (F10 on/off, F9 port)\n"; }

extern "C" const Machine c64_machine = {
    "Commodore 64",
    1,
    m_prealloc, m_run, m_ready, m_frames,
    m_hid, m_type, m_typing,
    m_screen_row, m_screen_mode, m_char_pattern, m_peek, nullptr,
    m_set_sound, m_sound_on,
    m_entry_count, m_entry_name, m_select_entry, m_selected_entry, m_switch_to,
    m_debug_command, m_debug_help,
    nullptr,
};
