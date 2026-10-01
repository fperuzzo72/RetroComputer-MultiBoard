#ifndef C64_KEYS_H
#define C64_KEYS_H

#include <cstdint>

/* The keyboard, see c64_keys.cpp. attach once with the running C64Sys (for
 * RESTORE and the joystick port), report every HID report. */
void retro_c64_keys_attach(void *c64sys);
void retro_c64_keys_report(const uint8_t report[8]);
uint8_t retro_c64_matrix_read(uint8_t select, bool rows_from_columns);
int retro_c64_joystick_on(void);   /* 0 off, else the port */

#endif
