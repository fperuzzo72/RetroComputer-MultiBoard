/* screens - the Paper Mono's own screens and the 8-bit picture path, drawn
 * on the development machine.
 *
 *   screens <out-prefix>
 *
 * Writes each menu screen as it would reach the panel (turned back upright
 * to look at), and checks the 8-bit picture path against a synthetic
 * picture: every panel pixel it touches, tone by tone.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "canvas.h"
#include "ui.h"
#include "display.h"
#include "picture.h"

static uint8_t canvas[CANVAS_BYTES];

static void png(const char *path)
{
    const int w = CANVAS_W, h = CANVAS_H, stride = w / 8;
    unsigned char *raw = malloc((size_t)(stride + 1) * h);
    for (int y = 0; y < h; y++) {
        raw[y * (stride + 1)] = 0;
        for (int x = 0; x < w; x++) {
            /* upright: the canvas holds the panel's own, turned frame */
            int sx = EINK_UPSIDE_DOWN ? w - 1 - x : x, sy = EINK_UPSIDE_DOWN ? h - 1 - y : y;
            int white = (canvas[sy * stride + sx / 8] >> (7 - sx % 8)) & 1;
            unsigned char *o = &raw[y * (stride + 1) + 1 + x / 8];
            if (x % 8 == 0) *o = 0;
            if (white) *o |= 0x80 >> (x % 8);
        }
    }
    uLongf zl = compressBound((uLong)(stride + 1) * h);
    unsigned char *z = malloc(zl);
    compress(z, &zl, raw, (uLong)(stride + 1) * h);
    FILE *f = fopen(path, "wb");
    #define BE(p,v) do{(p)[0]=(v)>>24;(p)[1]=(v)>>16;(p)[2]=(v)>>8;(p)[3]=(v);}while(0)
    unsigned char sig[8] = {0x89,'P','N','G','\r','\n',0x1a,'\n'}, ih[25], b4[4];
    fwrite(sig, 1, 8, f);
    BE(ih,13); memcpy(ih+4,"IHDR",4); BE(ih+8,w); BE(ih+12,h); ih[16]=1; ih[17]=0; ih[18]=ih[19]=ih[20]=0;
    BE(ih+21,(unsigned)crc32(0,ih+4,17)); fwrite(ih,1,25,f);
    BE(b4,(unsigned)zl); fwrite(b4,1,4,f); fwrite("IDAT",1,4,f); fwrite(z,1,zl,f);
    BE(b4,(unsigned)crc32(crc32(0,(const unsigned char*)"IDAT",4),z,zl)); fwrite(b4,1,4,f);
    unsigned char ie[12]={0,0,0,0,'I','E','N','D',0xae,0x42,0x60,0x82}; fwrite(ie,1,12,f);
    fclose(f); free(raw); free(z);
    printf("  %s\n", path);
}

static const char *names[] = {
    "Nemesis", "Nemesis 2", "Nemesis 3 - The Eve of Destruction", "Knightmare", "Knightmare II - The Maze of Galious",
    "King's Valley II", "Penguin Adventure", "Parodius", "Space Manbow", "Zanac Ex", "F-1 Spirit", "Treasure of Usas",
    "Tetris", "Famicle Parodic", "Space Invaders", "Road Fighter", "Hyper Rally", "Yie Ar Kung-Fu", "Antarctic Adventure",
    "Gradius", "Salamander", "Metal Gear", "Vampire Killer", "Maze of Galious",
};
static const char *name(int i) { return i == 0 ? "MSX-BASIC" : names[(i - 1) % 24]; }

static int failures;
static void check(int ok, const char *what) { printf("%s %s\n", ok ? "ok  " : "FAIL", what); failures += !ok; }

