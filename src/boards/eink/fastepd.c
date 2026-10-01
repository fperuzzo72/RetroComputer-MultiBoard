/* fastepd.c - see fastepd.h.
 *
 * The scanning is Modos Smooth Graphics (PaperBoy's main/msg/msg.c,
 * Copyright 2026 Wenting Zhang, MIT; third_party_licenses/paperboy.txt):
 * the frame start sequence, the row latch, the 2-bit source codes (01
 * pushes a pixel black, 10 white, 00 leaves it) and the per-pixel state
 * byte with its two 3-bit counters. What is different here:
 *
 *   - Rows, not a window. PaperBoy drives one fixed rectangle, the Game
 *     Boy's. Here every row of the panel may be anything (a menu, the
 *     Mac, an MSX with its border), so a row is worked on only while its
 *     picture differs from what its pixels were last pushed to, or while
 *     some pixel in it is still being pushed. Every other row goes out as
 *     zeros, which costs the DMA time and nothing else.
 *
 *   - It stops. PaperBoy scans for ever. A pixel in an active matrix keeps
 *     the last voltage it was given until its row is scanned again, so
 *     after the last push one more scan of zeros goes out, and then the
 *     task sleeps until the next picture. It also gives the core back for
 *     a tick after every scan: it shares core 0 with touch, BLE and the
 *     sound, where PaperBoy has a core to itself.
 *
 *   - The whole state lives in PSRAM (two pixels a byte, 259kB), since the
 *     rows are all live; the DMA line buffers are internal.
 */
#include "fastepd.h"

#if EINK_FAST_PANEL   /* the PaperS3 only: env papers3-fast */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rom/ets_sys.h"

#include "BoardPaperS3Pins.h"

#define W        960
#define H        540
#define STRIDE   (W / 8)              /* picture bytes a row */
#define PAIRS    (W / 2)              /* state bytes a row */
#define LINE     (W / 4)              /* source bytes a row, 2 bits a pixel */
#define PAD      16
#define XCK      24000000

/* PaperBoy's names for the PaperS3's EPD lines, from freeink-sdk's. */
#define PIN_PWR_EN  PAPERS3_EP_OE     /* 45 */
#define PIN_BST_EN  PAPERS3_EP_PWR    /* 46 */
#define PIN_SDCE    PAPERS3_EP_SPH    /* 13 */
#define PIN_SDLE    PAPERS3_EP_LE     /* 15 */
#define PIN_SDCK    PAPERS3_EP_CL     /* 16 */
#define PIN_GDSP    PAPERS3_EP_SPV    /* 17 */
#define PIN_GDCK    PAPERS3_EP_CKV    /* 18 */

/* A pixel's state, two to a byte (MSG): count1[2:0] color1 count0[2:0]
 * color0, laid out as bits 7-5, 4-2 for the counts and 1, 0 for colour;
 * a count reaching 4 sets bit 7 (or 4) and the pixel is left alone. */
#define DONE_BOTH  0x90

static esp_lcd_i80_bus_handle_t bus;
static esp_lcd_panel_io_handle_t io;
static volatile bool dma_done = true;
static uint8_t *dma[2];
static int dma_cur;

static uint8_t *target;               /* 1 = black, panel order */
static uint8_t *state;
static volatile uint8_t row_dirty[H];
static uint8_t row_active[H];
static TaskHandle_t task;
static volatile bool clean_req;
static volatile bool paused;          /* `fe s`: the panel left as it is, for tests */

static bool flip_x, flip_y;

/* What the source drivers' shift register holds: a row of zeros, shifted
 * already, needs no shifting again. A run of rows that are left alone then
 * costs a gate step and a latch each, not a DMA transfer: the empty scan
 * went from 24ms to a few. The row is still held selected for zero_row_us
 * so that a pixel pushed in the last scan really is given its 0V. */
static bool sr_zero;
static int zero_row_us = 10;
static int scan_prio = 2;
static const uint8_t *last_pic;
static uint8_t rev8[256];

/* for `fe` */
static volatile unsigned long frames, busy_rows, last_frame_us, cleans;

