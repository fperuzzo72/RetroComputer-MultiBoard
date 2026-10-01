/* msx_machine.c - the MSX, as the board sees it.
 *
 * Thin on purpose: it forwards to msx_bridge.c and msx_keys.c, which are
 * the code that was brought up and verified on hardware. What this file
 * adds is the Machine table, so src/boards/cyd/ can drive an MSX or a
 * Spectrum without knowing which it has.
 */
#include <stdio.h>

#include "machine.h"
#include "msx_bridge.h"
#include "msx_keys.h"
#include "msx_carts.h"
#include "selector.h"
#include "display.h"

void msx_prof_report(unsigned long *videoUs, unsigned long *soundUs,
                     unsigned long *loopUs, unsigned long *frames);

static int  m_prealloc(void)      { return msx_video_prealloc(); }
static void m_run(void)           { msx_keys_init(); msx_run(); }
static int  m_ready(void)         { return msx_memory_claimed(); }
static unsigned long m_frames(void) { return msx_frame_count(); }

static void m_hid(const uint8_t report[8]) { msx_keys_set_report(report); }
static int  m_type(const char *text)       { return msx_keys_type(text); }
static int  m_typing(void)                 { return msx_keys_typing(); }

static int m_screen_row(int row, uint8_t *out, int max) {
    return msx_screen_row(row, out, max);
}

static const char *m_screen_mode(void) {
    static char buf[24];
    snprintf(buf, sizeof(buf), "SCREEN %d", msx_screen_mode());
    return buf;
}

static int  m_char_pattern(int c, uint8_t *r) { return msx_char_pattern(c, r); }
static int  m_peek(int a)                     { return msx_peek(a); }
static void m_set_sound(int on)               { msx_set_sound(on); }
static int  m_sound_on(void)                  { return msx_sound_on(); }

/* Entry 0 is the machine with an empty slot, which on a real BIOS means
 * MSX-BASIC. The rest are the cartridges built into this firmware. */
static int m_entry_count(void) { return 1 + msx_cart_count_get(); }

static const char *m_entry_name(int i) {
    return i <= 0 ? "MSX-BASIC" : msx_cart_name(i - 1);
}

static void m_select_entry(int i) { msx_cart_select(i <= 0 ? -1 : i - 1); }

/* Inserting a cartridge on a real MSX means turning it off, swapping the
 * cartridge and turning it on again. Here it is the same thing without
 * the board rebooting: the core reloads the slot and resets the machine
 * around it. */
static void m_switch_to(int i) {
    msx_cart_select(i <= 0 ? -1 : i - 1);
    msx_insert_cartridge();
}
static int  m_selected_entry(void) { return msx_cart_selected() + 1; }

/* Commands that only make sense here: the dead-key probe and the raw
 * matrix press, both of which exist because this machine's keyboard had
 * to be mapped out by pressing keys and reading the result back. */
static const char *m_debug_help(void) {
    return "  d <n> <shift> <char>   press dead key n then a character\n"
           "  p <row> <bit> <mods>   press one matrix position (1 Shift, 2 Ctrl)\n"
           "  c [n]                  list cartridges, or select one and reboot\n"
           "  q                      where a frame's time goes\n"
           "  u [pct]                frames drawn, in percent\n"
           "  hz [50|60]             the VDP's rate (restarts)\n"
           "  v                      frames in each of the last 60 seconds";
}

static int m_debug_command(const char *line) {
    int a = 0, b = 0, c = 0;
    char ch = 'a';

    switch (line[0]) {
        case 'd':
            if (sscanf(line + 1, "%d %d %c", &a, &b, &ch) >= 1) {
                msx_keys_probe_dead(a, b, ch);
                printf("dead-key probe: position %d, shift %d, base '%c'\n", a, b, ch);
            }
            return 1;
        case 'p':
            if (sscanf(line + 1, "%i %i %i", &a, &b, &c) >= 2) {
                msx_keys_press_matrix(a, b, c);
                printf("pressed row %d bit 0x%02X mods %d (1=Shift 2=Ctrl)\n", a, b, c);
            }
            return 1;
        case 'c':
            if (sscanf(line + 1, "%d", &a) == 1) {
                msx_cart_select(a);
                printf("selected: %s - rebooting\n", msx_cart_name(a));
                msx_reboot();
            } else {
                msx_cart_list();
            }
            return 1;
        case 'v':
            msx_pace_report();
            return 1;
        case 'h':
            if (sscanf(line, "hz %d", &a) == 1 && (a == 50 || a == 60)) {
                msx_set_hz(a);
                printf("MSX at %d Hz from the next start; restarting\n", a);
                msx_reboot();
            }
            printf("MSX at %d Hz (hz 50 | hz 60)\n", msx_hz());
            return 1;
        case 'u':
            a = 0;
            sscanf(line + 1, "%d", &a);
            printf("drawing %d%% of frames\n", msx_draw_percent(a));
            return 1;
        case 'q': {
            /* Where a frame goes. The Z80 is rarely the answer. */
            unsigned long v, snd, loop, n, blit = display_blit_us();
            msx_prof_report(&v, &snd, &loop, &n);
            display_blit_us_reset();
            if (!n) { printf("cpu: nothing measured yet\n"); return 1; }
            /* LoopZ80 runs once a scanline and holds everything fMSX does
             * besides execute instructions - the VDP, the sprites, the
             * sound, and the blit, which happens inside the line drawing.
             * So the Z80 itself is what is left over. */
            printf("per frame, over %lu frames:\n"
                   "  fMSX per-scanline work %5lu us   (of which:)\n"
                   "    drawing + blit       %5lu us\n"
                   "      blit alone         %5lu us\n"
                   "    sound                %5lu us\n"
                   "  Z80 and everything else, by subtraction\n",
                   n, loop / n, v / n, blit / n, snd / n);
            return 1;
        }
        default:
            return 0;
    }
}

const Machine msx_machine = {
    "MSX1 (Hotbit HB-8000)",
    1,   /* 1:1. At 1.5x the blit alone is 24ms of a frame and this
          * machine has none to spare - see README, "Speed". */
    m_prealloc, m_run, m_ready, m_frames,
    m_hid, m_type, m_typing,
    m_screen_row, m_screen_mode, m_char_pattern, m_peek, msx_matrix_row,
    m_set_sound, m_sound_on,
    m_entry_count, m_entry_name, m_select_entry, m_selected_entry, m_switch_to,
    m_debug_command, m_debug_help,
};
