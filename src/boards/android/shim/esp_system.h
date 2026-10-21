#ifndef RETRO_ESP_SYSTEM_H
#define RETRO_ESP_SYSTEM_H
/* A restart is the app starting over as the computer now chosen: the board
 * does it (retro_board.c), relaunching the Activity on Android. */
#ifdef __cplusplus
extern "C" {
#endif
void retro_board_restart(void);
static inline void esp_restart(void) { retro_board_restart(); }
#ifdef __cplusplus
}
#endif
#endif
