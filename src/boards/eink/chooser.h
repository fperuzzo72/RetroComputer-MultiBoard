#ifndef EINK_CHOOSER_H
#define EINK_CHOOSER_H
#ifdef __cplusplus
extern "C" {
#endif

/* Picking a computer and what it starts with, by touch, on the Paper Mono.
 * The screens are ui.cpp; this waits for the finger. Every call blocks
 * until something is picked, and whatever machine is running is stopped
 * meanwhile. */

#define CHOOSER_TIMEOUT (-10)

/* Which computer. `mark` is the one outlined, and the one returned as
 * CHOOSER_TIMEOUT's meaning when `timeout_ms` passes untouched (0 waits
 * for ever). -1 if cancelled.
 *
 * When the other app slot holds a firmware (CrossPlay on the Paper Mono,
 * CrossPoint on the PaperS3: EINK_OTHER_APP), one more band after the
 * computers goes back to it; picking it switches the slot and restarts, and
 * does not return. Offered here and only here, so every screen that lists
 * the computers offers it: the boot menu and the in-game selector once
 * differed, the selector having been written without it. */
int chooser_pick_machine(int allow_cancel, int mark, const char *note, unsigned long timeout_ms);

/* Which entry of that machine: an index, or -1 for "back". */
int chooser_pick_entry(int machine_index, int allow_cancel);

#ifdef __cplusplus
}
#endif
#endif
