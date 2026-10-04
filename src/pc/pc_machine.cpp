/* pc_machine.cpp - the PC as a Machine, for a board.
 *
 * The emulation is pc_core.c over lib/pc8086 (M5PaperDOS's 8086 core).
 * What is here is what a board needs from it:
 *
 *   - the 8086 runs in the machine's task in slices of 20ms of real time,
 *     yielding a tick between them (a loop that never blocks starves the
 *     BLE stack's start-up, as the C64's did);
 *   - the screen is a 640x400 1-bit picture (pc_text.c) handed to the
 *     board through display_mono.h, like the Mac's, and scaled by it;
 *   - what it boots from is an entry: the disk images in /pc/ on the card,
 *     and /msdos.img at its root, where M5PaperDOS keeps its disk (left
 *     there so that firmware still finds it).
 *     A hard disk image boots as C:, a floppy-sized one (up to 2.88MB) as
 *     A:, with the first hard disk image beside it as C:. Writes go to the
 *     image in place, so what is saved stays. tools/sd_put.py puts images
 *     there over USB;
 *   - F12, or a tap on the panel, opens the selector, and the PC stands
 *     still while it is open.
 */
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "machine.h"
#include "display_mono.h"
#include "selector.h"

#include "pc_core.h"
#include "pc_keys.h"
#include "pc_text.h"

#include <Arduino.h>
#include <SDCardManager.h>
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "nvs.h"

#define PC_DIR "/pc"
#define FLOPPY_MAX (2949120u)   /* 2.88MB: anything larger is a hard disk */

static volatile int running;
static volatile unsigned long frames;
static uint8_t *fb;

/* --- entries: the images in /pc/ ----------------------------------------- */

struct Image { std::string name; std::string path; uint32_t size; };
static std::vector<Image> images;
static int selected = -1;

static bool wanted(const char *n)
{
    const char *dot = strrchr(n, '.');
    return dot && n[0] != '.' &&
           (!strcasecmp(dot, ".img") || !strcasecmp(dot, ".ima") || !strcasecmp(dot, ".vhd"));
}

static void list_images(void)
{
    static bool done;
    if (done) return;
    done = true;
    FsFile m = SDCardManager::getInstance().open("/msdos.img");
    if (m) {
        images.push_back({"msdos.img", "/msdos.img", (uint32_t)m.fileSize()});
        m.close();
    }
    FsFile dir = SDCardManager::getInstance().open(PC_DIR);
    if (!dir || !dir.isDirectory()) return;
    FsFile f;
    char n[64];
    while (images.size() < 32 && f.openNext(&dir, O_RDONLY)) {
        f.getName(n, sizeof n);
        if (!f.isDirectory() && wanted(n))
            images.push_back({n, std::string(PC_DIR "/") + n, (uint32_t)f.fileSize()});
        f.close();
    }
    dir.close();
}

#define NVS_NS  "cyd"
#define NVS_KEY "pcimage"

static void remember(int i)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, NVS_KEY, i >= 0 && i < (int)images.size() ? images[i].path.c_str() : "");
    nvs_commit(h);
    nvs_close(h);
}

static int recall(void)
{
    nvs_handle_t h;
    char name[64] = "";
    size_t len = sizeof name;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_str(h, NVS_KEY, name, &len) != ESP_OK) name[0] = 0;
        nvs_close(h);
    }
    list_images();
    for (size_t i = 0; name[0] && i < images.size(); i++)
        if (images[i].path == name) return (int)i;
    return 0;
}

static int m_entry_count(void) { list_images(); return images.empty() ? 1 : (int)images.size(); }

static const char *m_entry_name(int i)
{
    static std::string label;
    list_images();
    if (images.empty()) return "Sem disco no cartão";
    if (i < 0 || i >= (int)images.size()) i = 0;
    label = images[i].name;
    const size_t dot = label.rfind('.');
    if (dot != std::string::npos) label.erase(dot);
    return label.c_str();
}

/* The code page, remembered: 860 unless 437 was asked for. */
static void load_codepage(void)
{
    nvs_handle_t h;
    int32_t cp = 860;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_i32(h, "pccp", &cp);
        nvs_close(h);
    }
    pc_keys_set_codepage((int)cp);
}

static void save_codepage(int cp)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i32(h, "pccp", cp);
    nvs_commit(h);
    nvs_close(h);
}

static void m_select_entry(int i) { selected = i; remember(i); }
static int  m_selected_entry(void) { return selected < 0 ? recall() : selected; }

/* Another disk while one runs: the board restarts straight into it, as
 * switching the power off and on with the other disk in would. */
static void m_switch_to(int entry)
{
    m_select_entry(entry);
    selector_restart_into_choice();
}

/* --- typing, for the console ---------------------------------------------- */

static std::string to_type;
static size_t typed;

static int m_type(const char *text)
{
    to_type.assign(text);
    for (auto &c : to_type) if (c == '|') c = ' ';   /* the console's space */
    typed = 0;
    return 1;
}
static int m_typing(void) { return typed < to_type.size(); }

/* --- the loop ------------------------------------------------------------- */

static void show_message(const char *line)
{
    /* the PC's own screen is not up: write straight into the picture */
    memset(fb, 0, PC_FB_W / 8 * PC_FB_H);
    printf("pc: %s\n", line);
}


