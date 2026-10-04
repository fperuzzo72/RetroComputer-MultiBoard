/* panel.cpp - the e-ink panel, EINK_PANEL_W x EINK_PANEL_H (eink_board.h).
 *
 * Everything that wants to be seen draws into one canvas, in the panel's
 * own frame (canvas.h): the Mac's picture, scaled from its framebuffer
 * here; the 8-bit machines' pictures, put there band by band by display8.c;
 * the menus, by chooser.cpp. On top of that goes an optional message box
 * (the BLE pairing code), and the result goes to the glass when it differs
 * from what the glass already shows. Nothing that draws ever waits for the
 * panel: a refresh takes as long as the waveform takes, the picture is
 * always the latest, and some intermediate frames are simply never shown.
 *
 * Fast refreshes leave ghosts, so every so often, and whenever a button
 * asks, a full one clears them.
 *
 * The picture is scaled to fill the panel's height (picture.h): a Mac
 * pixel for a panel pixel was too small to read.
 */
#include "panel_eink.h"

#include <Arduino.h>
#include <EInkDisplay.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdlib.h>
#include <string.h>

#include "display_mono.h"
#include "picture.h"
#include "canvas.h"
#include "ui.h"

#if EINK_FAST_PANEL
/* The PaperS3 driven directly (fastepd.c), no waveform: a changed picture
 * is handed over and on the glass within tens of milliseconds, so this
 * never waits and the ghost cleaning below is left to a hold or a button.
 * Counting "fast refreshes" towards a clean made no sense at ~60 a
 * second. */
#include "fastepd.h"
static uint8_t *frame;
#else
static EInkDisplay epd(-1, -1, -1, -1, -1, -1);   /* pins come from the board profile */
#endif

static const uint8_t *mono_fb;
static int mono_w, mono_h;
static eink_view view;
static volatile unsigned long mono_vsyncs;

static unsigned long refreshes, full_refreshes, last_refresh_ms, total_refresh_ms, last_full_ms;
static int fast_since_full;
static unsigned long last_change_ms;
static volatile bool full_requested;

/* When to clean the ghosts. A full refresh is the only thing that does: it
 * is the one mode the Paper Mono driver treats as corrective. HALF_REFRESH
 * was used here at first and cleaned nothing; the owner saw the ghosting
 * pile up and the button do nothing about it.
 *
 * A full refresh flashes, so it waits for a pause: once the picture has
 * sat still for IDLE_CLEAN_MS after at least IDLE_CLEAN_AFTER fast ones,
 * it is cleaned while nobody is moving anything. A picture that never sits
 * still is cleaned anyway after FAST_PER_FULL. Starting values. */
static int IDLE_CLEAN_AFTER = 5;
static unsigned long IDLE_CLEAN_MS = 3000;
static int FAST_PER_FULL = 200;

/* `c AFTER MS` on the console changes the first two while the owner looks
 * at the glass: tuning this by reflashing costs a cycle per guess. */
void panel_set_idle_clean(int after, unsigned long ms)
{
    IDLE_CLEAN_AFTER = after < 1 ? 1 : after;
    IDLE_CLEAN_MS = ms;
}

/* `c AFTER MS CAP`: also the cap, for a game that never sits still and so
 * is only ever cleaned by it (the Paper Mono darkening in The Goonies,
 * 2026-10-01). */
void panel_set_clean_cap(int cap) { FAST_PER_FULL = cap < 2 ? 2 : cap; }
int  panel_get_clean_cap(void) { return FAST_PER_FULL; }

void panel_get_idle_clean(int *after, unsigned long *ms)
{
    *after = IDLE_CLEAN_AFTER;
    *ms = IDLE_CLEAN_MS;
}

/* And "full" is not the flashing black-white-black clean other panels do:
 * this driver has none, and its corrective mode drives every pixel through
 * the ordinary waveform instead. Under the Mac's half-black desktop pattern
 * that reads as the whole screen blinking. The first values here (10 fast,
 * 1.5s idle, 60 cap) cleaned every ~20s, at nearly every pause, and the
 * owner found it blinked far too much; the next (30 fast, 4s) never came
 * at all during a Mac's boot, whose ~20 fast refreshes darkened the
 * desktop pattern step by step (owner, 2026-09-29: "vai escurecendo a
 * medida em que vai desenhando"). Now 5 fast, 3s still; `c` tunes it. */
static unsigned long cleans_button, cleans_idle, cleans_cap, caret_skips;

