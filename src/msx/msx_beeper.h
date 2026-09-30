#ifndef MSX_BEEPER_H
#define MSX_BEEPER_H
/* The PSG's loudest voice on a board's one-bit speaker. See msx_beeper.c. */
void msx_beeper_frame(void);    /* once a frame */
void msx_beeper_silence(void);  /* before the machine stands still */
#endif
