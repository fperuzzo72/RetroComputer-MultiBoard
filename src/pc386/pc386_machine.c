/* pc386_machine.c - a 386/486 PC with VGA, as a Machine: DOSBox Pure.
 *
 * lib/dosbox-pure is a libretro core, built by the Android app as its own
 * library (libdosbox_pure.so) and loaded here with dlopen, so that its
 * hundreds of globals stay out of the other cores' way. This file is a
 * small libretro frontend, the same one as tools/dosboxhost, on the board's
 * calls instead of a PNG:
 *
 *   - the core runs in the machine's thread, a retro_run() a frame at its
 *     own rate (70Hz in text and VGA modes), paced by the clock; it keeps
 *     time by the wall clock itself;
 *   - its picture (XRGB8888, up to 1024x768) goes to the board as RGB565
 *     (display_write_rgb565), and only what changed counts;
 *   - its stereo sound, mixed to one channel, to the board's audio
 *     (audio_start_push), the queue kept under 100ms;
 *   - the board's keyboard reports become key events and key states; the
 *     pointer (the trackpad) becomes the mouse's movement and its button;
 *   - what it starts with: entry 0 is the command line with no disk, the
 *     rest the card's pc386/ folder (.zip, .img, .ima, .vhd, .iso, .cue,
 *     .conf) and the 8086 PC's /msdos.img. DOSBox Pure's own start menu
 *     then lists the programs in it. Another entry is a start over, as on
 *     the C64.
 *
 * Android only: there is no 386 on the ESP32 boards.
 */
#include <dirent.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "machine.h"
#include "display.h"
#include "selector.h"
#include "audio.h"
#include "nvs.h"
#include "retro_board.h"

#include "libretro.h"

/* --- the core ------------------------------------------------------------ */

static void (*core_set_environment)(retro_environment_t);
static void (*core_set_video_refresh)(retro_video_refresh_t);
static void (*core_set_audio_sample)(retro_audio_sample_t);
static void (*core_set_audio_sample_batch)(retro_audio_sample_batch_t);
static void (*core_set_input_poll)(retro_input_poll_t);
static void (*core_set_input_state)(retro_input_state_t);
static void (*core_init)(void);
static bool (*core_load_game)(const struct retro_game_info *);
static void (*core_get_system_av_info)(struct retro_system_av_info *);
static void (*core_run)(void);

static int load_core(void)
{
    void *lib = dlopen("libdosbox_pure.so", RTLD_NOW | RTLD_LOCAL);
    if (!lib) { printf("pc386: %s\n", dlerror()); return 0; }
#define SYM(var, name) if (!(*(void **)&var = dlsym(lib, name))) { printf("pc386: no %s\n", name); return 0; }
    SYM(core_set_environment, "retro_set_environment");
    SYM(core_set_video_refresh, "retro_set_video_refresh");
    SYM(core_set_audio_sample, "retro_set_audio_sample");
    SYM(core_set_audio_sample_batch, "retro_set_audio_sample_batch");
    SYM(core_set_input_poll, "retro_set_input_poll");
    SYM(core_set_input_state, "retro_set_input_state");
    SYM(core_init, "retro_init");
    SYM(core_load_game, "retro_load_game");
    SYM(core_get_system_av_info, "retro_get_system_av_info");
    SYM(core_run, "retro_run");
#undef SYM
    return 1;
}

static volatile int running;
static volatile unsigned long frames;

/* --- the picture ------------------------------------------------------- */

#define PIC_MAX_W 1024
#define PIC_MAX_H 768
static uint16_t frame565[PIC_MAX_W * PIC_MAX_H];
static enum retro_pixel_format pixfmt = RETRO_PIXEL_FORMAT_0RGB1555;

static uint8_t to5[256], to6[256];

