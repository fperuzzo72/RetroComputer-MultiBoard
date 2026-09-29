#ifndef BLE_KEYBOARD_H
#define BLE_KEYBOARD_H

/* BLE HID keyboard host (central role) for the FNK0103 MSX emulator.
 *
 * Scans for and connects to any BLE peripheral advertising the standard
 * HID service (0x1812), subscribes to its Boot Keyboard Input Report
 * characteristic (0x2A22), and feeds key transitions into fMSX's keyboard
 * matrix via KBD_SET()/KBD_RES() (see msx_keys.h).
 *
 * This covers modern BLE keyboards (Apple Magic Keyboard, Logitech K380,
 * etc). It does NOT cover Bluetooth Classic (BR/EDR) HID keyboards - that
 * needs a different stack (Bluedroid classic HID host) which is
 * meaningfully heavier and isn't wired up here. See the README for notes
 * on adding it later.
 *
 * Implemented in ble_keyboard.cpp (C++, uses NimBLE-Arduino). Declared
 * with C linkage so it can be called from the plain-C platform_glue.c.
 */

#ifdef __cplusplus
extern "C" {
#endif

void ble_keyboard_init(void);

/* Called from the MSX core's Keyboard() platform hook once per frame to
 * apply the latest received HID report to fMSX's KeyState[] matrix. */
void ble_keyboard_poll(void);

/* Non-zero once a keyboard is connected and reporting. */
int ble_keyboard_connected(void);

/* Diagnostics: how many HID notifications have arrived, and a switch that
 * dumps each one as hex on the serial console. A keyboard that connects
 * and types nothing is indistinguishable from one whose reports are being
 * discarded until you look at the bytes. */
unsigned long ble_keyboard_report_count(void);
void ble_keyboard_log_reports(int on);

/* Feed the machine a report nobody typed, for testing a key mapping
 * without the key. See the 'j' console command. */
void ble_keyboard_inject(const uint8_t report[8]);
unsigned long ble_keyboard_adverts_seen(void);
void ble_keyboard_scan(int on);

#ifdef __cplusplus
}
#endif

#endif
