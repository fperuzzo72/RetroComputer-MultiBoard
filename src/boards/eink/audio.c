/* audio.c - the MSX's mixed sound, as PCM on the e-ink boards' buzzer.
 *
 * The boards have a passive buzzer behind a transistor (EINK_BEEPER_PIN)
 * and nothing else. PaperBoy, the Game Boy emulator for the PaperS3
 * (gitlab.com/zephray/paperboy), plays PCM through it as PWM: a carrier
 * well above hearing whose duty follows the sample, which the buzzer and
 * the ear average into the waveform. That is what this does for fMSX's
 * mixer, PSG, SCC and all: LEDC at 32768Hz with 10-bit duty, and the
 * timer's overflow interrupt taking one sample from a ring each period.
 *
 * The ring is filled by a task of its own, calling fMSX's renderer at the
 * rate the buzzer drains it, rather than by the emulation (platform_glue.c
 * leaves PlayAllSound() alone when AUDIO_PULLS is defined). The MSX does
 * not always make 60 frames a second on these boards, and a sound fed
 * from the frames would stutter whenever it fell behind; rendered here, it
 * plays whatever the PSG is set to, continuously. PaperBoy decouples its
 * APU the same way.
 *
 * Silence is the buzzer off, not a 50% duty. The other mode (audio_mode
 * AUDIO_MODE_VOICE) leaves fMSX's sound off and msx_beeper.c plays the
 * loudest PSG voice as a square wave through beeper.h; `snd` on the serial
 * console switches, remembered in NVS.
 */
#include "audio.h"

#ifndef ARDUINO

/* The host tools: no sound. */
unsigned int audio_init(unsigned int rate) { (void)rate; return 0; }
void audio_shutdown(void) {}
unsigned int audio_write(const short *samples, unsigned int count) { (void)samples; return count; }
unsigned int audio_buffer_samples(void) { return 0; }
int  audio_ready(void) { return 0; }
int  audio_pause(int on) { (void)on; return 0; }
unsigned long audio_samples_written(void) { return 0; }
void audio_test_tone(int hz, int ms) { (void)hz; (void)ms; }
int  audio_mode(void) { return AUDIO_MODE_VOICE; }
void audio_set_mode(int mode) { (void)mode; }
void audio_report(void) {}

#else

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "driver/ledc.h"
#include "esp_attr.h"
#include "esp_intr_alloc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/ledc_ll.h"
#include "nvs.h"
#include "soc/interrupts.h"
#include "soc/ledc_struct.h"

#include "eink_board.h"
#include "Sound.h"

#define RATE       32768
#define DUTY_BITS  10
#define DUTY_MID   (1 << (DUTY_BITS - 1))
#define TIMER      LEDC_TIMER_0
#define CHANNEL    LEDC_CHANNEL_0
#define MODE       LEDC_LOW_SPEED_MODE
#define OVF_MASK   (1u << TIMER)       /* the timer's overflow bit (PaperBoy) */

#define RING       4096u               /* 125ms */
#define RING_MASK  (RING - 1u)
#define TARGET     1536u               /* ~47ms kept queued */
#define QUIET_RUN  3277                /* 100ms of zeros turns the buzzer off */

static int16_t DRAM_ATTR ring[RING];
static _Atomic uint32_t head, tail;
static volatile int running;
static volatile unsigned long written;
static volatile int quiet = 1;         /* buzzer off: nothing to play */
static volatile unsigned long underruns;  /* periods with the ring empty while playing */
static intr_handle_t intr;

static IRAM_ATTR void isr(void *arg)
{
    (void)arg;
    if (!(LEDC.int_st.val & OVF_MASK)) return;
    LEDC.int_clr.val = OVF_MASK;
    uint32_t t = atomic_load_explicit(&tail, memory_order_relaxed);
    uint32_t h = atomic_load_explicit(&head, memory_order_acquire);
    uint32_t duty = 0;
    if (h == t) { if (!quiet) underruns++; }
    else {
        const int32_t s = ring[t & RING_MASK];
        atomic_store_explicit(&tail, t + 1, memory_order_release);
        if (!quiet) {
            int32_t d = (s >> (16 - DUTY_BITS)) + DUTY_MID;
            duty = d < 0 ? 0 : d >= (1 << DUTY_BITS) ? (1 << DUTY_BITS) - 1 : (uint32_t)d;
        }
    }
    ledc_ll_set_duty_int_part(&LEDC, MODE, CHANNEL, duty);
    ledc_ll_ls_channel_update(&LEDC, MODE, CHANNEL);
}

