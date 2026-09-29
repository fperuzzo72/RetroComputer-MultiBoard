/* main.cpp - M5Stack Paper Mono firmware, entry point.
 *
 * The board: an ESP32-S3 with 8MB of PSRAM, an 800x480 1-bit e-ink panel
 * (SSD1677), an FT6336 capacitive touch panel, two buttons. Everything
 * about the hardware comes from freeink-sdk, the same library CrossPoint
 * and CrossPlay drive this device with; the panel, touch and power-rail
 * bring-up are its, not ours.
 *
 * Three tasks. The machine runs flat out on core 1 and never waits for the
 * panel. On core 0, the input task reads the touch panel and the buttons
 * every 10ms, and the board task refreshes the panel when the picture has
 * changed and answers the serial console.
 *
 * Input has to be a task of its own. A refresh blocks for the length of the
 * waveform, about 400ms, and every move of the pointer changes the picture
 * and starts another, so while a finger moved the panel refreshed back to
 * back: read from the same loop, touch was sampled once in ~450ms. The
 * pointer jumped, taps were lost or taken for holds, a double tap arrived
 * seconds late, and a short press of a button fitted between two samples
 * and was never seen. That was the first version on the device.
 *
 * The panel is a trackpad (trackpad.h): the finger pushes the pointer
 * rather than standing on it, because a finger is far too big for a Mac's
 * close box. Of the two buttons, the one on GPIO2 is the mouse button and
 * the one on GPIO3 asks for a full refresh, which clears the ghosts fast
 * refreshes leave behind.
 */
#include <Arduino.h>
#include <InputManager.h>

#include "machine.h"
#include "display_mono.h"
#include "panel_papermono.h"
#include "picture.h"
#include "trackpad.h"
#include <BoardConfig.h>

static InputManager input;

/* The panel, in pixels. The touch panel reports 0..1 across it. */
static const int PANEL_W = 800;
static const int PANEL_H = 480;

static void machineTask(void *arg)
{
    (void)arg;
    machine->run();
    vTaskDelete(NULL);
}

/* --- touch as a trackpad, and the two buttons --------------------------
 *
 * Read straight off their pins rather than through InputManager, whose
 * two-button logic reports a short press only when it is let go: a mouse
 * button has to be down for as long as it is held. Both are active low
 * with pull-ups, which InputManager::begin() configures. */

static trackpad tp;
static bool tp_ready;
static int last_x = -1, last_y = -1, last_b = -1;
static bool mouse_btn, refresh_btn;

static unsigned long input_gap_max;   /* longest time between samples, ms */
static volatile bool raw_touch_log;   /* `r`: print every touch sample */

/* The trackpad belongs to the input task; the console asks it. */
static volatile int tap_x, tap_y, tap_clicks;

static void pointer_service(void)
{
    if (!machine->pointer) return;
    const papermono_view *v = panel_view();
    if (!v) return;
    if (!tp_ready) {
        trackpad_init(&tp, v->w, v->h, (float)v->dh / v->h);
        tp_ready = true;
    }

    /* a tap asked for over the console: pointer there now, click once it
     * has had a moment to arrive */
    static unsigned long tap_due;
    static int tap_pending_clicks;
    if (tap_clicks) {
        trackpad_warp(&tp, tap_x, tap_y);
        tap_pending_clicks = tap_clicks;
        tap_clicks = 0;
        tap_due = millis() + 100;
    }
    if (tap_pending_clicks && (long)(millis() - tap_due) >= 0) {
        trackpad_click(&tp, millis(), tap_pending_clicks);
        tap_pending_clicks = 0;
    }

    const bool m = digitalRead(BoardConfig::ACTIVE.input.up) == LOW;
    const bool r = digitalRead(BoardConfig::ACTIVE.input.down) == LOW;
    if (m != mouse_btn) {
        mouse_btn = m;
        trackpad_button(&tp, m);
        Serial.printf("button: GPIO%d (mouse) %s\n", BoardConfig::ACTIVE.input.up, m ? "down" : "up");
    }
    if (r != refresh_btn) {
        refresh_btn = r;
        if (r) panel_request_full();
        Serial.printf("button: GPIO%d (refresh) %s\n", BoardConfig::ACTIVE.input.down, r ? "down" : "up");
    }

    float nx, ny;
    const bool touching = input.isTouchHeldAt(nx, ny);
    int x = (int)(nx * PANEL_W), y = (int)(ny * PANEL_H);
    if (touching) papermono_touch_upright(v, &x, &y);
    if (raw_touch_log) {
        static bool was;
        if (touching) Serial.printf("touch %lu %d %d\n", millis(), x, y);
        else if (was) Serial.printf("touch %lu up\n", millis());
        was = touching;
    }
    trackpad_update(&tp, millis(), touching, x, y);

    int px, py, b;
    trackpad_pointer(&tp, &px, &py, &b);
    if (px != last_x || py != last_y || b != last_b) {
        last_x = px;
        last_y = py;
        last_b = b;
        machine->pointer(px, py, b);
    }
}

