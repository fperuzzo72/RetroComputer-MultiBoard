/* papermono_test - the Paper Mono's touch and picture path, without the
 * Paper Mono.
 *
 * Runs the real Mac (src/mac/mac_core.c, lib/umac) behind the board's real
 * trackpad (src/boards/papermono/trackpad.c) and picture code (picture.h),
 * with a simulated finger, and writes the panel's framebuffer out as it
 * would be sent to the glass. The same idea as tools/machost, one layer
 * further out.
 *
 *   papermono_test <rom> <disc> <out-prefix>
 *
 * Checks, and says which failed:
 *   1. every panel pixel is the right Mac pixel, scaled and turned round
 *   2. a finger moving the pointer by trackpad lands it where aimed
 *   3. a double tap on the disc icon opens it
 *   4. tap-then-drag on the Apple menu holds it open
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "mac.h"
#include "umac.h"
#include "picture.h"
#include "trackpad.h"

enum { PW = 800, PH = 480 };
static uint64_t now_us;
static papermono_view view;
static trackpad tp;
static uint8_t panel[PW / 8 * PH];

/* 10ms: one board-task tick. The Mac runs its 5ms slices meanwhile. */
static void tick(int touching, int x, int y)
{
    now_us += 10000;
    mac_step(now_us - 5000);
    mac_step(now_us);
    trackpad_update(&tp, (unsigned long)(now_us / 1000), touching, x, y);
    int px, py, b;
    trackpad_pointer(&tp, &px, &py, &b);
    mac_pointer(px, py, b);
}

static void idle(int ms) { for (int i = 0; i < ms / 10; i++) tick(0, 0, 0); }

/* A finger dragged from (x0,y0) to (x1,y1), upright panel pixels. */
static void slide(int x0, int y0, int x1, int y1, int ms)
{
    int n = ms / 10;
    for (int i = 0; i <= n; i++)
        tick(1, x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n);
    tick(0, 0, 0);
}

/* Push the pointer to a Mac pixel the way a person would: look, push,
 * look again. Returns how many pushes it took, or -1. */
static int aim(int tx, int ty)
{
    for (int tries = 1; tries <= 12; tries++) {
        int x, y, b;
        trackpad_pointer(&tp, &x, &y, &b);
        int ex = tx - x, ey = ty - y;
        /* within 3 Mac pixels: the smallest nudge this trackpad makes is
         * the catch-up of its slop, several pixels, so exact pixels are
         * not always reachable; a close box is 11 wide */
        if (abs(ex) <= 3 && abs(ey) <= 3) return tries - 1;
        /* push slowly, so acceleration stays near its low end */
        int fx = (int)(ex * view.dw / (float)view.w / 0.8f);
        int fy = (int)(ey * view.dh / (float)view.h / 0.8f);
        int steps = (abs(fx) > abs(fy) ? abs(fx) : abs(fy)) / 3 + 1;
        /* a small correction is a slow roll of the finger, not a flick:
         * quicker than TAP_MAX_MS and inside the slop it is a tap */
        if (steps < 30) steps = 30;
        slide(400, 240, 400 + fx, 240 + fy, steps * 10);
        idle(300);
    }
    return -1;
}

static void tap(void) { tick(1, 400, 240); tick(1, 400, 240); tick(0, 0, 0); }

