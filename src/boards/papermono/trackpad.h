#ifndef PAPERMONO_TRACKPAD_H
#define PAPERMONO_TRACKPAD_H
#ifdef __cplusplus
extern "C" {
#endif

/* The panel as a laptop trackpad, for a machine with a mouse.
 *
 * Pointing straight at things does not work on this panel: a Mac close
 * box is 11 pixels of a 72-dpi screen, a couple of millimetres here, and a
 * finger covers it along with everything round it. So the finger does not
 * say where the pointer is, it pushes it: drag anywhere and the pointer
 * moves by as much, slowly for small careful movements and faster for
 * quick ones, and the finger never hides what it is aiming at.
 *
 *   drag            move the pointer
 *   tap             click
 *   tap, tap        double-click
 *   tap, then drag  hold the button down for the drag (menus, windows,
 *                   selecting text); lifting lets it go
 *   hardware button the mouse button, held for as long as it is
 *
 * Plain C with no board in it: fed touch samples and a clock, it answers
 * where the pointer is and whether the button is down. tools/papermono_test
 * runs it on the development machine.
 */

typedef struct {
    /* the machine's picture, pixels, and panel pixels per machine pixel */
    int w, h;
    float scale;

    /* pointer, in machine pixels, and fractions of one carried over */
    float x, y;

    /* the current contact */
    int touching;
    int moved;               /* past the slop: a drag, not a tap */
    int lx, ly;              /* last sample, panel pixels, upright */
    int sx, sy;              /* where it came down */
    unsigned long down_ms;

    /* a tap waits a moment in case it is the first half of tap-then-drag */
    int tap_pending;
    unsigned long tap_ms;
    int second;              /* this contact came right after a tap */
    int dragging;            /* button held by a tap-then-drag */

    /* clicks being played out: button down, then up a moment later */
    int click_phase;         /* 0 none, 1 down, 2 up (gap before a second) */
    int clicks_left;
    unsigned long click_ms;

    int hw_button;
    int button;              /* what the machine is told */
} trackpad;

void trackpad_init(trackpad *t, int w, int h, float scale);

/* One touch sample: `touching` and, if so, where, in upright panel
 * pixels. Call every few milliseconds whether or not anything changed;
 * taps and clicks are timed from `now_ms`. */
void trackpad_update(trackpad *t, unsigned long now_ms, int touching, int px, int py);

/* The hardware mouse button. */
void trackpad_button(trackpad *t, int down);

/* For the serial console: put the pointer somewhere, and click there. */
void trackpad_warp(trackpad *t, int x, int y);
void trackpad_click(trackpad *t, unsigned long now_ms, int clicks);

/* Where the pointer is, and whether the button is down, for the machine. */
void trackpad_pointer(const trackpad *t, int *x, int *y, int *button);

#ifdef __cplusplus
}
#endif
#endif
