/* pc_keys.c - the PC's keyboard: HID usages to scancode set 1 (what an
 * XT/AT keyboard sends after the controller's translation), plus the
 * character the BIOS puts in its buffer beside each. Plain C, no board.
 *
 * The layout is US-International, as on the owner's other machines (the
 * MSX, the Mac, MicroBASIC): ' ` ^ ~ " are dead keys, the letter after one
 * gets the accent, the accent and a space is the accent on its own, and
 * ' then c is c-cedilla. The accented letter is a byte of the code page the
 * screen is drawn in (pc_text.c): 860, Portuguese, by default, which has
 * every letter Portuguese needs and the same frames as 437; or 437, which
 * has no a-tilde, o-tilde or accented capitals but the first five, and
 * types the accent and the letter for those. */
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

/* HID usage -> character, unshifted and shifted, US layout. One entry a
 * key, written out: a string of these once lost a character to an escape
 * ("\\\;" is one backslash and a ';'), and every key after the backslash
 * typed its neighbour. */
static const char plain[0x39] = {
    [0x04] = 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
    'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
    [0x1E] = '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
    [0x28] = '\r', 0x1B, '\b', '\t', ' ', '-', '=', '[', ']', '\\', '\\', ';', '\'',
    '`', ',', '.', '/',
};
static const char shifted[0x39] = {
    [0x04] = 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
    'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
    [0x1E] = '!', '@', '#', '$', '%', '^', '&', '*', '(', ')',
    [0x28] = '\r', 0x1B, '\b', '\t', ' ', '_', '+', '{', '}', '|', '|', ':', '"',
    '~', '<', '>', '?',
};

static uint8_t last_mod;
static uint8_t last_keys[6];
static bool caps;
static int codepage = 860;
static char dead;               /* the pending dead key, or 0 */
static uint8_t dead_usage;      /* and the key it came from */

void pc_keys_set_codepage(int cp) { codepage = cp == 437 ? 437 : 860; }
int  pc_keys_codepage(void) { return codepage; }

static uint8_t ascii_of(uint8_t usage, uint8_t mod)
{
    const bool shift = mod & 0x22, ctrl = mod & 0x11, alt = mod & 0x44;
    if (alt) return 0;
    if (usage >= 0x04 && usage <= 0x38) {
        char c = (shift ? shifted : plain)[usage];
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

/* --- US-International ------------------------------------------------- */

static char dead_of(uint8_t usage, uint8_t mod)
{
    const bool shift = mod & 0x22;
    if (mod & 0x55) return 0;                       /* Ctrl or Alt: a command */
    if (usage == 0x34) return shift ? '"' : '\'';
    if (usage == 0x35) return shift ? '~' : '`';
    if (usage == 0x23 && shift) return '^';
    return 0;
}

/* The accented letter in the screen's code page, or 0 if it has none. */
static uint8_t compose(char d, char c)
{
    static const struct { char d, c; uint8_t cp437, cp860; } t[] = {
        { '\'', 'a', 0xA0, 0xA0 }, { '\'', 'e', 0x82, 0x82 }, { '\'', 'i', 0xA1, 0xA1 },
        { '\'', 'o', 0xA2, 0xA2 }, { '\'', 'u', 0xA3, 0xA3 }, { '\'', 'c', 0x87, 0x87 },
        { '\'', 'A', 0,    0x86 }, { '\'', 'E', 0x90, 0x90 }, { '\'', 'I', 0,    0x8B },
        { '\'', 'O', 0,    0x9F }, { '\'', 'U', 0,    0x96 }, { '\'', 'C', 0x80, 0x80 },
        { '`',  'a', 0x85, 0x85 }, { '`',  'e', 0x8A, 0x8A }, { '`',  'i', 0x8D, 0x8D },
        { '`',  'o', 0x95, 0x95 }, { '`',  'u', 0x97, 0x97 }, { '`',  'A', 0,    0x91 },
        { '^',  'a', 0x83, 0x83 }, { '^',  'e', 0x88, 0x88 }, { '^',  'i', 0x8C, 0    },
        { '^',  'o', 0x93, 0x93 }, { '^',  'u', 0x96, 0    }, { '^',  'A', 0,    0x8F },
        { '^',  'E', 0,    0x89 }, { '^',  'O', 0,    0x8C },
        { '~',  'a', 0,    0x84 }, { '~',  'o', 0,    0x94 }, { '~',  'n', 0xA4, 0xA4 },
        { '~',  'A', 0,    0x8E }, { '~',  'O', 0,    0x99 }, { '~',  'N', 0xA5, 0xA5 },
        { '"',  'a', 0x84, 0    }, { '"',  'e', 0x89, 0    }, { '"',  'i', 0x8B, 0    },
        { '"',  'o', 0x94, 0    }, { '"',  'u', 0x81, 0x81 }, { '"',  'y', 0x98, 0    },
        { '"',  'A', 0x8E, 0    }, { '"',  'O', 0x99, 0    }, { '"',  'U', 0x9A, 0x9A },
    };
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++)
        if (t[i].d == d && t[i].c == c) return codepage == 437 ? t[i].cp437 : t[i].cp860;
    return 0;
}

/* A key down and up with this character in the BIOS buffer. */
static void tap(uint8_t usage, uint8_t ch)
{
    const uint16_t s = sc[usage];
    pc_core_key((uint8_t)s, ch, (s & E) != 0, true);
    pc_core_key((uint8_t)s, 0, (s & E) != 0, false);
}

/* --- reports ----------------------------------------------------------- */

static void send(uint8_t usage, uint8_t mod, bool pressed)
{
    if (usage >= sizeof sc / sizeof sc[0] || !sc[usage]) return;
    const uint16_t s = sc[usage];
    pc_core_key((uint8_t)s, pressed ? ascii_of(usage, mod) : 0, (s & E) != 0, pressed);
}

/* A key going down, through the dead keys. */
static void press(uint8_t usage, uint8_t mod)
{
    const char d = dead_of(usage, mod);
    if (dead) {
        const char was = dead;
        const uint8_t was_usage = dead_usage;
        dead = 0;
        const uint8_t c = ascii_of(usage, mod);
        if (usage == 0x2C && !(mod & 0x55)) {      /* space: the accent itself */
            tap(was_usage, (uint8_t)was);
            return;
        }
        const uint8_t composed = c ? compose(was, (char)c) : 0;
        if (composed) {
            tap(usage, composed);
            return;
        }
        if (usage >= 0xE0 || (usage >= 0x39 && !c)) {   /* a key with no character */
            dead = was;
            dead_usage = was_usage;
            send(usage, mod, true);
            return;
        }
        tap(was_usage, (uint8_t)was);                  /* no accent: both as typed */
    }
    if (d) {
        dead = d;
        dead_usage = usage;
        return;
    }
    send(usage, mod, true);
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
        press(k, mod);
    }
    memcpy(last_keys, keys, 6);
    last_mod = mod;
    return f12;
}

/* --- typing, for the console and tools/pchost: plain US, no dead keys --- */

bool pc_keys_type(char c)
{
    if (c == '\n') c = '\r';
    if (c >= 1 && c <= 26 && c != '\b' && c != '\t' && c != '\r') {
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
        for (uint8_t usage = 0x04; usage <= 0x38; usage++) {
            if (t[usage] != c || !c) continue;
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
    }
    return false;
}
