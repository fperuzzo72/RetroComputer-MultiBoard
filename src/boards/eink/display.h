#ifndef DISPLAY_H
#define DISPLAY_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "eink_board.h"

/* The panel, for the 8-bit machines, on an e-ink board.
 *
 * The same contract as the CYD's display.h (src/boards/cyd/display.h):
 * a machine renders its picture a band of scanlines at a time, 8 bits a
 * pixel through an RGB565 palette, and hands each band over. Here that
 * lands in the board's panel buffer instead of on an SPI TFT, and the
 * panel refreshes on its own schedule (panel.cpp), never at the machine's
 * pace. See display8.c for the picture itself: 2x, and colour to 1 bit by
 * contrast with the border. */

#define DISPLAY_PICTURE_W 256

/* The panel, in the frame the picture is drawn in. */
#define DISPLAY_PANEL_W EINK_PANEL_W
#define DISPLAY_PANEL_H EINK_PANEL_H

/* A panel that must not be made to blink. Every change is a ~400ms
 * refresh of the whole e-ink panel, so a machine that flashes something
 * for its own sake (the Spectrum's FLASH attribute, twice a second) holds
 * it steady instead. */
#define DISPLAY_STEADY 1

void display_bridge_init(void);

void display_write_picture(short srcX, short srcY, short width, short height,
                           const uint8_t *buffer, uint16_t bgColor,
                           const uint16_t *palette);

void display_fill_panel(uint16_t color);
unsigned long display_full_repaints(void);
void display_service(void);
int  display_take_repaint(void);
void display_set_scale(int scale);
int  display_get_scale(void);
unsigned long display_blit_us(void);
void display_blit_us_reset(void);
void display_set_swap_bytes(int on);
int  display_get_swap_bytes(void);
void display_request_test_pattern(int which);

/* Board side: where the picture goes, and asking the machine to redraw
 * all of it (after a menu has used the panel). */
void display8_attach(uint8_t *canvas);
void display8_request_repaint(void);

#ifdef __cplusplus
}
#endif
#endif
