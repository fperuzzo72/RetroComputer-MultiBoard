/* spectrum.c - a ZX Spectrum 48K on the CYD.
 *
 * Shares the board with the MSX: the same panel layer, the same BLE
 * keyboard transport, the same serial console. What is here is only the
 * machine - memory map, ULA video, ports - plus the machine.h interface
 * the board talks through.
 *
 * The CPU is the Z80 already vendored for the MSX (lib/fmsx_core/Z80).
 * That core is a plain Z80 with no MSX in it; it wants RdZ80, WrZ80,
 * InZ80, OutZ80, PatchZ80 and LoopZ80 from whoever uses it, and this file
 * provides them.
 *
 * Deliberately not emulated yet: memory contention, the tape interface,
 * and the beeper's exact pulse timing. A 48K Spectrum without contention
 * runs everything except the handful of programs that count T-states.
 */
#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

#include "z80_names.h"
#include "Z80.h"
#include "machine.h"
#include "display.h"
#include "spectrum.h"
#include "selector.h"

#ifdef HAVE_SPECTRUM_ROM
extern const unsigned char spectrum_rom[SPEC_ROM_SIZE];
#endif

/* Used before they are defined: the frame loop can swap what is running,
 * and swapping restores a snapshot. */
static int  loadSnapshot(void);
static void m_switch_to(int entry);

/* ---------------------------------------------------------------- */
/* Machine state                                                      */
/* ---------------------------------------------------------------- */
static Z80      sCPU;
static uint8_t *sRAM;            /* 48kB, mapped at 0x4000 */
static const uint8_t *sROM;      /* 16kB, from flash or a RAM copy of it */
static uint8_t *sRomRam;         /* that copy, when there was room for it */

/* The tape's shortcut plants an opcode in the ROM, which only works on a
 * copy in RAM. Flash cannot be written, so without this the tape has to
 * play its signal for everything. */
uint8_t *spectrum_rom_writable(void) { return sRomRam; }
static uint8_t  sBorder = 7;
static uint8_t  sSpeaker;
static volatile int sReady;
static volatile unsigned long sFrames;
static int sSoundOn = 1;
static int64_t sNextFrameUs;
static unsigned long sAutoloadAt;
static unsigned long sCpuUs, sCpuFrames;
static unsigned long sRenderUs, sTouchUs, sFrameUs;

/* One band of the picture, the same trick the MSX side uses: a whole
 * 256x192 8bpp frame would be 48kB and this board has no PSRAM. */
#define BAND_LINES 24
static uint8_t *sBand;
static int sBandTop = -1, sBandFill;

/* Which bands have changed since they were last drawn.
 *
 * A Spectrum frame usually changes very little, and redrawing all 192
 * lines regardless was costing 78% of the frame - the blit, not the Z80,
 * was the whole bottleneck. Writes to the display file and to the
 * attributes mark the band they land in, and only marked bands are
 * redrawn. Everything that invalidates the whole picture - the flash
 * phase turning over, a border change, a scale change - marks all of
 * them. */
#define BANDS (192 / BAND_LINES)
static uint8_t sDirty[BANDS];
static uint8_t sLastBorder = 0xFF;

static void markAll(void) { memset(sDirty, 1, sizeof(sDirty)); }

/* The pixel row an address in the display file belongs to. The layout is
 * the interleaved one, so this is the inverse of screenAddr(). */
static void markAddress(uint16_t A) {
    int y;
    if (A >= SPEC_SCREEN && A < SPEC_ATTRS) {
        int o = A - SPEC_SCREEN;
        y = ((o & 0x1800) >> 5) | ((o & 0x0700) >> 8) | ((o & 0x00E0) >> 2);
        sDirty[y / BAND_LINES] = 1;
    } else if (A >= SPEC_ATTRS && A < SPEC_ATTRS + 768) {
        /* One attribute covers an 8-pixel cell, which can straddle two
         * bands only if BAND_LINES is not a multiple of 8. It is, but mark
         * both anyway rather than depend on that. */
        int row = (A - SPEC_ATTRS) / 32;
        sDirty[(row * 8) / BAND_LINES] = 1;
        sDirty[(row * 8 + 7) / BAND_LINES] = 1;
    }
}

/* The Spectrum's fifteen colours: eight at two brightnesses, with black
 * shared. RGB565, built the way the panel wants them. */