static bool IRAM_ATTR on_done(esp_lcd_panel_io_handle_t p, esp_lcd_panel_io_event_data_t *e, void *c)
{
    (void)p; (void)e; (void)c;
    dma_done = true;
    return false;
}

static void IRAM_ATTR send_row(const uint8_t *data)
{
    while (!dma_done) {}
    gpio_set_level(PIN_GDCK, 0);
    gpio_set_level(PIN_SDLE, 1);
    gpio_set_level(PIN_SDLE, 0);
    gpio_set_level(PIN_GDCK, 1);
    dma_done = false;
    sr_zero = false;
    esp_lcd_panel_io_tx_color(io, -1, data, LINE + PAD);
}

/* A row left alone: zeros, shifted only if the register does not hold
 * them already. */
static void IRAM_ATTR zero_row(uint8_t *buf)
{
    if (!sr_zero) {
        memset(buf, 0, LINE + PAD);
        send_row(buf);
        sr_zero = true;
        return;
    }
    while (!dma_done) {}
    gpio_set_level(PIN_GDCK, 0);
    gpio_set_level(PIN_SDLE, 1);
    gpio_set_level(PIN_SDLE, 0);
    gpio_set_level(PIN_GDCK, 1);
    ets_delay_us(zero_row_us);
}

static void frame_start(void)
{
    gpio_set_level(PIN_GDCK, 1);
    ets_delay_us(7);
    gpio_set_level(PIN_GDSP, 0);
    ets_delay_us(10);
    gpio_set_level(PIN_GDCK, 0);
    gpio_set_level(PIN_GDCK, 1);
    ets_delay_us(8);
    gpio_set_level(PIN_GDSP, 1);
    ets_delay_us(10);
    gpio_set_level(PIN_GDCK, 0);
    for (int i = 0; i < 2; i++) {
        gpio_set_level(PIN_GDCK, 1);
        gpio_set_level(PIN_GDCK, 0);
    }
}

static void frame_end(void)
{
    zero_row(dma[dma_cur]);           /* latches the last real row */
    while (!dma_done) {}
}

/* Every pixel pushed one way (code 01 black, 10 white) for one scan. */
static void push_all(uint8_t code)
{
    const uint8_t b = (uint8_t)(code | code << 2 | code << 4 | code << 6);
    memset(dma[0], b, LINE);
    memset(dma[0] + LINE, 0, PAD);
    frame_start();
    for (int r = 0; r < H; r++) send_row(dma[0]);
    dma_cur = 1;
    frame_end();
}

static void nop_scan(void)
{
    memset(dma[0], 0, LINE + PAD);
    frame_start();
    for (int r = 0; r < H; r++) send_row(dma[0]);
    dma_cur = 1;
    frame_end();
}

/* One row of source codes from the picture and the state, updating the
 * state. Returns whether any pixel in it is still being pushed. */
/* The four state bytes of eight pixels that are at rest in the colours of
 * picture byte b, as one little-endian word: most of a row that is being
 * worked on has not changed, and comparing a word skips them. */
static uint32_t at_rest[256];

static bool IRAM_ATTR work_row(int r, uint8_t *out)
{
    static const uint8_t reset_mask[4] = { 0xfc, 0xe0, 0x1c, 0x00 };
    const uint8_t *in = target + r * STRIDE;
    uint8_t *st = state + r * PAIRS;
    uint8_t live = 0;
    for (int j = 0; j < STRIDE; j++) {
        uint8_t px = in[j];
        if (*(const uint32_t *)st == at_rest[px]) {
            out[0] = out[1] = 0;
            out += 2;
            st += 4;
            continue;
        }
        for (int k = 0; k < 2; k++) {
            uint8_t o = 0;
            for (int l = 0; l < 2; l++) {
                uint8_t s = *st;
                const uint8_t dir = px >> 6;
                s &= reset_mask[(s ^ dir) & 3];
                s |= dir;
                o <<= 4;
                o |= (s & 0x80) ? 0 : (dir & 2) ? 0x4 : 0x8;
                o |= (s & 0x10) ? 0 : (dir & 1) ? 0x1 : 0x2;
                s += ((uint8_t)~s >> 2) & 0x24;
                live |= (uint8_t)~s & DONE_BOTH;
                *st++ = s;
                px <<= 2;
            }
            *out++ = o;
        }
    }
    return live != 0;
}

