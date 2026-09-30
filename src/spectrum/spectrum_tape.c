/* spectrum_tape.c - a tape as a signal, with a shortcut where one is safe.
 *
 * Two mechanisms, and the file is only worth reading if you know why both
 * are here.
 *
 * The shortcut catches the ROM's LD-BYTES at 0x0556 and hands over a whole
 * block at once. It is instant, and it works right up until a game loads
 * its own loader and starts reading the tape itself - which most did, and
 * which is why Nebulus stopped halfway when this was all there was.
 *
 * So the tape also does what a tape did: it produces edges. The ROM and every
 * custom loader read bit 6 of port 0xFE and time the gaps between
 * transitions, and a .tap block becomes the pulse train that produces
 * those gaps:
 *
 *   pilot   2168 T-states a half-pulse, 8063 of them for a header and
 *           3223 for data, which is the long tone you can hear
 *   sync    667 then 735, the marker that the data is about to start
 *   data    each bit as two half-pulses, 855 for a 0 and 1710 for a 1,
 *           most significant bit first
 *   pause   a second of silence between blocks
 *
 * Nothing is trapped and the ROM stays in flash, which is also 16kB of RAM
 * back.
 *
 * Knowing where the tape is means knowing the T-state, and the CPU core
 * gives it: ICount is what remains of the frame, so the position is the
 * frames gone by plus what this frame has used.
 *
 * The motor turns itself on. A loader reads port 0xFE in a tight loop,
 * tens of T-states apart; the ROM reading the keyboard reads it a handful
 * of times a frame, thousands apart. Two reads close together mean
 * somebody is listening, and that is when the tape starts playing.
 *
 * Playing the signal is slow, and that is not a fault. Measured: Halls of
 * the Things is 148 seconds of tape and Nebulus 273, and this board runs
 * the Z80 at about 0.9 of a real Spectrum, so a full signal load is
 * minutes and the screen is blank for all of it. That looks exactly like
 * a hang and is not one, which is what `spectrum_tape_progress()` is for.
 * The machine also drops its 50Hz pacing and most of its redrawing while
 * the tape turns.
 *
 * Hence the shortcut: whenever the ROM's own loader asks for a block and
 * the tape is sitting at the start of one, it is handed over whole and the
 * wait disappears. A game's own loader never calls LD-BYTES, so it gets
 * the signal, correctly, at tape speed. Games that load through the ROM
 * are instant; games that brought their own loader take the minutes they
 * always took.
 *
 * Verified on the host rather than by eye: tools/tapebench runs this
 * generator against the real ROM and the real Z80, boots, types LOAD "",
 * and loads Halls of the Things end to end - 24577 bytes, all three
 * blocks, 3783 of 6144 display bytes written. Run that before believing
 * anything about this file.
 */
#include <string.h>
#include <stdio.h>

#include "z80_names.h"
#include "Z80.h"
#include "spectrum.h"

/* Standard timings, in T-states. */
#define T_PILOT        2168
#define T_PILOT_HEADER 8063
#define T_PILOT_DATA   3223
#define T_SYNC1        667
#define T_SYNC2        735
#define T_BIT0         855
#define T_BIT1         1710
#define T_PAUSE        3500000      /* a second */

/* Two reads closer together than this mean a loader is listening. */
#define POLL_GAP       400

enum { OFF = 0, PILOT, SYNC1, SYNC2, DATA, PAUSE, DONE };

static const unsigned char *sTape;
static int sTapeLen;
static int sBlock;          /* offset of the block being played */
static int sBlockLen;
static int sPhase;
static int sPulses;         /* pilot half-pulses left */
static int sByte, sBit, sHalf;
static long long sNextEdge;
static long long sLastRead;
static uint8_t sLevel;
static int sBlocks;
static int sPlaying;

extern const unsigned char *spectrum_tape_image(void);
extern int spectrum_tape_size(void);

int spectrum_tape_blocks(void) { return sBlocks; }

/* How far along the tape is, as a percentage. Worth having: a tape that
 * is loading and a tape that has hung look identical from outside, and
 * that cost a day here. */
