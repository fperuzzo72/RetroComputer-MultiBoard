/* mac_core.c - the Macintosh, around umac.
 *
 * Plain C with nothing board-specific in it, so tools/machost builds this
 * same file on the development machine. mac_machine.c is the ESP32 side:
 * where the memory comes from, which task runs it, the Machine table.
 */
#include "mac.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "umac.h"
#include "rom.h"
#include "keymap.h"

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
/* 4MB of Mac and a copy of each of ROM and disc: PSRAM is the only place
 * any of that fits, and it is where it goes. */
static void *big_alloc(size_t n) { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
#else
static void *big_alloc(size_t n) { return malloc(n); }
#endif

#define ROM_SIZE 0x20000   /* the Mac Plus ROM, 128kB */

static uint8_t *ram;
static uint8_t *rom;
static uint8_t *disc;

/* Low-memory globals of the Mac Plus ROM. Big-endian, in emulated RAM. */
#define LM_MTEMP      0x828   /* point: v, h */
#define LM_RAWMOUSE   0x82c   /* point: v, h */
#define LM_MOUSE      0x830   /* point: v, h */
#define LM_CRSRNEW    0x8ce   /* byte: the cursor moved, redraw it */
#define LM_CRSRCOUPLE 0x8cf   /* byte: the cursor follows the mouse */

static void put16(unsigned addr, int v)
{
    ram[addr] = (uint8_t)(v >> 8);
    ram[addr + 1] = (uint8_t)v;
}

static int get16(unsigned addr)
{
    return (int16_t)((ram[addr] << 8) | ram[addr + 1]);
}

int mac_start(const uint8_t *rom_image, size_t rom_len,
              const uint8_t *disc_image, size_t disc_len)
{
    disc_descr_t discs[DISC_NUM_DRIVES];

    if (rom_len != ROM_SIZE) {
        printf("mac: ROM is %u bytes, a Mac Plus ROM is %u\n",
               (unsigned)rom_len, (unsigned)ROM_SIZE);
        return -1;
    }

    ram = big_alloc(RAM_SIZE);
    rom = big_alloc(ROM_SIZE);
    disc = disc_len ? big_alloc(disc_len) : NULL;
    if (!ram || !rom || (disc_len && !disc)) {
        printf("mac: out of memory (RAM %u, ROM %u, disc %u)\n",
               (unsigned)RAM_SIZE, (unsigned)ROM_SIZE, (unsigned)disc_len);
        return -1;
    }
    memset(ram, 0, RAM_SIZE);

    /* umac patches the ROM for the resolution it was built for and plants
     * its own disc driver in it, so it has to be a writable copy. */
    memcpy(rom, rom_image, ROM_SIZE);
    if (rom_patch(rom)) {
        printf("mac: this is not the Mac Plus v3 ROM (4D1F8172) umac needs\n");
        return -1;
    }

    /* The disc is copied too, so the Mac can write to it. Nothing the Mac
     * writes survives a reset yet; that waits for the card. */
    memset(discs, 0, sizeof(discs));
    if (disc) {
        memcpy(disc, disc_image, disc_len);
        discs[0].base = disc;
        discs[0].size = disc_len;
        discs[0].read_only = 0;
    }

    umac_init(ram, rom, discs);
    printf("mac: %ux%u, %ukB RAM, disc %ukB\n", DISP_WIDTH, DISP_HEIGHT,
           (unsigned)(RAM_SIZE / 1024), (unsigned)(disc_len / 1024));
    return 0;
}

uint8_t *mac_ram(void) { return ram; }
unsigned mac_ram_size(void) { return RAM_SIZE; }

const uint8_t *mac_framebuffer(void)
{
    return ram ? ram + umac_get_fb_offset() : NULL;
}

void mac_cursor(int *x, int *y)
{
    *y = get16(LM_MOUSE);
    *x = get16(LM_MOUSE + 2);
}

/* --- the pointer ------------------------------------------------------
 *
 * The Mac only has a relative mouse, but the board hands over an absolute
 * position, so it is written straight into the ROM's own cursor globals,
 * the way Mini vMac does it: the new point into MTemp and RawMouse, and
 * CrsrNew set so the next vertical blank moves the cursor there.
 *
 * Two catches, both found the hard way.
 *
 * Nothing may be written there until the system is keeping those globals.
 * During boot the ROM tests every byte of RAM by writing patterns and
 * reading them back, and a pointer written into the middle of that reads
 * back wrong: sad Mac 03FFFF. So the write waits until CrsrCouple is 0xFF
 * (the cursor follows the mouse) and Mouse is a point on the screen, which
 * no test pattern satisfies both of: all-ones puts Mouse at -1,-1 and
 * all-zeros clears CrsrCouple.
 *
 * And the button reaches the Mac at once, the position only when the VBL
 * task has copied it into Mouse, so a tap that delivered both together
 * would click wherever the cursor was before. A button change therefore
 * waits until Mouse says the cursor has arrived.
 */
static volatile int ptr_x = -1, ptr_y = -1, ptr_button;
static int button_now;

void mac_pointer(int x, int y, int button)
{
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= DISP_WIDTH) x = DISP_WIDTH - 1;
    if (y >= DISP_HEIGHT) y = DISP_HEIGHT - 1;
    ptr_x = x;
    ptr_y = y;
    ptr_button = button;
}

