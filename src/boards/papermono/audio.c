/* audio.c - no sound on the Paper Mono yet.
 *
 * The board has a buzzer on GPIO42, a one-bit beeper, which suits the
 * Spectrum and is some way from an MSX PSG. Until something drives it,
 * audio_init() says no, and fMSX runs without sound rather than rendering
 * samples nobody hears. */
#include "audio.h"

unsigned int audio_init(unsigned int rate) { (void)rate; return 0; }
void audio_shutdown(void) {}
unsigned int audio_write(const short *samples, unsigned int count) { (void)samples; return count; }
unsigned int audio_buffer_samples(void) { return 0; }
int  audio_ready(void) { return 0; }
int  audio_pause(int on) { (void)on; return 0; }
unsigned long audio_samples_written(void) { return 0; }
void audio_test_tone(int hz, int ms) { (void)hz; (void)ms; }
