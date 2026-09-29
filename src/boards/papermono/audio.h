#ifndef AUDIO_H
#define AUDIO_H
#ifdef __cplusplus
extern "C" {
#endif

/* The board's sound output.
 *
 * The path, from Freenove's schematic: GPIO26 is AUDIO_IN into an SC8002B
 * amplifier whose SHUTDOWN is GPIO4, active LOW, and the output is a
 * two-pin SP+/SP- header. The board does NOT have a speaker fitted - one
 * has to be connected there. See docs/DISPLAY.md. */

/* Bring the DAC up at the given rate. Returns the rate actually used, or
 * 0 if it could not start. */
unsigned int audio_init(unsigned int rate);
void audio_shutdown(void);

/* Signed 16-bit mono samples. Returns how many were taken. */
unsigned int audio_write(const short *samples, unsigned int count);

unsigned int audio_buffer_samples(void);
int  audio_ready(void);
int  audio_pause(int on);       /* 2 toggles */

/* Samples handed to the DAC since boot, so silence can be told apart
 * from nothing being generated. */
unsigned long audio_samples_written(void);

/* A square wave straight to the DAC with the machine out of the way. If
 * this is audible the output path works. */
void audio_test_tone(int hz, int ms);

#ifdef __cplusplus
}
#endif
#endif
