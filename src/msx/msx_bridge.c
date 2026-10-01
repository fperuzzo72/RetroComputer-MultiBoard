/* msx_bridge.c - the C side of the wall described in msx_bridge.h.
 *
 * Plain C, includes MSX.h, and must never include Arduino.h (see the
 * `word` typedef note in msx_bridge.h). Anything that needs both worlds
 * gets split across this file and a .cpp file.
 */
#include <string.h>
#include "MSX.h"
#include "Sound.h"
#include "msx_bridge.h"
#include "display.h"
#include "msx_display.h"

#include "esp_heap_caps.h"
#include "esp_system.h"
#include "nvs.h"

int msx_video_prealloc(void) { return PreallocVideo(); }

/* Where the cartridge comes from. With cartridges built into the firmware
 * that is a sentinel rather than a path - see LOCAL_CART_PATH in MSX.c -
 * because this board has no room to hold an image in RAM and the flash
 * copy is used in place. With none selected, or none built in, the SD
 * path is tried, fails, and the machine boots into BASIC. */
#include "msx_carts.h"

static const char *gameRomPath(void) {
#ifdef HAVE_LOCAL_CART
    if (msx_cart_selected() >= 0) return "flash:cart";
#endif
    return "/sdcard/msx/games/game.rom";
}

void msx_run(void) {
    ROMName[0] = (char *)gameRomPath();

    /* Brazilian machines are PAL-M: PAL colour encoding on 60Hz/NTSC
     * timing, so NTSC is the right choice for the emulated frame rate. */
    Mode = MSX_MSX1 | (msx_hz() == 50 ? MSX_PAL : MSX_NTSC);
    printf("MSX: %d Hz\n", msx_hz());
    /* 64kB, which is what a Hotbit HB-8000 has. This only fits because
     * the video layer holds one 24-line band (6kB) instead of a whole
     * frame (55kB) - see docs/MEMORY.md. */
    RAMPages = 4;
    VRAMPages = 1; /* 16kB, what a TMS9918 has; safe now the tables are clamped */

    /* The core never calls these itself: every fMSX port is expected to
     * bring its own machine up before StartMSX() and tear it down after.
     * Without InitMachine() the video layer is never initialised and
     * ResetMSX() writes its palette through a NULL XPal. */
    if (!InitMachine()) return;

    /* Bring the mixer up before the machine starts writing to the PSG.
     * 22050Hz mono is what audio_glue.c can keep fed while the same core
     * is emulating a Z80 and pushing pixels; the latency figure is in
     * milliseconds. */
    {
        unsigned int rate = InitSound(22050, 30);
        printf("MSX: sound %s (%u Hz)\n", rate ? "on" : "OFF", rate);
    }
#ifdef DISPLAY_FRAME_PERCENT
    UPeriod = DISPLAY_FRAME_PERCENT;
#endif
    StartMSX(Mode, RAMPages, VRAMPages);
    TrashMSX();
    TrashMachine();
}

void msx_kbd_write(const uint8_t state[16]) {
    int i;
    for (i = 0; i < 16; i++) KeyState[i] = state[i];
}

/* What the machine is actually being told is held down. A zero bit is a
 * closed contact. Reading this back is how a key mapping gets checked
 * without watching for its effect on the screen, which is indirect and,
 * as it turned out, easy to misread. */
int msx_matrix_row(int row) {
    if (row < 0 || row > 15) return -1;
    return KeyState[row];
}

int msx_screen_mode(void) { return ScrMode; }

int msx_screen_row(int row, uint8_t *out, int max) {
    int cols, i;

    /* SCREEN 0 is 40 columns, SCREEN 1 is 32; anything else is a bitmap
     * mode with no character codes to read back. */
    if (ScrMode == 0)      cols = 40;
    else if (ScrMode == 1) cols = 32;
    else                   return 0;

    if (!ChrTab || row < 0 || row >= 24) return 0;
    if (cols > max) cols = max;

    for (i = 0; i < cols; i++) out[i] = ChrTab[row * (ScrMode == 0 ? 40 : 32) + i];
    return cols;
}

volatile unsigned int MSXFrames = 0; /* incremented in platform_glue.c */

unsigned int msx_frame_count(void) { return MSXFrames; }

/* Neither of these is declared in MSX.h, only defined in MSX.c: RAM[] is
 * the eight 8kB pages currently mapped into the Z80's address space, and
 * EnWrite says which 16kB slots are writable RAM rather than ROM. */
extern byte *RAM[8];
extern byte EnWrite[4];

/* MSX system variables, the same on every machine of this generation:
 * KEYBUF is the BIOS's 40-byte keyboard ring, PUTPNT is where the next
 * character goes in and GETPNT where the next one comes out. */
