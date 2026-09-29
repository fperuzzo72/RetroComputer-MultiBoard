/* panel.cpp - the Paper Mono's 800x480 e-ink panel, for a 1-bit machine.
 *
 * The machine draws into its own framebuffer as fast as it likes and never
 * waits for the panel. This side looks at that framebuffer on its own
 * schedule, and when it differs from what the glass shows, copies it over
 * and refreshes. A refresh takes as long as the waveform takes, whatever
 * the machine does meanwhile, so the picture is always the machine's
 * latest and some intermediate frames are simply never shown.
 *
 * Fast refreshes leave ghosts, so every so often, and whenever a button
 * asks, a full one clears them.
 *
 * The picture is scaled to fill the panel's height (picture.h): a Mac
 * pixel for a panel pixel was too small to read.
 */
#include "panel_papermono.h"

#include <Arduino.h>
#include <EInkDisplay.h>
#include <string.h>

#include "display_mono.h"
#include "picture.h"

static EInkDisplay epd(-1, -1, -1, -1, -1, -1);   /* pins come from the board profile */

static const uint8_t *mono_fb;
static int mono_w, mono_h;
static papermono_view view;
static volatile unsigned long mono_vsyncs;

/* What is on the glass, in the machine's own polarity, for the comparison. */
static uint8_t *shown;

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
static const int IDLE_CLEAN_AFTER = 30;
static const unsigned long IDLE_CLEAN_MS = 4000;
static const int FAST_PER_FULL = 200;

/* And "full" is not the flashing black-white-black clean other panels do:
 * this driver has none, and its corrective mode drives every pixel through
 * the ordinary waveform instead. Under the Mac's half-black desktop pattern
 * that reads as the whole screen blinking. The first values here (10 fast,
 * 1.5s idle, 60 cap) cleaned every ~20s, at nearly every pause, and the
 * owner found it blinked far too much. */
static unsigned long cleans_button, cleans_idle, cleans_cap, caret_skips;

/* Window refreshes (only the changed rectangle, through the driver's
 * displayWindow) were tried on 2026-09-28 and taken out. Measured on the
 * device: 2.3s a refresh against ~400ms for the whole panel, the input
 * task blocked for up to 2s behind them, and the owner saw the pointer
 * leap and the screen darken. Do not bring them back without measuring.
 *
 * Between refreshes the controller is put to sleep. Left awake it keeps
 * the panel's drive rails up, and a still picture slowly darkened: the
 * owner saw it with no refresh at all happening (the count did not move).
 * The driver rebuilds everything from its own model on waking, ~40ms. */
static bool controller_awake;
static unsigned long last_refresh_end;
static const unsigned long CONTROLLER_IDLE_MS = 1000;
static unsigned long controller_sleeps;

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

extern "C" void display_mono_attach(const uint8_t *fb, int w, int h)
{
    mono_w = w;
    mono_h = h;
    view = papermono_view_make(EInkDisplay::DISPLAY_WIDTH, EInkDisplay::DISPLAY_HEIGHT, w, h);
    mono_fb = fb;
}

const papermono_view *panel_view(void) { return mono_fb ? &view : NULL; }

extern "C" void display_mono_vsync(void) { mono_vsyncs = mono_vsyncs + 1; }

const uint8_t *panel_mono_picture(int *w, int *h)
{
    *w = mono_w;
    *h = mono_h;
    return mono_fb;
}

void panel_begin(void)
{
    epd.begin();
    epd.clearScreen(0xFF);
    epd.displayBuffer(EInkDisplay::FULL_REFRESH);
    shown = (uint8_t *)heap_caps_malloc(EInkDisplay::BUFFER_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (shown) memset(shown, 0, EInkDisplay::BUFFER_SIZE);
}

void panel_request_full(void) { full_requested = true; }

/* The machine's picture into the panel's framebuffer: scaled, centred and
 * the right way up for how the device is held (picture.h). */
static void copy_in(void)
{
    papermono_draw(&view, epd.getFrameBuffer(), mono_fb);
    memcpy(shown, mono_fb, (size_t)(mono_w / 8) * mono_h);
}

bool panel_service(void)
{
    if (!mono_fb || !shown) return false;
    const size_t n = (size_t)(mono_w / 8) * mono_h;
    const unsigned long now = millis();
    bool changed = memcmp(shown, mono_fb, n) != 0;
    if (changed && caret_only(shown, mono_fb, mono_w / 8, mono_h)) {
        changed = false;
        caret_skips++;
    }
    if (changed) last_change_ms = now;
    const bool idle_clean = !changed && fast_since_full >= IDLE_CLEAN_AFTER &&
                            now - last_change_ms >= IDLE_CLEAN_MS;
    const bool full = full_requested || idle_clean || fast_since_full >= FAST_PER_FULL;
    if (!full && !changed) {
        if (controller_awake && now - last_refresh_end >= CONTROLLER_IDLE_MS) {
            epd.controllerIdle();
            controller_awake = false;
            controller_sleeps++;
        }
        return false;
    }

    copy_in();
    const unsigned long t0 = millis();
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
