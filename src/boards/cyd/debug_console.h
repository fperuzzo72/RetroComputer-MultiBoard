#ifndef DEBUG_CONSOLE_H
#define DEBUG_CONSOLE_H
#ifdef __cplusplus
extern "C" {
#endif

/* A tiny serial console, so this firmware can be checked from the USB
 * cable instead of by reading the panel over someone's shoulder: it can
 * type into the emulated machine and read back what ended up on its
 * screen. That closed loop is what makes it possible to verify the
 * keyboard layout - including which dead key carries which accent, which
 * the BIOS tables do not say - without a BLE keyboard in the room.
 *
 * Runs on its own task (core 0), because the emulator saturates core 1. */
void debug_console_init(void);

#ifdef __cplusplus
}
#endif
#endif