static int cursor_live(int cx, int cy)
{
    return ram[LM_CRSRCOUPLE] == 0xFF &&
           cx >= 0 && cx < DISP_WIDTH && cy >= 0 && cy < DISP_HEIGHT;
}

static void pointer_service(void)
{
    int x = ptr_x, y = ptr_y, b = ptr_button;
    int cx, cy;

    if (x < 0) return;   /* nobody has pointed at anything yet */
    mac_cursor(&cx, &cy);
    if (!cursor_live(cx, cy)) return;

    if (cx != x || cy != y) {
        put16(LM_MTEMP, y);
        put16(LM_MTEMP + 2, x);
        put16(LM_RAWMOUSE, y);
        put16(LM_RAWMOUSE + 2, x);
        ram[LM_CRSRNEW] = ram[LM_CRSRCOUPLE];
    } else if (b != button_now) {
        button_now = b;
        umac_mouse(0, 0, b);
    }
}

/* --- the keyboard -----------------------------------------------------
 *
 * HID usages to Mac key codes, after pico-mac's kbd.c. The emulated
 * keyboard holds one event at a time, so presses and releases wait in a
 * small queue here and go over one by one as the Mac takes them.
 */
static const uint8_t hid_to_mac[256] = {
    [0x04] = MKC_A,  [0x05] = MKC_B,  [0x06] = MKC_C,  [0x07] = MKC_D,
    [0x08] = MKC_E,  [0x09] = MKC_F,  [0x0a] = MKC_G,  [0x0b] = MKC_H,
    [0x0c] = MKC_I,  [0x0d] = MKC_J,  [0x0e] = MKC_K,  [0x0f] = MKC_L,
    [0x10] = MKC_M,  [0x11] = MKC_N,  [0x12] = MKC_O,  [0x13] = MKC_P,
    [0x14] = MKC_Q,  [0x15] = MKC_R,  [0x16] = MKC_S,  [0x17] = MKC_T,
    [0x18] = MKC_U,  [0x19] = MKC_V,  [0x1a] = MKC_W,  [0x1b] = MKC_X,
    [0x1c] = MKC_Y,  [0x1d] = MKC_Z,
    [0x1e] = MKC_1,  [0x1f] = MKC_2,  [0x20] = MKC_3,  [0x21] = MKC_4,
    [0x22] = MKC_5,  [0x23] = MKC_6,  [0x24] = MKC_7,  [0x25] = MKC_8,
    [0x26] = MKC_9,  [0x27] = MKC_0,
    [0x28] = MKC_Return, [0x29] = MKC_Escape, [0x2a] = MKC_BackSpace,
    [0x2b] = MKC_Tab, [0x2c] = MKC_Space, [0x2d] = MKC_Minus,
    [0x2e] = MKC_Equal, [0x2f] = MKC_LeftBracket, [0x30] = MKC_RightBracket,
    [0x31] = MKC_BackSlash, [0x33] = MKC_SemiColon, [0x34] = MKC_SingleQuote,
    [0x35] = MKC_Grave, [0x36] = MKC_Comma, [0x37] = MKC_Period,
    [0x38] = MKC_Slash, [0x39] = MKC_CapsLock,
    [0x4c] = MKC_BackSpace,                        /* Delete */
    [0x4f] = MKC_Right, [0x50] = MKC_Left, [0x51] = MKC_Down, [0x52] = MKC_Up,
    [0x58] = MKC_Enter,                            /* keypad Enter */
};

