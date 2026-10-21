/* android_audio.c - the Android board's sound: a machine's samples on AAudio.
 *
 * The same shape as the e-ink boards' audio.c, with AAudio where the buzzer
 * was. Pulled: a thread of the board's keeps a ring of samples filled
 * through fMSX's RenderAndPlayAudio(), whose WriteAudio() lands in
 * audio_write() below, and AAudio's callback drains the ring. So the sound
 * does not stutter when a frame is late, as it would if the machine's own
 * frames fed it. A machine that writes frames itself (the C64's SID) uses
 * audio_start_push(), and nothing pulls.
 *
 * An empty ring plays silence and counts an underrun; a full one drops
 * what does not fit. Mono, signed 16-bit, at whatever rate AAudio opens
 * with (asked for the machine's own, which it resamples if it must).
 */
#include "audio.h"

#include <aaudio/AAudio.h>
#include <android/log.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

unsigned int RenderAndPlayAudio(unsigned int Samples);   /* EMULib Sound.c */

#define RING      16384u               /* 370ms at 44.1kHz */
#define RING_MASK (RING - 1u)
#define TAG       "RetroAudio"

static int16_t ring[RING];
static _Atomic uint32_t head, tail;
static AAudioStream *stream;
static volatile int running, paused, pulled;
static unsigned int target;            /* samples kept queued when pulled */
static volatile unsigned long written, underruns;
static pthread_t pump_thread;

static uint32_t ring_count(void)
{
    return atomic_load_explicit(&head, memory_order_acquire) - atomic_load_explicit(&tail, memory_order_acquire);
}

static aaudio_data_callback_result_t on_audio(AAudioStream *s, void *user, void *data, int32_t frames)
{
    (void)s;
    (void)user;
    int16_t *out = (int16_t *)data;
    uint32_t t = atomic_load_explicit(&tail, memory_order_relaxed);
    const uint32_t h = atomic_load_explicit(&head, memory_order_acquire);
    int32_t i = 0;
    if (!paused)
        for (; i < frames && t != h; i++, t++) out[i] = ring[t & RING_MASK];
    if (i < frames) {
        if (running && !paused) underruns++;
        memset(out + i, 0, (size_t)(frames - i) * sizeof *out);
    }
    atomic_store_explicit(&tail, t, memory_order_release);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
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

static unsigned int start(unsigned int rate, int pull)
{
    if (stream) return (unsigned int)AAudioStream_getSampleRate(stream);
    AAudioStreamBuilder *b = NULL;
    if (AAudio_createStreamBuilder(&b) != AAUDIO_OK) return 0;
    AAudioStreamBuilder_setDirection(b, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setSampleRate(b, (int32_t)rate);
    AAudioStreamBuilder_setChannelCount(b, 1);
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_NONE);
    AAudioStreamBuilder_setDataCallback(b, on_audio, NULL);
    const aaudio_result_t r = AAudioStreamBuilder_openStream(b, &stream);
    AAudioStreamBuilder_delete(b);
    if (r != AAUDIO_OK) {
        __android_log_print(ANDROID_LOG_WARN, TAG, "openStream: %s", AAudio_convertResultToText(r));
        stream = NULL;
        return 0;
    }
    const unsigned int got = (unsigned int)AAudioStream_getSampleRate(stream);
    target = got / 16;                 /* ~60ms queued */
    pulled = pull;
    running = 1;
    atomic_store(&head, 0);
    atomic_store(&tail, 0);
    if (AAudioStream_requestStart(stream) != AAUDIO_OK) {
        AAudioStream_close(stream);
        stream = NULL;
        running = 0;
        return 0;
    }
    pthread_create(&pump_thread, NULL, pump, NULL);
    __android_log_print(ANDROID_LOG_INFO, TAG, "sound on: %u Hz asked, %u Hz got, %s", rate, got,
                        pull ? "pulled" : "pushed");
    return got;
}

unsigned int audio_init(unsigned int rate) { return start(rate ? rate : 44100, 1); }
unsigned int audio_start_push(unsigned int rate) { return start(rate, 0); }

void audio_shutdown(void)
{
    if (!stream) return;
    running = 0;
    pthread_join(pump_thread, NULL);
    AAudioStream_requestStop(stream);
    AAudioStream_close(stream);
    stream = NULL;
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
int audio_ready(void) { return stream != NULL; }
int audio_pause(int on) { paused = on == 2 ? !paused : on; return paused; }
unsigned long audio_samples_written(void) { return written; }
int audio_mode(void) { return AUDIO_MODE_PCM; }
void audio_set_mode(int mode) { (void)mode; }
void audio_mute(int on) { (void)on; }
void audio_hold(int pct) { (void)pct; }
void audio_test_tone(int hz, int ms) { (void)hz; (void)ms; }

void audio_report(void)
{
    __android_log_print(ANDROID_LOG_INFO, TAG, "ring %u of %u, %lu written, %lu underruns", ring_count(), RING,
                        written, underruns);
}