int spectrum_tape_progress(void) {
    long long pos;
    if (!sTape || sTapeLen <= 0) return 0;
    pos = sBlock + (sPhase == DATA ? sByte : 0);
    return (int)(pos * 100 / sTapeLen);
}

/* Loading a 36kB block at tape speed takes three minutes, because that is
 * how long it took. Nobody wants that, so the machine is let off its 50Hz
 * pacing while the tape is turning and the wait becomes seconds. It stops
 * as soon as the loader does. */
int spectrum_tape_playing(void) { return sPlaying && sPhase != DONE; }

/* If nothing has read the port for a while, the loader has stopped
 * listening and the tape should stop too. */
#define IDLE_TSTATES (SPEC_FRAME_TSTATES * 2)

/* ---- the shortcut: the ROM's own loader, served whole blocks ---- */

#define LD_BYTES 0x0556

/* The trap is an opcode planted in the ROM, so it needs the RAM copy;
 * without one the machine simply plays the signal for everything. */
static uint8_t *sRomRam;

const uint8_t *spectrum_tape_rom(void) { return sRomRam; }

static void plantTrap(void) {
    sRomRam = spectrum_rom_writable();
    if (!sRomRam) return;
    sRomRam[LD_BYTES]     = 0xED;   /* the core's own patch opcode */
    sRomRam[LD_BYTES + 1] = 0xFE;
}

/* Hand over the block the tape is sitting on. The ROM asks for a kind of
 * block in A; the wrong kind is not an error but a block for somebody
 * else, and it is passed over with carry clear so the ROM asks again.
 *
 * The rest is what LD-BYTES itself does, to the byte, because loaders
 * look at it: it reads DE bytes, then one more as the parity, XORs all
 * of them with the flag into H, and returns with A = H and carry set if
 * H is 0. A block longer than asked for leaves its tail unread, and
 * then the "parity" is a data byte and H is not 0. Elite asks for three
 * bytes short of its block and carries on only if A comes back 0xDC; an
 * earlier version of this handed over the whole block with A untouched,
 * and Elite asked again forever. A block shorter than asked for runs off
 * the end of the signal: the ROM times out, carry clear. */
static void handOverBlock(Z80 *R) {
    int len, flag, data, i, n;
    word dest   = R->IX.W;
    word wanted = R->DE.W;
    byte want   = R->AF.B.h;
    const int load = (R->AF.B.l & C_FLAG) != 0;   /* carry clear is VERIFY */

    R->AF.B.l &= (byte)~C_FLAG;

    if (!sTape || sBlock + 2 > sTapeLen) return;
    len = sTape[sBlock] | (sTape[sBlock + 1] << 8);
    if (len < 2 || sBlock + 2 + len > sTapeLen) return;

    flag = sTape[sBlock + 2];
    data = sBlock + 3;                  /* past the length and the flag */

    /* Spent either way, and the signal resumes at whatever follows. */
    sBlock += 2 + len;
    sPhase   = OFF;
    sPlaying = 0;
    sBlocks++;

    if (flag != want) return;

    n = len - 1;                        /* what follows the flag */
    byte h = (byte)flag;
    int ok = 1;
    int got = (int)wanted;
    if (got + 1 > n) { got = n; ok = 0; }
    for (i = 0; i < got; i++) {
        const byte b = sTape[data + i];
        h ^= b;
        if (load) WrZ80((word)(dest + i), b);
        else if (RdZ80((word)(dest + i)) != b) { got = i; ok = 0; break; }
    }
    R->IX.W = (word)(dest + got);
    R->DE.W = (word)(wanted - got);
    if (!ok) return;

    h ^= sTape[data + got];             /* the parity, or what stands for it */
    R->AF.B.h = h;
    R->AF.B.l = (byte)((R->AF.B.l & ~(C_FLAG | Z_FLAG))
                       | (h == 0 ? C_FLAG : 0) | (h == 1 ? Z_FLAG : 0));
}

