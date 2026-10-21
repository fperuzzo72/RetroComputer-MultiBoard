/* retro_board.c - the Android board, the part with no Android in it.
 *
 * What a board does for a machine (src/machine.h), on a phone or tablet
 * running Android: start it on its own thread, keep the picture it draws,
 * give it keys, and run its selector. The pixels reach the panel through
 * the platform side (android_jni.cpp), and the same file builds on the Mac
 * for tools/androidhost, which writes the picture out as a PNG instead.
 *
 * The picture is kept in colour, RGB565 at the machine's own size: the
 * Boox Tab Mini C has a colour panel, and on a black-and-white one the
 * system makes the grey. The e-ink boards' tone-by-contrast (display8.c)
 * is not needed here.
 *
 * Every write is compared with what is already held, and only a real
 * change moves the sequence number. The platform presents only when it
 * moves: an MSX redraws its whole screen fifty times a second, and on e-ink
 * every frame posted is a refresh, unchanged or not.
 */
#include "retro_board.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "audio.h"
#include "display.h"
#include "display_mono.h"
#include "machine.h"
#include "selector.h"
#include "ble_keyboard.h"

/* Big enough for the largest picture any machine hands over: the PC 386's
 * SVGA, up to 1024x768 (Windows 3.11 at 800x600). The PC's text screen is
 * 640x400, the 8-bit machines use 216 rows of up to 320. */
#define PIC_W 1024
#define PIC_H 768

/* --- the picture ----------------------------------------------------------- */

static pthread_mutex_t pic_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t retro_critical = PTHREAD_MUTEX_INITIALIZER;   /* freertos/FreeRTOS.h */
static uint16_t pic[PIC_H][PIC_W];
static int pic_w = DISPLAY_PICTURE_W;
static int pic_h = DISPLAY_PICTURE_H;
static uint16_t pic_border;
static uint32_t pic_seq = 1;
static int pic_filled;
static unsigned long full_repaints;
static volatile int repaint_wanted;

void display_bridge_init(void) {}

void display_write_picture(short srcX, short srcY, short width, short height,
                           const uint8_t *buffer, uint16_t bgColor,
                           const uint16_t *palette)
{
    (void)srcX;
    if (width <= 0 || height <= 0) return;
    const int w = width > DISPLAY_PICTURE_W_MAX ? DISPLAY_PICTURE_W_MAX : width;
    uint16_t row[PIC_W];
    pthread_mutex_lock(&pic_lock);
    /* Rows a machine never writes are its border: the C64 draws its 200
     * lines from row 8 of the 216, and the rest came out black. So the
     * whole picture takes the border colour whenever it or the size
     * changes, and the machine's rows go on top. */
    if (w != pic_w || pic_h != DISPLAY_PICTURE_H || bgColor != pic_border || !pic_filled) {
        pic_w = w;
        pic_h = DISPLAY_PICTURE_H;
        pic_border = bgColor;
        for (int y = 0; y < pic_h; y++)
            for (int x = 0; x < pic_w; x++) pic[y][x] = bgColor;
        pic_filled = 1;
        pic_seq++;
    }
    for (int r = 0; r < height; r++) {
        const int y = srcY + r;
        if (y < 0 || y >= DISPLAY_PICTURE_H) continue;
        if (!buffer) {
            for (int x = 0; x < w; x++) row[x] = bgColor;
        } else {
            const uint8_t *src = buffer + r * width;
            for (int x = 0; x < w; x++) row[x] = palette[src[x]];
        }
        if (memcmp(pic[y], row, (size_t)w * 2)) {
            memcpy(pic[y], row, (size_t)w * 2);
            pic_seq++;
        }
    }
    pthread_mutex_unlock(&pic_lock);
}

/* A whole picture in RGB565 at its own size, from a machine that draws in
 * true colour (the PC 386): no border, and only rows that changed move
 * the sequence. */
void display_write_rgb565(const uint16_t *px, int w, int h)
{
    if (w <= 0 || h <= 0) return;
    if (w > PIC_W) w = PIC_W;
    if (h > PIC_H) h = PIC_H;
    pthread_mutex_lock(&pic_lock);
    if (w != pic_w || h != pic_h) {
        pic_w = w;
        pic_h = h;
        pic_border = 0;
        pic_seq++;
    }
    for (int y = 0; y < h; y++) {
        if (memcmp(pic[y], px + y * w, (size_t)w * 2)) {
            memcpy(pic[y], px + y * w, (size_t)w * 2);
            pic_seq++;
        }
    }
    pthread_mutex_unlock(&pic_lock);
}

