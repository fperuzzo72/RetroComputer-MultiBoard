/* pc_text.c - the PC's text modes on a 1-bit panel. Plain C, no board.
 *
 * Each cell is drawn with the VGA's 8x16 font (lib/pc8086's font8x16).
 * Colour becomes black and white by contrast, the way display8 does it
 * for the 8-bit machines: of a cell's two colours, the brighter is paper.
 * So DOS's light grey on black, and EDIT's white on blue, come out as
 * black text on white, and a highlighted menu item (black on grey) as
 * white on black, which is what made it stand out in the first place.
 *
 * The font is code page 437 from the owner's own EGA.CPI when the build
 * has it (HAVE_PC_FONT, tools/make_pc_font.py); lib/pc8086's own has only
 * 0x00-0x7F and a few line pieces, the rest blank.
 *
 * The cursor is a steady underline: a blinking one would keep an e-ink
 * panel refreshing for nothing. Only cells that changed are redrawn. */
#include "pc_text.h"

#include <string.h>

#include "pc8086.h"
#include "pc_core.h"

/* Luma of the 16 CGA colours, 0-255 */
static const uint8_t luma[16] = {
    0, 20, 100, 120, 50, 70, 90, 170,
    85, 105, 185, 205, 135, 155, 240, 255,
};

#ifdef HAVE_PC_FONT
extern const uint8_t pc_font_cp437[256 * 16];
static const uint8_t *glyph(uint8_t c) { return pc_font_cp437 + c * 16; }
#else
static const uint8_t *glyph(uint8_t c) { return font8x16_get_glyph(c); }
#endif

static uint8_t shown[80 * 25 * 2];
static uint8_t shown_cols, shown_cursor_row = 0xFF, shown_cursor_col;
static bool valid;

void pc_text_invalidate(void) { valid = false; }

static void draw_cell(uint8_t *fb, int cols, int row, int col, uint8_t ch, uint8_t attr, bool cursor)
{
    const uint8_t fg = attr & 15, bg = (attr >> 4) & 15;
    const bool inverse = luma[fg] < luma[bg];
    const bool blank = fg == bg;
    const uint8_t *g = glyph(ch);
    const int wide = cols == 40;   /* 40 columns: each cell twice as wide */
    for (int y = 0; y < 16; y++) {
        uint8_t bits = blank ? 0 : g[y];
        if (cursor && y >= 14) bits = 0xFF;
        if (inverse) bits = (uint8_t)~bits;
        uint8_t *p = fb + (row * 16 + y) * (PC_FB_W / 8);
        if (!wide) {
            p[col] = bits;
        } else {
            /* each bit twice */
            uint16_t w = 0;
            for (int b = 0; b < 8; b++)
                if (bits & (0x80 >> b)) w |= (uint16_t)(0xC000 >> (2 * b));
            p[col * 2] = (uint8_t)(w >> 8);
            p[col * 2 + 1] = (uint8_t)w;
        }
    }
}

bool pc_text_render(uint8_t *fb)
{
    pc_screen s;
    pc_core_screen(&s);
    if (s.graphics || !s.cells) {
        /* graphics modes are not drawn yet: say so once, plainly */
        if (valid && shown_cols == 0) return false;
        memset(fb, 0, PC_FB_W / 8 * PC_FB_H);
        static const char msg[] = "graphics mode: not drawn yet";
        for (int i = 0; msg[i]; i++) draw_cell(fb, 80, 12, 26 + i, (uint8_t)msg[i], 0x07, false);
        memset(shown, 0, sizeof shown);
        shown_cols = 0;
        valid = true;
        return true;
    }
    const int cols = s.cols == 40 ? 40 : 80;
    const int n = cols * 25 * 2;
    if (!valid || shown_cols != cols) {
        memset(shown, 0xFF, sizeof shown);   /* nothing matches: all redrawn */
        shown_cols = (uint8_t)cols;
        shown_cursor_row = 0xFF;
        valid = true;
    }
    const int cr = s.cursor_visible ? s.cursor_row : -1, cc = s.cursor_col;
    bool changed = false;
    for (int i = 0; i < n; i += 2) {
        const int row = i / 2 / cols, col = i / 2 % cols;
        const bool cur = row == cr && col == cc;
        const bool was_cur = row == shown_cursor_row && col == shown_cursor_col;
        if (shown[i] == s.cells[i] && shown[i + 1] == s.cells[i + 1] && cur == was_cur) continue;
        draw_cell(fb, cols, row, col, s.cells[i], s.cells[i + 1], cur);
        shown[i] = s.cells[i];
        shown[i + 1] = s.cells[i + 1];
        changed = true;
    }
    shown_cursor_row = (uint8_t)cr;
    shown_cursor_col = (uint8_t)cc;
    return changed;
}
