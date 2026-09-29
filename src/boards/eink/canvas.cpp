/* canvas.cpp - drawing on the panel buffer. See canvas.h. */
#include "canvas.h"
#include "picture.h"

#include <string.h>

#include <FreeInkUIFont.h>

using freeink::ui::FontGlyph;
using freeink::ui::kNotoSansFont;

void canvas_clear(uint8_t *c, int black)
{
    memset(c, black ? 0x00 : 0xFF, CANVAS_BYTES);
}

void canvas_px(uint8_t *c, int x, int y, int black)
{
    if (x < 0 || y < 0 || x >= CANVAS_W || y >= CANVAS_H) return;
#if EINK_UPSIDE_DOWN
    x = CANVAS_W - 1 - x;
    y = CANVAS_H - 1 - y;
#endif
    uint8_t *b = c + y * (CANVAS_W / 8) + (x >> 3);
    const uint8_t m = (uint8_t)(0x80 >> (x & 7));
    if (black) *b &= (uint8_t)~m;
    else *b |= m;
}

void canvas_fill(uint8_t *c, int x, int y, int w, int h, int black)
{
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++) canvas_px(c, i, j, black);
}

void canvas_frame(uint8_t *c, int x, int y, int w, int h, int t, int black)
{
    canvas_fill(c, x, y, w, t, black);
    canvas_fill(c, x, y + h - t, w, t, black);
    canvas_fill(c, x, y, t, h, black);
    canvas_fill(c, x + w - t, y, t, h, black);
}

static const FontGlyph *glyph(char ch)
{
    unsigned u = (unsigned char)ch;
    if (u < kNotoSansFont.first || u > kNotoSansFont.last) u = '?';
    return &kNotoSansFont.glyphs[u - kNotoSansFont.first];
}

int canvas_line_height(int scale) { return kNotoSansFont.yAdvance * scale; }

int canvas_text_width(const char *s, int scale)
{
    int w = 0;
    for (; *s; s++) w += glyph(*s)->xAdvance * scale;
    return w;
}

/* Glyph bits are one stream per glyph, MSB first, not padded at the end of
 * each row (Adafruit-GFX layout). Reading them as byte-aligned rows is the
 * EpdFont bug that garbled every font whose width was not a multiple of 8. */
static void draw_glyph(uint8_t *c, int penx, int top, const FontGlyph *g, int scale, int black)
{
    const uint8_t *bits = kNotoSansFont.bitmap + g->bitmapOffset;
    const int gx = penx + g->xOffset * scale;
    const int gy = top + (kNotoSansFont.ascent + g->yOffset) * scale;
    unsigned n = 0;
    for (int y = 0; y < g->height; y++)
        for (int x = 0; x < g->width; x++, n++) {
            if (!(bits[n >> 3] & (0x80 >> (n & 7)))) continue;
            if (scale == 1) canvas_px(c, gx + x, gy + y, black);
            else canvas_fill(c, gx + x * scale, gy + y * scale, scale, scale, black);
        }
}

int canvas_text(uint8_t *c, int x, int y, const char *s, int scale, int black)
{
    int pen = x;
    for (; *s; s++) {
        const FontGlyph *g = glyph(*s);
        draw_glyph(c, pen, y, g, scale, black);
        pen += g->xAdvance * scale;
    }
    return pen - x;
}

void canvas_text_in(uint8_t *c, int x, int y, int w, int h, const char *s, int scale, int black)
{
    char buf[64];
    strncpy(buf, s, sizeof buf - 1);
    buf[sizeof buf - 1] = 0;
    const int room = w - 12;
    if (canvas_text_width(buf, scale) > room) {
        size_t n = strlen(buf);
        while (n > 1) {
            buf[--n] = 0;
            char tmp[68];
            strcpy(tmp, buf);
            strcat(tmp, "..");
            if (canvas_text_width(tmp, scale) <= room) {
                strcpy(buf, tmp);
                break;
            }
        }
    }
    const int tw = canvas_text_width(buf, scale);
    const int th = canvas_line_height(scale);
    canvas_text(c, x + (w - tw) / 2, y + (h - th) / 2, buf, scale, black);
}
