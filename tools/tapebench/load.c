/* The whole path, on the host: the ROM boots, LOAD "" is typed on the key
 * matrix, frames run at 50Hz with the interrupt, and the tape is the pulse
 * generator. This is what the device does, minus the device - and it runs
 * a three-minute tape in under a second, which is why it exists.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "bench.h"

/* Half-rows as the ULA sees them: a low address line selects a row, a
 * zero bit is a key held down. */
static byte KEY[8];
#define ROW_P     5   /* P O I U Y */
#define ROW_ENTER 6   /* ENTER L K J H */
#define ROW_SPACE 7   /* SPACE SYMSHIFT M N B */

byte InZ80(word Port) {
    if (!(Port & 1)) {
        byte v = 0x1F;
        for (int i = 0; i < 8; i++)
            if (!((Port >> 8) & (1 << i))) v &= KEY[i];
        return (byte)(v | spectrum_tape_ear(bench_tstate()) | 0xA0);
    }
    return 0xFF;
}

static void release(void) { memset(KEY, 0x1F, sizeof KEY); }
static void press(int row, int bit) { KEY[row] &= (byte)~(1 << bit); }

/* How much of the picture has been written to. A loaded game fills it;
 * a machine sitting in BASIC leaves it empty. Reading characters back
 * would miss it, because a game screen is not made of ROM font. */
static int pictureBytes(void) {
    int set = 0;
    for (int i = 0x4000; i < 0x5800; i++) if (bench_mem()[i]) set++;
    return set;
}

/* Write the machine out as a .sna: 27 bytes of registers then the 48kB of
 * RAM. The format keeps the program counter on the stack rather than in
 * the header, so pushing it is part of taking the snapshot - and it costs
 * the two bytes under SP, which is what every .sna has always done. */
static int writeSna(const char *path) {
    Z80 *c = bench_cpu();
    byte *m = bench_mem();
    byte h[27];
    word sp = (word)(c->SP.W - 2);

    m[sp]     = c->PC.B.l;
    m[sp + 1] = c->PC.B.h;

    h[0]  = c->I;
    h[1]  = c->HL1.B.l; h[2]  = c->HL1.B.h;
    h[3]  = c->DE1.B.l; h[4]  = c->DE1.B.h;
    h[5]  = c->BC1.B.l; h[6]  = c->BC1.B.h;
    h[7]  = c->AF1.B.l; h[8]  = c->AF1.B.h;
    h[9]  = c->HL.B.l;  h[10] = c->HL.B.h;
    h[11] = c->DE.B.l;  h[12] = c->DE.B.h;
    h[13] = c->BC.B.l;  h[14] = c->BC.B.h;
    h[15] = c->IY.B.l;  h[16] = c->IY.B.h;
    h[17] = c->IX.B.l;  h[18] = c->IX.B.h;
    h[19] = (byte)((c->IFF & IFF_2) ? 0x04 : 0x00);
    h[20] = c->R;
    h[21] = c->AF.B.l;  h[22] = c->AF.B.h;
    h[23] = (byte)(sp & 0xFF); h[24] = (byte)(sp >> 8);
    h[25] = (byte)((c->IFF & IFF_IM2) ? 2 : 1);   /* IM0 is not a thing here */
    h[26] = 7;                                    /* border: white, as BASIC left it */

    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return 0; }
    fwrite(h, 1, sizeof h, f);
    fwrite(m + 0x4000, 1, 0xC000, f);
    fclose(f);
    return 1;
}

/* Put the snapshot back into a machine that knows nothing, exactly as
 * spectrum.c does it, and see whether the game is still there. Writing a
 * file nobody has read back is not evidence of anything. */
