/* dosboxhost - DOSBox Pure, the libretro core, run on the Mac.
 *
 *   dosboxhost <core.dylib> <content> <seconds> <out.png> [text to type]
 *
 * The bench for the PC 386 before it goes into the Android app, as
 * machost, pchost and c64host are for their cores: loads the core, runs
 * <seconds> of the machine, types the text into it as key presses, and
 * writes the last picture out as a PNG.
 *
 * In real time: the core keeps time by the wall clock too (its menu
 * ignores keys for 200ms after it opens, and fifteen seconds of machine
 * run flat out took a third of a second). It says how busy the core kept
 * the CPU, which is the number that decides what the Mini C can run;
 * DBP_FAST=1 runs flat out instead.
 *
 * <content> is what DOSBox Pure takes: a .zip or a folder (mounted as C:),
 * a disk image (.img, .ima, .vhd) or a .conf. In the text, '\n' is Enter,
 * '|' waits a second, and {F1}..{F12}, {ESC}, {UP} {DOWN} {LEFT} {RIGHT},
 * {TAB}, {BS}, {HOME}, {END} are those keys. {+x} presses a letter and
 * keeps it down, {-x} lets it go: for keys held under others.
 *
 * Core options as DBP_OPTS="dosbox_pure_cycles=max;dosbox_pure_machine=svga",
 * the system and save folders as DBP_DIR (default: the current one).
 */
#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <zlib.h>

#include "libretro.h"

/* --- the picture -------------------------------------------------------- */

static uint32_t *pic;              /* XRGB8888, the last frame shown */
static unsigned pic_w, pic_h;
static enum retro_pixel_format pixfmt = RETRO_PIXEL_FORMAT_0RGB1555;
static unsigned long frames, frames_new;

static void video(const void *data, unsigned w, unsigned h, size_t pitch)
{
    frames++;
    if (!data) return;             /* the same picture again */
    frames_new++;
    if (w != pic_w || h != pic_h) {
        free(pic);
        pic = malloc((size_t)w * h * 4);
        pic_w = w;
        pic_h = h;
    }
    for (unsigned y = 0; y < h; y++) {
        const uint8_t *row = (const uint8_t *)data + y * pitch;
        for (unsigned x = 0; x < w; x++) {
            uint32_t c;
            if (pixfmt == RETRO_PIXEL_FORMAT_XRGB8888) {
                c = ((const uint32_t *)row)[x];
            } else {
                const uint16_t p = ((const uint16_t *)row)[x];
                if (pixfmt == RETRO_PIXEL_FORMAT_RGB565)
                    c = ((p >> 11 & 31) * 255 / 31) << 16 | ((p >> 5 & 63) * 255 / 63) << 8 | (p & 31) * 255 / 31;
                else
                    c = ((p >> 10 & 31) * 255 / 31) << 16 | ((p >> 5 & 31) * 255 / 31) << 8 | (p & 31) * 255 / 31;
            }
            pic[y * w + x] = c;
        }
    }
}

static void png(const char *path)
{
    if (!pic) { fprintf(stderr, "no picture\n"); return; }
    const unsigned w = pic_w, h = pic_h;
    const size_t stride = (size_t)w * 3 + 1;
    unsigned char *raw = malloc(stride * h);
    for (unsigned y = 0; y < h; y++) {
        raw[y * stride] = 0;
        for (unsigned x = 0; x < w; x++) {
            const uint32_t c = pic[y * w + x];
            unsigned char *o = raw + y * stride + 1 + x * 3;
            o[0] = c >> 16;
            o[1] = c >> 8;
            o[2] = c;
        }
    }
    uLongf zl = compressBound((uLong)(stride * h));
    unsigned char *z = malloc(zl);
    compress(z, &zl, raw, (uLong)(stride * h));
    FILE *f = fopen(path, "wb");
#define BE(p, v) do { (p)[0] = (v) >> 24; (p)[1] = (v) >> 16; (p)[2] = (v) >> 8; (p)[3] = (v); } while (0)
    unsigned char sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'}, ih[25], b4[4];
    fwrite(sig, 1, 8, f);
    BE(ih, 13); memcpy(ih + 4, "IHDR", 4); BE(ih + 8, w); BE(ih + 12, h);
    ih[16] = 8; ih[17] = 2; ih[18] = ih[19] = ih[20] = 0;
    BE(ih + 21, (unsigned)crc32(0, ih + 4, 17)); fwrite(ih, 1, 25, f);
    BE(b4, (unsigned)zl); fwrite(b4, 1, 4, f); fwrite("IDAT", 1, 4, f); fwrite(z, 1, zl, f);
    BE(b4, (unsigned)crc32(crc32(0, (const unsigned char *)"IDAT", 4), z, zl)); fwrite(b4, 1, 4, f);
    unsigned char ie[12] = {0, 0, 0, 0, 'I', 'E', 'N', 'D', 0xae, 0x42, 0x60, 0x82};
    fwrite(ie, 1, 12, f);
    fclose(f);
    free(raw);
    free(z);
}

