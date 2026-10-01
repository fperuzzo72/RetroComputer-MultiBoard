#ifndef FASTEPD_H
#define FASTEPD_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* The PaperS3's panel driven directly, at up to ~60 pictures a second.
 *
 * Built on Modos Smooth Graphics, the driver of PaperBoy (Wenting Zhang,
 * gitlab.com/zephray/paperboy, MIT): no waveform, no controller. The ESP32
 * clocks the ED047TC1's gate and source drivers itself through the LCD
 * peripheral, and a pixel whose colour changes is pushed towards it for a
 * few scans and then left alone. A change is on the glass within tens of
 * milliseconds instead of the ~400 of a waveform refresh.
 *
 * fastepd_show() takes the whole 960x540 picture (1 = white, as the canvas
 * is) and returns at once; a task of its own does the scanning, only while
 * something is changing. fastepd_clean() pushes the whole panel black and
 * white a few times and draws the picture again, which is what clears the
 * ghosts this way of driving leaves. */

int  fastepd_begin(void);                 /* 0 on success */
void fastepd_show(const uint8_t *picture);
void fastepd_clean(void);

/* The panel's rails off before the board is. */
void fastepd_power_off(void);

/* For the console: `fe`. */
void fastepd_command(const char *args);

#ifdef __cplusplus
}
#endif
#endif
