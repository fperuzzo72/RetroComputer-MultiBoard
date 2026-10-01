#ifndef PC_TEXT_H
#define PC_TEXT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The PC's screen as a 1-bit picture for display_mono.h: 640x400, the
 * VGA's own text resolution (80x25 cells of 8x16), 1 = black. */
#define PC_FB_W 640
#define PC_FB_H 400

/* Draws what changed since the last call into fb. True if anything did. */
bool pc_text_render(uint8_t *fb);

/* Forget what was drawn: the next render redraws everything. */
void pc_text_invalidate(void);

#ifdef __cplusplus
}
#endif

#endif
