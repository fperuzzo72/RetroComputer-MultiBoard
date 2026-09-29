/* display_bridge.cpp
 *
 * Implements display.h using
 * TFT_eSPI, wired to the FNK0103 3.5" ST7796 panel's real pinout (pulled
 * from Freenove's own TFT_eSPI setup file for this board - see
 * platformio.ini build_flags for the pin numbers and README for source).
 */
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "display.h"

/* Default-constructed on purpose. Passing (480,320) here - the landscape
 * size we actually draw in - tells TFT_eSPI the panel is NATIVELY 480x320,
 * and it is not: it is a 320x480 portrait panel that setRotation(1) turns
 * into a landscape one. With the wrong native size every setAddrWindow
 * lands somewhere else, which is what put garbage on the screen. The
 * working driver for this same board (CYD-MicroBASIC-MicroWriter,
 * editor/src/main.cpp) default-constructs it too. */
static TFT_eSPI tft;

/* The boot menu draws before any machine exists, and it needs the same
 * panel object: two TFT_eSPI instances would fight over one bus. */
TFT_eSPI &panel_tft() { return tft; }
/* Several rows at a time. pushPixels() has a fixed cost per call and a
 * row is only 512 to 768 bytes, so sending four rows in one go spreads
 * that cost over four times as much wire. */
#define BLIT_ROWS 2
static uint16_t lineBuf[BLIT_ROWS * DISPLAY_PANEL_W];

/* --- where the picture goes on the panel -------------------------
 *
 * A machine draws 256x216 here; the panel is 480x320. At 1:1 that leaves a wide
 * black surround: crisp, but small. At 1.5x the picture becomes 384x324
 * and very nearly fills the panel, at the cost of every third column and
 * row being a repeat. Neither is obviously right on a text machine, so
 * both are here and `z` on the serial console switches between them.
 * 1.5x is the default because it is the one you can read across a desk.
 * ---------------------------------------------------------------- */
#define PIC_W DISPLAY_PICTURE_W
#define PIC_H 216

static int sScale = 2; /* 1 = one panel pixel per machine pixel, 2 = three per two */

static inline int destW(void)  { return sScale == 1 ? PIC_W : PIC_W * 3 / 2; }
static inline int destH(void)  { return sScale == 1 ? PIC_H : PIC_H * 3 / 2; }
static inline int destX0(void) { return (DISPLAY_PANEL_W - destW()) / 2; }
static inline int destY0(void) { return (DISPLAY_PANEL_H - destH()) / 2; } /* sized for the largest possible width param (full-screen clear) */

/* TFT_eSPI sends a uint16_t buffer to the panel in the CPU's byte order
 * unless told otherwise, and the panel wants RGB565 the other way round.
 * Getting this wrong does not blank the screen - it scrambles every
 * colour, which is exactly what a garbled picture looks like. It is a
 * runtime setting rather than a constant here so the `w` command in the
 * serial console can flip it and settle the question on the panel. */
static bool sSwapBytes = true;

extern "C" void display_set_swap_bytes(int on) {
    sSwapBytes = on ? true : false;
    tft.setSwapBytes(sSwapBytes);
}

extern "C" int display_get_swap_bytes(void) { return sSwapBytes ? 1 : 0; }

extern "C" void display_request_test_pattern(int which);

/* Whole-panel fills, as opposed to the picture area. A handful since boot
 * is normal; once per frame means something is thrashing. */
static volatile unsigned long sFullRepaints = 0;
extern "C" unsigned long display_full_repaints(void) { return sFullRepaints; }

extern "C" void display_fill_panel(uint16_t color) {
    sFullRepaints++;
    tft.fillScreen(color);
}

/* A pending "repaint everything" request. It used to poke a global that
 * lives in the MSX video layer, which is exactly the kind of thing that
 * stops a second machine existing. The machine asks for it here and picks
 * it up on its own terms. */
static volatile int sRepaintWanted = 0;
extern "C" int display_take_repaint(void) {
    int v = sRepaintWanted;
    sRepaintWanted = 0;
    return v;
}

/* TFT_eSPI must be driven from ONE task. The serial console runs on core
 * 0 and the emulator on core 1, so a console command that draws directly
 * collides with the frame being blitted and wedges the SPI bus - which is
 * exactly what the task watchdog caught the first time `z` was typed.
 * Console commands therefore only leave a request here; display_service()
 * carries it out at the top of a frame, on the emulation task. */
static void drawTestPattern(int which);

static volatile int sPendingScale   = 0;  /* 0 = nothing pending */
static volatile int sPendingPattern = -1; /* -1 = nothing pending */

extern "C" void display_set_scale(int scale) {
    sPendingScale = (scale == 2) ? 2 : 1;
}

extern "C" int display_get_scale(void) {
    return sPendingScale ? sPendingScale : sScale;
}

/* How much of each frame goes into pushing pixels, as opposed to
 * emulating. Without this, "can it go faster" is an opinion; with it, it
 * is a number. */
static volatile uint32_t sBlitUs = 0;
extern "C" unsigned long display_blit_us(void) { return sBlitUs; }
extern "C" void display_blit_us_reset(void) { sBlitUs = 0; }

extern "C" void display_service(void) {
    if (sPendingScale) {
        sScale = sPendingScale;
        sPendingScale = 0;
        /* The old scale left pixels outside the new picture area. */
        tft.fillScreen(TFT_BLACK);
        sRepaintWanted = 1;
    }
    if (sPendingPattern >= 0) {
        int which = sPendingPattern;
        sPendingPattern = -1;
        drawTestPattern(which);
        sRepaintWanted = 1;   /* let the machine take the panel back */
    }
}