static void loop_forever(void)
{
    uint64_t last_draw = 0;
    for (;;) {
        if (selector_active()) {
            int chosen = -1;
            while (selector_active()) {
                const int e = selector_frame();
                if (e >= 0) chosen = e;
                vTaskDelay(pdMS_TO_TICKS(20));
            }
            /* the menu had the panel: everything is drawn again */
            pc_text_invalidate();
            display_mono_attach(fb, PC_FB_W, PC_FB_H);
            if (chosen >= 0) m_switch_to(chosen);
        }
        pc_core_run(20000);
        if (typed < to_type.size()) {
            const char c = to_type[typed++];
            pc_keys_type(c == '\n' ? '\r' : c);
        }
        const uint64_t now = (uint64_t)esp_timer_get_time();
        if (now - last_draw >= 50000) {
            last_draw = now;
            if (pc_text_render(fb)) frames++;
            display_mono_vsync();
        }
        vTaskDelay(1);
    }
}

static void m_run(void)
{
    load_codepage();
    fb = (uint8_t *)heap_caps_malloc(PC_FB_W / 8 * PC_FB_H, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    memset(fb, 0, PC_FB_W / 8 * PC_FB_H);
    display_mono_attach(fb, PC_FB_W, PC_FB_H);
    running = 1;

    if (selected < 0) selected = recall();
    list_images();
    if (images.empty()) {
        show_message("no disk images on the card (/pc/*.img or /msdos.img)");
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    if (selected < 0 || selected >= (int)images.size()) selected = 0;

    const Image &img = images[selected];
    std::string boot = img.path, c_drive;
    const bool floppy = img.size <= FLOPPY_MAX;
    if (floppy) {
        /* a hard disk beside the floppy, if there is one */
        for (const Image &o : images)
            if (o.size > FLOPPY_MAX) { c_drive = o.path; break; }
    }
    printf("pc: booting %s as %s%s%s\n", img.name.c_str(), floppy ? "A:" : "C:",
           c_drive.empty() ? "" : ", C: is ", c_drive.empty() ? "" : c_drive.c_str());
    const bool ok = floppy
        ? pc_core_init(c_drive.empty() ? nullptr : c_drive.c_str(), boot.c_str())
        : pc_core_init(boot.c_str(), nullptr);
    if (!ok) {
        show_message(pc_core_error());
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    loop_forever();
}

static int m_prealloc(void) { return 1; }   /* PSRAM: nothing to race for */
static int m_ready(void) { return running; }
static unsigned long m_frames(void) { return frames; }

static void m_hid(const uint8_t report[8])
{
    if (pc_keys_report(report)) selector_open();
}

static int m_screen_row(int row, uint8_t *out, int max)
{
    pc_screen s;
    pc_core_screen(&s);
    if (s.graphics || !s.cells || row < 0 || row >= s.rows) return 0;
    const int n = max < s.cols ? max : s.cols;
    for (int c = 0; c < n; c++) out[c] = s.cells[(row * s.cols + c) * 2];
    return n;
}

static const char *m_screen_mode(void)
{
    static char m[24];
    pc_screen s;
    pc_core_screen(&s);
    snprintf(m, sizeof m, "mode %02Xh%s", s.mode, s.graphics ? " graphics" : "");
    return m;
}

static int m_char_pattern(int code, uint8_t *rows8) { (void)code; (void)rows8; return 0; }
static int m_peek(int addr) { (void)addr; return -1; }
static void m_set_sound(int on) { (void)on; }
static int m_sound_on(void) { return 0; }

static int m_debug_command(const char *line)
{
    /* `w dir` types "dir" and Enter, `wn dir` without the Enter; spaces
     * travel as '|', as for put */
    if (!strncmp(line, "w ", 2) || !strncmp(line, "wn ", 3)) {
        const bool enter = line[1] == ' ';
        std::string s(line + (enter ? 2 : 3));
        if (enter) s += '\n';
        m_type(s.c_str());
        return 1;
    }
    int cp;
    if (sscanf(line, "cp %d", &cp) == 1 && (cp == 437 || cp == 860)) {
        pc_keys_set_codepage(cp);
        save_codepage(cp);
        pc_text_invalidate();
        printf("pc: code page %d, for the screen and the accents\n", cp);
        return 1;
    }
    if (!strcmp(line, "cp")) {
        printf("pc: code page %d (cp 860 Portuguese, cp 437 US)\n", pc_keys_codepage());
        return 1;
    }
    if (!strcmp(line, "s")) {
        char p[160];
        pc_core_profile(p, sizeof p);
        printf("pc: %lu instructions/s; %s\n", (unsigned long)pc_core_ips(), p);
        return 1;
    }
    return 0;
}
static const char *m_debug_help(void) { return "s  instructions per second\nw TEXT  type TEXT and Enter ( | for space); wn TEXT without Enter\ncp 860|437  code page for the screen and the accents\n"; }

extern "C" const Machine pc_machine = {
    "PC (MS-DOS)",
    1,
    m_prealloc, m_run, m_ready, m_frames,
    m_hid, m_type, m_typing,
    m_screen_row, m_screen_mode, m_char_pattern, m_peek, nullptr,
    m_set_sound, m_sound_on,
    m_entry_count, m_entry_name, m_select_entry, m_selected_entry, m_switch_to,
    m_debug_command, m_debug_help,
    nullptr,
};
