/* androidhost - the Android board (src/boards/android) on the Mac: the same
 * retro_board.c, NVS-in-files and machines, in real time, with the
 * picture written out as a colour PNG instead of posted to a Surface.
 *
 *   androidhost <storage-dir> <seconds> <out.png> [text to type]
 *
 * The storage dir holds what the machines remember (the chosen machine
 * and entry, as files), as the app's own files directory does on the
 * device. Typing goes through retro_board_key(), the path the Android
 * key events take, with a US key table of its own.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>

#include "retro_board.h"

static void png(const char *path, const uint16_t *px, int w, int h)
{
    const size_t stride = (size_t)w * 3 + 1;
    unsigned char *raw = malloc(stride * h);
    for (int y = 0; y < h; y++) {
        raw[y * stride] = 0;
        for (int x = 0; x < w; x++) {
            const uint16_t c = px[y * w + x];
            unsigned char *o = raw + y * stride + 1 + x * 3;
            o[0] = (unsigned char)(((c >> 11) & 31) * 255 / 31);
            o[1] = (unsigned char)(((c >> 5) & 63) * 255 / 63);
            o[2] = (unsigned char)((c & 31) * 255 / 31);
        }
    }
    uLongf zl = compressBound((uLong)(stride * h));
    unsigned char *z = malloc(zl);
    compress(z, &zl, raw, (uLong)(stride * h));
    FILE *f = fopen(path, "wb");
#define BE(p, v) do { (p)[0] = (v) >> 24; (p)[1] = (v) >> 16; (p)[2] = (v) >> 8; (p)[3] = (v); } while (0)
    unsigned char sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'}, ih[25], b4[4];
    fwrite(sig, 1, 8, f);
    BE(ih, 13); memcpy(ih + 4, "IHDR", 4); BE(ih + 8, w); BE(ih + 12, h);
    ih[16] = 8; ih[17] = 2; ih[18] = ih[19] = ih[20] = 0;
    BE(ih + 21, (unsigned)crc32(0, ih + 4, 17)); fwrite(ih, 1, 25, f);
    BE(b4, (unsigned)zl); fwrite(b4, 1, 4, f); fwrite("IDAT", 1, 4, f); fwrite(z, 1, zl, f);
    BE(b4, (unsigned)crc32(crc32(0, (const unsigned char *)"IDAT", 4), z, zl)); fwrite(b4, 1, 4, f);
    unsigned char ie[12] = {0, 0, 0, 0, 'I', 'E', 'N', 'D', 0xae, 0x42, 0x60, 0x82};
    fwrite(ie, 1, 12, f);
    fclose(f);
    free(raw);
    free(z);
}

/* A US keyboard, written apart from any machine's table. */
static void type_char(char c)
{
    static const char plain[] = "abcdefghijklmnopqrstuvwxyz1234567890\n\x1b\b\t -=[]\\#;'`,./";
    static const char shifted[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*()\n\x1b\b\t _+{}|~:\"~<>?";
    for (int pass = 0; pass < 2; pass++) {
        const char *t = pass ? shifted : plain;
        const char *p = strchr(t, c);
        if (!p || !c) continue;
        const uint8_t usage = (uint8_t)(0x04 + (p - t));
        if (pass) retro_board_key(0xE1, 1);
        retro_board_key(usage, 1);
        usleep(60000);
        retro_board_key(usage, 0);
        if (pass) retro_board_key(0xE1, 0);
        usleep(60000);
        return;
    }
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: %s <storage-dir> <seconds> <out.png> [text]\n", argv[0]);
        return 2;
    }
    retro_board_set_storage(argv[1]);
    retro_board_start();
    const double secs = atof(argv[2]);
    usleep((useconds_t)(secs / 2 * 1e6));
    if (argc > 4) for (const char *c = argv[4]; *c; c++) type_char(*c);
    usleep((useconds_t)(secs / 2 * 1e6));
    static uint16_t px[216 * 320];
    int w, h;
    uint16_t border;
    const uint32_t seq = retro_board_picture(px, &w, &h, &border);
    png(argv[3], px, w, h);
    printf("picture %dx%d, border %04X, sequence %u: %s\n", w, h, border, seq, argv[3]);
    return 0;
}