/* --- serial console ----------------------------------------------------
 *
 *   s          statistics: panel refreshes, heap
 *   f          full refresh on the next pass
 *   r          toggle printing every touch sample, "touch <ms> <x> <y>",
 *              in upright panel pixels: the jitter, measured
 *   t X Y      put the pointer at X,Y (machine pixels) and click there
 *   tt X Y     the same, double-clicking
 *   k U        press and release HID usage U (hex), e.g. k 04 is A
 *   d          the machine's picture, as hex rows: tools/fbdump.py makes a
 *              PNG of it, so the screen can be checked with nobody looking
 *   anything else goes to the machine's own debug_command
 */

static void dump_picture(void)
{
    static const char hex[] = "0123456789abcdef";
    int w, h;
    const uint8_t *fb = panel_mono_picture(&w, &h);
    if (!fb) { Serial.println("no picture"); return; }
    const int stride = w / 8;
    char line[2 * 128 + 1];
    Serial.printf("FB %d %d\n", w, h);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < stride; x++) {
            line[2 * x] = hex[fb[y * stride + x] >> 4];
            line[2 * x + 1] = hex[fb[y * stride + x] & 15];
        }
        line[2 * stride] = 0;
        Serial.println(line);
    }
    Serial.println("FB END");
}

static void tap(int x, int y, int clicks)
{
    tap_x = x;
    tap_y = y;
    tap_clicks = clicks;
}

static void key(unsigned usage)
{
    uint8_t r[8] = {0};
    r[2] = (uint8_t)usage;
    machine->hid_report(r);
    delay(80);
    r[2] = 0;
    machine->hid_report(r);
}

static void console_command(const char *line)
{
    int a, b;
    unsigned u;
    if (!strcmp(line, "s")) {
        unsigned long n, fulls, last, avg;
        panel_stats(&n, &fulls, &last, &avg);
        Serial.printf("panel: %lu refreshes (%lu full, last full %lums), last %lums, average %lums\n",
                      n, fulls, panel_last_full_ms(), last, avg);
        unsigned long cb, ci, cc, ck;
        panel_clean_stats(&cb, &ci, &cc, &ck);
        Serial.printf("cleans: %lu by button, %lu at a pause, %lu at the cap; %lu caret blinks not shown\n",
                      cb, ci, cc, ck);
        Serial.printf("controller put to sleep %lu times between refreshes\n", panel_controller_sleeps());
        Serial.printf("heap: internal %u free (largest %u), PSRAM %u free\n",
                      heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                      heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        Serial.printf("input: longest gap between samples %lums\n", input_gap_max);
        input_gap_max = 0;
        machine->debug_command("s");
    } else if (!strcmp(line, "r")) {
        raw_touch_log = !raw_touch_log;
        Serial.printf("raw touch log %s\n", raw_touch_log ? "on" : "off");
    } else if (!strcmp(line, "f")) {
        panel_request_full();
    } else if (sscanf(line, "tt %d %d", &a, &b) == 2) {
        tap(a, b, 2);
    } else if (sscanf(line, "t %d %d", &a, &b) == 2) {
        tap(a, b, 1);
    } else if (sscanf(line, "k %x", &u) == 1) {
        key(u);
    } else if (!strcmp(line, "d")) {
        dump_picture();
    } else if (!machine->debug_command(line)) {
        Serial.printf("? %s\n", line);
    }
}

static void console_service(void)
{
    static char buf[64];
    static int len;
    while (Serial.available()) {
        int c = Serial.read();
        if (c == '\r' || c == '\n') {
            if (len) {
                buf[len] = 0;
                console_command(buf);
                len = 0;
            }
        } else if (len < (int)sizeof(buf) - 1) {
            buf[len++] = (char)c;
        }
    }
}

static void inputTask(void *arg)
{
    (void)arg;
    unsigned long last = millis();
    for (;;) {
        const unsigned long now = millis();
        if (now - last > input_gap_max) input_gap_max = now - last;
        last = now;
        input.update();
        pointer_service();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void boardTask(void *arg)
{
    (void)arg;
    for (;;) {
        console_service();
        if (!panel_service()) vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void setup()
{
    Serial.begin(115200);
    delay(250);

    machine_storage_init();
    machine = machine_list[machine_chosen_index()];
    Serial.printf("\n\nPaper Mono: %s\n", machine->name);
    Serial.printf("boot: internal %u free, PSRAM %u free\n",
                  heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    panel_begin();
    input.begin();
    Serial.printf("touch: %s\n", input.hasTouch() ? "yes" : "NO (FT6336 did not answer)");

    machine->prealloc_video();
    /* 16kB, where the CYD gets 12: nothing here is short of internal RAM,
     * and umac longjmps back into its loop out of bus errors. */
    xTaskCreatePinnedToCore(machineTask, "machine", 16384, NULL, 5, NULL, 1);
    /* Input above the board task, so a refresh never delays a sample. */
    xTaskCreatePinnedToCore(inputTask, "input", 6144, NULL, 4, NULL, 0);
    xTaskCreatePinnedToCore(boardTask, "board", 8192, NULL, 3, NULL, 0);
}

void loop()
{
    vTaskDelay(pdMS_TO_TICKS(1000));
}
