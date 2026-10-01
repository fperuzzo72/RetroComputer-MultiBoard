#ifndef MSX_BRIDGE_H
#define MSX_BRIDGE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Thin C-linkage wall between the Arduino/C++ world (TFT_eSPI, NimBLE)
 * and the vendored fMSX core.
 *
 * WHY THIS EXISTS: fMSX's Z80.h does `typedef unsigned short word` while
 * the ESP32 Arduino core's Arduino.h does `typedef unsigned int word`.
 * Any translation unit that pulls in both fails to compile, and the
 * "fix" of letting Arduino's 32-bit `word` win would silently change the
 * layout of every fMSX structure. So no .cpp file may include MSX.h /
 * Z80.h: C++ code talks to the core only through this header, and the
 * implementations live in plain-C files that never see Arduino.h. */

/* Claim the framebuffer up front, before anything else fragments the one
 * large DRAM region this chip has. Call before msx_run(). */
int msx_video_prealloc(void);

/* Configure and run the emulator. Blocks forever (StartMSX only returns
 * on ExitNow, which nothing triggers here). Call from its own task. */
void msx_run(void);

/* Overwrite the whole 16-byte key matrix image at once. msx_keys.c
 * rebuilds it from scratch every frame and hands it over here; a byte
 * per matrix row, a bit per contact, 1 = open = not pressed. Writing it
 * wholesale is deliberate: the previous version diffed HID reports into
 * individual KBD_SET/KBD_RES calls and had to special-case left/right
 * modifiers sharing one contact. */
void msx_kbd_write(const uint8_t state[16]);

/* --- debug/self-test surface, used by debug_console.cpp --------------
 * Lets the firmware read back what the emulated machine is actually
 * showing, over serial, without anyone having to look at the panel. */

/* Current screen mode (0 = SCREEN 0 text 40col, 1 = SCREEN 1 32col). */
int msx_screen_mode(void);

/* One row of the key matrix as the machine currently sees it. */
int msx_matrix_row(int row);

/* Copies one row of the text-mode name table into `out` as raw MSX
 * character codes. Returns the number of columns written (0 if the
 * current screen mode is not a text mode). */
int msx_screen_row(int row, uint8_t *out, int max);

/* Put a character straight into the BIOS's keyboard buffer, as if the
 * machine had decoded a keypress into it. Returns 0 if the buffer is
 * full.
 *
 * This exists for accented characters. The Hotbit has no key for á, and
 * its BIOS only produces one by composing a dead key with a letter - a
 * composition whose case follows CAPS, and which ignores Shift entirely.
 * Driving that from outside means the accent layer cannot decide the case
 * of what it types, which is why Shift did nothing for accented letters.
 * Composing in our own code and handing the finished character over gives
 * back that control. Plain keys still go through the matrix, where games
 * expect to find them. */
int msx_type_char(unsigned char code);

/* Restart the board, so a new cartridge selection takes effect. */
void msx_reboot(void);

/* Put whichever cartridge is selected into slot 0 and reset the machine
 * around it, the way turning it off and on with a different cartridge
 * would. The board keeps running. */
void msx_insert_cartridge(void);

/* Sound on or off at runtime. Off gives back about a quarter of the frame
 * rate; see docs/DISPLAY.md on why speed is scarce here. */
/* The VDP's rate, 60 (the Hotbit) or 50 (a European MSX). Set takes
 * effect at the next start. */
int  msx_hz(void);
void msx_set_hz(int hz);

/* Percentage of frames drawn (1-100), or just report it with 0. */
int msx_draw_percent(int pct);
void msx_set_sound(int on);
int  msx_sound_on(void);

/* Read a byte of the emulated machine's memory, for checking that the
 * system-variable addresses this firmware pokes at are really where the
 * documentation says they are. Returns -1 if that page is not RAM. */
int msx_peek(int addr);

/* The machine's CAPS LOCK state, which is what decides the case of a
 * letter when nothing is holding Shift. */
int msx_caps_on(void);

/* Copy the 8x8 bitmap the machine uses for character `code` out of the
 * VDP pattern table. The serial console draws it as text, which is the
 * only way to find out what an accented character code actually looks
 * like on this BIOS without photographing the panel. Returns non-zero on
 * success. */
int msx_char_pattern(int code, uint8_t *rows8);

/* Frames the emulator has drawn since power-on. The serial console
 * divides it by elapsed time to report the speed the machine is actually
 * running at - 60 means real MSX speed. */
unsigned int msx_frame_count(void);

/* Set once the core has claimed its RAM, VRAM and framebuffer. Anything
 * else that wants a lot of heap should wait for this. */
int msx_memory_claimed(void);

#ifdef __cplusplus
}
#endif
#endif
