#ifndef DISPLAY_H
#define DISPLAY_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* The panel, for whichever machine is built in.
 *
 * The picture is drawn a horizontal band at a time rather than a frame at
 * a time: this board has no PSRAM and a full 8bpp frame costs 55kB it
 * does not have. A machine renders a band of scanlines into its own small
 * buffer and hands it here as soon as it is complete. See docs/MEMORY.md
 * and docs/DISPLAY.md. */

/* The picture area, in machine pixels. Both machines here draw 256 across;
 * where that lands on the 480x320 panel, and at what scale, is this
 * layer's business. */
#define DISPLAY_PICTURE_W 256

/* The panel itself, in landscape. It is a 320x480 portrait ST7796 turned
 * by setRotation(1) - see docs/DISPLAY.md, where telling the library
 * otherwise cost an evening. */
#define DISPLAY_PANEL_W 480
#define DISPLAY_PANEL_H 320

void display_bridge_init(void);

/* Blit `height` rows of an 8bpp indexed band. `srcY` is the picture row
 * the band starts at, not a panel row. A NULL buffer is a flat fill of
 * bgColor, which is how the borders are drawn. */
void display_write_picture(short srcX, short srcY, short width, short height,
                           const uint8_t *buffer, uint16_t bgColor,
                           const uint16_t *palette);

/* Fill the whole panel, surround included. */
void display_fill_panel(uint16_t color);
unsigned long display_full_repaints(void);

/* Carry out any panel change the serial console asked for. Call at the
 * top of a frame, from the machine's own task: TFT_eSPI may only be
 * driven from one task and the console runs on the other core. */
void display_service(void);

/* Non-zero once if something asked for the whole picture to be redrawn -
 * a scale change, a test pattern. A machine calls this each frame and
 * repaints when it says so. */
int display_take_repaint(void);

/* Picture scale: 1 = one panel pixel per machine pixel, 2 = three panel
 * pixels per two (1.5x, nearly full screen). */
void display_set_scale(int scale);
int  display_get_scale(void);

/* Microseconds spent pushing pixels, so "can it go faster" is a number
 * rather than an opinion. */
unsigned long display_blit_us(void);
void display_blit_us_reset(void);

void display_set_swap_bytes(int on);
int  display_get_swap_bytes(void);
void display_request_test_pattern(int which);

#ifdef __cplusplus
}
#endif
#endif
