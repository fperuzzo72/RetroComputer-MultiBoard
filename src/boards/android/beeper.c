/* beeper.c - the Spectrum's one-bit speaker on the Android board's audio.
 *
 * src/beeper.h hands over a frame at a time: the level at its start and
 * the cycle offsets where it flipped. Here each frame becomes PCM at 44.1kHz
 * (the level held between flips, a little quieter than full scale) and is
 * pushed to the ring android_audio.c plays. A machine's frames come a touch
 * faster or slower than the audio clock drains them; a frame that would
 * take the queue past 80ms is cut short, so the delay never grows. */
#include "beeper.h"

#include <stdint.h>

#include "audio.h"

#define RATE 44100u
#define LEVEL 6000

static int started;
static unsigned int rate;

void beeper_frame(int level, const uint32_t *edges, int n, uint32_t frame, uint32_t clock_hz)
{
    if (!started) {
        started = 1;
        rate = audio_start_push(RATE);
    }
    if (!rate || !clock_hz) return;
    static short buf[4096];
    static uint64_t carry;   /* fractional samples, in clock cycles x rate */
    const uint64_t total = (uint64_t)frame * rate + carry;
    unsigned int samples = (unsigned int)(total / clock_hz);
    carry = total % clock_hz;
    if (samples > sizeof buf / sizeof buf[0]) samples = sizeof buf / sizeof buf[0];
    int e = 0;
    for (unsigned int i = 0; i < samples; i++) {
        const uint64_t t = (uint64_t)i * clock_hz / rate;
        while (e < n && edges[e] <= t) { level = !level; e++; }
        buf[i] = (short)(n ? (level ? LEVEL : -LEVEL) : 0);
    }
    const unsigned int queued = audio_queued(), most = rate * 80 / 1000;
    if (queued >= most) return;
    if (queued + samples > most) samples = most - queued;
    audio_write(buf, samples);
}
