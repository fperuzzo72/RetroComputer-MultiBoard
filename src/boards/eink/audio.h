#ifndef AUDIO_H
#define AUDIO_H
#ifdef __cplusplus
extern "C" {
#endif

/* The e-ink boards' sound output: fMSX's mix as PCM on the buzzer, see
 * audio.c. The ring is filled by audio.c's own task, not by the machine's
 * frames, hence AUDIO_PULLS. */
#define AUDIO_PULLS 1

#define AUDIO_MODE_PCM   0   /* fMSX's full mix as PWM (the default) */
#define AUDIO_MODE_VOICE 1   /* the loudest PSG voice as a square wave */
#define AUDIO_MODE_OFF   2
int  audio_mode(void);
void audio_set_mode(int mode);   /* takes effect at the next start */
void audio_report(void);         /* for `snd` on the console */

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
