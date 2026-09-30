/* msx_beeper.c - the PSG, one voice at a time, on a one-bit speaker.
 *
 * The e-ink boards have a buzzer and nothing else (beeper.h). The MSX's
 * AY-3-8910 has three tone voices, noise, and a volume on each; a buzzer
 * has one note and no volume. So once a frame this takes the loudest
 * voice the PSG is sounding and plays that: a tone as a square wave at its
 * frequency, noise (a drum, an explosion) as a random wave at the noise
 * rate. Volume becomes the duty cycle, which a buzzer hears as loudness:
 * full volume is a 50% square, quiet ones are narrower pulses. The tune
 * comes through; the harmony under it does not.
 *
 * fMSX keeps PSG.Freq[] and PSG.Volume[] current with the sound off, the
 * envelope included (Loop8910 runs every eight scanlines regardless), so
 * this only reads them. The wave carries on across frames, so a held note
 * has no seam at 60Hz. On the CYD, which has real audio, nothing links a
 * beeper and the do-nothing default below is what runs.
 */
#include <math.h>
#include <stdint.h>
#include "MSX.h"
#include "beeper.h"
#include "msx_beeper.h"
#include "Sound.h"
#include "audio.h"

extern AY8910 PSG;   /* MSX.c; not in MSX.h */

__attribute__((weak)) void beeper_frame(int level, const uint32_t *edges, int n,
                                        uint32_t frame, uint32_t clock_hz)
{
    (void)level; (void)edges; (void)n; (void)frame; (void)clock_hz;
}

#define FRAME_US   16667               /* the MSX is paced to 60Hz */
#define MAX_EDGES  1024
#define TONE_MAX   12000               /* above this a buzzer only whines */
#define NOISE_MAX  8000

static uint32_t sEdges[MAX_EDGES];
static int      sLevel;                /* the wave at the frame's start */
static int32_t  sNext;                 /* next edge, us from frame start */
static uint32_t sLfsr = 1;

void msx_beeper_silence(void)
{
    sLevel = 0;
    sNext = 0;
    beeper_frame(0, sEdges, 0, FRAME_US, 1000000);
}

void msx_beeper_frame(void)
{
    /* The board plays fMSX's own mix (PCM), or has it off: nothing here. */
    if (GetSndRate() > 0) return;
#ifdef AUDIO_PULLS
    static int mode = -1;               /* NVS, read once */
    if (mode < 0) mode = audio_mode();
    if (mode != AUDIO_MODE_VOICE) return;
#endif
    int tone = -1, noise = -1, j;
    for (j = 0; j < 3; j++)
        if (PSG.Freq[j] > 20 && PSG.Freq[j] <= TONE_MAX && PSG.Volume[j] > 0
            && (tone < 0 || PSG.Volume[j] > PSG.Volume[tone])) tone = j;
    for (j = 3; j < 6; j++)
        if (PSG.Freq[j] > 0 && PSG.Volume[j] > 0
            && (noise < 0 || PSG.Volume[j] > PSG.Volume[noise])) noise = j;
    /* Noise volumes are kept at half by fMSX; a tone wins a tie. */
    const int useNoise = noise >= 0 && (tone < 0 || PSG.Volume[noise] * 2 > PSG.Volume[tone]);
    const int voice = useNoise ? noise : tone;
    if (voice < 0) { msx_beeper_silence(); return; }

    const int start = sLevel;
    int n = 0, level = sLevel;
    int32_t t = sNext;

    if (useNoise) {
        int f = PSG.Freq[voice] > NOISE_MAX ? NOISE_MAX : PSG.Freq[voice];
        const int32_t step = 1000000 / f;
        if (t <= 0) t = 1;
        while (t < FRAME_US && n < MAX_EDGES) {
            /* The AY's 17-bit noise register. */
            sLfsr = (sLfsr >> 1) | ((((sLfsr >> 0) ^ (sLfsr >> 3)) & 1) << 16);
            const int bit = sLfsr & 1;
            if (bit != level) { sEdges[n++] = (uint32_t)t; level = bit; }
            t += step;
        }
    } else {
        const int32_t period = 1000000 / PSG.Freq[voice];
        /* Volume[] is the AY's amplitude, 0..255 on 3dB steps. A pulse
         * that narrow would be all but silent on a buzzer: tunes often
         * sit at 4-32 (Penguin Adventure's does), so the width follows
         * the square root, which keeps quiet voices audible and loud
         * ones louder. */
        int32_t high = (int32_t)(period * 0.5f * sqrtf(PSG.Volume[voice] / 255.0f));
        if (high < period / 12) high = period / 12;
        if (high < 2) high = 2;
        const int32_t low = period - high;
        if (t <= 0) t = 1;
        while (t < FRAME_US && n < MAX_EDGES) {
            sEdges[n++] = (uint32_t)t;
            level = !level;
            t += level ? high : low;
        }
    }
    sLevel = level;
    sNext = t - FRAME_US;
    beeper_frame(start, sEdges, n, FRAME_US, 1000000);
}