static void video(const void *data, unsigned w, unsigned h, size_t pitch)
{
    if (!to6[255])
        for (int i = 0; i < 256; i++) {
            to5[i] = (uint8_t)((i * 31 + 127) / 255);
            to6[i] = (uint8_t)((i * 63 + 127) / 255);
        }
    frames++;
    if (!data) return;                  /* the same as last time */
    if (w > PIC_MAX_W) w = PIC_MAX_W;
    if (h > PIC_MAX_H) h = PIC_MAX_H;
    for (unsigned y = 0; y < h; y++) {
        const uint8_t *row = (const uint8_t *)data + y * pitch;
        uint16_t *o = frame565 + y * w;
        if (pixfmt == RETRO_PIXEL_FORMAT_XRGB8888) {
            /* rounded, not cut: cut, the menu's blue-grey (25,29,31) came
             * out a faintly green grey, and the Boox's colour boost made
             * that a plain dark green */
            const uint32_t *p = (const uint32_t *)row;
            for (unsigned x = 0; x < w; x++)
                o[x] = (uint16_t)(to5[p[x] >> 16 & 255] << 11 | to6[p[x] >> 8 & 255] << 5 | to5[p[x] & 255]);
        } else if (pixfmt == RETRO_PIXEL_FORMAT_RGB565) {
            memcpy(o, row, w * 2);
        } else {
            const uint16_t *p = (const uint16_t *)row;
            for (unsigned x = 0; x < w; x++)
                o[x] = (uint16_t)((p[x] << 1 & 0xFFC0) | (p[x] & 0x1F));
        }
    }
    display_write_rgb565(frame565, (int)w, (int)h);
}

/* --- sound ------------------------------------------------------------- */

static unsigned rate;

static size_t audio_batch(const int16_t *d, size_t n)
{
    static int16_t mono[4096];
    if (!rate) return n;
    /* the core makes a frame's worth at its pace, AAudio drains at its own:
     * past 100ms queued the sound would lag the picture, so drop */
    if (audio_queued() > rate / 10) return n;
    for (size_t done = 0; done < n;) {
        size_t k = n - done < 4096 ? n - done : 4096;
        for (size_t i = 0; i < k; i++) mono[i] = (int16_t)((d[(done + i) * 2] + d[(done + i) * 2 + 1]) / 2);
        audio_write(mono, (unsigned)k);
        done += k;
    }
    return n;
}

static void audio_one(int16_t l, int16_t r) { int16_t s[2] = {l, r}; audio_batch(s, 1); }

/* --- keys ------------------------------------------------------------------
 * The boards' keyboard is a HID boot report; libretro's is key events and
 * key states, by RETROK codes. US positions: the core's own keyboard
 * layout is "us". */

static uint8_t held[RETROK_LAST];
static retro_keyboard_event_t key_event;
static uint8_t last_report[8];

static unsigned retrok_of(uint8_t u)
{
    if (u >= 0x04 && u <= 0x1D) return RETROK_a + (u - 0x04);
    if (u >= 0x1E && u <= 0x26) return RETROK_1 + (u - 0x1E);
    if (u >= 0x3A && u <= 0x45) return RETROK_F1 + (u - 0x3A);
    if (u >= 0x59 && u <= 0x61) return RETROK_KP1 + (u - 0x59);
    switch (u) {
    case 0x27: return RETROK_0;
    case 0x28: return RETROK_RETURN;
    case 0x29: return RETROK_ESCAPE;
    case 0x2A: return RETROK_BACKSPACE;
    case 0x2B: return RETROK_TAB;
    case 0x2C: return RETROK_SPACE;
    case 0x2D: return RETROK_MINUS;
    case 0x2E: return RETROK_EQUALS;
    case 0x2F: return RETROK_LEFTBRACKET;
    case 0x30: return RETROK_RIGHTBRACKET;
    case 0x31: return RETROK_BACKSLASH;
    case 0x33: return RETROK_SEMICOLON;
    case 0x34: return RETROK_QUOTE;
    case 0x35: return RETROK_BACKQUOTE;
    case 0x36: return RETROK_COMMA;
    case 0x37: return RETROK_PERIOD;
    case 0x38: return RETROK_SLASH;
    case 0x39: return RETROK_CAPSLOCK;
    case 0x46: return RETROK_PRINT;
    case 0x47: return RETROK_SCROLLOCK;
    case 0x48: return RETROK_PAUSE;
    case 0x49: return RETROK_INSERT;
    case 0x4A: return RETROK_HOME;
    case 0x4B: return RETROK_PAGEUP;
    case 0x4C: return RETROK_DELETE;
    case 0x4D: return RETROK_END;
    case 0x4E: return RETROK_PAGEDOWN;
    case 0x4F: return RETROK_RIGHT;
    case 0x50: return RETROK_LEFT;
    case 0x51: return RETROK_DOWN;
    case 0x52: return RETROK_UP;
    case 0x53: return RETROK_NUMLOCK;
    case 0x54: return RETROK_KP_DIVIDE;
    case 0x55: return RETROK_KP_MULTIPLY;
    case 0x56: return RETROK_KP_MINUS;
    case 0x57: return RETROK_KP_PLUS;
    case 0x58: return RETROK_KP_ENTER;
    case 0x62: return RETROK_KP0;
    case 0x63: return RETROK_KP_PERIOD;
    }
    return 0;
}

