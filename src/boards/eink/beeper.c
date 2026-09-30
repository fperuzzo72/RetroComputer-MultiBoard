/* beeper.c - beeper.h on the e-ink boards' buzzer.
 *
 * Both boards have a passive buzzer behind a transistor on one GPIO
 * (EINK_BEEPER_PIN) and nothing else that makes a sound. A frame's flips
 * are played by the RMT peripheral, which clocks out (duration, level)
 * pairs by itself: the emulation hands over a frame and gets on with the
 * next one, and the timing is the hardware's, to the microsecond.
 *
 * Frames are queued, a few deep. When the emulation runs ahead the extra
 * frame is dropped rather than letting the queue grow into lag; after
 * silence the first frame starts 10ms late, so that a frame arriving a
 * little late does not leave a gap. A frame of sound is 19.968ms and the
 * machine is paced to 20ms, so that lead wears away over a few seconds;
 * after that the line holds its level for the 32us between frames. A buzzer held on draws current and
 * says nothing, so a frame with no flips turns it off.
 */
#include "beeper.h"
#include "eink_board.h"

#include <stdio.h>
#include <string.h>

#include "driver/rmt_tx.h"
#include "freertos/FreeRTOS.h"

#define TICK_HZ      1000000             /* 1us */
#define BUFS         4
#define SYMS         640                  /* per frame: 1280 halves */
#define LEAD_US      10000

static rmt_channel_handle_t chan;
static rmt_encoder_handle_t enc;
static int failed;
static rmt_symbol_word_t buf[BUFS][SYMS];
static int next_buf;
static volatile int in_flight;
static int line_high;                    /* where the last frame left it */
static int playing;                      /* the last frame had sound */

static bool IRAM_ATTR done(rmt_channel_handle_t c, const rmt_tx_done_event_data_t *e, void *ctx)
{
    (void)c; (void)e; (void)ctx;
    __atomic_fetch_sub(&in_flight, 1, __ATOMIC_RELAXED);
    return false;
}

static int begin(void)
{
    if (chan) return 1;
    if (failed) return 0;
    rmt_tx_channel_config_t cfg = {
        .gpio_num = EINK_BEEPER_PIN,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = TICK_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = BUFS,
    };
    rmt_copy_encoder_config_t ecfg = {0};
    rmt_tx_event_callbacks_t cbs = { .on_trans_done = done };
    if (rmt_new_tx_channel(&cfg, &chan) != ESP_OK
        || rmt_new_copy_encoder(&ecfg, &enc) != ESP_OK
        || rmt_tx_register_event_callbacks(chan, &cbs, NULL) != ESP_OK
        || rmt_enable(chan) != ESP_OK) {
        printf("beeper: no RMT channel for GPIO%d, no sound\n", EINK_BEEPER_PIN);
        failed = 1;
        chan = NULL;
        return 0;
    }
    printf("beeper: GPIO%d\n", EINK_BEEPER_PIN);
    return 1;
}

/* Halves are packed two to a symbol. A duration of 0 ends a transmission,
 * so no half may be empty, and longer than 15 bits is split. */
static rmt_symbol_word_t *sym;
static int nsym, half;

static int put(uint32_t us, int level)
{
    while (us) {
        uint32_t d = us > 32767 ? 32767 : us;
        us -= d;
        if (!half) {
            if (nsym >= SYMS) return 0;
            sym[nsym].duration0 = d;
            sym[nsym].level0 = level;
            half = 1;
        } else {
            sym[nsym].duration1 = d;
            sym[nsym].level1 = level;
            nsym++;
            half = 0;
        }
    }
    return 1;
}

static void send(int eot)
{
    if (half) {                          /* odd: split the last half in two */
        rmt_symbol_word_t *s = &sym[nsym];
        if (s->duration0 < 2) s->duration0 = 2;
        s->duration1 = s->duration0 / 2;
        s->duration0 -= s->duration1;
        s->level1 = s->level0;
        nsym++;
        half = 0;
    }
    rmt_transmit_config_t tc = { .loop_count = 0 };
    tc.flags.eot_level = eot;
    tc.flags.queue_nonblocking = 1;
    __atomic_fetch_add(&in_flight, 1, __ATOMIC_RELAXED);
    if (rmt_transmit(chan, enc, sym, nsym * sizeof(rmt_symbol_word_t), &tc) != ESP_OK)
        __atomic_fetch_sub(&in_flight, 1, __ATOMIC_RELAXED);
    else
        next_buf = (next_buf + 1) % BUFS;
    line_high = eot;
}

void beeper_frame(int level, const uint32_t *edges, int n, uint32_t frame, uint32_t clock_hz)
{
    if (n == 0) {
        playing = 0;
        if (!line_high || !chan || in_flight >= BUFS - 1) return;
        sym = buf[next_buf]; nsym = 0; half = 0;
        put(100, 0);
        send(0);
        return;
    }
    if (!begin()) return;
    if (in_flight >= BUFS - 1) return;   /* running ahead: drop this frame */

    sym = buf[next_buf]; nsym = 0; half = 0;
    if (!playing) put(LEAD_US, 0);
    playing = 1;

    const uint32_t end = (uint32_t)((uint64_t)frame * TICK_HZ / clock_hz);
    uint32_t last = 0;
    for (int i = 0; i < n; i++) {
        uint32_t at = (uint32_t)((uint64_t)edges[i] * TICK_HZ / clock_hz);
        if (at <= last) at = last + 1;   /* two flips in one tick */
        if (at >= end) break;
        if (at > last && !put(at - last, level)) break;
        last = at;
        level = !level;
    }
    if (end > last) put(end - last, level);
    send(level);
}