/* The MSX video layer's name for the same thing, as on the other boards. */
void display_write_frame_msx(short left, short top, short width, short height,
                             const uint8_t *buffer, uint16_t bgColor,
                             const uint16_t *palette)
{
    display_write_picture(left, top, width, height, buffer, bgColor, palette);
}

void display_fill_panel(uint16_t color)
{
    pthread_mutex_lock(&pic_lock);
    full_repaints++;
    if (color != pic_border) { pic_border = color; pic_seq++; }
    pthread_mutex_unlock(&pic_lock);
}

/* display_mono.h: a machine with a 1-bit framebuffer of its own (the Mac,
 * the PC) hands it over once and never waits. It is read here when the
 * platform asks for the picture, turned into black on white, and counts
 * as a change only where it changed. Called with pic_lock held. */
static const uint8_t *mono_fb;
static int mono_w, mono_h;
static unsigned long mono_vsyncs;

void display_mono_attach(const uint8_t *fb, int w, int h)
{
    pthread_mutex_lock(&pic_lock);
    mono_fb = fb;
    mono_w = w > PIC_W ? PIC_W : w;
    mono_h = h > PIC_H ? PIC_H : h;
    pic_w = mono_w;
    pic_h = mono_h;
    pic_border = 0xFFFF;
    pic_filled = 0;
    pic_seq++;
    pthread_mutex_unlock(&pic_lock);
}

void display_mono_vsync(void) { mono_vsyncs++; }

static void mono_convert(void)
{
    uint16_t row[PIC_W];
    const int stride = mono_w / 8;
    for (int y = 0; y < mono_h; y++) {
        const uint8_t *src = mono_fb + (size_t)y * stride;
        for (int x = 0; x < mono_w; x++)
            row[x] = (src[x >> 3] & (0x80 >> (x & 7))) ? 0x0000 : 0xFFFF;   /* 1 = black */
        if (memcmp(pic[y], row, (size_t)mono_w * 2)) {
            memcpy(pic[y], row, (size_t)mono_w * 2);
            pic_seq++;
        }
    }
}

uint32_t retro_board_picture(uint16_t *out, int *w, int *h, uint16_t *border)
{
    pthread_mutex_lock(&pic_lock);
    if (mono_fb) mono_convert();
    const uint32_t seq = pic_seq;
    *w = pic_w;
    *h = pic_h;
    *border = pic_border;
    if (out)
        for (int y = 0; y < pic_h; y++) memcpy(out + y * pic_w, pic[y], (size_t)pic_w * 2);
    pthread_mutex_unlock(&pic_lock);
    return seq;
}

unsigned long display_full_repaints(void) { return full_repaints; }
void display_service(void) {}
int display_take_repaint(void) { int v = repaint_wanted; repaint_wanted = 0; return v; }
void display_set_scale(int scale) { (void)scale; }
int display_get_scale(void) { return 1; }
unsigned long display_blit_us(void) { return 0; }
void display_blit_us_reset(void) {}
void display_set_swap_bytes(int on) { (void)on; }
int display_get_swap_bytes(void) { return 0; }
void display_request_test_pattern(int which) { (void)which; }
void display8_attach(uint8_t *canvas) { (void)canvas; }
void display8_request_repaint(void) { repaint_wanted = 1; }

/* --- keys -------------------------------------------------------------------
 * The machines take a BLE keyboard's 8-byte boot report (modifiers, a
 * reserved byte, six keys) and do their own layout, US-International dead
 * keys included. A keyboard on Android arrives as key events instead; here
 * they become the same report, so each machine's keyboard code runs as it
 * does on the boards. */

static pthread_mutex_t key_lock = PTHREAD_MUTEX_INITIALIZER;
static uint8_t report[8];
static int keys_seen;