/* --- sound: counted, not played ---------------------------------------- */

static unsigned long samples;
static void audio1(int16_t l, int16_t r) { (void)l; (void)r; samples++; }
static size_t audio(const int16_t *d, size_t n) { (void)d; samples += n; return n; }

/* --- keys --------------------------------------------------------------- */

static uint8_t held[RETROK_LAST];
static retro_keyboard_event_t key_event;

static void key(unsigned k, int down, uint32_t ch)
{
    held[k] = (uint8_t)down;
    if (key_event) key_event(down, k, ch, held[RETROK_LSHIFT] ? RETROKMOD_SHIFT : 0);
}

static void input_poll(void) {}

static int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
    (void)index;
    if (port == 0 && device == RETRO_DEVICE_KEYBOARD && id < RETROK_LAST) return held[id];
    return 0;
}

/* One step of the text: a key and whether it needs shift, from a US
 * keyboard. Returns how many characters of the text it used. */
static int next_key(const char *t, unsigned *k, int *shift, int *wait)
{
    static const char plain[] = "`1234567890-=[]\\;',./";
    static const char shifted[] = "~!@#$%^&*()_+{}|:\"<>?";
    static const unsigned code[] = {RETROK_BACKQUOTE, RETROK_1, RETROK_2, RETROK_3, RETROK_4, RETROK_5,
                                    RETROK_6, RETROK_7, RETROK_8, RETROK_9, RETROK_0, RETROK_MINUS,
                                    RETROK_EQUALS, RETROK_LEFTBRACKET, RETROK_RIGHTBRACKET, RETROK_BACKSLASH,
                                    RETROK_SEMICOLON, RETROK_QUOTE, RETROK_COMMA, RETROK_PERIOD, RETROK_SLASH};
    *shift = 0;
    *wait = 0;
    const char c = *t;
    if (c == '|') { *wait = 1; return 1; }
    if (c == '{' && (t[1] == '+' || t[1] == '-') && t[2] >= 'a' && t[2] <= 'z' && t[3] == '}') {
        *k = RETROK_a + (t[2] - 'a');
        *wait = t[1] == '+' ? 2 : 3;   /* hold, let go */
        return 4;
    }
    if (c == '{') {
        static const struct { const char *name; unsigned k; } named[] = {
            {"ESC", RETROK_ESCAPE}, {"UP", RETROK_UP}, {"DOWN", RETROK_DOWN}, {"LEFT", RETROK_LEFT},
            {"RIGHT", RETROK_RIGHT}, {"TAB", RETROK_TAB}, {"BS", RETROK_BACKSPACE}, {"ENTER", RETROK_RETURN},
            {"HOME", RETROK_HOME}, {"END", RETROK_END}, {"SPACE", RETROK_SPACE}};
        const char *e = strchr(t, '}');
        if (e) {
            const int n = (int)(e - t - 1);
            for (unsigned i = 0; i < sizeof named / sizeof named[0]; i++)
                if ((int)strlen(named[i].name) == n && !strncmp(t + 1, named[i].name, n)) { *k = named[i].k; return n + 2; }
            if (t[1] == 'F' && n >= 2) { *k = RETROK_F1 + atoi(t + 2) - 1; return n + 2; }
        }
    }
    if (c == '\n') { *k = RETROK_RETURN; return 1; }
    if (c == ' ') { *k = RETROK_SPACE; return 1; }
    if (c >= 'a' && c <= 'z') { *k = RETROK_a + (c - 'a'); return 1; }
    if (c >= 'A' && c <= 'Z') { *k = RETROK_a + (c - 'A'); *shift = 1; return 1; }
    for (int i = 0; plain[i]; i++) if (plain[i] == c) { *k = code[i]; return 1; }
    for (int i = 0; shifted[i]; i++) if (shifted[i] == c) { *k = code[i]; *shift = 1; return 1; }
    *k = 0;
    return 1;
}

/* --- the frontend's side of the core ----------------------------------- */

static const char *dir = ".";
static char *opts;

static const char *option(const char *key)
{
    if (!opts) return NULL;
    static char val[256];
    const size_t kl = strlen(key);
    for (const char *p = opts; *p;) {
        const char *end = strchr(p, ';');
        if (!end) end = p + strlen(p);
        if (!strncmp(p, key, kl) && p[kl] == '=') {
            size_t n = (size_t)(end - p - kl - 1);
            if (n >= sizeof val) n = sizeof val - 1;
            memcpy(val, p + kl + 1, n);
            val[n] = 0;
            return val;
        }
        p = *end ? end + 1 : end;
    }
    return NULL;
}

