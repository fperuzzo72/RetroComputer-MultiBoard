#ifndef CHOOSER_H
#define CHOOSER_H

/* Picking a machine and a game, on a 3.5" panel with a finger.
 *
 * The old menu was one scrolling list of fifty-three thirty-pixel rows
 * and it was miserable to hit. This is three screens instead, each with
 * targets big enough to press without aiming:
 *
 *   1. which machine        two half-screen panels
 *   2. which group          eight tiles, the list cut into eight
 *   3. which game           the handful of games in that group
 *
 * Every screen blocks until something is pressed, so a caller runs it and
 * gets an answer. The machine, if one is running, is stopped meanwhile.
 */
#ifdef __cplusplus
extern "C" {
#endif

/* Screen 1. Returns an index into machine_list[], or -1 if cancelled. */
int chooser_pick_machine(int allowCancel);

/* Screens 2 and 3, for one machine. Returns an entry index, or -1. */
int chooser_pick_entry(int machineIndex, int allowCancel);

/* The touch calibration, kept in NVS and shared with whatever else is
 * flashed on this board. Without it TFT_eSPI maps the panel with rough
 * defaults and presses land in the wrong place. */
void chooser_apply_calibration(void);
void chooser_forget_calibration(void);
void chooser_calibrate(void);
int  chooser_is_calibrated(void);

#ifdef __cplusplus
}
#endif
#endif
