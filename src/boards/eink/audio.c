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
 * The duty does not sit at 50% with the sound swinging round it, as
 * PaperBoy's does. Measured on the PaperS3 (2026-10-01): the direct-drive
 * panel makes the supply ripple, and the buzzer turns the ripple into a
 * hiss in proportion to the current it carries; held at a fixed 50% it
 * hissed, at 10% hardly. So the centre follows the sound's own level, with
 * 2ms of lookahead so it is up before a loud note arrives and a 60ms
 * release: between notes and in quiet passages the buzzer carries almost
 * nothing, and when it is loud the music covers the rest. Peaks reach the
 * same duty as before. The ring therefore holds duties, not samples.
 *
 * Silence is the buzzer off. The other mode (audio_mode
 * AUDIO_MODE_VOICE) leaves fMSX's sound off and msx_beeper.c plays the
 * loudest PSG voice as a square wave through beeper.h; `snd` on the serial
 * console switches, remembered in NVS.
 */
#include "audio.h"

#ifndef ARDUINO

/* The host tools: no sound. */
unsigned int audio_init(unsigned int rate) { (void)rate; return 0; }
unsigned int audio_start_push(unsigned int rate) { (void)rate; return 0; }
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
void audio_mute(int on) { (void)on; }
void audio_hold(int pct) { (void)pct; }

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
#define LOOK       64                  /* lookahead, 2ms */

static int16_t DRAM_ATTR ring[RING];
static _Atomic uint32_t head, tail;
static volatile int running;
static volatile unsigned long written;
static volatile int32_t centre;        /* the duty the sound swings about, now */
static volatile unsigned long underruns;  /* periods with the ring empty while playing */
static volatile int muted;             /* `snd mudo`: the buzzer still, the rest running */
static volatile int held;              /* `snd dc [pct]`: a fixed duty, no samples, for tests */
static volatile uint32_t held_duty = DUTY_MID;
static intr_handle_t intr;

static IRAM_ATTR void isr(void *arg)
{
    (void)arg;
    if (!(LEDC.int_st.val & OVF_MASK)) return;
    LEDC.int_clr.val = OVF_MASK;
    uint32_t t = atomic_load_explicit(&tail, memory_order_relaxed);
    uint32_t h = atomic_load_explicit(&head, memory_order_acquire);
    uint32_t duty = 0;
    if (h == t) { if (centre) underruns++; }
    else {
        duty = (uint16_t)ring[t & RING_MASK];
        atomic_store_explicit(&tail, t + 1, memory_order_release);
        if (muted) duty = 0;
    }
    if (held) duty = held_duty;
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

static volatile int pushed;             /* the machine writes; no pump */

static unsigned int start(unsigned int rate, int pull)
{
    if (running) return rate;

    ledc_timer_config_t tc = {};
    tc.speed_mode = MODE;
    tc.timer_num = TIMER;
    tc.duty_resolution = (ledc_timer_bit_t)DUTY_BITS;
    tc.freq_hz = rate;
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
    if (pull) xTaskCreatePinnedToCore(pump, "audio", 4096, NULL, 4, NULL, 0);
    printf("audio: PCM on GPIO%d, %lu Hz, %s\n", EINK_BEEPER_PIN,
           (unsigned long)ledc_get_freq(MODE, TIMER), pull ? "pulled" : "pushed");
    return rate;
}

unsigned int audio_init(unsigned int rate)
{
    (void)rate;
    if (audio_mode() != AUDIO_MODE_PCM) return 0;
    return start(RATE, 1);
}

/* A machine that makes its own frames of samples (the C64's SID, a frame
 * every 1/50s) and writes them. The LEDC cannot hit every rate exactly
 * (32750Hz asked, ~32786 got), so audio_write keeps the ring near TARGET
 * by dropping or repeating one sample when it strays, which nobody
 * hears; it starts with TARGET of silence so it is not empty from the
 * first frame. Not `snd off`; `snd voz` is the MSX's alone and plays PCM
 * here. */
unsigned int audio_start_push(unsigned int rate)
{
    if (audio_mode() == AUDIO_MODE_OFF) return 0;
    if (!start(rate, 0)) return 0;
    pushed = 1;
    static const short zeros[256];
    for (unsigned i = 0; i < TARGET; i += 256) audio_write(zeros, 256);
    return rate;
}

void audio_shutdown(void) { running = 0; }

unsigned int audio_write(const short *samples, unsigned int count)
{
    /* pushed: keep near TARGET, see audio_start_push */
    static int nudging;
    if (pushed && !nudging && count > 1) {
        const uint32_t have = ring_count();
        if (have > TARGET + 512) { samples++; count--; }
        else if (have < TARGET - 512) { nudging = 1; audio_write(samples, 1); nudging = 0; }
    }
    static int16_t look[LOOK];         /* the next LOOK samples, in duty units */
    static int pos, peak, peak_age;
    static int32_t cq;                 /* the centre, in 1/65536 of a duty step */
    uint32_t h = atomic_load_explicit(&head, memory_order_relaxed);
    const uint32_t free_ = RING - ring_count();
    if (count > free_) count = free_;
    for (unsigned int i = 0; i < count; i++) {
        const int x = samples[i] >> (16 - DUTY_BITS);        /* -512..511 */
        const int out = look[pos];
        look[pos] = (int16_t)x;
        pos = (pos + 1) % LOOK;

        /* the loudest of the samples about to go out */
        const int ax = x < 0 ? -x : x;
        if (ax >= peak) { peak = ax; peak_age = 0; }
        else if (++peak_age >= LOOK) {
            peak = 0;
            for (int k = 0; k < LOOK; k++) {
                const int a = look[k] < 0 ? -look[k] : look[k];
                if (a > peak) peak = a;
            }
            peak_age = 0;
        }
        const int32_t want = (int32_t)(peak ? peak + 4 : 0) << 16;
        if (want > cq) cq += ((want - cq) >> 3) + 1;       /* up within the lookahead */
        else cq -= (cq - want) >> 11;                      /* down over ~60ms */

        const int32_t c = (cq + 0x8000) >> 16;
        int32_t d = c ? c + out : 0;
        if (d < 0) d = 0;
        if (d > (1 << DUTY_BITS) - 1) d = (1 << DUTY_BITS) - 1;
        ring[(h + i) & RING_MASK] = (int16_t)d;
        centre = c;
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

void audio_mute(int on) { muted = on == 1; held = on == 2; }
void audio_hold(int pct) { held_duty = (uint32_t)((1 << DUTY_BITS) * pct / 100); held = 1; muted = 0; }

void audio_report(void)
{
    printf("audio: %s%s, ring %lu of %u, %lu samples, %lu empty periods while playing\n",
           running ? "PCM" : "off", muted ? " (muted)" : held ? " (held at a fixed duty)" : "", (unsigned long)ring_count(), RING, (unsigned long)written,
           (unsigned long)underruns);
}

#endif
