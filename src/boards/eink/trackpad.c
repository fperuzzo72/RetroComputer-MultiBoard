/* trackpad.c - the panel as a laptop trackpad. See trackpad.h. */
#include "trackpad.h"

#include <stdlib.h>
#include <string.h>

/* Starting values, to be tuned on the device.
 *
 * The panel is about 220 pixels to the inch, so SLOP_PX is under 2mm:
 * less movement than that between down and up is a tap, not a drag. */
#define SLOP_PX       14
#define TAP_MAX_MS    250   /* a contact longer than this is not a tap */
#define TAP_WAIT_MS   250   /* after a tap, how long a second contact makes it tap-then-drag */
#define CLICK_DOWN_MS 80    /* how long a click holds the button */
#define CLICK_GAP_MS  80    /* between the two clicks of a double-click */

/* A finger coming off the glass rolls, and the last samples before the lift
 * slide the pointer a few pixels: in a menu held open with tap-then-drag,
 * enough to let go below "Quit" instead of on it (owner, 2026-09-29, two
 * tries of three). Whatever the pointer did in the last LIFT_UNDO_MS before
 * the lift is undone. */
#define LIFT_UNDO_MS  70

/* Pointer acceleration: machine pixels per panel pixel of finger movement,
 * as a multiple of "the pointer stays under the finger". Slow movement
 * goes finer than the finger, fast movement further. `speed` is panel
 * pixels since the last sample, which comes every 10ms or so. */
static float accel(int speed)
{
    float a = 0.6f + 0.08f * (float)speed;
    return a > 3.0f ? 3.0f : a;
}

void trackpad_init(trackpad *t, int w, int h, float scale)
{
    memset(t, 0, sizeof(*t));
    t->w = w;
    t->h = h;
    t->scale = scale;
    t->x = (float)(w / 2);
    t->y = (float)(h / 2);
}

static void play_clicks(trackpad *t, unsigned long now, int n)
{
    t->clicks_left = n;
    t->click_phase = 1;
    t->click_ms = now;
}

/* `speed` is how fast the finger is going, which is not always the size
 * of the move: see the slop below. */
static void move_by(trackpad *t, int dx, int dy, int speed)
{
    float g = accel(speed) / t->scale;
    t->x += (float)dx * g;
    t->y += (float)dy * g;
    if (t->x < 0) t->x = 0;
    if (t->y < 0) t->y = 0;
    if (t->x > t->w - 1) t->x = (float)(t->w - 1);
    if (t->y > t->h - 1) t->y = (float)(t->h - 1);
}

void trackpad_update(trackpad *t, unsigned long now, int touching, int px, int py)
{
    if (touching && !t->touching) {
        /* a finger comes down */
        t->touching = 1;
        t->moved = 0;
        t->sx = t->lx = px;
        t->sy = t->ly = py;
        t->down_ms = now;
        if (t->tap_pending && now - t->tap_ms <= TAP_WAIT_MS) {
            /* A second contact right after a tap. It is either the second
             * half of a double tap or the start of a tap-then-drag, and
             * which is not known until it moves, lingers or lifts. The
             * button waits: pressed on contact, a double tap held it down
             * for 20ms, between two of the Mac's looks at it, and the Mac
             * saw one click. */
            t->tap_pending = 0;
            t->second = 1;
        }
    } else if (touching) {
        /* A finger moves. The pointer holds still until the finger has
         * gone SLOP_PX, then catches that stretch up at the speed the
         * finger is actually going, and follows.
         *
         * This is the movement the owner found good on the device. Two
         * attempts at finer nudging made it worse and were undone
         * (2026-09-28): following from the first pixel, and following
         * after a 6px slop without catching up. Raw samples showed the
         * touch panel is not noisy (still finger: no jitter at all; moving:
         * 1-6px steps every ~11ms), so whatever made those feel erratic, it
         * was not jitter. Measure with `r` before trying again. */
        const int step = abs(px - t->lx) > abs(py - t->ly) ? abs(px - t->lx) : abs(py - t->ly);
        if (!t->moved && (abs(px - t->sx) > SLOP_PX || abs(py - t->sy) > SLOP_PX)) {
            t->moved = 1;
            move_by(t, px - t->sx, py - t->sy, step);
        } else if (t->moved) {
            move_by(t, px - t->lx, py - t->ly, step);
        }
        {
            const int k = t->trail_n++ % 8;
            t->trail[k].x = t->x;
            t->trail[k].y = t->y;
            t->trail[k].ms = now;
        }
        /* a second contact that moves or lingers is a drag: button down */
        if (t->second && (t->moved || now - t->down_ms > TAP_MAX_MS)) {
            t->second = 0;
            t->dragging = 1;
        }
        t->lx = px;
        t->ly = py;
    } else if (t->touching) {
        /* a finger lifts: first put the pointer back where it was before
         * the finger started coming off */
        t->touching = 0;
        if (t->moved) {
            for (int j = 1; j <= 8 && j <= t->trail_n; j++) {
                const int k = (t->trail_n - j) % 8;
                if (now - t->trail[k].ms >= LIFT_UNDO_MS) {
                    t->x = t->trail[k].x;
                    t->y = t->trail[k].y;
                    break;
                }
            }
        }
        t->trail_n = 0;
        const int quick = !t->moved && now - t->down_ms <= TAP_MAX_MS;
        if (t->second) {
            /* lifted before it could become a drag: a double tap */
            t->second = 0;
            play_clicks(t, now, 2);
        } else if (t->dragging) {
            t->dragging = 0;
        } else if (quick) {
            t->tap_pending = 1;
            t->tap_ms = now;
        }
    }

    /* a tap nobody followed up is a click */
    if (t->tap_pending && !t->touching && now - t->tap_ms > TAP_WAIT_MS) {
        t->tap_pending = 0;
        play_clicks(t, now, 1);
    }

    /* play clicks out */
    if (t->click_phase == 1 && now - t->click_ms >= CLICK_DOWN_MS) {
        t->clicks_left--;
        t->click_phase = t->clicks_left > 0 ? 2 : 0;
        t->click_ms = now;
    } else if (t->click_phase == 2 && now - t->click_ms >= CLICK_GAP_MS) {
        t->click_phase = 1;
        t->click_ms = now;
    }

    t->button = t->hw_button || t->dragging || t->click_phase == 1;
}

void trackpad_button(trackpad *t, int down)
{
    t->hw_button = down;
    t->button = t->hw_button || t->dragging || t->click_phase == 1;
}

void trackpad_warp(trackpad *t, int x, int y)
{
    t->x = (float)x;
    t->y = (float)y;
}

void trackpad_click(trackpad *t, unsigned long now, int clicks)
{
    play_clicks(t, now, clicks);
}

void trackpad_pointer(const trackpad *t, int *x, int *y, int *button)
{
    *x = (int)t->x;
    *y = (int)t->y;
    *button = t->button;
}