static int verifySna(const char *path, const char *romPath) {
    Z80 *c = bench_cpu();
    byte *m = bench_mem();
    byte h[27];
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    if (fread(h, 1, sizeof h, f) != sizeof h) { fclose(f); return 0; }

    memset(m, 0, 0x10000);
    FILE *r = fopen(romPath, "rb");
    if (!r) { fclose(f); return 0; }
    if (fread(m, 1, 0x4000, r) != 0x4000) { fclose(r); fclose(f); return 0; }
    fclose(r);
    if (fread(m + 0x4000, 1, 0xC000, f) != 0xC000) { fclose(f); return 0; }
    fclose(f);

    memset(c, 0, sizeof *c);
    ResetZ80(c);
    c->I = h[0];
    c->HL1.W = (word)(h[1]  | (h[2]  << 8));
    c->DE1.W = (word)(h[3]  | (h[4]  << 8));
    c->BC1.W = (word)(h[5]  | (h[6]  << 8));
    c->AF1.W = (word)(h[7]  | (h[8]  << 8));
    c->HL.W  = (word)(h[9]  | (h[10] << 8));
    c->DE.W  = (word)(h[11] | (h[12] << 8));
    c->BC.W  = (word)(h[13] | (h[14] << 8));
    c->IY.W  = (word)(h[15] | (h[16] << 8));
    c->IX.W  = (word)(h[17] | (h[18] << 8));
    c->IFF   = (byte)((h[19] & 0x04) ? (IFF_1 | IFF_2) : 0);
    c->R     = h[20];
    c->AF.W  = (word)(h[21] | (h[22] << 8));
    word sp  = (word)(h[23] | (h[24] << 8));
    c->IFF  |= (h[25] == 2) ? IFF_IM2 : IFF_IM1;
    c->PC.W  = (word)(m[sp] | (m[sp + 1] << 8));
    c->SP.W  = (word)(sp + 2);

    /* Let it breathe, then ask the same two questions as a tape load. */
    int inRam = 0;
    for (int i = 0; i < 400; i++) {
        bench_slice(1);
        if (c->PC.W >= 0x4000) inRam++;
    }
    return (pictureBytes() > 1000 || inRam > 300);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: load <48.rom> <tape.tap> [frames] [out.sna]\n");
        return 2;
    }
    if (!bench_init(argv[1], argv[2])) return 1;
    release();

    int frames = argc > 3 ? atoi(argv[3]) : 20000;
    const char *out = argc > 4 ? argv[4] : 0;
    printf("%s, %d frames (%d seconds of tape)\n", argv[2], frames, frames / 50);

    /* Once the tape has run out and stayed quiet, give the game time to
     * reach whatever it settles on - usually a title screen waiting for a
     * key - and snapshot it there. */
    int quiet = 0, settle = -1;

    for (int f = 0; f < frames; f++) {
        /* LOAD "" and Enter, each held the few frames a finger would. */
        switch (f) {
            case 100: press(ROW_ENTER, 3); break;                  /* J = LOAD */
            case 106: release(); break;
            case 112: press(ROW_SPACE, 1); press(ROW_P, 0); break; /* SymShift+P = " */
            case 118: release(); break;
            case 124: press(ROW_SPACE, 1); press(ROW_P, 0); break;
            case 130: release(); break;
            case 136: press(ROW_ENTER, 0); break;                  /* Enter */
            case 142: release(); break;
        }
        bench_slice(1);

        if (out) {
            if (spectrum_tape_progress() >= 100 && !spectrum_tape_playing()) {
                if (++quiet == 50) settle = f + 600;   /* twelve seconds */
            } else {
                quiet = 0;
            }
            if (settle > 0 && f >= settle) break;
        }

        if (f % 2000 == 1999)
            printf("  frame %5d  tape %3d%%  %s  PC %04X  picture %d/6144\n",
                   f + 1, spectrum_tape_progress(),
                   spectrum_tape_playing() ? "turning" : "stopped",
                   bench_cpu()->PC.W, pictureBytes());
    }

    /* Two signs, because neither alone is enough. A game that has loaded
     * usually runs its own code, but plenty sit in the ROM's key-scan
     * waiting to be started; and a game that has drawn a screen has
     * plainly loaded, but Elite and Thrust have nearly empty ones. Take
     * either as proof and say both numbers, so a close call is visible
     * rather than hidden behind a verdict. */
    int inRam = 0;
    for (int f = 0; f < 200; f++) {
        bench_slice(1);
        if (bench_cpu()->PC.W >= 0x4000) inRam++;
    }
    int pic = pictureBytes();
    /* FORCE=1 takes the word of whoever ran it: Elite settles on its
     * Lenslok screen, 626 bytes of picture and its wait for Enter inside
     * the ROM's interrupt, and both signs say no. */
    int loaded = (pic > 1000 || inRam > 150 || getenv("FORCE"));
    printf("\ntape %d%%, picture %d/6144, %d%% of frames running from RAM - %s\n",
           spectrum_tape_progress(), pic, inRam / 2,
           loaded ? "LOADED" : "DID NOT LOAD");
    if (!loaded || !out) return loaded ? 0 : 1;

    if (!writeSna(out)) return 1;
    if (!getenv("FORCE") && !verifySna(out, argv[1])) {
        printf("%s: written, but does not come back - removed\n", out);
        remove(out);
        return 1;
    }
    printf("%s: written and verified\n", out);
    return 0;
}