static const unsigned mod_keys[8] = {RETROK_LCTRL, RETROK_LSHIFT, RETROK_LALT, RETROK_LSUPER,
                                     RETROK_RCTRL, RETROK_RSHIFT, RETROK_RALT, RETROK_RSUPER};

static uint16_t mods_of(uint8_t m)
{
    return (uint16_t)(((m & 0x22) ? RETROKMOD_SHIFT : 0) | ((m & 0x11) ? RETROKMOD_CTRL : 0) |
                      ((m & 0x44) ? RETROKMOD_ALT : 0) | ((m & 0x88) ? RETROKMOD_META : 0));
}

static void key(unsigned k, int down, uint16_t mods)
{
    if (!k || held[k] == down) return;
    held[k] = (uint8_t)down;
    if (key_event) key_event(down, k, 0, mods);
}

static int in(const uint8_t r[8], uint8_t u)
{
    for (int i = 2; i < 8; i++) if (r[i] == u) return 1;
    return 0;
}

static void m_hid(const uint8_t r[8])
{
    /* F12 is the selector, as on the other machines: never the PC's */
    if (in(r, 0x45) && !in(last_report, 0x45)) selector_open();
    const uint16_t mods = mods_of(r[0]);
    for (int b = 0; b < 8; b++) key(mod_keys[b], (r[0] >> b) & 1, mods);
    for (int i = 2; i < 8; i++)
        if (last_report[i] && !in(r, last_report[i])) key(retrok_of(last_report[i]), 0, mods);
    for (int i = 2; i < 8; i++)
        if (r[i] && r[i] != 0x45 && !in(last_report, r[i])) key(retrok_of(r[i]), 1, mods);
    memcpy(last_report, r, 8);
}

/* --- the mouse: the board's pointer is absolute, libretro's relative --- */

static pthread_mutex_t mouse_lock = PTHREAD_MUTEX_INITIALIZER;
static int mouse_x = -1, mouse_y, mouse_dx, mouse_dy, mouse_b;
static int poll_dx, poll_dy;

static void m_pointer(int x, int y, int button)
{
    pthread_mutex_lock(&mouse_lock);
    if (mouse_x >= 0) { mouse_dx += x - mouse_x; mouse_dy += y - mouse_y; }
    mouse_x = x;
    mouse_y = y;
    mouse_b = button;
    pthread_mutex_unlock(&mouse_lock);
}

static void input_poll(void)
{
    pthread_mutex_lock(&mouse_lock);
    poll_dx = mouse_dx;
    poll_dy = mouse_dy;
    mouse_dx = mouse_dy = 0;
    pthread_mutex_unlock(&mouse_lock);
}

static int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
    (void)index;
    if (port != 0) return 0;
    if (device == RETRO_DEVICE_KEYBOARD) return id < RETROK_LAST ? held[id] : 0;
    if (device == RETRO_DEVICE_MOUSE) {
        switch (id) {
        case RETRO_DEVICE_ID_MOUSE_X: return (int16_t)poll_dx;
        case RETRO_DEVICE_ID_MOUSE_Y: return (int16_t)poll_dy;
        case RETRO_DEVICE_ID_MOUSE_LEFT: return (int16_t)(mouse_b & 1);
        case RETRO_DEVICE_ID_MOUSE_RIGHT: return (int16_t)((mouse_b >> 1) & 1);
        }
    }
    return 0;
}

