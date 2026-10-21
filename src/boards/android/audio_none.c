/* audio_none.c - no sound: tools/androidhost, and the fallback if AAudio
 * will not open. The MSX's InitSound() then reports the sound off. */
#include "audio.h"

int audio_mode(void) { return AUDIO_MODE_OFF; }
void audio_set_mode(int mode) { (void)mode; }
void audio_report(void) {}
void audio_mute(int on) { (void)on; }
void audio_hold(int pct) { (void)pct; }
unsigned int audio_init(unsigned int rate) { (void)rate; return 0; }
unsigned int audio_start_push(unsigned int rate) { (void)rate; return 0; }
void audio_shutdown(void) {}
unsigned int audio_write(const short *s, unsigned int n) { (void)s; return n; }
unsigned int audio_buffer_samples(void) { return 0; }
unsigned int audio_queued(void) { return 0; }
int audio_ready(void) { return 0; }
int audio_pause(int on) { (void)on; return 0; }
unsigned long audio_samples_written(void) { return 0; }
void audio_test_tone(int hz, int ms) { (void)hz; (void)ms; }
