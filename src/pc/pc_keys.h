#ifndef PC_KEYS_H
#define PC_KEYS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A BLE keyboard's 8-byte boot report, turned into PC scancodes and the
 * ASCII the BIOS queues with them (pc_keys.c). True when F12 went down,
 * which is the board's (the selector), not the PC's. */
bool pc_keys_report(const uint8_t report[8]);

/* Types one character, press and release, US layout. For the serial
 * console and tools/pchost. False if it has no key. */
bool pc_keys_type(char c);

/* The code page accented letters are typed in, and the screen drawn in:
 * 860 (Portuguese, the default) or 437. */
void pc_keys_set_codepage(int cp);
int  pc_keys_codepage(void);

#ifdef __cplusplus
}
#endif

#endif
