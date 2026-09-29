#ifndef EINK_PICTURE_H
#define EINK_PICTURE_H

/* How a machine's 1-bit picture lands on an e-ink panel: which way up,
 * how big, and where. Plain C, so tools/eink_test can check it on the
 * development machine.
 *
 * Which way up is the board's (eink_board.h, EINK_UPSIDE_DOWN): the Paper
 * Mono is held turned round so its buttons are along the top.
 *
 * How big. These panels are 200-odd pixels to the inch and the Mac was
 * drawn for 72, so a Mac pixel for a panel pixel is too small to read.
 * The picture is scaled up to fill the panel's height, keeping its shape:
 * a 512x342 Mac comes out 719x480 on the Paper Mono (1.40x) and 808x540 on
 * the PaperS3 (1.58x), centred. Nearest-pixel, so some Mac pixels become
 * two panel pixels and some one; for 1-bit text that reads better than
 * anything averaged would on a 1-bit panel.
 *
 * The picture and the touch panel both go through the functions below. */

#include <stdint.h>
#include <string.h>

#include "eink_board.h"

/* A byte of eight pixels, mirrored left to right: what turning the panel
 * round does to a byte of it. */
static inline uint8_t eink_reverse8(uint8_t b)
{
    b = (uint8_t)((b & 0xF0) >> 4 | (b & 0x0F) << 4);
    b = (uint8_t)((b & 0xCC) >> 2 | (b & 0x33) << 2);
    b = (uint8_t)((b & 0xAA) >> 1 | (b & 0x55) << 1);
    return b;
}

typedef struct {
    int pw, ph;         /* panel, pixels */
    int w, h;           /* machine picture, pixels */
    int dw, dh;         /* picture as drawn on the panel */
    int x0, y0;         /* where it is drawn, in the upright frame */
} eink_view;

/* Largest size that fits and keeps the picture's shape. */
static inline eink_view eink_view_make(int pw, int ph, int w, int h)
{
    eink_view v;
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
static inline void eink_draw(const eink_view *v, uint8_t *dst, const uint8_t *src)
{
    const int pstride = v->pw / 8, sstride = v->w / 8;
    memset(dst, 0xFF, (size_t)pstride * v->ph);
    for (int dy = 0; dy < v->dh; dy++) {
        const uint8_t *srow = src + (long)dy * v->h / v->dh * sstride;
        int py = v->y0 + dy;
#if EINK_UPSIDE_DOWN
        py = v->ph - 1 - py;
#endif
        uint8_t *prow = dst + (long)py * pstride;
        for (int dx = 0; dx < v->dw; dx++) {
            const int sx = (int)((long)dx * v->w / v->dw);
            if (!(srow[sx >> 3] & (0x80 >> (sx & 7)))) continue;   /* white */
            int px = v->x0 + dx;
#if EINK_UPSIDE_DOWN
            px = v->pw - 1 - px;
#endif
            prow[px >> 3] &= (uint8_t)~(0x80 >> (px & 7));
        }
    }
}

/* A touch point, in the panel's own frame, to the upright frame the
 * picture is drawn in. For movement, which is what the trackpad wants. */
static inline void eink_touch_upright(const eink_view *v, int *x, int *y)
{
#if EINK_UPSIDE_DOWN
    *x = v->pw - 1 - *x;
    *y = v->ph - 1 - *y;
#else
    (void)v; (void)x; (void)y;
#endif
}

/* An upright panel point to the machine pixel drawn there, clamped to the
 * picture. For pointing straight at something. */
static inline void eink_upright_to_picture(const eink_view *v, int *x, int *y)
{
    int mx = (int)((long)(*x - v->x0) * v->w / v->dw);
    int my = (int)((long)(*y - v->y0) * v->h / v->dh);
    *x = mx < 0 ? 0 : mx >= v->w ? v->w - 1 : mx;
    *y = my < 0 ? 0 : my >= v->h ? v->h - 1 : my;
}

#endif