/* --- entries: the command line, then the card's pc386/ ----------------- */

#define MAX_FILES 64
static char names[MAX_FILES][64];
static char paths[MAX_FILES][512];
static int nfiles = -1;
static int selected = -1;
static char card[480] = ".";

static int wanted(const char *n)
{
    static const char *const ext[] = {".zip", ".img", ".ima", ".vhd", ".iso", ".cue", ".conf", ".dosz"};
    const char *dot = strrchr(n, '.');
    if (!dot || n[0] == '.') return 0;
    for (unsigned i = 0; i < sizeof ext / sizeof ext[0]; i++) if (!strcasecmp(dot, ext[i])) return 1;
    return 0;
}

static void add(const char *name, const char *path)
{
    if (nfiles >= MAX_FILES) return;
    snprintf(names[nfiles], sizeof names[0], "%s", name);
    snprintf(paths[nfiles], sizeof paths[0], "%s", path);
    nfiles++;
}

static void list_files(void)
{
    if (nfiles >= 0) return;
    nfiles = 0;
    if (getenv("RETRO_CARD")) snprintf(card, sizeof card, "%s", getenv("RETRO_CARD"));
    char dir[512], path[512];
    snprintf(dir, sizeof dir, "%s/pc386", card);
    mkdir(dir, 0775);
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (!wanted(e->d_name)) continue;
            snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
            add(e->d_name, path);
        }
        closedir(d);
    }
    /* the 8086 PC's hard disk, for the same games at a 386's speed */
    struct stat st;
    snprintf(path, sizeof path, "%s/msdos.img", card);
    if (stat(path, &st) == 0) add("msdos.img (o C: do PC)", path);
}

#define NVS_NS  "cyd"
#define NVS_KEY "pc386entry"

static void remember(int i)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, NVS_KEY, i > 0 && i - 1 < nfiles ? names[i - 1] : "");
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
    for (int i = 0; name[0] && i < nfiles; i++)
        if (!strcmp(names[i], name)) return i + 1;
    return 0;
}

static int m_entry_count(void) { list_files(); return 1 + nfiles; }

static const char *m_entry_name(int i)
{
    static char label[64];
    list_files();
    if (i <= 0 || i - 1 >= nfiles) return "Linha de comando";
    snprintf(label, sizeof label, "%s", names[i - 1]);
    char *dot = strrchr(label, '.');
    if (dot && !strchr(dot, ' ')) *dot = 0;
    return label;
}

static void m_select_entry(int i) { selected = i; remember(i); }
static int  m_selected_entry(void) { return selected < 0 ? recall() : selected; }

/* Another disk while one runs: a start over, straight into it. */
static void m_switch_to(int entry)
{
    m_select_entry(entry);
    selector_restart_into_choice();
}

/* --- the frontend ------------------------------------------------------- */

static char sysdir[512], savedir[512];

/* What this frontend sets; everything else at the core's defaults. */
static const char *option(const char *k)
{
    if (!strcmp(k, "dosbox_pure_keyboard_layout")) return "us";
    if (!strcmp(k, "dosbox_pure_on_screen_keyboard")) return "false";   /* there is a keyboard */
    if (!strcmp(k, "dosbox_pure_mouse_input")) return "virtual";        /* the trackpad moves it */
    return NULL;
}

