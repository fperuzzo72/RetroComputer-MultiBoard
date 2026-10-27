/* linux_main.c - the Linux board: the RetroComputer on a PC with nothing
 * but a Linux kernel, the Sony VAIO VGN-SZ330P first (tools/vaio).
 *
 * The machines run as on the Android board, through its platform-free
 * part (src/boards/android/retro_board.c); this file is the platform side
 * that android_jni.cpp and the Kotlin activity are there:
 *
 *   - the picture goes straight into the framebuffer (/dev/fb0, which the
 *     i915 driver gives at the panel's 1280x800, 32 bits), scaled to fill
 *     the height with square pixels, or at 4:3 for a machine that asks;
 *     the console is put in graphics mode so it draws nothing over it;
 *   - keys and the mouse come from evdev (/dev/input/event*), every
 *     keyboard and mouse there is, grabbed so the console sees none of it;
 *     a key becomes its HID usage, as the BLE keyboards' on the boards;
 *   - the menu (another game, another computer, switch off) is drawn here,
 *     in the VGA font lib/pc8086 has, and driven by the arrows, Enter and
 *     Esc. F12 opens it, on every machine (the Android board has a tap on
 *     the panel for the Spectrum and the system's back for the Mac, neither
 *     of which a keyboard has), and so does the Menu key;
 *   - another computer is a start over: the program runs itself again.
 *
 *     retro CARD_DIR NVS_DIR
 *
 * CARD_DIR is the card's RetroComputer folder (the Android board's), and
 * NVS_DIR where the choices are kept.
 */
#include <dirent.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "retro_board.h"
#include "machine.h"
#include "selector.h"
#include "font8x16.h"

/* --- the screen ------------------------------------------------------------ */

static uint32_t *fb;
static int fb_w, fb_h, fb_stride;   /* stride in pixels */
static int tty_fd = -1;

static void screen_open(void)
{
    int fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) { perror("retro: /dev/fb0"); exit(1); }
    struct fb_var_screeninfo v;
    struct fb_fix_screeninfo f;
    ioctl(fd, FBIOGET_VSCREENINFO, &v);
    ioctl(fd, FBIOGET_FSCREENINFO, &f);
    if (v.bits_per_pixel != 32) { printf("retro: fb is %u bits, want 32\n", v.bits_per_pixel); exit(1); }
    fb_w = (int)v.xres;
    fb_h = (int)v.yres;
    fb_stride = (int)(f.line_length / 4);
    fb = mmap(NULL, f.line_length * v.yres, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (fb == MAP_FAILED) { perror("retro: mmap fb"); exit(1); }
    printf("retro: screen %dx%d\n", fb_w, fb_h);
    /* the console stops drawing (text, cursor) while we own the screen */
    tty_fd = open("/dev/tty0", O_RDWR);
    if (tty_fd >= 0) ioctl(tty_fd, KDSETMODE, KD_GRAPHICS);
}

static void screen_close(void)
{
    if (tty_fd >= 0) ioctl(tty_fd, KDSETMODE, KD_TEXT);
}

static inline uint32_t rgb(uint16_t c)
{
    return ((uint32_t)(c >> 11) * 255 / 31) << 16 | ((uint32_t)((c >> 5) & 63) * 255 / 63) << 8 |
           (uint32_t)(c & 31) * 255 / 31;
}

/* Clipped on all four sides: what does not fit is not drawn. A menu taller
 * than the VAIO's 800 rows once started at row -8 and wrote in front of the
 * framebuffer. */
static void fill(int x, int y, int w, int h, uint32_t c)
{
    const int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    const int x1 = x + w < fb_w ? x + w : fb_w, y1 = y + h < fb_h ? y + h : fb_h;
    for (int j = y0; j < y1; j++)
        for (int i = x0; i < x1; i++) fb[j * fb_stride + i] = c;
}

#define PIC_MAX (1024 * 768)
static uint16_t pic[PIC_MAX];
static int xmap[2048];

/* The machine's picture, as big as the height allows: square pixels, or a
 * 4:3 box for a machine that asks for it. */
