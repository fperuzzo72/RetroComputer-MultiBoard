/* ui.cpp - the Paper Mono's screens. See ui.h. */
#include "ui.h"
#include "canvas.h"

#include <stdio.h>

static const int M = 16;          /* outer margin */
static const int HEAD_H = 64;     /* title band */
static const int FOOT_Y = CANVAS_H - 76;   /* footer band starts here */
static const int FOOT_H = 64;

static int inside(int x, int y, int bx, int by, int bw, int bh)
{
    return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

/* --- which computer ------------------------------------------------- */

/* Up to four choices, one column of wide bands. More than that (five
 * computers and the other app slots, the owner's request of 2026-10-01),
 * two columns, filled down the left first: the computers come first in
 * the list, so they keep the left and the ways out end the right. */
#define UI_ONE_COLUMN_MAX 4

static void machine_band(int i, int n, int *x, int *y, int *w, int *h)
{
    const int top = HEAD_H, bottom = FOOT_Y - 8, gap = 14;
    const int cols = n > UI_ONE_COLUMN_MAX ? 2 : 1;
    const int rows = (n + cols - 1) / cols;
    *h = (bottom - top - gap * (rows - 1)) / rows;
    if (*h > 110) *h = 110;
    *w = (CANVAS_W - 2 * M - gap * (cols - 1)) / cols;
    *x = M + (i / rows) * (*w + gap);
    *y = top + (i % rows) * (*h + gap);
}

static void back_button(uint8_t *c)
{
    canvas_frame(c, M, FOOT_Y, 200, FOOT_H - 8, 3, 1);
    canvas_text_in(c, M, FOOT_Y, 200, FOOT_H - 8, "Voltar", 1, 1);
}

static int in_back(int x, int y) { return inside(x, y, M, FOOT_Y, 200, FOOT_H - 8); }

void ui_draw_machines(uint8_t *c, const char *const *names, int n, int mark,
                      int allow_back, const char *note)
{
    canvas_clear(c, 0);
    canvas_text(c, M, 14, "Qual computador?", 1, 1);
    for (int i = 0; i < n; i++) {
        int x, y, w, h;
        machine_band(i, n, &x, &y, &w, &h);
        canvas_frame(c, x, y, w, h, i == mark ? 8 : 3, 1);
        canvas_text_in(c, x, y, w, h, names[i], n > UI_ONE_COLUMN_MAX ? 1 : 2, 1);
    }
    if (allow_back) back_button(c);
    if (note) canvas_text(c, allow_back ? M + 220 : M, FOOT_Y + 14, note, 1, 1);
}

int ui_hit_machines(int x, int y, int n, int allow_back)
{
    for (int i = 0; i < n; i++) {
        int bx, by, bw, bh;
        machine_band(i, n, &bx, &by, &bw, &bh);
        if (inside(x, y, bx, by, bw, bh)) return i;
    }
    if (allow_back && in_back(x, y)) return UI_BACK;
    return UI_NONE;
}

/* --- which entry ------------------------------------------------------ */

static void cell(int slot, int *x, int *y, int *w, int *h)
{
    const int gap = 8;
    const int col = slot % UI_COLS, row = slot / UI_COLS;
    *w = (CANVAS_W - 2 * M - gap * (UI_COLS - 1)) / UI_COLS;
    *h = (FOOT_Y - 8 - HEAD_H - gap * (UI_ROWS - 1)) / UI_ROWS;
    *x = M + col * (*w + gap);
    *y = HEAD_H + row * (*h + gap);
}

int ui_pages(int count) { return count <= 0 ? 1 : (count + UI_PER_PAGE - 1) / UI_PER_PAGE; }

/* Previous and next, at the right end of the footer. */
static const int ARROW_W = 120;
static int prev_x(void) { return CANVAS_W - M - 2 * ARROW_W - 12; }
static int next_x(void) { return CANVAS_W - M - ARROW_W; }

void ui_draw_entries(uint8_t *c, const char *title, ui_name_fn name, int count,
                     int page, int allow_back)
{
    canvas_clear(c, 0);
    canvas_text(c, M, 14, title, 1, 1);
    const int pages = ui_pages(count);
    const int first = page * UI_PER_PAGE;
    for (int s = 0; s < UI_PER_PAGE && first + s < count; s++) {
        int x, y, w, h;
        cell(s, &x, &y, &w, &h);
        canvas_frame(c, x, y, w, h, 2, 1);
        canvas_text_in(c, x, y, w, h, name(first + s), 1, 1);
    }
    if (allow_back) back_button(c);
    if (pages > 1) {
        char p[16];
        snprintf(p, sizeof p, "%d/%d", page + 1, pages);
        canvas_text(c, prev_x() - 16 - canvas_text_width(p, 1), FOOT_Y + 14, p, 1, 1);
        canvas_frame(c, prev_x(), FOOT_Y, ARROW_W, FOOT_H - 8, page > 0 ? 3 : 1, 1);
        canvas_text_in(c, prev_x(), FOOT_Y, ARROW_W, FOOT_H - 8, "<", 2, 1);
        canvas_frame(c, next_x(), FOOT_Y, ARROW_W, FOOT_H - 8, page + 1 < pages ? 3 : 1, 1);
        canvas_text_in(c, next_x(), FOOT_Y, ARROW_W, FOOT_H - 8, ">", 2, 1);
    }
}

int ui_hit_entries(int x, int y, int count, int page, int allow_back)
{
    const int first = page * UI_PER_PAGE;
    for (int s = 0; s < UI_PER_PAGE && first + s < count; s++) {
        int bx, by, bw, bh;
        cell(s, &bx, &by, &bw, &bh);
        if (inside(x, y, bx, by, bw, bh)) return first + s;
    }
    if (allow_back && in_back(x, y)) return UI_BACK;
    if (ui_pages(count) > 1) {
        if (inside(x, y, prev_x(), FOOT_Y, ARROW_W, FOOT_H - 8) && page > 0) return UI_PREV;
        if (inside(x, y, next_x(), FOOT_Y, ARROW_W, FOOT_H - 8) && page + 1 < ui_pages(count)) return UI_NEXT;
    }
    return UI_NONE;
}

/* --- a message ----------------------------------------------------------- */

void ui_draw_message(uint8_t *c, const char *line1, const char *line2)
{
    const int w = 640, h = line2 ? 150 : 100;
    const int x = (CANVAS_W - w) / 2, y = (CANVAS_H - h) / 2;
    canvas_fill(c, x, y, w, h, 0);
    canvas_frame(c, x, y, w, h, 6, 1);
    const int lh = canvas_line_height(1);
    if (line2) {
        canvas_text_in(c, x, y + 20, w, lh + 10, line1, 1, 1);
        canvas_text_in(c, x, y + 30 + lh, w, 2 * lh + 10, line2, 2, 1);
    } else {
        canvas_text_in(c, x, y, w, h, line1, 1, 1);
    }
}
