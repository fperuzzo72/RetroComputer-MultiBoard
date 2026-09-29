#ifndef PAPERMONO_PICTURE_H
#define PAPERMONO_PICTURE_H

/* How a machine's 1-bit picture lands on the Paper Mono's 800x480 panel:
 * which way up, how big, and where. Plain C, so tools/papermono_test can
 * check it on the development machine.
 *
 * Which way up. freeink-sdk's profile shows the panel with the two
 * buttons along the bottom edge, which is where the hand holding the
 * device rests - and pressing them by accident is a refresh at best and a
 * reset at worst. Turned round, the buttons are along the top. The owner
 * asked for it this way.
 *
 * How big. The panel is about 220 pixels to the inch and the Mac was
 * drawn for 72, so a Mac pixel for a panel pixel is too small to read.
 * The picture is scaled up to fill the panel's height, keeping its shape:
 * a 512x342 Mac comes out 719x480, 1.40 times, centred. Nearest-pixel, so
 * some Mac pixels become two panel pixels and some one; for 1-bit text
 * that reads better than anything averaged would on a 1-bit panel.
 *
 * The picture and the touch panel both go through the functions below.
 * Change the flag here and both follow; never flip one on its own. */

#include <stdint.h>
#include <string.h>

#define PAPERMONO_UPSIDE_DOWN 1

typedef struct {
    int pw, ph;         /* panel, pixels */
    int w, h;           /* machine picture, pixels */
    int dw, dh;         /* picture as drawn on the panel */
    int x0, y0;         /* where it is drawn, in the upright frame */
} papermono_view;

/* Largest size that fits and keeps the picture's shape. */
static inline papermono_view papermono_view_make(int pw, int ph, int w, int h)
{
    papermono_view v;
    v.pw = pw; v.ph = ph; v.w = w; v.h = h;
    if ((long)pw * h <= (long)ph * w) {   /* width-limited */
        v.dw = pw;
        v.dh = (int)((long)h * pw / w);
    } else {                              /* height-limited */
        v.dh = ph;
        v.dw = (int)((long)w * ph / h);
    }
    v.x0 = (pw - v.dw) / 2;
    v.y0 = (ph - v.dh) / 2;
    return v;
}

/* Draw `src` (1 = black, MSB leftmost, w/8 bytes a row) into `dst` (the
 * panel's framebuffer: 1 = white, pw/8 bytes a row). The margins come out
 * white. */
static inline void papermono_draw(const papermono_view *v, uint8_t *dst, const uint8_t *src)
{
    const int pstride = v->pw / 8, sstride = v->w / 8;
    memset(dst, 0xFF, (size_t)pstride * v->ph);
    for (int dy = 0; dy < v->dh; dy++) {
        const uint8_t *srow = src + (long)dy * v->h / v->dh * sstride;
        int py = v->y0 + dy;
#if PAPERMONO_UPSIDE_DOWN
        py = v->ph - 1 - py;
#endif
        uint8_t *prow = dst + (long)py * pstride;
        for (int dx = 0; dx < v->dw; dx++) {
            const int sx = (int)((long)dx * v->w / v->dw);
            if (!(srow[sx >> 3] & (0x80 >> (sx & 7)))) continue;   /* white */
            int px = v->x0 + dx;
#if PAPERMONO_UPSIDE_DOWN
            px = v->pw - 1 - px;
#endif
            prow[px >> 3] &= (uint8_t)~(0x80 >> (px & 7));
        }
    }
}

/* A touch point, in the panel's own frame, to the upright frame the
 * picture is drawn in. For movement, which is what the trackpad wants. */
static inline void papermono_touch_upright(const papermono_view *v, int *x, int *y)
{
#if PAPERMONO_UPSIDE_DOWN
    *x = v->pw - 1 - *x;
    *y = v->ph - 1 - *y;
#else
    (void)v; (void)x; (void)y;
#endif
}

/* An upright panel point to the machine pixel drawn there, clamped to the
 * picture. For pointing straight at something. */
static inline void papermono_upright_to_picture(const papermono_view *v, int *x, int *y)
{
    int mx = (int)((long)(*x - v->x0) * v->w / v->dw);
    int my = (int)((long)(*y - v->y0) * v->h / v->dh);
    *x = mx < 0 ? 0 : mx >= v->w ? v->w - 1 : mx;
    *y = my < 0 ? 0 : my >= v->h ? v->h - 1 : my;
}

#endif