/* The core calls this where the ED FE sits, with PC already past it. */
void spec_PatchZ80(Z80 *R) {
    if (R->PC.W != LD_BYTES + 2) return;

    /* Only from a standing start. If the signal is already partway into a
     * block then somebody is mid-load and the pulses must finish the job;
     * jumping in with a whole block would hand over the same bytes twice.
     * Put the ROM's own opcodes back and stay out of the way from here on:
     * once a loader is reading the tape itself, it is in charge. */
    if (sPhase == DATA) {
        sRomRam[LD_BYTES]     = 0x14;   /* INC D, as the ROM has it */
        sRomRam[LD_BYTES + 1] = 0x08;   /* EX AF,AF' */
        R->PC.W = LD_BYTES;
        return;
    }

    handOverBlock(R);

    /* LD-BYTES ends in a RET, and so must we. */
    R->PC.B.l = RdZ80(R->SP.W);
    R->PC.B.h = RdZ80((word)(R->SP.W + 1));
    R->SP.W += 2;
}

int spectrum_tape_begin(const unsigned char *rom) {
    (void)rom;
    sTape = spectrum_tape_image();
    sTapeLen = spectrum_tape_size();
    sBlock = 0;
    sBlocks = 0;
    sPhase = OFF;
    sPlaying = 0;
    sLevel = 0;
    sLastRead = 0;
    if (!sTape || sTapeLen <= 0) return 0;
    plantTrap();
    printf("tape: %d bytes, as a signal%s\n", sTapeLen,
           sRomRam ? " with the ROM loader served whole blocks"
                   : " (no RAM for the ROM: signal only, minutes per game)");
    return 1;
}

static void startBlock(void) {
    if (!sTape || sBlock + 2 > sTapeLen) { sPhase = DONE; return; }
    sBlockLen = sTape[sBlock] | (sTape[sBlock + 1] << 8);
    if (sBlockLen < 2 || sBlock + 2 + sBlockLen > sTapeLen) { sPhase = DONE; return; }

    /* A header gets the long pilot, data the short one. */
    sPulses = (sTape[sBlock + 2] < 128) ? T_PILOT_HEADER : T_PILOT_DATA;
    sPhase = PILOT;
    sByte = 0;
    sBit = 7;
    sHalf = 0;
    sBlocks++;
}

/* Move to the next edge and say how long until the one after it. */
static int nextInterval(void) {
    switch (sPhase) {
        case PILOT:
            if (--sPulses <= 0) sPhase = SYNC1;
            return T_PILOT;

        case SYNC1:
            sPhase = SYNC2;
            return T_SYNC1;

        case SYNC2:
            sPhase = DATA;
            return T_SYNC2;

        case DATA: {
            int bit = (sTape[sBlock + 2 + sByte] >> sBit) & 1;
            int len = bit ? T_BIT1 : T_BIT0;
            if (++sHalf >= 2) {          /* both halves of this bit done */
                sHalf = 0;
                if (--sBit < 0) {
                    sBit = 7;
                    if (++sByte >= sBlockLen) {
                        sPhase = PAUSE;
                        sBlock += 2 + sBlockLen;
                    }
                }
            }
            return len;
        }

        case PAUSE:
            startBlock();
            return T_PAUSE;

        default:
            return T_PAUSE;
    }
}

/* Bit 6 of port 0xFE, as the tape would drive it. `now` is the T-state. */
uint8_t spectrum_tape_ear(long long now) {
    if (!sTape) return 0;

    /* Nobody has listened for two frames: the loader has finished or given
     * up, so stop the tape rather than run it past the end. */
    if (sPlaying && now - sLastRead > IDLE_TSTATES) {
        sPlaying = 0;
        sPhase = OFF;        /* the next start begins this block afresh */
        sLastRead = now;
        return 0;
    }

    /* Motor control, by ear: a loader polls tightly, a keyboard scan does
     * not. */
    if (!sPlaying) {
        long long gap = now - sLastRead;
        sLastRead = now;
        if (gap > 0 && gap < POLL_GAP) {
            /* Pick up where the tape was, not at the beginning. The
             * loader stops listening between blocks, and rewinding on
             * every restart meant it read the first block over and over. */
            sPlaying = 1;
            if (sPhase == OFF) startBlock();
            sNextEdge = now + T_PILOT;
            sLevel = 0;
        }
        return 0;
    }

    sLastRead = now;

    while (sPhase != DONE && now >= sNextEdge) {
        sLevel ^= 0x40;
        sNextEdge += nextInterval();
    }
    return (sPhase == DONE) ? 0 : sLevel;
}
