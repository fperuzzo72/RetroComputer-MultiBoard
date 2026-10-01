/* display8.c - the 8-bit machines' pictures on an e-ink panel.
 *
 * Size. The picture is 256 wide and up to 216 tall with its border, and
 * it goes up exactly twice, to 512x432 in the middle of the panel (800x480
 * on the Paper Mono, 960x540 on the PaperS3).
 * An integer scale keeps every pixel square and every character stroke
 * the same width, which the 1.40x the Mac needs cannot.
 *
 * Colour. The panel has two. Each machine pixel is four panel pixels, so
 * it can be one of five tones - white, three ordered-dither greys, black -
 * and the tone is how far the colour stands from the border colour, not
 * how bright it is. On e-ink the background should be the paper: MSX-BASIC
 * is white text on a blue border, and by brightness that is white text on
 * a grey screen; by contrast with the border it is black text on white.
 * A Spectrum's black ink on white paper comes out as it is, and a game on
 * a black border comes out with bright things dark on white.
 *
 * Plain C: tools/eink_test runs it on the development machine.
 */
#include "display.h"
#include "picture.h"

#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_timer.h"
static int64_t now_us(void) { return esp_timer_get_time(); }
static void *big(size_t n) { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
#else
static long long now_us(void) { return 0; }
static void *big(size_t n) { return malloc(n); }
#endif

#define PIC_H   216
#define SCALE   2
/* Whole bytes: a machine pixel is two panel pixels, four to a byte. */
#define X0      (((DISPLAY_PANEL_W - DISPLAY_PICTURE_W * SCALE) / 2) & ~7)
#define Y0      ((DISPLAY_PANEL_H - PIC_H * SCALE) / 2)
#define STRIDE  (DISPLAY_PANEL_W / 8)

static uint8_t *canvas;
static volatile int repaint_wanted = 1;
static unsigned long blit_us, full_repaints;

/* The tones each picture row was last drawn with. A row whose tones have
 * not changed is not drawn again: most of a frame is the same as the one
 * before, and converting and writing it all cost ~24ms a drawn frame on
 * the PaperS3, which once its panel kept up was what held the MSX back.
 * Comparing tones rather than pixels catches a palette or border change
 * too. Forgotten whenever the canvas is wiped. */
static uint8_t (*shown)[DISPLAY_PICTURE_W];
static uint8_t shown_ok[PIC_H];

void display8_attach(uint8_t *c) { canvas = c; memset(shown_ok, 0, sizeof shown_ok); }
void display8_request_repaint(void) { repaint_wanted = 1; memset(shown_ok, 0, sizeof shown_ok); }

void display_bridge_init(void) {}

int display_take_repaint(void)
{
    int v = repaint_wanted;
    repaint_wanted = 0;
    return v;
}

static int luma(uint16_t c)
{
    const int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    return (r * 8 * 299 + g * 4 * 587 + b * 8 * 114) / 1000;   /* 0..255 */
}

/* Tone 0 (white) to 4 (black) from contrast with the border. Steps of 32
 * with a dead band at the bottom, so a colour within 16 of the border is
 * the border. */
static int tone(int l, int lbg)
{
    int d = l > lbg ? l - lbg : lbg - l;
    d = (d + 16) / 32;
    return d > 4 ? 4 : d;
}

/* The two panel pixels a machine pixel becomes on one panel row, as the
 * top two bits of a nibble, for tone t on panel row parity `odd`. A 2x2
 * ordered dither: tone 1 is one black pixel of the four, tone 2 two on a
 * diagonal, tone 3 three. */
static const uint8_t bayer[2][2] = { { 0, 2 }, { 3, 1 } };

static uint8_t pair_bits(int t, int odd)
{
    /* bit set = white, left pixel is the higher bit */
    const int left_black = t > bayer[odd][0];
    const int right_black = t > bayer[odd][1];
    return (uint8_t)((left_black ? 0 : 2) | (right_black ? 0 : 1));
}

/* One machine row into its two panel rows. `tones` holds a tone per
 * machine pixel. Four machine pixels make one byte of an upright panel
 * row; the device is held upside down (picture.h), so that byte goes in
 * bit-reversed at the mirrored position. */
static void put_row(int y, const uint8_t *tones)
{
    static uint8_t pb[2][5];
    static int pb_ready;
    if (!pb_ready) {
        for (int o = 0; o < 2; o++) for (int t = 0; t < 5; t++) pb[o][t] = pair_bits(t, o);
        pb_ready = 1;
    }
    for (int odd = 0; odd < 2; odd++) {
        const uint8_t *q = pb[odd];
        int uy = Y0 + y * SCALE + odd;
#if EINK_UPSIDE_DOWN
        uint8_t *row = canvas + (DISPLAY_PANEL_H - 1 - uy) * STRIDE;
#else
        uint8_t *row = canvas + uy * STRIDE;
#endif
        for (int k = 0; k < DISPLAY_PICTURE_W / 4; k++) {
            const uint8_t *t = tones + k * 4;
            uint8_t b = (uint8_t)(q[t[0]] << 6 | q[t[1]] << 4 | q[t[2]] << 2 | q[t[3]]);
#if EINK_UPSIDE_DOWN
            row[(DISPLAY_PANEL_W - 1 - (X0 + k * 8 + 7)) / 8] = eink_reverse8(b);
#else
            row[(X0 + k * 8) / 8] = b;
#endif
        }
    }
}

void display_write_picture(short srcX, short srcY, short width, short height,
                           const uint8_t *buffer, uint16_t bgColor,
                           const uint16_t *palette)
{
    (void)srcX;
    if (!canvas || height <= 0 || width <= 0) return;
    const long long t0 = now_us();
    const int lbg = luma(bgColor);

    /* tone per palette index, worked out once per call */
    uint8_t lut[256];
    uint8_t have[256];
    memset(have, 0, sizeof have);

    uint8_t tones[DISPLAY_PICTURE_W];
    for (int r = 0; r < height; r++) {
        const int y = srcY + r;
        if (y < 0 || y >= PIC_H) continue;
        if (!buffer) {
            memset(tones, 0, sizeof tones);
        } else {
            const uint8_t *src = buffer + r * width;
            const int n = width < DISPLAY_PICTURE_W ? width : DISPLAY_PICTURE_W;
            for (int x = 0; x < n; x++) {
                const uint8_t i = src[x];
                if (!have[i]) { lut[i] = (uint8_t)tone(luma(palette[i]), lbg); have[i] = 1; }
                tones[x] = lut[i];
            }
            for (int x = n; x < DISPLAY_PICTURE_W; x++) tones[x] = 0;
        }
        if (!shown) shown = big((size_t)PIC_H * DISPLAY_PICTURE_W);
        if (shown) {
            if (shown_ok[y] && !memcmp(shown[y], tones, sizeof tones)) continue;
            memcpy(shown[y], tones, sizeof tones);
            shown_ok[y] = 1;
        }
        put_row(y, tones);
    }
    blit_us += (unsigned long)(now_us() - t0);
}

void display_fill_panel(uint16_t color)
{
    (void)color;   /* the paper is white whatever the machine's surround */
    full_repaints++;
    memset(shown_ok, 0, sizeof shown_ok);
    if (canvas) memset(canvas, 0xFF, (size_t)STRIDE * DISPLAY_PANEL_H);
}

unsigned long display_full_repaints(void) { return full_repaints; }
void display_service(void) {}
void display_set_scale(int scale) { (void)scale; }
int  display_get_scale(void) { return 1; }
unsigned long display_blit_us(void) { return blit_us; }
void display_blit_us_reset(void) { blit_us = 0; }
void display_set_swap_bytes(int on) { (void)on; }
int  display_get_swap_bytes(void) { return 1; }
void display_request_test_pattern(int which) { (void)which; }

/* The vendored MSX video layer calls this name; a one-line forward, as on
 * the CYD, rather than another edit to upstream AVideo.i. */
void display_write_frame_msx(short left, short top, short width, short height,
                             const uint8_t *buffer, uint16_t bgColor,
                             const uint16_t *palette)
{
    display_write_picture(left, top, width, height, buffer, bgColor, palette);
}
