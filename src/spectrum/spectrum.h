#ifndef SPECTRUM_H
#define SPECTRUM_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ZX Spectrum 48K.
 *
 *   0x0000-0x3FFF  16kB ROM, executed straight out of flash
 *   0x4000-0x57FF  the display file, 6144 bytes of bitmap
 *   0x5800-0x5AFF  768 attribute bytes, one per 8x8 cell
 *   0x5B00-0xFFFF  the rest of the 48kB of RAM
 *
 * The ROM is not in this repo. Supply your own and build it in:
 *
 *   python3 tools/embed_rom.py 48.rom src/spectrum/spectrum_rom_data.c spectrum_rom
 *
 * Amstrad have long permitted the Spectrum ROMs to be distributed with
 * emulators, but that is their permission to give and not mine, so the
 * image stays out of the repository and out of the firmware until you put
 * it there. Without one the machine says so and stops. */

#define SPEC_ROM_SIZE   0x4000
#define SPEC_RAM_SIZE   0xC000   /* 48kB, mapped at 0x4000 */
#define SPEC_SCREEN     0x4000
#define SPEC_ATTRS      0x5800
#define SPEC_FONT_ADDR  0x3D00   /* the ROM's 8x8 font, from space */

/* 3.5MHz, 50Hz: 69888 T-states a frame. */
#define SPEC_FRAME_TSTATES 69888

/* 256x192 picture, drawn inside the same 256x216 area the panel layer
 * centres, with the border filling what is left. */
#define SPEC_PICTURE_TOP 12

/* A .sna snapshot: 27 bytes of registers then 48kB of RAM from 0x4000.
 * The format keeps the program counter on the stack, so restoring it is
 * the last thing the loader does. */
#define SNA_HEADER   27
#define SNA_SIZE     (SNA_HEADER + 49152)

/* Tapes built into this firmware. A .tap is played as a signal on bit 6
 * of port 0xFE, so a game's own loader works as well as the ROM's; see
 * spectrum_tape.c. */
int         spectrum_tape_count(void);
const char *spectrum_tape_name(int i);
int         spectrum_tape_selected(void);
void        spectrum_tape_select(int i);
int         spectrum_tape_begin(const unsigned char *rom);
int         spectrum_tape_blocks(void);

/* The ROM copied into RAM, which the tape's shortcut patches, or NULL
 * when it would not fit and the signal has to do all the work. */
uint8_t    *spectrum_rom_writable(void);
const uint8_t *spectrum_tape_rom(void);

/* Bit 6 of port 0xFE as the tape drives it, given the current T-state. */
uint8_t     spectrum_tape_ear(long long tstate);

/* Non-zero while a tape is actually being read, so the machine can drop
 * its 50Hz pacing and get the loading over with. */
int         spectrum_tape_playing(void);

/* How far through the tape, 0 to 100, for the console and the panel. */
int         spectrum_tape_progress(void);
void        spectrum_keys_autoload(void);

/* Snapshots built into this firmware, the Spectrum's answer to the MSX's
 * cartridges. Nothing here is distributed with the project. */
int         spectrum_snapshot_count(void);
const char *spectrum_snapshot_name(int i);
int         spectrum_snapshot_selected(void);
void        spectrum_snapshot_select(int i);

/* The keyword crib in the panel margins, toggled with F1. */
void spectrum_help_toggle(void);
int  spectrum_help_active(void);
void spectrum_help_draw(void);
void spectrum_help_invalidate(void);

void spectrum_keys_reset(void);
void spectrum_keys_hid(const uint8_t report[8]);
uint8_t spectrum_keys_read(uint8_t highAddr);
/* The Kempston joystick on the arrows and Ctrl (spectrum_keys.c). */
void    spectrum_keys_joystick(int on);
int     spectrum_keys_joystick_on(void);
uint8_t spectrum_keys_kempston(void);
int  spectrum_keys_type(const char *text);
int  spectrum_keys_typing(void);
void spectrum_keys_frame(void);

#ifdef __cplusplus
}
#endif

#endif