/* HID modifier bits, left and right folded together. */
static const struct { uint8_t bits; uint8_t mac; } modifiers[] = {
    { 0x11, MKC_Control },
    { 0x22, MKC_Shift },
    { 0x44, MKC_Option },
    { 0x88, MKC_Command },
};

#define KQ_SIZE 32
static volatile uint16_t kq[KQ_SIZE];
static volatile unsigned kq_head, kq_tail;
static uint8_t last_report[8];

/* Mac key codes: MKC_A is 0, which is why a separate "present" test is
 * needed. What goes to the keyboard is (code << 1) | 1, as in pico-mac. */
static void kq_push(uint8_t mkc, int down)
{
    unsigned next = (kq_head + 1) % KQ_SIZE;
    if (next == kq_tail) return;   /* full: drop, never block the caller */
    kq[kq_head] = (uint16_t)(((mkc << 1) | 1) | (down ? 0x8000 : 0));
    kq_head = next;
}

static int in_report(const uint8_t r[8], uint8_t usage)
{
    for (int i = 2; i < 8; i++)
        if (r[i] == usage) return 1;
    return 0;
}

static int mapped(uint8_t usage)
{
    return usage == 0x04 || hid_to_mac[usage] != 0;
}

void mac_hid_report(const uint8_t r[8])
{
    for (unsigned i = 0; i < sizeof(modifiers) / sizeof(modifiers[0]); i++) {
        int was = (last_report[0] & modifiers[i].bits) != 0;
        int is = (r[0] & modifiers[i].bits) != 0;
        if (was != is) kq_push(modifiers[i].mac, is);
    }
    for (int i = 2; i < 8; i++) {
        uint8_t u = last_report[i];
        if (u && mapped(u) && !in_report(r, u)) kq_push(hid_to_mac[u], 0);
    }
    for (int i = 2; i < 8; i++) {
        uint8_t u = r[i];
        if (u && mapped(u) && !in_report(last_report, u)) kq_push(hid_to_mac[u], 1);
    }
    memcpy(last_report, r, 8);
}

static void keyboard_service(void)
{
    if (kq_tail == kq_head || umac_kbd_pending()) return;
    uint16_t k = kq[kq_tail];
    kq_tail = (kq_tail + 1) % KQ_SIZE;
    umac_kbd_event((uint8_t)(k & 0xff), (k & 0x8000) != 0);
}

/* --- the loop ---------------------------------------------------------- */

static uint64_t last_vsync_us, last_1hz_us;

void mac_step(uint64_t now_us)
{
    umac_loop();

    if (now_us - last_vsync_us >= 16667) {
        umac_vsync_event();
        last_vsync_us = now_us;
    }
    if (now_us - last_1hz_us >= 1000000) {
        umac_1hz_event();
        last_1hz_us = now_us;
    }
    pointer_service();
    keyboard_service();
}