/* Window refreshes (only the changed rectangle, through the driver's
 * displayWindow) were tried on 2026-09-28 and taken out. Measured on the
 * device: 2.3s a refresh against ~400ms for the whole panel, the input
 * task blocked for up to 2s behind them, and the owner saw the pointer
 * leap and the screen darken. Do not bring them back without measuring.
 *
 * Putting the controller to sleep between refreshes was tried too, and
 * taken out on 2026-09-29. It was meant to stop a still screen darkening,
 * and did not: the owner saw it darken with the controller asleep five
 * times over. And waking it pulses the panel's reset through the IOE1
 * expander, on the I2C bus the touch panel and the power chip share, from
 * the board task while the input task reads touch on the same bus: touch
 * died for a whole session, the input task hung in its first read with
 * the panel never refreshing, and the refresh button froze the board.
 * Awake, the controller does not touch I2C at all. Whatever darkens the
 * screen, it is not this; a full refresh clears it.
 *
 * i2c_lock stays: every use of that bus from here and from the input task
 * takes it, so they cannot meet again whatever else comes to use it. */
static bool controller_awake = true;
static unsigned long last_refresh_end;
static unsigned long controller_sleeps;

static SemaphoreHandle_t i2c_lock;

void panel_set_i2c_lock(SemaphoreHandle_t lock) { i2c_lock = lock; }


/* The menus draw the whole canvas at once; copied halfway through, the
 * first boot showed the title and not the choices. */
static SemaphoreHandle_t canvas_lock;
void panel_canvas_lock(void) { if (canvas_lock) xSemaphoreTake(canvas_lock, portMAX_DELAY); }
void panel_canvas_unlock(void) { if (canvas_lock) xSemaphoreGive(canvas_lock); }

/* For `s`: why panel_service came back without refreshing. */
static unsigned long svc_calls, svc_no_buffer, svc_unchanged;

/* The Mac's text caret blinks about twice a second, and every blink is a
 * refresh: in a text editor the panel never rested. A change that is only
 * a one-pixel-wide vertical line, one bit in one byte column, is taken to
 * be the caret and not shown. Anything typed changes more than that and
 * goes out at once, blink state and all. */
static bool caret_only(const uint8_t *a, const uint8_t *b, int stride, int h)
{
    int col = -1, first = -1, last = -1;
    uint8_t bit = 0;
    for (int y = 0; y < h; y++) {
        const uint8_t *ra = a + y * stride, *rb = b + y * stride;
        for (int x = 0; x < stride; x++) {
            const uint8_t d = (uint8_t)(ra[x] ^ rb[x]);
            if (!d) continue;
            if (d & (d - 1)) return false;            /* more than one pixel */
            if (col < 0) { col = x; bit = d; first = y; }
            else if (x != col || d != bit) return false;
            last = y;
        }
    }
    return col >= 0 && last - first < 24;
}

/* The canvas everything draws into, the frame last sent to the glass, and
 * the Mac's framebuffer as last drawn onto the canvas (for its caret test). */
static uint8_t *canvas;
static uint8_t *glass;
static uint8_t *mono_shown;

/* The message box over the picture, if any. */
static char msg1[64], msg2[32];
static volatile bool msg_on;

extern "C" void display_mono_attach(const uint8_t *fb, int w, int h)
{
    mono_w = w;
    mono_h = h;
    view = eink_view_make(CANVAS_W, CANVAS_H, w, h);
    free(mono_shown);
    mono_shown = (uint8_t *)calloc((size_t)(w / 8) * h, 1);
    mono_fb = fb;
}

const eink_view *panel_view(void) { return mono_fb ? &view : NULL; }

extern "C" void display_mono_vsync(void) { mono_vsyncs = mono_vsyncs + 1; }

const uint8_t *panel_mono_picture(int *w, int *h)
{
    *w = mono_w;
    *h = mono_h;
    return mono_fb;
}

uint8_t *panel_canvas(void) { return canvas; }

void panel_message(const char *line1, const char *line2)
{
    strncpy(msg1, line1 ? line1 : "", sizeof msg1 - 1);
    strncpy(msg2, line2 ? line2 : "", sizeof msg2 - 1);
    msg_on = true;
}

void panel_message_clear(void) { msg_on = false; }

