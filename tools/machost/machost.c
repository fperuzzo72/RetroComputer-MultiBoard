/* machost - the Macintosh without a board.
 *
 * Builds src/mac/mac_core.c and lib/umac exactly as the firmware does and
 * runs them here, against a real ROM and a real disc, on emulated time, so
 * a boot or a click can be checked in seconds with no device plugged in.
 * The same idea as tools/tapebench for the Spectrum.
 *
 *   machost <rom> <disc> <script>
 *
 * The script is commands separated by ';':
 *
 *   run S          run S seconds of emulated time
 *   move X Y       put the pointer at X,Y (screen pixels), as a touch would
 *   down / up      the button
 *   click X Y      move, down, run 0.2, up, run 0.2
 *   key U          press and release HID usage U (hex), e.g. key 04 is A
 *   cmd U          the same with Command held
 *   mod M U        the same with HID modifier byte M (hex) held: 06 is
 *                  Shift-Option
 *   text STRING    type STRING (to the end of the command) on a US
 *                  keyboard, shift and all: text voc^e -> vocÃª with the
 *                  US-International dead keys
 *   shot FILE      the screen as a PNG
 *   ram FILE       the Mac's RAM, raw, to find what an application holds
 *   cursor         print where the Mac says the cursor is
 *
 *   machost macplus.rom system608.img "run 25; shot boot.png"
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "mac.h"
#include "umac.h"

static uint64_t now_us;

static void run_for(double seconds)
{
    /* Each step is 5ms of emulated time (UMAC_EXECLOOP_QUANTUM), so the
     * clock the interrupts follow is advanced by the same amount. */
    long steps = (long)(seconds * 200.0);
    for (long i = 0; i < steps; i++) {
        now_us += 5000;
        mac_step(now_us);
    }
}

static unsigned char *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(1); }
    fseek(f, 0, SEEK_END);
    *len = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *p = malloc(*len);
    if (fread(p, 1, *len, f) != *len) { perror(path); exit(1); }
    fclose(f);
    return p;
}

static void put32(unsigned char *p, unsigned v)
{
    p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}

static void chunk(FILE *f, const char *type, const unsigned char *data, unsigned len)
{
    unsigned char hdr[8];
    put32(hdr, len);
    memcpy(hdr + 4, type, 4);
    fwrite(hdr, 1, 8, f);
    if (len) fwrite(data, 1, len, f);
    unsigned long crc = crc32(0, (const unsigned char *)type, 4);
    if (len) crc = crc32(crc, data, len);
    unsigned char c[4];
    put32(c, (unsigned)crc);
    fwrite(c, 1, 4, f);
}

/* 1-bit greyscale PNG, 0 = black: the Mac's bits inverted. */
static void shot(const char *path)
{
    const int w = DISP_WIDTH, h = DISP_HEIGHT, stride = w / 8;
    const uint8_t *fb = mac_framebuffer();
    size_t rawlen = (size_t)(stride + 1) * h;
    unsigned char *raw = malloc(rawlen);
    for (int y = 0; y < h; y++) {
        raw[y * (stride + 1)] = 0;
        for (int x = 0; x < stride; x++)
            raw[y * (stride + 1) + 1 + x] = (unsigned char)~fb[y * stride + x];
    }
    uLongf zlen = compressBound(rawlen);
    unsigned char *z = malloc(zlen);
    compress(z, &zlen, raw, rawlen);

    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    unsigned char ihdr[13];
    put32(ihdr, w); put32(ihdr + 4, h);
    ihdr[8] = 1; ihdr[9] = 0; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, (unsigned)zlen);
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw);
    free(z);
    printf("shot: %s at %.2fs\n", path, now_us / 1e6);
}

static int px, py, pb;