void display_bridge_init() {
    /* Drive the backlight before anything else, the way the working
     * driver for this board does. */
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);

    tft.init();
    /* Panel is portrait-native (320x480); rotate to landscape so the
     * 256x192 picture centres nicely on the wider dimension. If your
     * picture comes up mirrored/upside-down, try rotation 3 instead. */
    tft.setRotation(1);
    tft.setSwapBytes(sSwapBytes);
    tft.fillScreen(TFT_BLACK);
}

/* Draw something known, straight to the panel, with the emulator out of
 * the picture. Runs on the emulation task via display_service(). This separates the two things that both look like "garbage
 * on screen": wrong geometry (the picture is in the wrong place, torn, or
 * the wrong size) and wrong colour order (the picture is in the right
 * place but every colour is nonsense). */
static void drawTestPattern(int which) {
    switch (which) {
        case 0: tft.fillScreen(TFT_BLACK); break;
        case 1: tft.fillScreen(TFT_RED);   break;
        case 2: tft.fillScreen(TFT_GREEN); break;
        case 3: tft.fillScreen(TFT_BLUE);  break;
        case 4:
            /* A white block exactly where the picture is drawn, with
             * a one-pixel red frame around the whole panel. */
            tft.fillScreen(TFT_BLACK);
            tft.drawRect(0, 0, DISPLAY_PANEL_W, DISPLAY_PANEL_H, TFT_RED);
            tft.fillRect((DISPLAY_PANEL_W - PIC_W) / 2, (DISPLAY_PANEL_H - PIC_H) / 2,
                         PIC_W, PIC_H, TFT_WHITE);
            break;
        case 5: {
            /* The same block, but drawn the way a machine draws it:
             * through display_write_picture, in bands, out of a palette.
             * If case 4 looks right and this does not, the fault is in the
             * band path rather than in the panel setup. */
            static uint8_t band[PIC_W];
            static uint16_t pal[16];
            for (int i = 0; i < 16; i++)
                pal[i] = tft.color565((uint8_t)(i * 17), (uint8_t)(255 - i * 17), 0x40);
            tft.fillScreen(TFT_BLACK);
            for (int y = 0; y < PIC_H; y++) {
                for (int x = 0; x < PIC_W; x++) band[x] = (uint8_t)((y / 8 + x / 16) & 0x0F);
                display_write_picture(0, y, PIC_W, 1, band, pal[0], pal);
            }
            break;
        }
        default: break;
    }
}

/* Blit a horizontal slice of the picture. `srcY` and `height` are in
 * picture rows, not panel rows: where that lands on the panel and at
 * what size is this file's business and nobody else's. A NULL buffer is a
 * flat fill of bgColor, which is how the borders are drawn. */
extern "C" void display_write_picture(short srcX, short srcY, short width, short height,
                                         const uint8_t *buffer, uint16_t bgColor,
                                         const uint16_t *palette) {
    (void)srcX; /* every caller blits full-width slices */
    if (width <= 0 || height <= 0) return;

    const int dy0 = destY0();
    const int dw  = destW();
    int first, last;

    if (sScale == 1) {
        first = dy0 + srcY;
        last  = first + height;
    } else {
        first = dy0 + srcY * 3 / 2;
        last  = dy0 + (srcY + height) * 3 / 2;
    }
    if (first < 0) first = 0;
    if (last > DISPLAY_PANEL_H) last = DISPLAY_PANEL_H;
    if (last <= first) return;

    const uint32_t t0 = micros();

    tft.startWrite();
    tft.setAddrWindow(destX0(), first, dw, last - first);

    int held = 0;                      /* rows converted but not yet sent */
    for (int d = first; d < last; d++) {
        uint16_t *out = lineBuf + (size_t)held * dw;

        if (!buffer) {
            for (int i = 0; i < dw; i++) out[i] = bgColor;
        } else {
            /* At 1.5x every third destination row and column repeats the
             * one before it. On a text screen that shows as slightly
             * uneven letter widths, which is the whole reason 1:1 is still
             * the default and this is switchable. */
            int srcRow = (sScale == 1) ? (d - dy0) : ((d - dy0) * 2 / 3);
            int rel = srcRow - srcY;
            if (rel < 0) rel = 0;
            if (rel >= height) rel = height - 1;
            const uint8_t *row = buffer + (size_t)rel * width;
            if (sScale == 1)
                for (int x = 0; x < dw; x++) out[x] = palette[row[x]];
            else
                for (int x = 0; x < dw; x++) out[x] = palette[row[(x * 2) / 3]];
        }

        if (++held == BLIT_ROWS) {
            tft.pushPixels(lineBuf, dw * held);
            held = 0;
        }
    }
    if (held) tft.pushPixels(lineBuf, dw * held);

    tft.endWrite();
    sBlitUs += micros() - t0;
}

extern "C" void display_request_test_pattern(int which) { sPendingPattern = which; }

/* The vendored MSX video layer calls this name. Kept as a one-line
 * forward rather than editing AVideo.i again: that file is upstream code
 * and every edit to it is one more thing to carry. */
extern "C" void display_write_frame_msx(short left, short top, short width, short height,
                                        const uint8_t *buffer, uint16_t bgColor,
                                        const uint16_t *palette) {
    display_write_picture(left, top, width, height, buffer, bgColor, palette);
}