void panel_begin(void)
{
#if EINK_FAST_PANEL
    frame = (uint8_t *)heap_caps_malloc(CANVAS_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (frame) memset(frame, 0xFF, CANVAS_BYTES);
    if (fastepd_begin() != 0) Serial.println("panel: the direct drive would not start");
#else
    epd.begin();
    /* Everything here draws for EINK_PANEL_W x EINK_PANEL_H; if freeink-sdk
     * disagrees about the panel, say so before drawing garbage. */
    if (epd.getDisplayWidth() != CANVAS_W || epd.getDisplayHeight() != CANVAS_H)
        Serial.printf("panel: freeink-sdk says %ux%u, this build draws %dx%d - wrong board?\n",
                      epd.getDisplayWidth(), epd.getDisplayHeight(), CANVAS_W, CANVAS_H);
    epd.clearScreen(0xFF);
    epd.displayBuffer(EInkDisplay::FULL_REFRESH);
#endif
    canvas = (uint8_t *)heap_caps_malloc(CANVAS_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    /* PSRAM too: it is only compared and copied, and 48kB of internal RAM
     * is what the BLE keyboard needs once it connects (26kB was all that
     * was left with the MSX running, 2026-09-29). */
    glass = (uint8_t *)heap_caps_malloc(CANVAS_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (canvas) memset(canvas, 0xFF, CANVAS_BYTES);
    if (glass) memset(glass, 0xFF, CANVAS_BYTES);
    canvas_lock = xSemaphoreCreateMutex();
    if (!canvas || !glass)
        Serial.printf("panel: buffers FAILED (canvas %p, glass %p)\n", (void *)canvas, (void *)glass);
}

void panel_diag(void)
{
    Serial.printf("panel: canvas %p glass %p, service %lu calls, %lu without buffers, %lu unchanged, controller %s\n",
                  (void *)canvas, (void *)glass, svc_calls, svc_no_buffer, svc_unchanged,
                  controller_awake ? "awake" : "asleep");
}

void panel_request_full(void) { full_requested = true; }

/* The Mac's picture onto the canvas when it has changed, other than by the
 * caret blinking: scaled, centred, the right way up (picture.h). */
static bool mono_to_canvas(void)
{
    if (!mono_fb || !mono_shown) return false;
    const size_t n = (size_t)(mono_w / 8) * mono_h;
    if (memcmp(mono_shown, mono_fb, n) == 0) return false;
    if (caret_only(mono_shown, mono_fb, mono_w / 8, mono_h)) {
        caret_skips++;
        return false;
    }
    eink_draw(&view, canvas, mono_fb);
    memcpy(mono_shown, mono_fb, n);
    return true;
}

bool panel_service(void)
{
    svc_calls++;
    if (!canvas || !glass) { svc_no_buffer++; return false; }
    mono_to_canvas();

    /* canvas and message into the frame the controller is sent */
#if EINK_FAST_PANEL
    uint8_t *out = frame;
    if (!out) { svc_no_buffer++; return false; }
#else
    uint8_t *out = epd.getFrameBuffer();
#endif
    panel_canvas_lock();
    memcpy(out, canvas, CANVAS_BYTES);
    panel_canvas_unlock();
    if (msg_on) ui_draw_message(out, msg1, msg2[0] ? msg2 : NULL);

    const unsigned long now = millis();
    const bool changed = memcmp(out, glass, CANVAS_BYTES) != 0;
    if (changed) last_change_ms = now;
    const bool idle_clean = !changed && fast_since_full >= IDLE_CLEAN_AFTER &&
                            now - last_change_ms >= IDLE_CLEAN_MS;
#if EINK_FAST_PANEL
    const bool full = full_requested;
#else
    const bool full = full_requested || idle_clean || fast_since_full >= FAST_PER_FULL;
#endif
    if (!full && !changed) {
        svc_unchanged++;
        return false;
    }

    memcpy(glass, out, CANVAS_BYTES);
    const unsigned long t0 = millis();
#if EINK_FAST_PANEL
    if (full_requested) { cleans_button++; fastepd_clean(); full_requested = false; full_refreshes++; }
    fastepd_show(out);
    last_refresh_ms = millis() - t0;
    total_refresh_ms += last_refresh_ms;
    refreshes++;
    return true;
#else
    if (full) {
        if (full_requested) cleans_button++;
        else if (idle_clean) cleans_idle++;
        else cleans_cap++;
        epd.displayBuffer(EInkDisplay::FULL_REFRESH);
        full_requested = false;
        fast_since_full = 0;
        full_refreshes++;
        last_full_ms = millis() - t0;
    } else {
        epd.displayBuffer(EInkDisplay::FAST_REFRESH);
        fast_since_full++;
    }
    last_refresh_ms = millis() - t0;
    last_refresh_end = millis();
    controller_awake = true;
    total_refresh_ms += last_refresh_ms;
    refreshes++;
    return true;
#endif
}

unsigned long panel_last_full_ms(void) { return last_full_ms; }

unsigned long panel_controller_sleeps(void) { return controller_sleeps; }

void panel_clean_stats(unsigned long *button, unsigned long *idle, unsigned long *cap, unsigned long *carets)
{
    *button = cleans_button;
    *idle = cleans_idle;
    *cap = cleans_cap;
    *carets = caret_skips;
}

void panel_stats(unsigned long *count, unsigned long *fulls, unsigned long *last_ms, unsigned long *avg_ms)
{
    *count = refreshes;
    *fulls = full_refreshes;
    *last_ms = last_refresh_ms;
    *avg_ms = refreshes ? total_refresh_ms / refreshes : 0;
}
