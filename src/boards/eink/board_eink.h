#ifndef BOARD_EINK_H
#define BOARD_EINK_H
#ifdef __cplusplus
extern "C" {
#endif

/* What main.cpp's input task offers the menus.
 *
 * While a menu is up (board_ui(1)), a tap on the panel is queued for it
 * instead of going to the machine; board_take_tap() waits for one. Taps
 * are in the upright frame (canvas.h). */

void board_ui(int on);

/* 1 and the tap in x,y, or 0 if none came within `wait_ms`. */
int  board_take_tap(int *x, int *y, unsigned long wait_ms);

#ifdef __cplusplus
}
#endif
#endif