static void present(int w, int h, uint16_t border)
{
    int dw, dh;
    if (retro_board_four3()) {
        dh = fb_h;
        dw = fb_h * 4 / 3;
    } else {
        dh = fb_h;
        dw = w * fb_h / h;
    }
    if (dw > fb_w) {
        dh = dh * fb_w / dw;
        dw = fb_w;
    }
    const int ox = (fb_w - dw) / 2, oy = (fb_h - dh) / 2;
    const uint32_t b = rgb(border);
    fill(0, 0, fb_w, oy, b);
    fill(0, oy + dh, fb_w, fb_h - oy - dh, b);
    fill(0, oy, ox, dh, b);
    fill(ox + dw, oy, fb_w - ox - dw, dh, b);
    for (int i = 0; i < dw; i++) xmap[i] = i * w / dw;
    int last = -1;
    for (int j = 0; j < dh; j++) {
        const int sy = j * h / dh;
        uint32_t *row = fb + (oy + j) * fb_stride + ox;
        if (sy == last) {
            memcpy(row, row - fb_stride, (size_t)dw * 4);
            continue;
        }
        last = sy;
        const uint16_t *src = pic + sy * w;
        for (int i = 0; i < dw; i++) row[i] = rgb(src[xmap[i]]);
    }
}

static void text(int x, int y, const char *s, int scale, uint32_t c)
{
    for (; *s; s++, x += 8 * scale) {
        const uint8_t *g = font8x16_get_glyph((uint8_t)*s);
        for (int r = 0; r < 16; r++)
            for (int b = 0; b < 8; b++)
                if (g[r] & (0x80 >> b)) fill(x + b * scale, y + r * scale, scale, scale, c);
    }
}

/* --- the menu ---------------------------------------------------------------
 * The Kotlin activity's, drawn: the running machine's entries with
 * "another computer" first, then the computers, then the chosen one's
 * entries. */

enum { M_CLOSED, M_ENTRIES, M_MACHINES, M_OTHER };
static volatile int menu_wanted;
static int menu_level, menu_sel, menu_top, menu_machine;
static int menu_dirty;

static int menu_count(void)
{
    switch (menu_level) {
    case M_ENTRIES: return machine->entry_count() + 2;   /* + another computer, switch off */
    case M_MACHINES: return machine_count;
    case M_OTHER: return machine_list[menu_machine]->entry_count();
    }
    return 0;
}

static const char *menu_item(int i)
{
    switch (menu_level) {
    case M_ENTRIES:
        if (i == 0) return "Outro computador...";
        if (i == machine->entry_count() + 1) return "Desligar";
        return machine->entry_name(i - 1);
    case M_MACHINES: return machine_list[i]->name;
    case M_OTHER: return machine_list[menu_machine]->entry_name(i);
    }
    return "";
}

static const char *menu_title(void)
{
    switch (menu_level) {
    case M_ENTRIES: return machine->name;
    case M_MACHINES: return "Qual computador?";
    case M_OTHER: return machine_list[menu_machine]->name;
    }
    return "";
}

/* as many rows as the screen has room for, the title and a margin aside */
static int rows = 20;

static void menu_draw(void)
{
    const int s = 2, lh = 16 * s + 4;
    rows = (fb_h - 24 - 40) / lh - 2;
    if (rows > 20) rows = 20;
    if (rows < 1) rows = 1;
    const int w = 900, h = lh * (rows + 2) + 24;
    const int x = (fb_w - w) / 2, y = (fb_h - h) / 2;
    fill(x - 4, y - 4, w + 8, h + 8, 0xC0C0C0);
    fill(x, y, w, h, 0x101830);
    text(x + 16, y + 8, menu_title(), s, 0xFFFF80);
    const int n = menu_count();
    if (menu_sel < menu_top) menu_top = menu_sel;
    if (menu_sel >= menu_top + rows) menu_top = menu_sel - rows + 1;
    for (int r = 0; r < rows && menu_top + r < n; r++) {
        const int i = menu_top + r, ty = y + 8 + lh * (r + 1) + 8;
        char line[56];
        snprintf(line, sizeof line, "%s", menu_item(i));
        if (i == menu_sel) fill(x + 8, ty - 2, w - 16, lh, 0x3050A0);
        text(x + 16, ty, line, s, 0xFFFFFF);
    }
    if (menu_top + rows < n) text(x + w - 40, y + h - 30, "...", s, 0xC0C0C0);
    menu_dirty = 0;
}

