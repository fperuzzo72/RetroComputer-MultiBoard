/* linux_audio.c - the Linux board's sound: a machine's samples on ALSA.
 *
 * android_audio.c with ALSA where AAudio was: the same ring, filled the
 * same two ways (pulled through fMSX's RenderAndPlayAudio() by a thread
 * of ours, or pushed by a machine that writes its own frames), drained by
 * a thread that writes it to the sound card a period at a time, which is
 * what keeps the pace. An empty ring plays silence and counts an underrun.
 * Mono, signed 16-bit, through ALSA's "default" (its plug converts the rate
 * the card does not have).
 */
#include "audio.h"

#include <alsa/asoundlib.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

unsigned int RenderAndPlayAudio(unsigned int Samples);   /* EMULib Sound.c */

#define RING      16384u
#define RING_MASK (RING - 1u)
#define PERIOD    512

static int16_t ring[RING];
static _Atomic uint32_t head, tail;
static snd_pcm_t *pcm;
static unsigned int rate_got;
static volatile int running, paused, pulled;
static unsigned int target;
static volatile unsigned long written, underruns;
static pthread_t pump_thread, out_thread;

static uint32_t ring_count(void)
{
    return atomic_load_explicit(&head, memory_order_acquire) - atomic_load_explicit(&tail, memory_order_acquire);
}

static void *pump(void *arg)
{
    (void)arg;
    while (running) {
        const uint32_t have = ring_count();
        if (pulled && !paused && have < target) RenderAndPlayAudio(target - have);
        usleep(5000);
    }
    return NULL;
}

static void *out(void *arg)
{
    (void)arg;
    int16_t buf[PERIOD];
    while (running) {
        uint32_t t = atomic_load_explicit(&tail, memory_order_relaxed);
        const uint32_t h = atomic_load_explicit(&head, memory_order_acquire);
        int i = 0;
        if (!paused)
            for (; i < PERIOD && t != h; i++, t++) buf[i] = ring[t & RING_MASK];
        atomic_store_explicit(&tail, t, memory_order_release);
        if (i < PERIOD) {
            if (!paused && i == 0) underruns++;
            memset(buf + i, 0, (size_t)(PERIOD - i) * sizeof buf[0]);
        }
        snd_pcm_sframes_t n = snd_pcm_writei(pcm, buf, PERIOD);
        if (n < 0) snd_pcm_recover(pcm, (int)n, 1);
    }
    return NULL;
}

static unsigned int start(unsigned int rate, int pull)
{
    if (pcm) return rate_got;
    int e = snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (e < 0) {
        printf("audio: no sound card (%s)\n", snd_strerror(e));
        pcm = NULL;
        return 0;
    }
    /* about 4 periods buffered: ~45ms at 44.1kHz */
    e = snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 1, rate, 1, 45000);
    if (e < 0) {
        printf("audio: %u Hz mono refused (%s)\n", rate, snd_strerror(e));
        snd_pcm_close(pcm);
        pcm = NULL;
        return 0;
    }
    rate_got = rate;
    target = rate / 16;                /* ~60ms queued, as on Android */
    pulled = pull;
    running = 1;
    atomic_store(&head, 0);
    atomic_store(&tail, 0);
    pthread_create(&out_thread, NULL, out, NULL);
    pthread_create(&pump_thread, NULL, pump, NULL);
    printf("audio: sound on, %u Hz, %s\n", rate, pull ? "pulled" : "pushed");
    return rate_got;
}

unsigned int audio_init(unsigned int rate) { return start(rate ? rate : 44100, 1); }
unsigned int audio_start_push(unsigned int rate) { return start(rate, 0); }

void audio_shutdown(void)
{
    if (!pcm) return;
    running = 0;
    pthread_join(pump_thread, NULL);
    pthread_join(out_thread, NULL);
    snd_pcm_close(pcm);
    pcm = NULL;
}

unsigned int audio_write(const short *samples, unsigned int count)
{
    uint32_t h = atomic_load_explicit(&head, memory_order_relaxed);
    const uint32_t t = atomic_load_explicit(&tail, memory_order_acquire);
    unsigned int i = 0;
    for (; i < count && h - t < RING; i++, h++) ring[h & RING_MASK] = samples[i];
    atomic_store_explicit(&head, h, memory_order_release);
    written += i;
    return count;
}

unsigned int audio_buffer_samples(void) { return RING; }
unsigned int audio_queued(void) { return ring_count(); }
int audio_ready(void) { return pcm != NULL; }
int audio_pause(int on) { paused = on == 2 ? !paused : on; return paused; }
unsigned long audio_samples_written(void) { return written; }
int audio_mode(void) { return AUDIO_MODE_PCM; }
void audio_set_mode(int mode) { (void)mode; }
void audio_mute(int on) { (void)on; }
void audio_hold(int pct) { (void)pct; }
void audio_test_tone(int hz, int ms) { (void)hz; (void)ms; }

void audio_report(void)
{
    printf("audio: ring %u of %u, %lu written, %lu underruns\n", ring_count(), RING, written, underruns);
}
