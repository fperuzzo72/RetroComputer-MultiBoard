#ifndef EINK_CANVAS_H
#define EINK_CANVAS_H
#ifdef __cplusplus
extern "C" {
#endif

/* Drawing on an e-ink panel buffer: one bit a pixel, a set bit white,
 * most significant bit leftmost, in the panel's own frame, EINK_PANEL_W x
 * EINK_PANEL_H (eink_board.h).
 *
 * Every coordinate here is in the upright frame, the way the device is
 * held (buttons along the top, picture.h); the turn to the panel's frame
 * happens in canvas_px and nowhere else. Plain drawing, no machine and no
 * hardware in it, so tools/eink_test can draw the same screens on the
 * development machine and look at them.
 *
 * Text is Noto Sans at 24px, from freeink-sdk's FreeInkUI (header only,
 * SIL Open Font License), ASCII only: the menus are written in words that
 * need no accent. */

#include <stdint.h>

#include "eink_board.h"

#define CANVAS_W EINK_PANEL_W
#define CANVAS_H EINK_PANEL_H
#define CANVAS_BYTES (CANVAS_W / 8 * CANVAS_H)

void canvas_clear(uint8_t *c, int black);
void canvas_px(uint8_t *c, int x, int y, int black);
void canvas_fill(uint8_t *c, int x, int y, int w, int h, int black);

/* An outline `t` pixels thick. */
void canvas_frame(uint8_t *c, int x, int y, int w, int h, int t, int black);

/* Text with its top-left at x,y, `scale` times the font's size (1 or 2).
 * Returns the width drawn. Characters outside ASCII print as '?'. */
int  canvas_text(uint8_t *c, int x, int y, const char *s, int scale, int black);
int  canvas_text_width(const char *s, int scale);
int  canvas_line_height(int scale);

/* Text centred in a box, cut short with ".." if it does not fit. */
void canvas_text_in(uint8_t *c, int x, int y, int w, int h, const char *s, int scale, int black);

#ifdef __cplusplus
}
#endif
#endif