#define RGB565(r,g,b) (uint16_t)((((r)&0xF8)<<8)|(((g)&0xFC)<<3)|((b)>>3))
static uint16_t sPalette[16];

static void buildPalette(void) {
    int i;
    for (i = 0; i < 16; i++) {
        int bright = i & 8;
        int lvl = bright ? 0xFF : 0xCD;   /* the usual normal/bright pair */
        int b = (i & 1) ? lvl : 0;
        int r = (i & 2) ? lvl : 0;
        int g = (i & 4) ? lvl : 0;
        sPalette[i] = RGB565(r, g, b);
    }
}

/* ---------------------------------------------------------------- */
/* Memory and ports                                                   */
/* ---------------------------------------------------------------- */
byte RdZ80(word A) {
    if (A < SPEC_ROM_SIZE) return sROM ? sROM[A] : 0xFF;
    return sRAM[A - SPEC_ROM_SIZE];
}

void WrZ80(word A, byte V) {
    if (A < SPEC_ROM_SIZE) return;      /* ROM is ROM */
    sRAM[A - SPEC_ROM_SIZE] = V;
    if (A < SPEC_ATTRS + 768) markAddress(A);
}

/* Where the tape is, in T-states. ICount is what remains of the frame the
 * CPU core is running, so the position is the frames gone by plus what
 * this one has used. Measured monotonic across EI and interrupts. */
static long long tstate(void) {
    return (long long)sFrames * SPEC_FRAME_TSTATES
         + (SPEC_FRAME_TSTATES - sCPU.ICount);
}

byte InZ80(word Port) {
    /* Anything with A0 low is the ULA: the keyboard in bits 0 to 4, the
     * tape in bit 6, and the unread bits floating high. */
    if (!(Port & 0x0001))
        return (byte)(spectrum_keys_read((uint8_t)(Port >> 8))
                      | spectrum_tape_ear(tstate()) | 0xA0);
    return 0xFF;
}

void OutZ80(word Port, byte V) {
    if (!(Port & 0x0001)) {
        sBorder  = V & 0x07;
        sSpeaker = (V >> 4) & 1;
    }
}


/* ---------------------------------------------------------------- */
/* Video                                                              */
/* ---------------------------------------------------------------- */

/* The display file is famously not linear: within a third of the screen,
 * consecutive addresses step eight pixel rows at a time. */
static uint16_t screenAddr(int y, int xByte) {
    /* Absolute, including the 0x4000 base, so callers can subtract
     * SPEC_ROM_SIZE to index sRAM. Returning a bare offset here and then
     * subtracting 0x4000 from it - which is what this did first - indexes
     * 16kB before the buffer. */
    return (uint16_t)(SPEC_SCREEN
                      | ((y & 0xC0) << 5) | ((y & 0x07) << 8)
                      | ((y & 0x38) << 2) | xByte);
}

static void flushBand(void) {
    if (sBandTop >= 0 && sBandFill > 0)
        display_write_picture(0, (short)(SPEC_PICTURE_TOP + sBandTop),
                              DISPLAY_PICTURE_W, (short)sBandFill,
                              sBand, sPalette[sBorder], sPalette);
    sBandTop = -1;
    sBandFill = 0;
}

static void fillRows(int top, int rows) {
    if (rows > 0)
        display_write_picture(0, (short)top, DISPLAY_PICTURE_W, (short)rows,
                              NULL, sPalette[sBorder], sPalette);
}

/* Render one pixel row of the picture into the band. */
static void renderLine(int y, int flashPhase) {
    const uint8_t *bitmap = sRAM + (screenAddr(y, 0) - SPEC_ROM_SIZE);
    const uint8_t *attrs  = sRAM + (SPEC_ATTRS - SPEC_ROM_SIZE) + (y >> 3) * 32;
    uint8_t *p;
    int x;

    if (sBandTop >= 0 && (sBandFill >= BAND_LINES || y != sBandTop + sBandFill))
        flushBand();
    if (sBandTop < 0) { sBandTop = y; sBandFill = 0; }

    p = sBand + sBandFill * DISPLAY_PICTURE_W;
    sBandFill++;

    for (x = 0; x < 32; x++) {
        uint8_t bits = bitmap[x];
        uint8_t a    = attrs[x];
        uint8_t ink  = (uint8_t)((a & 0x07) | ((a & 0x40) >> 3));
        uint8_t pap  = (uint8_t)(((a >> 3) & 0x07) | ((a & 0x40) >> 3));
        int i;
        /* Bit 7 of the attribute swaps ink and paper twice a second. */
        if ((a & 0x80) && flashPhase) { uint8_t t = ink; ink = pap; pap = t; }
        for (i = 0; i < 8; i++, bits <<= 1)
            *p++ = (bits & 0x80) ? ink : pap;
    }
}