static void do_clean(void)
{
    for (int round = 0; round < 2; round++) {
        for (int i = 0; i < 8; i++) push_all(0x1);     /* black */
        for (int i = 0; i < 4; i++) nop_scan();
        for (int i = 0; i < 8; i++) push_all(0x2);     /* white */
        for (int i = 0; i < 4; i++) nop_scan();
    }
    /* Everything is white and at rest; what is black gets pushed next. */
    memset(state, DONE_BOTH, (size_t)PAIRS * H);
    memset(row_active, 0, sizeof row_active);
    for (int r = 0; r < H; r++) row_dirty[r] = 1;
    cleans++;
}

static void scan_task(void *arg)
{
    (void)arg;
    bool tail = false;                 /* one scan of zeros after a push */
    for (;;) {
        if (clean_req) { clean_req = false; do_clean(); }

        bool work = false;
        for (int r = 0; r < H && !work; r++) work = row_dirty[r] || row_active[r];
        if (paused && !tail) work = false;
        if (!work && !tail) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }

        const int64_t t0 = esp_timer_get_time();
        unsigned long rows = 0;
        bool pushed = false;
        frame_start();
        for (int r = 0; r < H; r++) {
            uint8_t *buf = dma[dma_cur];
            if (row_dirty[r] || row_active[r]) {
                row_dirty[r] = 0;
                row_active[r] = work_row(r, buf);
                memset(buf + LINE, 0, PAD);
                rows++;
                pushed = true;
                send_row(buf);
                dma_cur ^= 1;
            } else {
                zero_row(buf);
                dma_cur ^= 1;
            }
        }
        frame_end();
        tail = pushed;
        frames++;
        busy_rows = rows;
        last_frame_us = (unsigned long)(esp_timer_get_time() - t0);
        vTaskDelay(1);                 /* touch, BLE and the sound share this core */
    }
}