static uint32_t ring_count(void)
{
    return atomic_load_explicit(&head, memory_order_acquire) - atomic_load_explicit(&tail, memory_order_relaxed);
}

/* Keeps the ring at TARGET by asking fMSX to mix; its WriteAudio() lands
 * in audio_write() below. */
static void pump(void *arg)
{
    (void)arg;
    for (;;) {
        const uint32_t have = ring_count();
        if (running && have < TARGET) RenderAndPlayAudio(TARGET - have);
        vTaskDelay(pdMS_TO_TICKS(8));
    }
}

#define NVS_NS  "cyd"
#define NVS_KEY "msxsnd"

int audio_mode(void)
{
    nvs_handle_t h;
    uint8_t v = AUDIO_MODE_PCM;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_u8(h, NVS_KEY, &v) != ESP_OK) v = AUDIO_MODE_PCM;
        nvs_close(h);
    }
    return v <= AUDIO_MODE_OFF ? v : AUDIO_MODE_PCM;
}

void audio_set_mode(int mode)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, NVS_KEY, (uint8_t)mode);
    nvs_commit(h);
    nvs_close(h);
}

unsigned int audio_init(unsigned int rate)
{
    (void)rate;
    if (audio_mode() != AUDIO_MODE_PCM) return 0;
    if (running) return RATE;

    ledc_timer_config_t tc = {};
    tc.speed_mode = MODE;
    tc.timer_num = TIMER;
    tc.duty_resolution = (ledc_timer_bit_t)DUTY_BITS;
    tc.freq_hz = RATE;
    tc.clk_cfg = LEDC_AUTO_CLK;
    ledc_channel_config_t cc = {};
    cc.speed_mode = MODE;
    cc.channel = CHANNEL;
    cc.timer_sel = TIMER;
    cc.intr_type = LEDC_INTR_DISABLE;
    cc.gpio_num = EINK_BEEPER_PIN;
    cc.duty = 0;
    if (ledc_timer_config(&tc) != ESP_OK || ledc_channel_config(&cc) != ESP_OK) {
        printf("audio: no LEDC for GPIO%d\n", EINK_BEEPER_PIN);
        return 0;
    }
    LEDC.int_ena.val |= OVF_MASK;
    if (esp_intr_alloc(ETS_LEDC_INTR_SOURCE, ESP_INTR_FLAG_IRAM, isr, NULL, &intr) != ESP_OK) {
        printf("audio: no LEDC interrupt\n");
        return 0;
    }
    running = 1;
    xTaskCreatePinnedToCore(pump, "audio", 4096, NULL, 4, NULL, 0);
    printf("audio: PCM on GPIO%d, %lu Hz\n", EINK_BEEPER_PIN, (unsigned long)ledc_get_freq(MODE, TIMER));
    return RATE;
}

void audio_shutdown(void) { running = 0; }

unsigned int audio_write(const short *samples, unsigned int count)
{
    static int zeros;
    uint32_t h = atomic_load_explicit(&head, memory_order_relaxed);
    const uint32_t free_ = RING - ring_count();
    if (count > free_) count = free_;
    for (unsigned int i = 0; i < count; i++) {
        ring[(h + i) & RING_MASK] = samples[i];
        if (samples[i]) { zeros = 0; quiet = 0; }
        else if (++zeros >= QUIET_RUN) quiet = 1;
    }
    atomic_store_explicit(&head, h + count, memory_order_release);
    written += count;
    return count;
}

/* fMSX asks how much room there is before it mixes. */
unsigned int audio_buffer_samples(void) { return running ? RING - ring_count() : 0; }
int  audio_ready(void) { return running; }
int  audio_pause(int on) { (void)on; return 0; }
unsigned long audio_samples_written(void) { return written; }
void audio_test_tone(int hz, int ms) { (void)hz; (void)ms; }

void audio_report(void)
{
    printf("audio: %s, ring %lu of %u, %lu samples, %lu empty periods while playing\n",
           running ? "PCM" : "off", (unsigned long)ring_count(), RING, (unsigned long)written,
           (unsigned long)underruns);
}

#endif