/* An ASCII character as a US keyboard types it: usage, and shift. */
static int us_key(char ch, unsigned *usage, int *shift)
{
    static const char plain[] = "abcdefghijklmnopqrstuvwxyz1234567890\n\x1b\b\t -=[]\\#;'`,./";
    static const char shifted[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*()\n\x1b\b\t _+{}|#:\"~<>?";
    const char *p;
    if ((p = strchr(plain, ch)) && ch) { *usage = 0x04 + (unsigned)(p - plain); *shift = 0; }
    else if ((p = strchr(shifted, ch)) && ch) { *usage = 0x04 + (unsigned)(p - shifted); *shift = 1; }
    else return 0;
    return 1;
}

static void type_text(const char *s)
{
    for (; *s; s++) {
        unsigned u;
        int sh;
        if (!us_key(*s, &u, &sh)) continue;
        uint8_t r[8] = {0};
        r[0] = sh ? 0x02 : 0;
        if (sh) { mac_hid_report(r); run_for(0.03); }
        r[2] = (uint8_t)u;
        mac_hid_report(r);
        run_for(0.06);
        r[2] = 0;
        mac_hid_report(r);
        run_for(0.03);
        if (sh) { r[0] = 0; mac_hid_report(r); run_for(0.03); }
    }
    run_for(0.3);
}

static void key(unsigned usage, unsigned mods)
{
    uint8_t r[8] = {0};
    if (mods) { r[0] = (uint8_t)mods; mac_hid_report(r); run_for(0.05); }
    r[2] = (uint8_t)usage;
    mac_hid_report(r);
    run_for(0.1);
    r[2] = 0;
    mac_hid_report(r);
    run_for(0.05);
    if (mods) { r[0] = 0; mac_hid_report(r); run_for(0.05); }
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "usage: %s <rom> <disc> <script>\n", argv[0]);
        return 1;
    }
    size_t rom_len, disc_len;
    unsigned char *rom = slurp(argv[1], &rom_len);
    unsigned char *disc = slurp(argv[2], &disc_len);
    if (mac_start(rom, rom_len, disc, disc_len)) return 1;

    char *script = strdup(argv[3]);
    for (char *cmd = strtok(script, ";"); cmd; cmd = strtok(NULL, ";")) {
        char arg[256];
        double a = 0, b = 0;
        unsigned u, m;
        while (*cmd == ' ') cmd++;
        if (!*cmd) continue;
        if (sscanf(cmd, "run %lf", &a) == 1) run_for(a);
        else if (sscanf(cmd, "move %lf %lf", &a, &b) == 2) {
            px = (int)a; py = (int)b; mac_pointer(px, py, pb);
        }
        else if (sscanf(cmd, "click %lf %lf", &a, &b) == 2) {
            px = (int)a; py = (int)b;
            mac_pointer(px, py, 0); run_for(0.1);
            pb = 1; mac_pointer(px, py, pb); run_for(0.2);
            pb = 0; mac_pointer(px, py, pb); run_for(0.2);
        }
        else if (!strcmp(cmd, "down")) { pb = 1; mac_pointer(px, py, pb); }
        else if (!strcmp(cmd, "up")) { pb = 0; mac_pointer(px, py, pb); }
        else if (sscanf(cmd, "key %x", &u) == 1) key(u, 0);
        else if (sscanf(cmd, "cmd %x", &u) == 1) key(u, 0x08);
        else if (sscanf(cmd, "mod %x %x", &m, &u) == 2) key(u, m);
        else if (!strncmp(cmd, "text ", 5)) type_text(cmd + 5);
        else if (sscanf(cmd, "shot %255s", arg) == 1) shot(arg);
        else if (sscanf(cmd, "ram %255s", arg) == 1) {
            FILE *f = fopen(arg, "wb");
            if (f) { fwrite(mac_ram(), 1, mac_ram_size(), f); fclose(f); }
        }
        else if (!strcmp(cmd, "cursor")) {
            int x, y;
            mac_cursor(&x, &y);
            printf("cursor: %d,%d (asked for %d,%d)\n", x, y, px, py);
        }
        else { fprintf(stderr, "machost: what is '%s'?\n", cmd); return 1; }
    }
    return 0;
}
