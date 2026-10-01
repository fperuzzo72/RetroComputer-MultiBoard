/* c64_keys.cpp - a PC keyboard, as a BLE HID report, on the C64's matrix.
 *
 * The C64 scans an 8x8 matrix through CIA 1: a zero bit written to $DC00
 * selects a column, zero bits read from $DC01 are the keys down in it.
 * T-HMI-C64's own keyboards hand over one key at a time; here the whole
 * matrix is kept, so a game can see several keys at once
 * (lib/c64/src/C64Sys.cpp's getDC01 reads it through
 * retro_c64_matrix_read).
 *
 * The mapping is by symbol, the way the MSX's is: a key gives what is
 * printed on it. The C64 puts many symbols elsewhere (" is shift-2, [ is
 * shift-:), so each PC key names the C64 key it means and whether shift
 * must be down, up, or left as typed. Keys the C64 does not have:
 *
 *   left, up         shift + CRSR right, CRSR down
 *   Esc              RUN/STOP          Page Up    RESTORE
 *   Alt              C=                Tab        CTRL
 *   Insert           shift + INST/DEL  End        shift + CLR/HOME
 *   F2 F4 F6 F8      shift + F1 F3 F5 F7
 *   \ |              the pound sign    _ `        the left arrow
 *
 * F10 turns the arrows and Ctrl into a joystick (fire), on port 2, which
 * is the one most games read; F9 moves it to port 1. F12 opens the
 * selector, as on the other machines.
 */
#include <cstdint>
#include <cstring>

#include "C64Sys.h"
#include "retro_c64_drivers.h"
#include "c64_keys.h"
#include "selector.h"

/* A C64 key: column (the $DC00 bit) and row (the $DC01 bit). */
#define K(col, row) (uint8_t)(((col) << 3) | (row))
enum {
    C_DEL = K(0, 0), C_RETURN = K(0, 1), C_RIGHT = K(0, 2), C_F7 = K(0, 3),
    C_F1 = K(0, 4), C_F3 = K(0, 5), C_F5 = K(0, 6), C_DOWN = K(0, 7),
    C_3 = K(1, 0), C_W = K(1, 1), C_A = K(1, 2), C_4 = K(1, 3),
    C_Z = K(1, 4), C_S = K(1, 5), C_E = K(1, 6), C_LSHIFT = K(1, 7),
    C_5 = K(2, 0), C_R = K(2, 1), C_D = K(2, 2), C_6 = K(2, 3),
    C_C = K(2, 4), C_F = K(2, 5), C_T = K(2, 6), C_X = K(2, 7),
    C_7 = K(3, 0), C_Y = K(3, 1), C_G = K(3, 2), C_8 = K(3, 3),
    C_B = K(3, 4), C_H = K(3, 5), C_U = K(3, 6), C_V = K(3, 7),
    C_9 = K(4, 0), C_I = K(4, 1), C_J = K(4, 2), C_0 = K(4, 3),
    C_M = K(4, 4), C_K = K(4, 5), C_O = K(4, 6), C_N = K(4, 7),
    C_PLUS = K(5, 0), C_P = K(5, 1), C_L = K(5, 2), C_MINUS = K(5, 3),
    C_PERIOD = K(5, 4), C_COLON = K(5, 5), C_AT = K(5, 6), C_COMMA = K(5, 7),
    C_POUND = K(6, 0), C_STAR = K(6, 1), C_SEMI = K(6, 2), C_HOME = K(6, 3),
    C_RSHIFT = K(6, 4), C_EQUALS = K(6, 5), C_UPARROW = K(6, 6), C_SLASH = K(6, 7),
    C_1 = K(7, 0), C_LEFTARROW = K(7, 1), C_CTRL = K(7, 2), C_2 = K(7, 3),
    C_SPACE = K(7, 4), C_CBM = K(7, 5), C_Q = K(7, 6), C_STOP = K(7, 7),
    C_NONE = 0xff,
};

/* Shift for a mapped key: as typed, forced down, forced up. */
enum { S_AS = 0, S_ON = 1, S_OFF = 2 };

struct Map { uint8_t key; uint8_t shift; };
/* Per HID usage: unshifted, then shifted. */
struct Pair { Map plain, shifted; };

static Pair table[256];

static void set(uint8_t u, uint8_t k, uint8_t s, uint8_t ks, uint8_t ss)
{
    table[u].plain = { k, s };
    table[u].shifted = { ks, ss };
}