static void menu_open(void)
{
    menu_level = M_ENTRIES;
    menu_sel = menu_top = 0;
    menu_dirty = 1;
}

static void close_menu(int entry)
{
    menu_level = M_CLOSED;
    retro_board_menu_done(entry);
}

static void choose(int m, int e)
{
    /* The Mac cannot change disc in place: another disc is a start over */
    if (m == machine_chosen_index() && !retro_board_has_pointer()) { close_menu(e); return; }
    menu_level = M_CLOSED;
    retro_board_choose_machine(m, e);
}

/* Through init (busybox's: SIGUSR2 is poweroff), so OpenRC stops the
 * services and puts the pendrive back read-only; straight to the kernel
 * only if init will not have it. */
static void switch_off(void)
{
    screen_close();
    sync();
    if (kill(1, SIGUSR2) == 0) {
        for (;;) pause();
    }
    reboot(RB_POWER_OFF);
    exit(0);
}

/* A crash leaves the console in graphics mode, and whatever runs next on
 * tty1 (start.sh's shell) cannot be seen: text mode back, then the crash. */
static void crashed(int sig)
{
    screen_close();
    signal(sig, SIG_DFL);
    raise(sig);
}

static void menu_key(int code)
{
    const int n = menu_count();
    switch (code) {
    case KEY_UP: if (menu_sel > 0) menu_sel--; break;
    case KEY_DOWN: if (menu_sel < n - 1) menu_sel++; break;
    case KEY_PAGEUP: menu_sel = menu_sel > rows ? menu_sel - rows : 0; break;
    case KEY_PAGEDOWN: menu_sel = menu_sel + rows < n ? menu_sel + rows : n - 1; break;
    case KEY_HOME: menu_sel = 0; break;
    case KEY_END: menu_sel = n - 1; break;
    case KEY_ESC: close_menu(-1); return;
    case KEY_ENTER:
    case KEY_KPENTER:
        if (menu_level == M_ENTRIES) {
            if (menu_sel == 0) { menu_level = M_MACHINES; menu_sel = machine_chosen_index(); menu_top = 0; }
            else if (menu_sel == n - 1) switch_off();
            else if (retro_board_has_pointer()) choose(machine_chosen_index(), menu_sel - 1);
            else close_menu(menu_sel - 1);
        } else if (menu_level == M_MACHINES) {
            menu_machine = menu_sel;
            if (machine_list[menu_machine]->entry_count() <= 1) { choose(menu_machine, 0); return; }
            menu_level = M_OTHER;
            menu_sel = menu_top = 0;
        } else {
            choose(menu_machine, menu_sel);
        }
        break;
    }
    menu_dirty = 1;
}

/* the machine's selector asks for the menu from its own thread */
static void menu_hook(void) { menu_wanted = 1; }

/* --- keys and the mouse ----------------------------------------------------- */