static void core_log(enum retro_log_level level, const char *fmt, ...)
{
    if (level < RETRO_LOG_INFO && !getenv("DBP_DEBUG")) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
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
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *(const char **)data = dir;
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
        *(unsigned *)data = 0;     /* the plain variables, through GET_VARIABLE */
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

#define SYM(name) do { *(void **)&name = dlsym(core, #name); if (!name) { fprintf(stderr, "no %s\n", #name); return 1; } } while (0)

int main(int argc, char **argv)
{
    if (argc < 5) {
        fprintf(stderr, "usage: %s <core.dylib> <content> <seconds> <out.png> [text]\n", argv[0]);
        return 2;
    }
    if (getenv("DBP_DIR")) dir = getenv("DBP_DIR");
    if (getenv("DBP_OPTS")) opts = strdup(getenv("DBP_OPTS"));
    void *core = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!core) { fprintf(stderr, "%s\n", dlerror()); return 1; }

    void (*retro_set_environment)(retro_environment_t);
    void (*retro_set_video_refresh)(retro_video_refresh_t);
    void (*retro_set_audio_sample)(retro_audio_sample_t);
    void (*retro_set_audio_sample_batch)(retro_audio_sample_batch_t);
    void (*retro_set_input_poll)(retro_input_poll_t);
    void (*retro_set_input_state)(retro_input_state_t);
    void (*retro_init)(void);
    bool (*retro_load_game)(const struct retro_game_info *);
    void (*retro_get_system_av_info)(struct retro_system_av_info *);
    void (*retro_run)(void);
    void (*retro_unload_game)(void);
    void (*retro_deinit)(void);
    SYM(retro_set_environment); SYM(retro_set_video_refresh); SYM(retro_set_audio_sample);
    SYM(retro_set_audio_sample_batch); SYM(retro_set_input_poll); SYM(retro_set_input_state);
    SYM(retro_init); SYM(retro_load_game); SYM(retro_get_system_av_info); SYM(retro_run);
    SYM(retro_unload_game); SYM(retro_deinit);

    retro_set_environment(environment);
    retro_set_video_refresh(video);
    retro_set_audio_sample(audio1);
    retro_set_audio_sample_batch(audio);
    retro_set_input_poll(input_poll);
    retro_set_input_state(input_state);
    retro_init();
    struct retro_game_info game = {argv[2], NULL, 0, NULL};
    if (!retro_load_game(&game)) { fprintf(stderr, "load failed: %s\n", argv[2]); return 1; }
    struct retro_system_av_info av;
    retro_get_system_av_info(&av);
    const double fps = av.timing.fps > 0 ? av.timing.fps : 60;
    const unsigned long total = (unsigned long)(atof(argv[3]) * fps);

    /* The text goes in one key at a time: down for three frames, up for
     * three, so a program that polls the keyboard sees every one. */
    const char *text = argc > 5 ? argv[5] : "";
    unsigned long next_at = (unsigned long)(2 * fps);   /* after the machine is up */
    int phase = 0;
    unsigned k = 0;
    int shift = 0;

    const int fast = getenv("DBP_FAST") != NULL;
    double busy = 0;
    const double t0 = now();
    for (unsigned long f = 0; f < total; f++) {
        if (f >= next_at && (*text || phase)) {
            if (phase == 0) {
                int wait;
                text += next_key(text, &k, &shift, &wait);
                if (wait == 2 || wait == 3) { key(k, wait == 2, 0); next_at = f + 3; k = 0; }
                else if (wait) next_at = f + (unsigned long)fps;
                else if (k) {
                    if (shift) key(RETROK_LSHIFT, 1, 0);
                    key(k, 1, 0);
                    phase = 1;
                    next_at = f + 3;
                }
            } else {
                key(k, 0, 0);
                if (shift) key(RETROK_LSHIFT, 0, 0);
                phase = 0;
                next_at = f + 3;
            }
        }
        const double r0 = now();
        retro_run();
        const double r1 = now();
        busy += r1 - r0;
        if (!fast) {
            const double due = t0 + (f + 1) / fps;
            if (due > r1) {
                struct timespec ts = {0, (long)((due - r1) * 1e9)};
                nanosleep(&ts, NULL);
            }
        }
    }
    const double took = now() - t0;
    printf("%lu frames (%lu new) at %.2f fps in %.2fs: %.0f%% of real time, core busy %.0f%%; %ux%u; "
           "%lu samples at %.0f Hz\n",
           frames, frames_new, fps, took, 100.0 * total / fps / took, 100.0 * busy / took, pic_w, pic_h,
           samples, av.timing.sample_rate);
    png(argv[4]);
    retro_unload_game();
    retro_deinit();
    return 0;
}
