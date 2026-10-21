#ifndef AUDIO_H
#define AUDIO_H
/* The Android board's sound: the same calls as the e-ink boards'
 * (src/boards/eink/audio.h). Pulled, as there: a thread of the board's
 * keeps a ring filled through RenderAndPlayAudio() and AAudio plays it
 * (android_audio.c). */
#ifdef __cplusplus
extern "C" {
#endif
#define AUDIO_PULLS 1
#define AUDIO_MODE_PCM   0
#define AUDIO_MODE_VOICE 1
#define AUDIO_MODE_OFF   2
int  audio_mode(void);
void audio_set_mode(int mode);
void audio_report(void);
void audio_mute(int on);
void audio_hold(int pct);
unsigned int audio_init(unsigned int rate);
unsigned int audio_start_push(unsigned int rate);
void audio_shutdown(void);
unsigned int audio_write(const short *samples, unsigned int count);
unsigned int audio_buffer_samples(void);
int  audio_ready(void);
int  audio_pause(int on);
unsigned long audio_samples_written(void);
void audio_test_tone(int hz, int ms);
/* Android board only: samples waiting to be played, for a pushing machine
 * to keep the latency down (beeper.c). */
unsigned int audio_queued(void);
#ifdef __cplusplus
}
#endif
#endif