int main(int argc, char **argv)
{
    if (argc != 2) { fprintf(stderr, "usage: %s <out-prefix>\n", argv[0]); return 1; }
    char p[512];
    const char *machines[] = { "MSX (Hotbit HB-8000)", "ZX Spectrum 48K", "Macintosh Plus", "Voltar ao CrossPlay" };

    ui_draw_machines(canvas, machines, 4, 0, 0, "Sem toque, em 5 s liga o marcado");
    snprintf(p, sizeof p, "%s-machines-4.png", argv[1]); png(p);
    ui_draw_machines(canvas, machines, 3, 0, 0, "Sem toque, em 5 s liga o marcado");
    snprintf(p, sizeof p, "%s-machines.png", argv[1]); png(p);
    /* more than four: two columns, filled down the left first */
    const char *six[] = { "MSX1 (Hotbit HB-8000)", "ZX Spectrum 48K", "Macintosh Plus", "Commodore 64",
                          "Voltar ao CrossPoint", "Voltar ao MicroBASIC" };
    ui_draw_machines(canvas, six, 6, 1, 0, "Sem toque, em 5 s liga o marcado");
    snprintf(p, sizeof p, "%s-machines-6.png", argv[1]); png(p);
    ui_draw_machines(canvas, six, 5, 0, 1, NULL);
    snprintf(p, sizeof p, "%s-machines-5.png", argv[1]); png(p);
    {
        int x, y, w, h, hits = 1;
        /* left column top, left column last, right column top */
        hits &= ui_hit_machines(CANVAS_W / 4, 100, 6, 0) == 0;
        hits &= ui_hit_machines(CANVAS_W / 4, CANVAS_H - 120, 6, 0) == 2;
        hits &= ui_hit_machines(CANVAS_W * 3 / 4, 100, 6, 0) == 3;
        (void)x; (void)y; (void)w; (void)h;
        check(hits, "two columns answer where they are drawn");
    }
    /* positions relative to the panel, so the same checks hold on both
     * boards (make EINK=PAPERS3 for the PaperS3) */
    const int W = CANVAS_W, H = CANVAS_H, FOOT = H - 50;
    check(ui_hit_machines(W / 2, 120, 3, 0) == 0 && ui_hit_machines(W / 2, 250, 3, 0) == 1 &&
          ui_hit_machines(W / 2, 370, 3, 0) == 2 && ui_hit_machines(W / 2, FOOT, 3, 0) == UI_NONE,
          "machine bands answer where they are drawn");

    ui_draw_entries(canvas, "MSX (Hotbit HB-8000)", name, 25, 0, 1);
    snprintf(p, sizeof p, "%s-entries-1.png", argv[1]); png(p);
    ui_draw_entries(canvas, "MSX (Hotbit HB-8000)", name, 25, 1, 1);
    snprintf(p, sizeof p, "%s-entries-2.png", argv[1]); png(p);
    check(ui_hit_entries(40, 90, 25, 0, 1) == 0 && ui_hit_entries(40, 90, 25, 1, 1) == UI_PER_PAGE &&
          ui_hit_entries(W - 80, FOOT, 25, 0, 1) == UI_NEXT && ui_hit_entries(W - 220, FOOT, 25, 1, 1) == UI_PREV &&
          ui_hit_entries(60, FOOT, 25, 0, 1) == UI_BACK,
          "entry cells, arrows and back answer where they are drawn");

    ui_draw_entries(canvas, "MSX (Hotbit HB-8000)", name, 25, 0, 0);
    ui_draw_message(canvas, "Teclado pedindo codigo: digite no teclado", "123456 Enter");
    snprintf(p, sizeof p, "%s-passkey.png", argv[1]); png(p);

    /* the 8-bit path: MSX-BASIC's colours, white text on a blue border */
    static const uint16_t pal[16] = {
        0x0000, 0x0000, 0x2648, 0x5ECF, 0x52BD, 0x7B3E, 0xD28A, 0x471E,
        0xFAAA, 0xFB6F, 0xD6CA, 0xE6F0, 0x2586, 0xC2F7, 0xCE59, 0xFFFF };
    display8_attach(canvas);
    display_fill_panel(0);
    uint8_t band[256 * 8];
    for (int y = 0; y < 216; y += 8) {
        for (int r = 0; r < 8; r++)
            for (int x = 0; x < 256; x++)
                band[r * 256 + x] = ((x / 8 + (y + r) / 8) % 3 == 0 && ((x ^ (y + r)) & 4)) ? 15 : 4;
        display_write_picture(0, (short)y, 256, 8, band, pal[4], pal);
    }
    snprintf(p, sizeof p, "%s-msx-colours.png", argv[1]); png(p);
    /* check: blue (the border colour) is white, white text is black */
    long bad = 0, black = 0;
    for (int y = 0; y < 216; y++)
        for (int x = 0; x < 256; x++) {
            int want_black = ((x / 8 + y / 8) % 3 == 0 && ((x ^ y) & 4));
            for (int d = 0; d < 4; d++) {
                const int X0 = ((W - 512) / 2) & ~7, Y0 = (H - 432) / 2;
                int ux = X0 + 2 * x + (d & 1), uy = Y0 + 2 * y + (d >> 1);
                int px = EINK_UPSIDE_DOWN ? W - 1 - ux : ux, py = EINK_UPSIDE_DOWN ? H - 1 - uy : uy;
                int is_black = !((canvas[py * (W / 8) + px / 8] >> (7 - px % 8)) & 1);
                bad += is_black != want_black;
                black += is_black;
            }
        }
    printf("  %ld panel pixels black, %ld wrong\n", black, bad);
    check(bad == 0 && black > 0, "MSX-BASIC's white on blue comes out black on white, 2x, turned");

    printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures != 0;
}