static void save_png(const char *path, const uint8_t *bits, int w, int h, int upright)
{
    int stride = w / 8;
    uLongf rawlen = (uLongf)(stride + 1) * h, zlen = compressBound(rawlen);
    unsigned char *raw = malloc(rawlen), *z = malloc(zlen);
    for (int y = 0; y < h; y++) {
        raw[y * (stride + 1)] = 0;
        for (int x = 0; x < w; x++) {
            int sx = upright ? w - 1 - x : x, sy = upright ? h - 1 - y : y;
            int white = (bits[sy * stride + sx / 8] >> (7 - sx % 8)) & 1;
            unsigned char *o = &raw[y * (stride + 1) + 1 + x / 8];
            if (x % 8 == 0) *o = 0;
            if (white) *o |= 0x80 >> (x % 8);
        }
    }
    compress(z, &zlen, raw, rawlen);
    FILE *f = fopen(path, "wb");
    unsigned char hdr[] = {0x89,'P','N','G','\r','\n',0x1a,'\n'};
    fwrite(hdr, 1, 8, f);
    #define BE32(p,v) do{(p)[0]=(v)>>24;(p)[1]=(v)>>16;(p)[2]=(v)>>8;(p)[3]=(v);}while(0)
    unsigned char ihdr[25]; BE32(ihdr,13); memcpy(ihdr+4,"IHDR",4); BE32(ihdr+8,w); BE32(ihdr+12,h);
    ihdr[16]=1; ihdr[17]=0; ihdr[18]=ihdr[19]=ihdr[20]=0; BE32(ihdr+21,(unsigned)crc32(0,ihdr+4,17));
    fwrite(ihdr,1,25,f);
    unsigned char b4[4]; BE32(b4,(unsigned)zlen); fwrite(b4,1,4,f); fwrite("IDAT",1,4,f); fwrite(z,1,zlen,f);
    unsigned long c=crc32(crc32(0,(const unsigned char*)"IDAT",4),z,zlen); BE32(b4,(unsigned)c); fwrite(b4,1,4,f);
    unsigned char iend[12]={0,0,0,0,'I','E','N','D',0xae,0x42,0x60,0x82}; fwrite(iend,1,12,f);
    fclose(f); free(raw); free(z);
}

static unsigned char *slurp(const char *p, size_t *n)
{
    FILE *f = fopen(p, "rb"); if (!f) { perror(p); exit(1); }
    fseek(f, 0, SEEK_END); *n = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc(*n); if (fread(b, 1, *n, f) != *n) exit(1); fclose(f); return b;
}

static void snapshot(const char *prefix, const char *name)
{
    char path[512];
    papermono_draw(&view, panel, mac_framebuffer());
    snprintf(path, sizeof path, "%s-%s.png", prefix, name);
    save_png(path, panel, PW, PH, 1);   /* turned back upright to look at */
    printf("  %s\n", path);
}

/* Share of black pixels in a rectangle of the Mac's screen. The desktop
 * pattern is half black; the inside of a window or a menu is white. */
static float blackness(int x0, int y0, int x1, int y1)
{
    const uint8_t *fb = mac_framebuffer();
    int black = 0, n = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++, n++)
            black += (fb[y * (DISP_WIDTH / 8) + x / 8] >> (7 - x % 8)) & 1;
    return (float)black / n;
}

/* Pixels that differ between a saved screen and the current one, in a
 * rectangle of the Mac's screen. */
static uint8_t saved[DISP_WIDTH / 8 * DISP_HEIGHT];
static void save_screen(void);
/* The cursor, a 16x16 sprite, where it was when the screen was saved and
 * where it is now: moving it is not the screen changing. */
static int saved_cx, saved_cy;
static int in_cursor(int x, int y, int cx, int cy)
{
    return x >= cx - 16 && x < cx + 16 && y >= cy - 16 && y < cy + 16;
}
static void save_screen(void)
{
    memcpy(saved, mac_framebuffer(), sizeof saved);
    mac_cursor(&saved_cx, &saved_cy);
}
static int changed(int x0, int y0, int x1, int y1)
{
    const uint8_t *fb = mac_framebuffer();
    int n = 0, cx, cy;
    mac_cursor(&cx, &cy);
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            if (in_cursor(x, y, cx, cy) || in_cursor(x, y, saved_cx, saved_cy)) continue;
            int i = y * (DISP_WIDTH / 8) + x / 8, m = 0x80 >> (x % 8);
            n += (fb[i] & m) != (saved[i] & m);
        }
    return n;
}

static int failures;
static void check(int ok, const char *what) { printf("%s %s\n", ok ? "ok  " : "FAIL", what); failures += !ok; }

