#ifndef BEEPER_H
#define BEEPER_H

/* A one-bit speaker, for a machine whose sound is one (the Spectrum's
 * beeper) on a board that has one (the e-ink boards' buzzer).
 *
 * The machine runs a frame's worth of emulated time in a burst and then
 * waits out the rest of the frame, so the speaker cannot be driven as the
 * emulated program writes it: the frame's sound would come out squeezed
 * into its first few milliseconds. Instead the machine notes when the
 * level changed, in its own clock cycles from the start of the frame, and
 * hands the frame over at its end; the board plays it back at the right
 * speed while the next one is emulated. A frame late, 20ms, which nobody
 * hears.
 *
 * `level` is the speaker's level at the frame's start, `edges` the cycle
 * offsets at which it flipped, in order, `frame` the frame's length in
 * cycles and `clock_hz` the machine's clock. A board without a speaker
 * does nothing; the Spectrum carries a do-nothing default. */
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void beeper_frame(int level, const uint32_t *edges, int n, uint32_t frame, uint32_t clock_hz);

#ifdef __cplusplus
}
#endif
#endif