void retro_board_key(uint8_t usage, int down)
{
    pthread_mutex_lock(&key_lock);
    if (usage >= 0xE0 && usage <= 0xE7) {
        const uint8_t bit = (uint8_t)(1u << (usage - 0xE0));
        if (down) report[0] |= bit; else report[0] &= (uint8_t)~bit;
    } else if (usage) {
        int at = -1;
        for (int i = 2; i < 8; i++) if (report[i] == usage) at = i;
        if (down && at < 0) {
            for (int i = 2; i < 8; i++) if (!report[i]) { report[i] = usage; break; }
        } else if (!down && at >= 0) {
            for (int i = at; i < 7; i++) report[i] = report[i + 1];
            report[7] = 0;
        }
    }
    keys_seen = 1;
    uint8_t copy[8];
    memcpy(copy, report, 8);
    pthread_mutex_unlock(&key_lock);
    if (machine) machine->hid_report(copy);
}

/* ble_keyboard.h, which the machines' glue calls: the report is handed over
 * as keys change, so there is nothing to poll. */
void ble_keyboard_poll(void) {}
int ble_keyboard_connected(void) { return keys_seen; }
void ble_keyboard_inject(const uint8_t r[8]) { if (machine) machine->hid_report(r); }

/* --- the pointer -------------------------------------------------------------
 * The e-ink boards' trackpad (src/boards/eink/trackpad.c, plain C): drag
 * moves the pointer with acceleration, a tap clicks, tap-tap double-clicks,
 * tap-then-drag holds the button. The owner tuned it on the Paper Mono; a
 * finger is too big to point at a Mac's close box directly. */
#include "../eink/trackpad.h"   /* by path: eink/ is not on this board's include path */

static trackpad tp;
static int tp_ready, last_x = -1, last_y = -1, last_b = -1;

int retro_board_has_pointer(void) { return machine && machine->pointer; }

void retro_board_touch(int touching, int mx, int my, float scale, unsigned long now_ms)
{
    if (!retro_board_has_pointer()) return;
    if (!tp_ready) {
        trackpad_init(&tp, pic_w, pic_h, scale);
        tp_ready = 1;
    }
    trackpad_update(&tp, now_ms, touching, mx, my);
    int x, y, b;
    trackpad_pointer(&tp, &x, &y, &b);
    if (x != last_x || y != last_y || b != last_b) {
        last_x = x;
        last_y = y;
        last_b = b;
        machine->pointer(x, y, b);
    }
}

/* --- the selector -------------------------------------------------------- */

void (*retro_board_menu_hook)(void);
void (*retro_board_restart_hook)(void);
static volatile int sel_active;
static volatile int sel_choice = -1;

void selector_open(void)
{
    if (sel_active) return;
    sel_choice = -1;
    sel_active = 1;
    if (retro_board_menu_hook) retro_board_menu_hook();
    else sel_active = 0;
}

void selector_poll_open(void) {}
/* Still active until the machine has taken the answer: the menu answers
 * from another thread, and a machine that checked selector_frame() a
 * moment before and selector_active() a moment after would leave with
 * nothing (the MSX's loop did, and another game never loaded). */
int selector_active(void) { return sel_active || sel_choice >= 0; }

int selector_frame(void)
{
    const int c = sel_choice;
    sel_choice = -1;
    return c;
}

void retro_board_menu_done(int entry)
{
    sel_choice = entry;
    sel_active = 0;
}

int selector_take_restart_choice(void) { return 0; }

void retro_board_restart(void)
{
    /* On Android the Activity relaunches itself and ends the process; this
     * thread only waits for that, or the process would die before the new
     * Activity was asked for. With no hook (tools/androidhost), just go. */
    if (retro_board_restart_hook) {
        retro_board_restart_hook();
        for (;;) usleep(100000);
    }
    exit(0);
}

void selector_restart_into_choice(void) { retro_board_restart(); }

void retro_board_choose_machine(int index, int entry)
{
    machine_choose(index, entry);
    retro_board_restart();
}

/* --- the machine --------------------------------------------------------- */

static void *machine_thread(void *arg)
{
    (void)arg;
    machine->run();
    return NULL;
}

void retro_board_start(void)
{
    static int started;
    if (started) return;
    started = 1;
    machine_storage_init();
    machine = machine_list[machine_chosen_index()];
    printf("retro: starting %s, entry %d (%s)\n", machine->name, machine->selected_entry(),
           machine->entry_name(machine->selected_entry()));
    machine->prealloc_video();
    /* 8MB, as the Android CrossPoint's reader thread: bionic's default 1MB
     * is not what any of this was written against, and running short shows
     * up as a crash that moves around, not as an error. */
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 8u << 20);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t th;
    pthread_create(&th, &attr, machine_thread, NULL);
    pthread_attr_destroy(&attr);
}