static void build(void)
{
    static bool done;
    if (done) return;
    done = true;
    for (int i = 0; i < 256; i++) table[i] = { { C_NONE, S_AS }, { C_NONE, S_AS } };
    static const uint8_t letters[26] = {
        C_A, C_B, C_C, C_D, C_E, C_F, C_G, C_H, C_I, C_J, C_K, C_L, C_M,
        C_N, C_O, C_P, C_Q, C_R, C_S, C_T, C_U, C_V, C_W, C_X, C_Y, C_Z };
    for (int i = 0; i < 26; i++) set(0x04 + i, letters[i], S_AS, letters[i], S_AS);
    /* digits, and what shift puts on them on a US keyboard */
    set(0x1e, C_1, S_OFF, C_1, S_ON);          /* 1 ! */
    set(0x1f, C_2, S_OFF, C_AT, S_OFF);        /* 2 @ */
    set(0x20, C_3, S_OFF, C_3, S_ON);          /* 3 # */
    set(0x21, C_4, S_OFF, C_4, S_ON);          /* 4 $ */
    set(0x22, C_5, S_OFF, C_5, S_ON);          /* 5 % */
    set(0x23, C_6, S_OFF, C_UPARROW, S_OFF);   /* 6 ^ */
    set(0x24, C_7, S_OFF, C_6, S_ON);          /* 7 & */
    set(0x25, C_8, S_OFF, C_STAR, S_OFF);      /* 8 * */
    set(0x26, C_9, S_OFF, C_8, S_ON);          /* 9 ( */
    set(0x27, C_0, S_OFF, C_9, S_ON);          /* 0 ) */
    set(0x28, C_RETURN, S_AS, C_RETURN, S_AS);
    set(0x29, C_STOP, S_AS, C_STOP, S_AS);     /* Esc */
    set(0x2a, C_DEL, S_OFF, C_DEL, S_ON);      /* Backspace; shift gives INST */
    set(0x2b, C_CTRL, S_AS, C_CTRL, S_AS);     /* Tab */
    set(0x2c, C_SPACE, S_AS, C_SPACE, S_AS);
    set(0x2d, C_MINUS, S_OFF, C_LEFTARROW, S_OFF);  /* - _ */
    set(0x2e, C_EQUALS, S_OFF, C_PLUS, S_OFF);      /* = + */
    set(0x2f, C_COLON, S_ON, C_COLON, S_ON);        /* [ { */
    set(0x30, C_SEMI, S_ON, C_SEMI, S_ON);          /* ] } */
    set(0x31, C_POUND, S_OFF, C_POUND, S_OFF);      /* \ | */
    set(0x33, C_SEMI, S_OFF, C_COLON, S_OFF);       /* ; : */
    set(0x34, C_7, S_ON, C_2, S_ON);                /* ' " */
    set(0x35, C_LEFTARROW, S_OFF, C_LEFTARROW, S_OFF);  /* ` ~ */
    set(0x36, C_COMMA, S_OFF, C_COMMA, S_ON);       /* , < */
    set(0x37, C_PERIOD, S_OFF, C_PERIOD, S_ON);     /* . > */
    set(0x38, C_SLASH, S_OFF, C_SLASH, S_ON);       /* / ? */
    set(0x3a, C_F1, S_OFF, C_F1, S_ON);             /* F1 */
    set(0x3b, C_F1, S_ON, C_F1, S_ON);              /* F2 */
    set(0x3c, C_F3, S_OFF, C_F3, S_ON);
    set(0x3d, C_F3, S_ON, C_F3, S_ON);
    set(0x3e, C_F5, S_OFF, C_F5, S_ON);
    set(0x3f, C_F5, S_ON, C_F5, S_ON);
    set(0x40, C_F7, S_OFF, C_F7, S_ON);
    set(0x41, C_F7, S_ON, C_F7, S_ON);
    set(0x49, C_DEL, S_ON, C_DEL, S_ON);            /* Insert */
    set(0x4a, C_HOME, S_OFF, C_HOME, S_ON);         /* Home; shift clears */
    set(0x4c, C_DEL, S_OFF, C_DEL, S_ON);           /* Delete */
    set(0x4d, C_HOME, S_ON, C_HOME, S_ON);          /* End: CLR */
    set(0x4f, C_RIGHT, S_OFF, C_RIGHT, S_OFF);
    set(0x50, C_RIGHT, S_ON, C_RIGHT, S_ON);
    set(0x51, C_DOWN, S_OFF, C_DOWN, S_OFF);
    set(0x52, C_DOWN, S_ON, C_DOWN, S_ON);
    set(0x58, C_RETURN, S_AS, C_RETURN, S_AS);      /* keypad Enter */
}