/* evdev key codes to HID usages: the keys a PC keyboard has */
static uint8_t usage_of(int k)
{
    static const uint8_t map[128] = {
        [KEY_ESC] = 0x29, [KEY_1] = 0x1E, [KEY_2] = 0x1F, [KEY_3] = 0x20, [KEY_4] = 0x21, [KEY_5] = 0x22,
        [KEY_6] = 0x23, [KEY_7] = 0x24, [KEY_8] = 0x25, [KEY_9] = 0x26, [KEY_0] = 0x27, [KEY_MINUS] = 0x2D,
        [KEY_EQUAL] = 0x2E, [KEY_BACKSPACE] = 0x2A, [KEY_TAB] = 0x2B, [KEY_Q] = 0x14, [KEY_W] = 0x1A,
        [KEY_E] = 0x08, [KEY_R] = 0x15, [KEY_T] = 0x17, [KEY_Y] = 0x1C, [KEY_U] = 0x18, [KEY_I] = 0x0C,
        [KEY_O] = 0x12, [KEY_P] = 0x13, [KEY_LEFTBRACE] = 0x2F, [KEY_RIGHTBRACE] = 0x30, [KEY_ENTER] = 0x28,
        [KEY_LEFTCTRL] = 0xE0, [KEY_A] = 0x04, [KEY_S] = 0x16, [KEY_D] = 0x07, [KEY_F] = 0x09, [KEY_G] = 0x0A,
        [KEY_H] = 0x0B, [KEY_J] = 0x0D, [KEY_K] = 0x0E, [KEY_L] = 0x0F, [KEY_SEMICOLON] = 0x33,
        [KEY_APOSTROPHE] = 0x34, [KEY_GRAVE] = 0x35, [KEY_LEFTSHIFT] = 0xE1, [KEY_BACKSLASH] = 0x31,
        [KEY_Z] = 0x1D, [KEY_X] = 0x1B, [KEY_C] = 0x06, [KEY_V] = 0x19, [KEY_B] = 0x05, [KEY_N] = 0x11,
        [KEY_M] = 0x10, [KEY_COMMA] = 0x36, [KEY_DOT] = 0x37, [KEY_SLASH] = 0x38, [KEY_RIGHTSHIFT] = 0xE5,
        [KEY_KPASTERISK] = 0x55, [KEY_LEFTALT] = 0xE2, [KEY_SPACE] = 0x2C, [KEY_CAPSLOCK] = 0x39,
        [KEY_F1] = 0x3A, [KEY_F2] = 0x3B, [KEY_F3] = 0x3C, [KEY_F4] = 0x3D, [KEY_F5] = 0x3E, [KEY_F6] = 0x3F,
        [KEY_F7] = 0x40, [KEY_F8] = 0x41, [KEY_F9] = 0x42, [KEY_F10] = 0x43, [KEY_NUMLOCK] = 0x53,
        [KEY_SCROLLLOCK] = 0x47, [KEY_KP7] = 0x5F, [KEY_KP8] = 0x60, [KEY_KP9] = 0x61, [KEY_KPMINUS] = 0x56,
        [KEY_KP4] = 0x5C, [KEY_KP5] = 0x5D, [KEY_KP6] = 0x5E, [KEY_KPPLUS] = 0x57, [KEY_KP1] = 0x59,
        [KEY_KP2] = 0x5A, [KEY_KP3] = 0x5B, [KEY_KP0] = 0x62, [KEY_KPDOT] = 0x63, [KEY_102ND] = 0x64,
        [KEY_F11] = 0x44, [KEY_F12] = 0x45, [KEY_RO] = 0x87, [KEY_KPENTER] = 0x58, [KEY_RIGHTCTRL] = 0xE4,
        [KEY_KPSLASH] = 0x54, [KEY_SYSRQ] = 0x46, [KEY_RIGHTALT] = 0xE6, [KEY_HOME] = 0x4A, [KEY_UP] = 0x52,
        [KEY_PAGEUP] = 0x4B, [KEY_LEFT] = 0x50, [KEY_RIGHT] = 0x4F, [KEY_END] = 0x4D, [KEY_DOWN] = 0x51,
        [KEY_PAGEDOWN] = 0x4E, [KEY_INSERT] = 0x49, [KEY_DELETE] = 0x4C, [KEY_PAUSE] = 0x48,
        [KEY_YEN] = 0x89, [KEY_LEFTMETA] = 0xE3, [KEY_RIGHTMETA] = 0xE7, [KEY_COMPOSE] = 0x65,
    };
    return k >= 0 && k < 128 ? map[k] : 0;
}

#define MAX_DEV 16
static struct pollfd devs[MAX_DEV];
static int ndevs;

static int has_bit(const unsigned long *bits, int b)
{
    return (bits[b / (8 * sizeof(long))] >> (b % (8 * sizeof(long)))) & 1;
}