int fastepd_begin(void)
{
    gpio_config_t cfg = {};
    cfg.mode = GPIO_MODE_OUTPUT;
    cfg.pin_bit_mask = (1ull << PIN_PWR_EN) | (1ull << PIN_BST_EN) | (1ull << PIN_SDLE)
                     | (1ull << PIN_GDSP) | (1ull << PIN_GDCK) | (1ull << PAPERS3_PWROFF_PULSE);
    if (gpio_config(&cfg) != ESP_OK) return -1;
    gpio_set_level(PAPERS3_PWROFF_PULSE, 0);

    esp_lcd_i80_bus_config_t bc = {};
    bc.dc_gpio_num = -1;
    bc.wr_gpio_num = PIN_SDCK;
    bc.clk_src = LCD_CLK_SRC_PLL160M;
    const int data[8] = { PAPERS3_EP_D0, PAPERS3_EP_D1, PAPERS3_EP_D2, PAPERS3_EP_D3,
                          PAPERS3_EP_D4, PAPERS3_EP_D5, PAPERS3_EP_D6, PAPERS3_EP_D7 };
    for (int i = 0; i < 8; i++) bc.data_gpio_nums[i] = data[i];
    bc.bus_width = 8;
    bc.max_transfer_bytes = LINE + PAD + 16;
    bc.dma_burst_size = 32;
    if (esp_lcd_new_i80_bus(&bc, &bus) != ESP_OK) {
        bc.dc_gpio_num = 49;          /* PaperBoy's: a pin the S3 does not have */
        if (esp_lcd_new_i80_bus(&bc, &bus) != ESP_OK) { printf("fastepd: no i80 bus\n"); return -1; }
    }

    esp_lcd_panel_io_i80_config_t ic = {};
    ic.cs_gpio_num = PIN_SDCE;
    ic.pclk_hz = XCK;
    ic.trans_queue_depth = 4;
    ic.on_color_trans_done = on_done;
    ic.lcd_cmd_bits = 8;
    ic.lcd_param_bits = 8;
    ic.dc_levels.dc_data_level = 1;
    if (esp_lcd_new_panel_io_i80(bus, &ic, &io) != ESP_OK) { printf("fastepd: no panel io\n"); return -1; }

    for (int i = 0; i < 2; i++)
        dma[i] = (uint8_t *)heap_caps_aligned_alloc(16, LINE + PAD + 16, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    target = (uint8_t *)heap_caps_malloc((size_t)STRIDE * H, MALLOC_CAP_SPIRAM);
    state = (uint8_t *)heap_caps_aligned_alloc(4, (size_t)PAIRS * H, MALLOC_CAP_SPIRAM);
    if (!dma[0] || !dma[1] || !target || !state) { printf("fastepd: out of memory\n"); return -1; }
    memset(target, 0, (size_t)STRIDE * H);
    memset(state, DONE_BOTH, (size_t)PAIRS * H);
    for (int b = 0; b < 256; b++)
        at_rest[b] = (uint32_t)(DONE_BOTH | ((b >> 6) & 3))
                   | (uint32_t)(DONE_BOTH | ((b >> 4) & 3)) << 8
                   | (uint32_t)(DONE_BOTH | ((b >> 2) & 3)) << 16
                   | (uint32_t)(DONE_BOTH | (b & 3)) << 24;
    for (int i = 0; i < 256; i++) {
        uint8_t v = 0;
        for (int b = 0; b < 8; b++) if (i & (1 << b)) v |= (uint8_t)(0x80 >> b);
        rev8[i] = v;
    }

    /* power: PaperBoy's order, 100us apart */
    gpio_set_level(PIN_PWR_EN, 1);
    ets_delay_us(100);
    gpio_set_level(PIN_BST_EN, 1);
    ets_delay_us(100);
    gpio_set_level(PIN_GDCK, 1);
    gpio_set_level(PIN_GDSP, 1);

    clean_req = true;
    xTaskCreatePinnedToCore(scan_task, "fastepd", 4096, NULL, scan_prio, &task, 0);
    printf("fastepd: panel driven directly, %dx%d at %d MHz\n", W, H, XCK / 1000000);
    return 0;
}

void fastepd_show(const uint8_t *pic)
{
    static uint8_t row[STRIDE];
    last_pic = pic;
    bool any = false;
    for (int r = 0; r < H; r++) {
        const uint8_t *src = pic + (flip_y ? H - 1 - r : r) * STRIDE;
        for (int j = 0; j < STRIDE; j++) {
            const uint8_t b = flip_x ? rev8[src[STRIDE - 1 - j]] : src[j];
            row[j] = (uint8_t)~b;              /* canvas 1 = white, here 1 = black */
        }
        uint8_t *t = target + r * STRIDE;
        if (memcmp(t, row, STRIDE)) {
            memcpy(t, row, STRIDE);
            row_dirty[r] = 1;
            any = true;
        }
    }
    if (any && task) xTaskNotifyGive(task);
}

void fastepd_clean(void)
{
    clean_req = true;
    if (task) xTaskNotifyGive(task);
}

void fastepd_command(const char *a)
{
    while (*a == ' ') a++;
    if (*a == 'x') flip_x = !flip_x;
    else if (*a == 'y') flip_y = !flip_y;
    else if (*a == 'c') fastepd_clean();
    else if (*a == 's') { paused = !paused; if (!paused && task) xTaskNotifyGive(task); }
    else if (*a == 'z') { int n = atoi(a + 1); if (n >= 1 && n <= 100) zero_row_us = n; }
    else if (*a == 'P') { int n = atoi(a + 1); if (n >= 1 && n <= 10) { scan_prio = n; vTaskPrioritySet(task, n); } }
    printf("fastepd: %lu scans, last %lu rows in %lu us, %lu cleans, flip x %d y %d, "
           "zero row %d us, priority %d%s\n",
           frames, busy_rows, last_frame_us, cleans, flip_x, flip_y, zero_row_us, scan_prio,
           paused ? ", PAUSED" : "");
    if ((*a == 'x' || *a == 'y') && last_pic) {
        /* the whole picture again, the new way round, on a clean panel */
        fastepd_clean();
        fastepd_show(last_pic);
    }
}

#endif /* EINK_FAST_PANEL */