/* The matrix, column by column: bit r set = the key at (column, r) down. */
static volatile uint8_t matrix[8];
static volatile uint8_t joy_bits = 0xff;      /* active low, as the CIA reads it */
static volatile int joy_on, joy_port = 2;
static C64Sys *sys;
static uint8_t last[8];

void retro_c64_keys_attach(void *c64sys)
{
    sys = (C64Sys *)c64sys;
    build();
    if (sys) sys->kbjoystickmode = joy_on ? (uint8_t)joy_port : 0;
}

static int in_report(const uint8_t r[8], uint8_t u)
{
    for (int i = 2; i < 8; i++) if (r[i] == u) return 1;
    return 0;
}

void retro_c64_keys_report(const uint8_t r[8])
{
    build();
    const uint8_t mods = r[0];
    const int shift_held = (mods & 0x22) != 0;
    const int ctrl = (mods & 0x11) != 0;
    uint8_t m[8] = { 0 };
    int force = -1;                     /* -1 as typed, else 0 or 1 */
    uint8_t joy = 0xff;

    /* toggles on the press only */
    if (in_report(r, 0x45) && !in_report(last, 0x45))             /* F12 */
        selector_open();
    if (in_report(r, 0x43) && !in_report(last, 0x43)) {          /* F10 */
        joy_on = !joy_on;
        if (sys) sys->kbjoystickmode = joy_on ? (uint8_t)joy_port : 0;
    }
    if (in_report(r, 0x42) && !in_report(last, 0x42)) {          /* F9 */
        joy_port = joy_port == 2 ? 1 : 2;
        if (sys && joy_on) sys->kbjoystickmode = (uint8_t)joy_port;
    }
    if (in_report(r, 0x4b) && !in_report(last, 0x4b) && sys)     /* Page Up */
        sys->restorenmi = true;

    for (int i = 2; i < 8; i++) {
        const uint8_t u = r[i];
        if (!u) continue;
        if (joy_on) {
            if (u == 0x52) { joy &= (uint8_t)~0x01; continue; }    /* up */
            if (u == 0x51) { joy &= (uint8_t)~0x02; continue; }    /* down */
            if (u == 0x50) { joy &= (uint8_t)~0x04; continue; }    /* left */
            if (u == 0x4f) { joy &= (uint8_t)~0x08; continue; }    /* right */
        }
        const Map &mp = shift_held ? table[u].shifted : table[u].plain;
        if (mp.key == C_NONE) continue;
        m[mp.key >> 3] |= (uint8_t)(1 << (mp.key & 7));
        if (mp.shift == S_ON) force = 1;
        else if (mp.shift == S_OFF) force = 0;
    }
    if (joy_on && ctrl) joy &= (uint8_t)~0x10;                     /* fire */
    else if (ctrl) m[C_CTRL >> 3] |= (uint8_t)(1 << (C_CTRL & 7));
    if (mods & 0x44) m[C_CBM >> 3] |= (uint8_t)(1 << (C_CBM & 7)); /* Alt: C= */

    const int shift = force < 0 ? shift_held : force;
    if (shift) {
        if (mods & 0x20) m[C_RSHIFT >> 3] |= (uint8_t)(1 << (C_RSHIFT & 7));
        else m[C_LSHIFT >> 3] |= (uint8_t)(1 << (C_LSHIFT & 7));
    }
    for (int c = 0; c < 8; c++) matrix[c] = m[c];
    joy_bits = joy;
    memcpy(last, r, 8);
}

/* Columns selected by zero bits of `select` (what was written to $DC00),
 * the rows down among them as zero bits; or, the program reading the
 * ports the other way round, rows selected and columns read. */
uint8_t retro_c64_matrix_read(uint8_t select, bool rows_from_columns)
{
    uint8_t out = 0;
    if (!rows_from_columns) {
        for (int c = 0; c < 8; c++)
            if (!(select & (1 << c))) out |= matrix[c];
    } else {
        for (int c = 0; c < 8; c++)
            if (matrix[c] & (uint8_t)~select) out |= (uint8_t)(1 << c);
    }
    return (uint8_t)~out;
}

uint8_t RetroC64Keyboard::getKBJoyValue() { return joy_bits; }

int retro_c64_joystick_on(void) { return joy_on ? joy_port : 0; }
