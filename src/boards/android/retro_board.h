#ifndef RETRO_BOARD_H
#define RETRO_BOARD_H
/* The Android board, platform-free part (retro_board.c): starts the chosen
 * machine on its own thread, keeps its picture, turns key events into the
 * HID reports the machines take, and runs the selector. The platform side
 * (android_jni.cpp on the device, tools/androidhost on the Mac) presents
 * the picture and supplies the menu. */
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Where NVS keeps its files. Before retro_board_start(). */
void retro_board_set_storage(const char *dir);
/* Bring up the machine chosen last time, on its own thread (8MB stack). */
void retro_board_start(void);

/* The picture as it is now: copies it into out (w*h RGB565, row-major),
 * returns its sequence number, which changes only when the picture does. */
uint32_t retro_board_picture(uint16_t *out, int *w, int *h, uint16_t *border);

/* A key, as a HID usage (0x04 'a' ... 0xE0-0xE7 modifiers), down or up. */
void retro_board_key(uint8_t usage, int down);

/* The selector: the platform is asked to show its menu by this hook... */
extern void (*retro_board_menu_hook)(void);
/* ...and answers with an entry of the running machine, or -1 to go back. */
void retro_board_menu_done(int entry);
/* Another computer: remembered, then the app starts over as it. */
void retro_board_choose_machine(int machine, int entry);
/* How the platform starts over (Android relaunches the Activity). */
extern void (*retro_board_restart_hook)(void);

#ifdef __cplusplus
}
#endif
#endif
