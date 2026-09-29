/* msx_audio.c - EMULib's audio interface, onto the board's DAC.
 *
 * fMSX's Sound.c calls InitAudio/TrashAudio/GetFreeAudio/GetTotalAudio/
 * WriteAudio/PauseAudio and expects a port to supply them. They are
 * nothing but a thin layer over src/boards/cyd/audio.c, which knows how to
 * put samples on this board's speaker and nothing about the MSX.
 */
#include "EMULib.h"
#include "Sound.h"
#include "audio.h"

unsigned int InitAudio(unsigned int Rate, unsigned int Latency) {
    (void)Latency;
    return audio_init(Rate);
}

void TrashAudio(void) { audio_shutdown(); }

unsigned int GetTotalAudio(void) { return audio_buffer_samples(); }

unsigned int GetFreeAudio(void) {
    /* i2s_write() blocks when the DMA chain is full, and the core only
     * hands over a frame's worth at a time, so reporting the whole buffer
     * as free is true enough and keeps it from throttling itself. */
    return audio_ready() ? audio_buffer_samples() : 0;
}

unsigned int WriteAudio(sample *Data, unsigned int Length) {
    return audio_write((const short *)Data, Length);
}

int PauseAudio(int Switch) { return audio_pause(Switch); }