/* ---------------------------------------------------------------- */
/* The frame                                                          */
/* ---------------------------------------------------------------- */
/* The CPU core calls this every IPeriod cycles. The frame is driven from
 * runFrame() instead, so there is nothing to do here but say "no
 * interrupt pending". */
word LoopZ80(Z80 *R) { (void)R; return INT_NONE; }

static void runFrame(void) {
    static int flashCounter, flashPhase;
    int y;

    int64_t tFrame0 = esp_timer_get_time();
    display_service();

    /* The selector owns the panel and the machine stands still under it. */
    if (selector_active()) {
        int chosen = selector_frame();
        if (!selector_active()) {
            sLastBorder = 0xFF;
            markAll();
            spectrum_help_invalidate();
            display_fill_panel(0);
            if (chosen >= 0) m_switch_to(chosen);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
        return;
    }

    {
        int64_t t0 = esp_timer_get_time();
        selector_poll_open();
        sTouchUs += (unsigned long)(esp_timer_get_time() - t0);
    }

    if (display_take_repaint()) { sLastBorder = 0xFF; markAll(); spectrum_help_invalidate(); }
    spectrum_help_draw();

    /* Border above the picture, then the picture, then the border below.
     * The border colour can change mid-frame on real hardware; this draws
     * it once per frame, which is right for everything that does not use
     * the border as an effect. */
    if (sBorder != sLastBorder) {
        fillRows(0, SPEC_PICTURE_TOP);
        fillRows(SPEC_PICTURE_TOP + 192, 216 - SPEC_PICTURE_TOP - 192);
        sLastBorder = sBorder;
    }

    /* A 24kB block is three minutes of tape, because that is what it was,
     * and the machine is let off its 50Hz pacing to get through it. The
     * blit is then what costs, so while the tape turns the picture is
     * redrawn a few times a second instead of fifty: the loading screen
     * still appears, a two-minute wait becomes a short one, and nothing
     * about the signal or the timing changes. Marks are left standing on
     * the frames that are skipped, so nothing is lost. */
    if (!spectrum_tape_playing() || (sFrames & 15) == 0) {
        int64_t t0 = esp_timer_get_time();
        for (y = 0; y < 192; y++)
            if (sDirty[y / BAND_LINES]) renderLine(y, flashPhase);
        flushBand();
        memset(sDirty, 0, sizeof(sDirty));
        sRenderUs += (unsigned long)(esp_timer_get_time() - t0);
    }

    /* The flash attribute swaps ink and paper twice a second, so every
     * cell using it has to be redrawn when the phase turns over.
     *
     * Not on a panel that must not blink (DISPLAY_STEADY, the Paper Mono's
     * e-ink): there every phase change would be a refresh of the whole
     * panel, twice a second for as long as BASIC's K cursor is on screen.
     * Flashing cells show steady, as ink on paper. */
#ifndef DISPLAY_STEADY
    if (++flashCounter >= 16) {
        flashCounter = 0;
        flashPhase = !flashPhase;
        markAll();
    }
#else
    (void)flashCounter;
#endif

    if (sAutoloadAt && sFrames >= sAutoloadAt) {
        sAutoloadAt = 0;
        spectrum_keys_autoload();
    }

    spectrum_keys_frame();

    {
        /* How long the Z80 itself takes, which is the number that matters:
         * a frame is 69888 T-states, so microseconds here convert straight
         * into the megahertz this board emulates at. A real Spectrum is
         * 3.5MHz; anything much under that and the machine runs slow no
         * matter what the picture costs. */
        int64_t t0 = esp_timer_get_time();
        ExecZ80(&sCPU, SPEC_FRAME_TSTATES);
        sCpuUs += (unsigned long)(esp_timer_get_time() - t0);
        sCpuFrames++;
    }
    IntZ80(&sCPU, INT_IRQ);      /* IM1: 50Hz maskable interrupt */

    sFrames++;

    /* Pace the machine to 50Hz.
     *
     * With only the changed bands being redrawn there is time to spare -
     * it free-ran at 105 fps, which is a Spectrum running at twice speed,
     * and every game would be unplayable. Sleeping the remainder also
     * feeds the watchdog and leaves the other core alone. If a frame
     * overruns, the deadline is reset rather than carried forward, so a
     * slow patch does not turn into a sprint afterwards. */
    sFrameUs += (unsigned long)(esp_timer_get_time() - tFrame0);

    if (spectrum_tape_playing()) {
        /* The tape is turning: run flat out and let the loading finish. */
        sNextFrameUs = 0;
        vTaskDelay(1);
        return;
    }

    {
        int64_t now = esp_timer_get_time();
        if (!sNextFrameUs) sNextFrameUs = now;
        sNextFrameUs += 1000000 / 50;
        if (sNextFrameUs > now) {
            int ms = (int)((sNextFrameUs - now) / 1000);
            vTaskDelay(ms > 0 ? pdMS_TO_TICKS(ms) : 1);
        } else if (now - sNextFrameUs > 200000) {
            /* More than a fifth of a second behind: something held the
             * machine up and there is no point sprinting to catch up. */
            sNextFrameUs = now;
            vTaskDelay(1);
        } else {
            /* Slightly late, which happens every frame because a tick is
             * 1ms and a frame is 20. Keep the deadline rather than
             * resetting it, or the small overshoot compounds into running
             * at 46Hz instead of 50. */
            vTaskDelay(1);
        }
    }
}

/* ---------------------------------------------------------------- */
/* machine.h                                                          */
/* ---------------------------------------------------------------- */
static int m_prealloc(void) {
    if (!sBand)
        sBand = (uint8_t *)heap_caps_malloc(DISPLAY_PICTURE_W * BAND_LINES,
                                            MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    return sBand != 0;
}


/* Restore a .sna over the running machine.
 *
 * The format is 27 bytes of registers then the whole 48kB of RAM. What
 * catches people out is the program counter: it is not in the header at
 * all, it is on the stack, so the last act of loading is to pop it. */
extern const unsigned char *spectrum_snapshot_image(void);

static int loadSnapshot(void) {
    const unsigned char *sna = spectrum_snapshot_image();
    word sp;

    if (!sna) return 0;

    memcpy(sRAM, sna + SNA_HEADER, SPEC_RAM_SIZE);

    sCPU.I          = sna[0];
    sCPU.HL1.W      = (word)(sna[1]  | (sna[2]  << 8));
    sCPU.DE1.W      = (word)(sna[3]  | (sna[4]  << 8));
    sCPU.BC1.W      = (word)(sna[5]  | (sna[6]  << 8));
    sCPU.AF1.W      = (word)(sna[7]  | (sna[8]  << 8));
    sCPU.HL.W       = (word)(sna[9]  | (sna[10] << 8));
    sCPU.DE.W       = (word)(sna[11] | (sna[12] << 8));
    sCPU.BC.W       = (word)(sna[13] | (sna[14] << 8));
    sCPU.IY.W       = (word)(sna[15] | (sna[16] << 8));
    sCPU.IX.W       = (word)(sna[17] | (sna[18] << 8));
    sCPU.IFF        = (byte)((sna[19] & 0x04) ? (IFF_1 | IFF_2) : 0);
    sCPU.R          = sna[20];
    sCPU.AF.W       = (word)(sna[21] | (sna[22] << 8));
    sp              = (word)(sna[23] | (sna[24] << 8));
    if (sna[25] == 1)      sCPU.IFF |= IFF_IM1;
    else if (sna[25] == 2) sCPU.IFF |= IFF_IM2;
    sBorder = (uint8_t)(sna[26] & 7);

    /* Pop the program counter the snapshot left on its own stack. */
    sCPU.PC.W = (word)(RdZ80(sp) | (RdZ80((word)(sp + 1)) << 8));
    sCPU.SP.W = (word)(sp + 2);

    markAll();
    sLastBorder = 0xFF;
    return 1;
}

static void m_run(void) {
#ifndef HAVE_SPECTRUM_ROM
    printf("spectrum: no ROM built in. See src/spectrum/spectrum.h - supply a\n"
           "          48K ROM and run tools/embed_rom.py. Stopping.\n");
    return;
#else
    buildPalette();
    spectrum_keys_reset();

    sRAM = (uint8_t *)heap_caps_malloc(SPEC_RAM_SIZE, MALLOC_CAP_8BIT);
    if (!sRAM) { printf("spectrum: could not allocate 48kB of RAM\n"); return; }
    memset(sRAM, 0, SPEC_RAM_SIZE);

    /* Every opcode the machine executes in the ROM is a read through the
     * flash cache, and a tape loader spins in a handful of ROM bytes for
     * minutes on end. A RAM copy is 16kB for a faster Z80; if it will not
     * fit, flash still works and the machine is only slower. */
    sROM = spectrum_rom;
    {
        sRomRam = (uint8_t *)heap_caps_malloc(SPEC_ROM_SIZE, MALLOC_CAP_8BIT);
        if (sRomRam) { memcpy(sRomRam, spectrum_rom, SPEC_ROM_SIZE); sROM = sRomRam; }
        printf("spectrum: 48kB RAM at %p, ROM %s, %d T-states a frame\n",
               (void *)sRAM, sRomRam ? "copied to RAM" : "from flash",
               SPEC_FRAME_TSTATES);
    }

    ResetZ80(&sCPU);
    sCPU.IPeriod = SPEC_FRAME_TSTATES;
    sCPU.IAutoReset = 1;

    markAll();

    if (loadSnapshot()) {
        printf("spectrum: started from a snapshot\n");
    } else if (spectrum_tape_begin(spectrum_rom)) {
        /* Not yet: the ROM has its own startup to get through, and this
         * one sits on a copyright screen until a key is pressed. Typing
         * into it before the cursor exists just loses the keystrokes. */
        sAutoloadAt = sFrames + 150;   /* three seconds of emulated time */
    }

    sReady = 1;
    for (;;) runFrame();
#endif
}

static int m_ready(void) { return sReady; }
static unsigned long m_frames(void) { return sFrames; }

static void m_hid(const uint8_t report[8]) { spectrum_keys_hid(report); }
static int  m_type(const char *text)       { return spectrum_keys_type(text); }
static int  m_typing(void)                 { return spectrum_keys_typing(); }

static const char *m_screen_mode(void) { return "256x192, 32x24 attributes"; }

/* The Spectrum has no text mode: what is on screen is a bitmap. But the
 * ROM carries an 8x8 font, so a cell can be matched against it and named.
 * That is what makes the console's screen readback work here at all, and
 * it is how this machine gets checked over the USB cable the same way the
 * MSX is. */
static int m_screen_row(int row, uint8_t *out, int max) {
#ifndef HAVE_SPECTRUM_ROM
    (void)row; (void)out; (void)max;
    return 0;
#else
    int col, cols = 32 > max ? max : 32;
    if (!sRAM || row < 0 || row >= 24) return 0;

    for (col = 0; col < cols; col++) {
        uint8_t cell[8];
        int i, ch, found = ' ';
        for (i = 0; i < 8; i++)
            cell[i] = sRAM[screenAddr(row * 8 + i, col) - SPEC_ROM_SIZE];
        for (ch = 32; ch < 128; ch++) {
            const unsigned char *g = spectrum_rom + SPEC_FONT_ADDR + (ch - 32) * 8;
            if (!memcmp(cell, g, 8)) { found = ch; break; }
        }
        out[col] = (uint8_t)found;
    }
    return cols;
#endif
}

static int m_char_pattern(int code, uint8_t *rows8) {
#ifndef HAVE_SPECTRUM_ROM
    (void)code; (void)rows8;
    return 0;
#else
    int i;
    if (code < 32 || code > 127) return 0;
    for (i = 0; i < 8; i++)
        rows8[i] = spectrum_rom[SPEC_FONT_ADDR + (code - 32) * 8 + i];
    return 1;
#endif
}

static int m_peek(int addr) {
    if (addr < 0 || addr > 0xFFFF) return -1;
    if (addr < SPEC_ROM_SIZE) {
#ifdef HAVE_SPECTRUM_ROM
        return spectrum_rom[addr];
#else
        return -1;
#endif
    }
    return sRAM ? sRAM[addr - SPEC_ROM_SIZE] : -1;
}

static void m_set_sound(int on) { sSoundOn = on ? 1 : 0; }
static int  m_sound_on(void)    { return sSoundOn; }

static const char *m_debug_help(void) {
    return "  y                      tape status: blocks served, where the CPU is\n"
           "  q                      Z80 speed since the last q, in emulated MHz";
}
static int m_debug_command(const char *line) {
    if (line[0] == 'q') {
        /* Z80 speed, measured rather than assumed. */
        unsigned long us = sCpuUs, n = sCpuFrames;
        unsigned long rend = sRenderUs, touch = sTouchUs, frame = sFrameUs;
        unsigned long blit = display_blit_us();
        sCpuUs = sCpuFrames = sRenderUs = sTouchUs = sFrameUs = 0;
        display_blit_us_reset();
        if (!n || !us) { printf("cpu: nothing measured yet\n"); return 1; }
        printf("cpu   %5lu us/frame  %.2f MHz emulated, %.0f%% of a real Spectrum\n"
               "draw  %5lu us/frame  (of which %lu blit)\n"
               "touch %5lu us/frame\n"
               "frame %5lu us/frame  = %.1f fps, %lu us unaccounted\n",
               us / n, (double)SPEC_FRAME_TSTATES * n / us,
               100.0 * SPEC_FRAME_TSTATES * n / us / 3.5,
               rend / n, blit / n,
               touch / n,
               frame / n, 1000000.0 * n / frame,
               (frame - us - rend - touch) / n);
        return 1;
    }
    if (line[0] == 'y') {
        printf("tape: %d%% through, %s, block %d, PC %04X SP %04X\n",
               spectrum_tape_progress(),
               spectrum_tape_playing() ? "turning" : "stopped",
               spectrum_tape_blocks(), sCPU.PC.W, sCPU.SP.W);
        return 1;
    }
    return 0;
}

/* Entry 0 is the machine on its own, then the snapshots, then the tapes. */
static int m_entry_count(void) {
    return 1 + spectrum_snapshot_count() + spectrum_tape_count();
}

static const char *m_entry_name(int i) {
    int snaps = spectrum_snapshot_count();
    if (i <= 0) return "Spectrum BASIC";
    if (i <= snaps) return spectrum_snapshot_name(i - 1);
    return spectrum_tape_name(i - 1 - snaps);
}

static void m_select_entry(int i) {
    int snaps = spectrum_snapshot_count();
    spectrum_snapshot_select((i > 0 && i <= snaps) ? i - 1 : -1);
    spectrum_tape_select(i > snaps ? i - 1 - snaps : -1);
}

/* Putting in another tape means starting the machine again with it, the
 * way you would have: reset, clear the RAM, rewind, and type LOAD "" when
 * the ROM is ready for it. */
static void m_switch_to(int i) {
    m_select_entry(i);
    memset(sRAM, 0, SPEC_RAM_SIZE);
    ResetZ80(&sCPU);
    sCPU.IPeriod = SPEC_FRAME_TSTATES;
    sCPU.IAutoReset = 1;
    sBorder = 7;
    sLastBorder = 0xFF;
    markAll();
    spectrum_help_invalidate();
    if (!loadSnapshot() && spectrum_tape_begin(spectrum_rom))
        sAutoloadAt = sFrames + 150;
}

static int m_selected_entry(void) {
    int snaps = spectrum_snapshot_count();
    if (spectrum_snapshot_selected() >= 0) return spectrum_snapshot_selected() + 1;
    if (spectrum_tape_selected() >= 0) return snaps + 1 + spectrum_tape_selected();
    return 0;
}

const Machine spectrum_machine = {
    "ZX Spectrum 48K",
    2,   /* 1.5x. It runs at twice the speed it needs, so the picture may
          * as well fill the panel. */
    m_prealloc, m_run, m_ready, m_frames,
    m_hid, m_type, m_typing,
    m_screen_row, m_screen_mode, m_char_pattern, m_peek, 0,
    m_set_sound, m_sound_on,
    m_entry_count, m_entry_name, m_select_entry, m_selected_entry, m_switch_to,
    m_debug_command, m_debug_help,
};
