/* pc_keys.c - the PC's keyboard: HID usages to scancode set 1 (what an
 * XT/AT keyboard sends after the controller's translation), plus the
 * ASCII the BIOS puts in its buffer beside each, from the US layout.
 * Plain C, no board. */
#include "pc_keys.h"

#include <string.h>

#include "pc_core.h"

#define E 0x100   /* extended: preceded by E0 */

/* HID usage 0x00-0x67 -> scancode (|E), 0 for none */
static const uint16_t sc[0x68] = {
    [0x04] = 0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
    0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C,
    [0x1E] = 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
    [0x28] = 0x1C, 0x01, 0x0E, 0x0F, 0x39, 0x0C, 0x0D, 0x1A, 0x1B, 0x2B, 0x2B, 0x27, 0x28,
    0x29, 0x33, 0x34, 0x35, 0x3A,
    [0x3A] = 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44, 0x57, 0x58,
    [0x47] = 0x46,
    [0x49] = 0x52 | E, 0x47 | E, 0x49 | E, 0x53 | E, 0x4F | E, 0x51 | E,
    0x4D | E, 0x4B | E, 0x50 | E, 0x48 | E,
    [0x53] = 0x45, 0x35 | E, 0x37, 0x4A, 0x4E, 0x1C | E,
    0x4F, 0x50, 0x51, 0x4B, 0x4C, 0x4D, 0x47, 0x48, 0x49, 0x52, 0x53,
    [0x64] = 0x56,
};

/* HID usage -> ASCII unshifted / shifted, 0x04-0x38 */
static const char plain[] = "abcdefghijklmnopqrstuvwxyz1234567890\r\x1b\b\t -=[]\\\;'`,./";
static const char shifted[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*()\r\x1b\b\t _+{}||:\"~<>?";

static uint8_t last_mod;
static uint8_t last_keys[6];
static bool caps;

static uint8_t ascii_of(uint8_t usage, uint8_t mod)
{
    const bool shift = mod & 0x22, ctrl = mod & 0x11, alt = mod & 0x44;
    if (alt) return 0;
    if (usage >= 0x04 && usage <= 0x38) {
        char c = (shift ? shifted : plain)[usage - 0x04];
        if (usage <= 0x1D) {
            if (ctrl) return (uint8_t)(usage - 0x04 + 1);
            if (caps) c = (c >= 'a') ? (char)(c - 32) : (char)(c + 32);
        }
        if (ctrl) return 0;
        return (uint8_t)c;
    }
    /* keypad, with NumLock taken as on */
    static const char kp[] = "/*-+\r1234567890.";
    if (usage >= 0x54 && usage <= 0x63) return (uint8_t)kp[usage - 0x54];
    return 0;
}

static void send(uint8_t usage, uint8_t mod, bool pressed)
{
    if (usage >= sizeof sc / sizeof sc[0] || !sc[usage]) return;
    const uint16_t s = sc[usage];
    pc_core_key((uint8_t)s, pressed ? ascii_of(usage, mod) : 0, (s & E) != 0, pressed);
}

static void modifier(uint8_t bit, uint8_t s, bool ext, uint8_t mod)
{
    if ((mod ^ last_mod) & bit) pc_core_key(s, 0, ext, (mod & bit) != 0);
}

bool pc_keys_report(const uint8_t r[8])
{
    const uint8_t mod = r[0];
    const uint8_t *keys = r + 2;
    bool f12 = false;

    modifier(0x01, 0x1D, false, mod);   /* left ctrl */
    modifier(0x02, 0x2A, false, mod);   /* left shift */
    modifier(0x04, 0x38, false, mod);   /* left alt */
    modifier(0x10, 0x1D, true, mod);    /* right ctrl */
    modifier(0x20, 0x36, false, mod);   /* right shift */
    modifier(0x40, 0x38, true, mod);    /* right alt */

    for (int i = 0; i < 6; i++)
        if (last_keys[i] && !memchr(keys, last_keys[i], 6)) send(last_keys[i], mod, false);
    for (int i = 0; i < 6; i++) {
        const uint8_t k = keys[i];
        if (k < 0x04 || memchr(last_keys, k, 6)) continue;
        if (k == 0x45) { f12 = true; continue; }
        if (k == 0x39) caps = !caps;
        send(k, mod, true);
    }
    memcpy(last_keys, keys, 6);
    last_mod = mod;
    return f12;
}

bool pc_keys_type(char c)
{
    if (c >= 1 && c <= 26 && c != '\b' && c != '\t' && c != '\r' && c != '\n') {
        /* a control character: Ctrl and its letter, as WordStar wants */
        const uint8_t usage = (uint8_t)(0x04 + c - 1);
        pc_core_key(0x1D, 0, false, true);
        send(usage, 0x01, true);
        send(usage, 0x01, false);
        pc_core_key(0x1D, 0, false, false);
        return true;
    }
    for (int pass = 0; pass < 2; pass++) {
        const char *t = pass ? shifted : plain;
        const char *p = c == '\n' ? strchr(t, '\r') : strchr(t, c);
        if (!p || !c) continue;
        const uint8_t usage = (uint8_t)(0x04 + (p - t));
        const uint8_t mod = pass ? 0x02 : 0;
        const bool was_caps = caps;
        caps = false;
        if (pass) pc_core_key(0x2A, 0, false, true);
        send(usage, mod, true);
        send(usage, mod, false);
        if (pass) pc_core_key(0x2A, 0, false, false);
        caps = was_caps;
        return true;
    }
    return false;
}
