#ifndef SELECTOR_H
#define SELECTOR_H
/* The Android board's selector: F12 or a tap asks the app's own menu
 * (retro_board.c hands it to the platform); the machine stands still in
 * its frame hook until a choice comes back, as on the boards. */
#ifdef __cplusplus
extern "C" {
#endif
void selector_open(void);
void selector_poll_open(void);
int  selector_active(void);
int  selector_frame(void);
int  selector_take_restart_choice(void);
void selector_restart_into_choice(void);
#ifdef __cplusplus
}
#endif
#endif