/* Every keyboard and mouse there is, grabbed. */
static void input_open(void)
{
    DIR *d = opendir("/dev/input");
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) && ndevs < MAX_DEV) {
        if (strncmp(e->d_name, "event", 5)) continue;
        char path[300], name[128] = "?";
        snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        unsigned long keys[KEY_MAX / (8 * sizeof(long)) + 1] = {0}, rel[1] = {0};
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys);
        ioctl(fd, EVIOCGBIT(EV_REL, sizeof rel), rel);
        ioctl(fd, EVIOCGNAME(sizeof name), name);
        const int keyboard = has_bit(keys, KEY_A) && has_bit(keys, KEY_SPACE);
        const int mouse = has_bit(keys, BTN_LEFT) && has_bit(rel, REL_X);
        if (!keyboard && !mouse) { close(fd); continue; }
        ioctl(fd, EVIOCGRAB, 1);
        devs[ndevs].fd = fd;
        devs[ndevs].events = POLLIN;
        ndevs++;
        printf("retro: %s %s (%s)\n", path, name, keyboard ? "keyboard" : "mouse");
    }
    closedir(d);
}

static int mouse_buttons, ctrl, alt;

static void key(int code, int down)
{
    if (code == KEY_LEFTCTRL || code == KEY_RIGHTCTRL) ctrl = down;
    if (code == KEY_LEFTALT || code == KEY_RIGHTALT) alt = down;
    if (menu_level != M_CLOSED) {
        if (down) menu_key(code);
        return;
    }
    /* Ctrl+Alt+Del: off, after the card is written */
    if (down && ctrl && alt && code == KEY_DELETE) switch_off();
    if (code == KEY_COMPOSE || code == KEY_F12) {
        if (down) selector_open();
        return;
    }
    const uint8_t u = usage_of(code);
    if (u) retro_board_key(u, down);
}

static void input_poll(int timeout_ms)
{
    if (poll(devs, (nfds_t)ndevs, timeout_ms) <= 0) return;
    int dx = 0, dy = 0, moved = 0;
    for (int i = 0; i < ndevs; i++) {
        if (!(devs[i].revents & POLLIN)) continue;
        struct input_event ev[64];
        const ssize_t n = read(devs[i].fd, ev, sizeof ev);
        for (int j = 0; j < n / (ssize_t)sizeof ev[0]; j++) {
            if (ev[j].type == EV_KEY && ev[j].value != 2) {
                const int c = ev[j].code, down = ev[j].value;
                if (c == BTN_LEFT || c == BTN_RIGHT) {
                    const int bit = c == BTN_LEFT ? 1 : 2;
                    mouse_buttons = down ? mouse_buttons | bit : mouse_buttons & ~bit;
                    moved = 1;
                } else {
                    key(c, down);
                }
            } else if (ev[j].type == EV_REL) {
                if (ev[j].code == REL_X) { dx += ev[j].value; moved = 1; }
                if (ev[j].code == REL_Y) { dy += ev[j].value; moved = 1; }
            }
        }
    }
    if (moved && menu_level == M_CLOSED) retro_board_mouse(dx / 2, dy / 2, mouse_buttons);
}

/* --- starting over ------------------------------------------------------------ */

static char **args;

static void restart_hook(void)
{
    for (int i = 0; i < ndevs; i++) close(devs[i].fd);
    screen_close();
    fflush(stdout);
    execv("/proc/self/exe", args);
    perror("retro: execv");
    exit(1);
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: retro CARD_DIR NVS_DIR\n");
        return 2;
    }
    args = argv;
    setvbuf(stdout, NULL, _IOLBF, 0);
    setenv("RETRO_CARD", argv[1], 1);
    mkdir(argv[2], 0775);
    screen_open();
    signal(SIGSEGV, crashed);
    signal(SIGBUS, crashed);
    signal(SIGILL, crashed);
    signal(SIGFPE, crashed);
    signal(SIGABRT, crashed);
    input_open();
    retro_board_set_storage(argv[2]);
    retro_board_menu_hook = menu_hook;
    retro_board_restart_hook = restart_hook;
    retro_board_start();

    uint32_t shown = 0;
    for (;;) {
        input_poll(5);
        if (menu_wanted) {
            menu_wanted = 0;
            menu_open();
        }
        if (menu_level != M_CLOSED) {
            if (menu_dirty) menu_draw();
            shown = 0;   /* the picture again once the menu goes */
            continue;
        }
        int w, h;
        uint16_t border;
        const uint32_t seq = retro_board_picture(NULL, &w, &h, &border);
        if (seq == shown || w * h > PIC_MAX) continue;
        shown = retro_board_picture(pic, &w, &h, &border);
        present(w, h, border);
    }
}
