#ifndef EINK_UI_H
#define EINK_UI_H
#ifdef __cplusplus
extern "C" {
#endif

/* The Paper Mono's screens, drawn and hit-tested, with nothing that waits
 * for a finger: chooser.cpp does the waiting. Split like this so that
 * tools/eink_test can draw every screen on the development machine.
 *
 * Coordinates are the upright frame (canvas.h). Targets are at least
 * 50 panel pixels tall, about 6mm on this panel: a fingertip, not a
 * cursor. */

#include <stdint.h>

#define UI_NONE  (-1)
#define UI_BACK  (-2)
#define UI_PREV  (-3)
#define UI_NEXT  (-4)

/* Which computer. One wide band each. `mark` is outlined heavier (the one
 * that will boot if nothing is touched), -1 for none. `note` is a line at
 * the bottom, or NULL. */
void ui_draw_machines(uint8_t *c, const char *const *names, int n, int mark,
                      int allow_back, const char *note);
int  ui_hit_machines(int x, int y, int n, int allow_back);

/* Which cartridge, snapshot or tape: a grid, a page at a time. */
#define UI_COLS 3
#define UI_ROWS 7
#define UI_PER_PAGE (UI_COLS * UI_ROWS)

typedef const char *(*ui_name_fn)(int index);

void ui_draw_entries(uint8_t *c, const char *title, ui_name_fn name, int count,
                     int page, int allow_back);
int  ui_hit_entries(int x, int y, int count, int page, int allow_back);
int  ui_pages(int count);

/* A boxed message in the middle of whatever is on the canvas: the pairing
 * code, mostly. Two lines, the second may be NULL. */
void ui_draw_message(uint8_t *c, const char *line1, const char *line2);

#ifdef __cplusplus
}
#endif
#endif
