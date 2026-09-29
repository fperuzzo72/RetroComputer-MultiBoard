#ifndef DISPLAY_MONO_H
#define DISPLAY_MONO_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* A picture that is already a 1-bit framebuffer in the machine's own
 * memory - the Macintosh. A board that can show one implements this; the
 * band-at-a-time display.h is for the 8-bit machines, whose pictures are
 * rendered a scanline at a time from a VDP.
 *
 * The machine hands over its framebuffer once and says when a frame is
 * complete. When the panel actually refreshes is the board's business: an
 * e-ink panel takes hundreds of milliseconds to change and reads whatever
 * the machine last drew, and nothing the machine does should wait for it.
 *
 * Format: `w` pixels across, `h` rows, w/8 bytes to a row, most significant
 * bit leftmost, 1 = black. That is the Mac's own layout. */
void display_mono_attach(const uint8_t *fb, int w, int h);

/* One emulated vertical blank has passed. Cheap: a counter, no drawing. */
void display_mono_vsync(void);

#ifdef __cplusplus
}
#endif
#endif
