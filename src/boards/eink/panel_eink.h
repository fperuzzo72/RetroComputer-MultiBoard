#ifndef PANEL_EINK_H
#define PANEL_EINK_H

/* The Paper Mono's panel, board-side. Machines see display_mono.h only. */

void panel_begin(void);

/* The I2C bus the touch panel, the power chip and the panel's reset share
 * (see panel.cpp): whoever uses it takes this. */
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
void panel_set_i2c_lock(SemaphoreHandle_t lock);

/* Drawing a whole screen onto the canvas (the menus): the panel does not
 * copy it halfway. */
void panel_canvas_lock(void);
void panel_canvas_unlock(void);

/* For the console: buffers, calls, and why nothing was sent. */
void panel_diag(void);

/* The canvas everything draws into (canvas.h), for display8 and menus. */
#include <stdint.h>
uint8_t *panel_canvas(void);

/* A boxed message over the picture until cleared: the pairing code. */
void panel_message(const char *line1, const char *line2);
void panel_message_clear(void);

/* Refresh if the machine's picture has changed since the last one.
 * Blocks for the length of the waveform. True if it refreshed. */
bool panel_service(void);

/* The next refresh is a full one, which clears the ghosts. */
void panel_request_full(void);

/* The machine's own 1-bit picture, for the serial console. NULL before
 * the machine has attached one. */
#include <stdint.h>
const uint8_t *panel_mono_picture(int *w, int *h);

/* How the machine's picture sits on the panel, for mapping touch; NULL
 * before the machine has attached one. */
#include "picture.h"
const eink_view *panel_view(void);

/* How long the last full (ghost-clearing) refresh took. */
unsigned long panel_last_full_ms(void);

/* Why each full refresh ran, and how many caret blinks were not shown. */
void panel_clean_stats(unsigned long *button, unsigned long *idle, unsigned long *cap, unsigned long *carets);

/* How many times the controller has been put to sleep between refreshes. */
unsigned long panel_controller_sleeps(void);

void panel_stats(unsigned long *count, unsigned long *fulls,
                 unsigned long *last_ms, unsigned long *avg_ms);

#endif