static void core_log(enum retro_log_level level, const char *fmt, ...)
{
    if (level < RETRO_LOG_WARN) return;
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

static bool environment(unsigned cmd, void *data)
{
    switch (cmd) {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        pixfmt = *(const enum retro_pixel_format *)data;
        return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        ((struct retro_log_callback *)data)->log = core_log;
        return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        *(const char **)data = sysdir;
        return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *(const char **)data = savedir;
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *v = data;
        v->value = option(v->key);
        return v->value != NULL;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *(bool *)data = false;
        return true;
    case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:
        key_event = ((const struct retro_keyboard_callback *)data)->callback;
        return true;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        *(bool *)data = true;
        return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        *(unsigned *)data = 0;
        return true;
    case RETRO_ENVIRONMENT_SET_VARIABLES:
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
    case RETRO_ENVIRONMENT_SET_MESSAGE:
        return true;
    default:
        return false;
    }
}

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void m_run(void)
{
    list_files();
    if (selected < 0) selected = recall();
    snprintf(sysdir, sizeof sysdir, "%s/pc386/system", card);
    snprintf(savedir, sizeof savedir, "%s/pc386/saves", card);
    mkdir(sysdir, 0775);
    mkdir(savedir, 0775);
    running = 1;
    if (!load_core()) for (;;) sleep(1);

    core_set_environment(environment);
    core_set_video_refresh(video);
    core_set_audio_sample(audio_one);
    core_set_audio_sample_batch(audio_batch);
    core_set_input_poll(input_poll);
    core_set_input_state(input_state);
    core_init();
    const int with_disk = selected > 0 && selected - 1 < nfiles;
    struct retro_game_info game = {with_disk ? paths[selected - 1] : NULL, NULL, 0, NULL};
    printf("pc386: starting %s\n", with_disk ? paths[selected - 1] : "the command line");
    if (!core_load_game(with_disk ? &game : NULL)) {
        printf("pc386: the core would not load it\n");
        for (;;) sleep(1);
    }
    struct retro_system_av_info av;
    core_get_system_av_info(&av);
    const double fps = av.timing.fps > 0 ? av.timing.fps : 70;
    rate = audio_start_push(av.timing.sample_rate > 0 ? (unsigned)av.timing.sample_rate : 48000);
    printf("pc386: %.2f frames a second, sound %u Hz\n", fps, rate);

    double due = now();
    double busy = 0, since = due;
    for (;;) {
        if (selector_active()) {
            int chosen = -1;
            audio_pause(1);
            while (selector_active()) {
                const int e = selector_frame();
                if (e >= 0) chosen = e;
                usleep(20000);
            }
            audio_pause(0);
            if (chosen >= 0) m_switch_to(chosen);
            due = now();
        }
        const double t0 = now();
        core_run();
        const double t1 = now();
        busy += t1 - t0;
        /* how hard the core works, once a minute: what decides how much of
         * a PC this tablet is */
        if (t1 - since > 60) {
            printf("pc386: core busy %.0f%% of the last minute\n", 100 * busy / (t1 - since));
            busy = 0;
            since = t1;
        }
        due += 1 / fps;
        if (due > t1) usleep((useconds_t)((due - t1) * 1e6));
        else if (t1 - due > 0.25) due = t1;   /* late: catch up no further */
    }
}

static int m_prealloc(void) { return 1; }
static int m_ready(void) { return running; }
static unsigned long m_frames(void) { return frames; }
static int m_type(const char *text) { (void)text; return 0; }
static int m_typing(void) { return 0; }
static int m_screen_row(int row, uint8_t *out, int max) { (void)row; (void)out; (void)max; return 0; }
static const char *m_screen_mode(void) { return "DOSBox Pure"; }
static int m_char_pattern(int code, uint8_t *rows8) { (void)code; (void)rows8; return 0; }
static int m_peek(int addr) { (void)addr; return -1; }
static void m_set_sound(int on) { (void)on; }
static int m_sound_on(void) { return 1; }
static int m_debug_command(const char *line) { (void)line; return 0; }
static const char *m_debug_help(void) { return ""; }

const Machine pc386_machine = {
    "PC 386 (DOSBox)",
    1,
    m_prealloc, m_run, m_ready, m_frames,
    m_hid, m_type, m_typing,
    m_screen_row, m_screen_mode, m_char_pattern, m_peek, NULL,
    m_set_sound, m_sound_on,
    m_entry_count, m_entry_name, m_select_entry, m_selected_entry, m_switch_to,
    m_debug_command, m_debug_help,
    m_pointer,
};