int main(int argc, char **argv)
{
    if (argc != 4) { fprintf(stderr, "usage: %s <rom> <disc> <out-prefix>\n", argv[0]); return 1; }
    size_t rl, dl;
    unsigned char *rom = slurp(argv[1], &rl), *disc = slurp(argv[2], &dl);
    if (mac_start(rom, rl, disc, dl)) return 1;
    view = papermono_view_make(PW, PH, DISP_WIDTH, DISP_HEIGHT);
    trackpad_init(&tp, DISP_WIDTH, DISP_HEIGHT, (float)view.dh / DISP_HEIGHT);
    printf("picture %dx%d drawn %dx%d at %d,%d (%.2fx)\n", view.w, view.h, view.dw, view.dh,
           view.x0, view.y0, (float)view.dh / view.h);

    idle(32000);   /* boot */

    /* 1. every panel pixel against the Mac pixel it should show */
    papermono_draw(&view, panel, mac_framebuffer());
    const uint8_t *fb = mac_framebuffer();
    long wrong = 0;
    for (int py = 0; py < PH; py++)
        for (int px = 0; px < PW; px++) {
            int ux = PW - 1 - px, uy = PH - 1 - py;   /* upright point shown at this panel pixel */
            int want_white = 1;
            if (ux >= view.x0 && ux < view.x0 + view.dw && uy >= view.y0 && uy < view.y0 + view.dh) {
                int mx = (ux - view.x0) * view.w / view.dw, my = (uy - view.y0) * view.h / view.dh;
                want_white = !((fb[my * (view.w / 8) + mx / 8] >> (7 - mx % 8)) & 1);
            }
            int is_white = (panel[py * (PW / 8) + px / 8] >> (7 - px % 8)) & 1;
            wrong += is_white != want_white;
        }
    check(wrong == 0, "every panel pixel shows the right Mac pixel, scaled and turned");

    /* touch mapping back: a panel point to the Mac pixel under it */
    int bad = 0;
    for (int my = 0; my < view.h; my += 7)
        for (int mx = 0; mx < view.w; mx += 7) {
            /* the first panel pixel that nearest-pixel scaling gives to
             * this Mac pixel */
            int ux = view.x0 + (mx * view.dw + view.w - 1) / view.w;
            int uy = view.y0 + (my * view.dh + view.h - 1) / view.h;
            int x = PW - 1 - ux, y = PH - 1 - uy;   /* as the panel reports it */
            papermono_touch_upright(&view, &x, &y);
            papermono_upright_to_picture(&view, &x, &y);
            if ((x != mx || y != my) && bad++ < 3) printf("  mac %d,%d drawn at upright %d,%d maps back to %d,%d\n", mx, my, ux, uy, x, y);
        }
    check(bad == 0, "a touch on a panel point maps back to the Mac pixel drawn there");
    snapshot(argv[3], "1-finder");


    /* 2. aim the pointer at the disc icon */
    int pushes = aim(471, 42);
    int cx, cy;
    idle(200);
    mac_cursor(&cx, &cy);
    printf("  aimed at 471,42 in %d pushes; the Mac's cursor is at %d,%d\n", pushes, cx, cy);
    check(pushes >= 0 && abs(cx - 471) <= 3 && abs(cy - 42) <= 3, "trackpad pushes land the pointer within 3 pixels of the aim");

    /* 3. double tap opens it */
    tap(); idle(60); tap(); idle(3000);
    snapshot(argv[3], "2-double-tap");
    printf("  inside where the window opens: %.0f%% black\n", 100 * blackness(30, 70, 300, 110));
    check(blackness(30, 70, 300, 110) < 0.2f, "double tap on the disc opens its window");

    /* 4. tap, then hold and drag down the Apple menu */
    aim(22, 9);
    idle(300);
    save_screen();
    tick(1, 400, 240); tick(0, 0, 0);          /* tap */
    idle(60);
    for (int i = 0; i < 40; i++) tick(1, 400, 240);          /* second contact held: button down */
    for (int i = 0; i <= 20; i++) tick(1, 400, 240 + i * 3); /* drag down onto the menu */
    idle(0);
    int x, y, b; trackpad_pointer(&tp, &x, &y, &b);
    for (int i = 0; i < 30; i++) tick(1, 400, 300);           /* keep holding while it draws */
    snapshot(argv[3], "3-menu-held");
    int menu = changed(10, 20, 150, 85);
    printf("  Apple menu area: %d pixels changed while held, button %s\n", menu, b ? "down" : "up");
    check(b && menu > 2000, "tap-then-drag holds the button and the Apple menu stays open");
    tick(0, 0, 0); idle(500);
    int after = changed(10, 20, 150, 85);
    printf("  after lifting: %d pixels still changed\n", after);
    check(!tp.button && after < 50, "lifting lets the button go and the menu closes");


    printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures != 0;
}