#define MSX_KEYBUF_START 0xFBF0
#define MSX_KEYBUF_END   0xFC17   /* inclusive, 40 bytes */
#define MSX_PUTPNT       0xF3F8
#define MSX_GETPNT       0xF3FA
#define MSX_CAPST        0xFCAB

static byte msxPeek(word A)          { return RAM[A >> 13][A & 0x1FFF]; }
static void msxPoke(word A, byte V)  { RAM[A >> 13][A & 0x1FFF] = V; }
static word msxPeekW(word A)         { return (word)(msxPeek(A) | (msxPeek(A + 1) << 8)); }
static void msxPokeW(word A, word V) { msxPoke(A, V & 0xFF); msxPoke(A + 1, V >> 8); }

int msx_type_char(unsigned char code) {
    word put  = msxPeekW(MSX_PUTPNT);
    word get  = msxPeekW(MSX_GETPNT);
    word next;

    /* Refuse unless page 3 really is writable RAM and the pointers are
     * inside the ring. Before BASIC is up neither holds, and this would
     * otherwise scribble on a ROM image. */
    if (!EnWrite[MSX_PUTPNT >> 14]) return 0;
    if (put < MSX_KEYBUF_START || put > MSX_KEYBUF_END) return 0;

    next = (word)(put + 1);
    if (next > MSX_KEYBUF_END) next = MSX_KEYBUF_START;
    if (next == get) return 0; /* ring full */

    msxPoke(put, code);
    msxPokeW(MSX_PUTPNT, next);
    return 1;
}

int msx_caps_on(void) { return msxPeek(MSX_CAPST) != 0; }

extern int MSXSoundOn; /* platform_glue.c */
extern byte *ROMData[MAXSLOTS]; /* MSX.c; not in MSX.h */

void msx_reboot(void) { esp_restart(); }

void msx_insert_cartridge(void) {
#ifdef HAVE_LOCAL_CART
    /* MSX-BASIC is no cartridge. Loading the SD path instead fails without
     * touching the slot, which left the last game in it: back to BASIC
     * booted that game again. Ejecting resets the machine, but only when
     * there was something to eject. */
    if (msx_cart_selected() < 0) {
        const int had = ROMData[0] != 0;
        LoadCart(NULL, 0, 0);
        if (!had) ResetMSX(Mode, RAMPages, VRAMPages);
        return;
    }
#endif
    /* LoadCart() ends by calling ResetMSX(), so this is the whole of it.
     * MAP_GUESS lets the core work out the mapper, which matters: a 128kB
     * Konami cartridge is not a 32kB one with more pages. */
    LoadCart(gameRomPath(), 0, MAP_GUESS);
}

/* 50Hz by default. The Hotbit is 60Hz (its TMS9128 runs NTSC timing under
 * PAL-M colour, and its BIOS says 60 at 0x002B), and that is what this
 * emulated first; but cartridges time themselves by the VDP's interrupt,
 * so at 60 they run a fifth faster than on a European MSX, music and all,
 * and the owner found 50 "perfeito, como lembro" (2026-09-30). `hz 60`
 * on the console gives the Hotbit back; remembered in NVS, taken at the
 * next start. */
#define HZ_NS  "cyd"
#define HZ_KEY "msxhz"

int msx_hz(void) {
    static int hz;
    if (!hz) {
        nvs_handle_t h;
        uint8_t v = 50;
        if (nvs_open(HZ_NS, NVS_READONLY, &h) == ESP_OK) {
            if (nvs_get_u8(h, HZ_KEY, &v) != ESP_OK) v = 50;
            nvs_close(h);
        }
        hz = v == 60 ? 60 : 50;
    }
    return hz;
}

void msx_set_hz(int hz) {
    nvs_handle_t h;
    if (nvs_open(HZ_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, HZ_KEY, (uint8_t)(hz == 60 ? 60 : 50));
    nvs_commit(h);
    nvs_close(h);
}

/* fMSX's UPeriod: the percentage of frames drawn. */
int msx_draw_percent(int pct) {
    if (pct >= 1 && pct <= 100) UPeriod = (byte)pct;
    return UPeriod;
}

void msx_set_sound(int on) { MSXSoundOn = on ? 1 : 0; }
int  msx_sound_on(void)    { return MSXSoundOn; }

int msx_peek(int addr) {
    if (addr < 0 || addr > 0xFFFF) return -1;
    if (!RAM[addr >> 13]) return -1;
    return msxPeek((word)addr);
}

int msx_char_pattern(int code, uint8_t *rows8) {
    int i;
    if (!ChrGen || code < 0 || code > 255) return 0;
    for (i = 0; i < 8; i++) rows8[i] = ChrGen[code * 8 + i];
    return 1;
}

volatile int MSXMemoryClaimed = 0; /* set from InitMachine(), see platform_glue.c */

int msx_memory_claimed(void) { return MSXMemoryClaimed; }
