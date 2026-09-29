/* audio.c - the board's sound output, for whichever machine is built in.
 *
 * Board-level and machine-neutral: it takes signed 16-bit mono samples
 * and puts them on the speaker. What generates them - an MSX PSG, a
 * Spectrum beeper - is not this file's business. The MSX's adapter onto
 * EMULib's audio interface lives in src/msx/msx_audio.c.
 */
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s.h"
#include "driver/gpio.h"

#include "audio.h"

/* The audio path on this board, read off Freenove's own schematic
 * (Datasheet/3.5inch_ESP32-32E_..._V1.0/Schematic):
 *
 *   GPIO26 (DAC channel 2) -> AUDIO_IN -> SC8002B amplifier -> SP+/SP-
 *   GPIO4  -> the amplifier's SHUTDOWN pin, active LOW
 *
 * Two things follow. The enable pin has to be pulled LOW or the amplifier
 * stays shut down no matter what the DAC is doing, which is what Freenove's
 * MP3 example does before playing anything and what nothing here did.
 *
 * And SP+/SP- is a two-pin header, not a fitted speaker: this board ships
 * with the amplifier but WITHOUT a speaker on it. A small 8-ohm speaker
 * has to be connected there before any of this can be heard. Everything
 * upstream can be perfect and still silent. */
#define AUDIO_EN_PIN    GPIO_NUM_4
#define AUDIO_EN_ACTIVE 0

#define AUDIO_PORT      I2S_NUM_0
#define AUDIO_DMA_BUFS  4
#define AUDIO_DMA_LEN   256   /* samples per DMA buffer */

static int sRate = 0;
static int sPaused = 0;

/* Samples actually handed to the DAC, so the serial console can say
 * whether silence means the core is not producing any or the speaker is
 * not playing what it is given. */
static unsigned long sSamplesOut = 0;

unsigned long audio_samples_written(void) { return sSamplesOut; }

unsigned int audio_init(unsigned int Rate) {
    i2s_config_t cfg;

    if (!Rate) return 0;
    /* 22050Hz is as much as this board can keep up with while emulating a
     * Z80 and pushing 256x212 pixels over SPI on the same core. */
    if (Rate > 22050) Rate = 22050;

    memset(&cfg, 0, sizeof(cfg));
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN);
    cfg.sample_rate = Rate;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_MSB;
    cfg.intr_alloc_flags = 0;
    cfg.dma_buf_count = AUDIO_DMA_BUFS;
    cfg.dma_buf_len = AUDIO_DMA_LEN;
    cfg.use_apll = false;

    gpio_set_direction(AUDIO_EN_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(AUDIO_EN_PIN, AUDIO_EN_ACTIVE);

    if (i2s_driver_install(AUDIO_PORT, &cfg, 0, NULL) != ESP_OK) return 0;
    i2s_set_pin(AUDIO_PORT, NULL); /* NULL pin config = built-in DAC */
    i2s_set_dac_mode(I2S_DAC_CHANNEL_BOTH_EN);
    i2s_zero_dma_buffer(AUDIO_PORT);

    sRate = (int)Rate;
    sPaused = 0;
    return Rate;
}

void audio_shutdown(void) {
    if (!sRate) return;
    i2s_driver_uninstall(AUDIO_PORT);
    sRate = 0;
}

unsigned int audio_buffer_samples(void) {
    return sRate ? AUDIO_DMA_BUFS * AUDIO_DMA_LEN : 0;
}

int audio_ready(void) { return sRate && !sPaused; }

unsigned int audio_write(const short *Data, unsigned int Length) {
    /* The DAC wants unsigned samples in the top 8 bits of each 16-bit
     * word, and both channels written; the core hands us signed mono. */
    static unsigned short conv[256];
    unsigned int done = 0;
    size_t wrote;

    if (!sRate || sPaused || !Length) return 0;

    while (done < Length) {
        unsigned int n = Length - done;
        unsigned int i;
        if (n > sizeof(conv) / sizeof(conv[0]) / 2) n = sizeof(conv) / sizeof(conv[0]) / 2;
        for (i = 0; i < n; i++) {
            unsigned short v = (unsigned short)((int)Data[done + i] + 32768);
            /* Both DAC channels get the sample. Only GPIO26 reaches the
             * amplifier on this board; GPIO25 is driven anyway because it
             * costs nothing and makes this work on boards wired either
             * way. */
            conv[i * 2]     = v;
            conv[i * 2 + 1] = v;
        }
        if (i2s_write(AUDIO_PORT, conv, n * 2 * sizeof(unsigned short), &wrote,
                      pdMS_TO_TICKS(2)) != ESP_OK)
            break;
        if (!wrote) break;
        done += wrote / (2 * sizeof(unsigned short));
        sSamplesOut += wrote / (2 * sizeof(unsigned short));
    }
    return done;
}

int audio_pause(int Switch) {
    if (Switch == 2) Switch = !sPaused;   /* 2 = toggle */
    if (Switch >= 0) {
        sPaused = Switch ? 1 : 0;
        if (sPaused && sRate) i2s_zero_dma_buffer(AUDIO_PORT);
    }
    return sPaused;
}

/* A plain square wave straight to the DAC, with the emulator out of it.
 * If this is audible the output path works and any silence is the core's;
 * if it is not, the fault is here or in the amplifier. */
void audio_test_tone(int hz, int ms) {
    static unsigned short buf[256];
    int total, i, phase = 0, half;
    size_t wrote;

    if (!sRate || hz <= 0) return;
    half = sRate / (hz * 2);
    if (half < 1) half = 1;
    total = sRate * ms / 1000;

    while (total > 0) {
        int n = total > 128 ? 128 : total;
        for (i = 0; i < n; i++) {
            unsigned short v = (phase < half) ? 0xC000 : 0x4000;
            buf[i * 2] = v;
            buf[i * 2 + 1] = v;
            if (++phase >= half * 2) phase = 0;
        }
        if (i2s_write(AUDIO_PORT, buf, n * 2 * sizeof(unsigned short), &wrote,
                      pdMS_TO_TICKS(200)) != ESP_OK)
            break;
        total -= n;
    }
}
