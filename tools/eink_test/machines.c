/* machines - the MSX and the Spectrum on the development machine, drawn
 * through the Paper Mono's picture path.
 *
 * The real machine code (src/spectrum/, src/msx/ with the vendored cores)
 * and the real display8.c, with tools/hostshim standing in for the few
 * ESP-IDF headers they include. The machine runs on its own thread as it
 * does on the device; time is simulated, so it runs as fast as the host
 * can. At the times asked for, what the panel would show is written out
 * as a PNG, turned upright.
 *
 *   machines <entry> <out-prefix> <seconds>[,<seconds>...] [<keys>]
 *
 * <entry> is the machine's own entry index: 0 is BASIC, the rest are its
 * built-in cartridges or snapshots, as on the boot menu. <keys> is typed
 * after the first capture, through the machine's own console typing.
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>

#include "hostshim.h"
#include "machine.h"
#include "display.h"
#include "picture.h"

volatile int64_t hostshim_now_us;

/* selector.h: nothing opens it here */
int  selector_active(void) { return 0; }
void selector_open(void) {}
void selector_poll_open(void) {}
int  selector_frame(void) { return -1; }

/* ble_keyboard.h, for the MSX's glue */
void ble_keyboard_poll(void) {}
int  ble_keyboard_connected(void) { return 0; }

static uint8_t canvas[DISPLAY_PANEL_W / 8 * DISPLAY_PANEL_H];

static void png(const char *path)
{
    const int w = DISPLAY_PANEL_W, h = DISPLAY_PANEL_H, stride = w / 8;
    unsigned char *raw = malloc((size_t)(stride + 1) * h);
    for (int y = 0; y < h; y++) {
        raw[y * (stride + 1)] = 0;
        for (int x = 0; x < w; x++) {
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
}

static void *run(void *arg)
{
    (void)arg;
    machine->run();
    return NULL;
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: %s <entry> <out-prefix> <seconds>[,<seconds>...] [<keys>]\n", argv[0]);
        return 1;
    }
    machine = machine_list[0];
    machine->select_entry(atoi(argv[1]));
    printf("%s, entry %d: %s\n", machine->name, atoi(argv[1]), machine->entry_name(atoi(argv[1])));

    display8_attach(canvas);
    memset(canvas, 0xFF, sizeof canvas);
    machine->prealloc_video();

    pthread_t th;
    pthread_create(&th, NULL, run, NULL);

    char *times = strdup(argv[3]);
    int shot = 0;
    for (char *t = strtok(times, ","); t; t = strtok(NULL, ","), shot++) {
        const int64_t at = (int64_t)(atof(t) * 1e6);
        while (hostshim_now_us < at) usleep(2000);
        char path[512];
        snprintf(path, sizeof path, "%s-%s.png", argv[2], t);
        png(path);
        printf("  %.1fs, %lu frames: %s\n", hostshim_now_us / 1e6, machine->frames(), path);
        /* SWITCH=k:e changes to entry e after shot k, as the selector does. */
        int k, e;
        if (getenv("SWITCH") && sscanf(getenv("SWITCH"), "%d:%d", &k, &e) == 2 && k == shot) {
            printf("  switching to entry %d: %s\n", e, machine->entry_name(e));
            machine->select_entry(e);
            machine->switch_to(e);
        }
        if (shot == 0 && argc > 4 && machine->type) {
            printf("  typing \"%s\"\n", argv[4]);
            machine->type(argv[4]);
        }
    }
    return 0;
}
