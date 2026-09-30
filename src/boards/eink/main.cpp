/* main.cpp - the e-ink boards' firmware, entry point.
 *
 * Two M5Stack devices, one of them per build (eink_board.h): the Paper
 * Mono (ESP32-S3, 8MB PSRAM, 800x480 1-bit SSD1677, FT6336 touch, two
 * buttons) and the PaperS3 (ESP32-S3, 8MB PSRAM, 960x540 ED047TC1 over a
 * parallel bus, GT911 touch, no button the firmware can read). Everything
 * about the hardware comes from freeink-sdk, the library CrossPoint,
 * CrossPlay and the PaperS3 MicroBASIC drive these devices with.
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
 * Three computers can be built in, chosen at boot by touch (chooser.cpp):
 * the Macintosh, which takes the panel as a trackpad (trackpad.h), and the
 * MSX and the Spectrum, which take only the BLE keyboard and leave the
 * panel free for a menu. So touch means different things:
 *
 *   a menu is up      a tap picks from it
 *   the Mac           trackpad; GPIO2 (top right, held buttons-up) is the
 *                     mouse button
 *   MSX, Spectrum     a tap, or GPIO2, opens the list of cartridges and
 *                     snapshots (selector.cpp)
 *
 * GPIO3 pressed is a full refresh, which clears the ghosts fast refreshes
 * leave; held for two seconds it restarts the board into the boot menu.
 *
 * The same two things are gestures too, so a board without buttons (the
 * PaperS3) has them: a finger held still 1.5 seconds is a full refresh,
 * held 5 seconds a restart into the boot menu. On the Mac a still finger
 * does nothing else (a tap is under a quarter of a second), and on the
 * 8-bit machines the long press does not also count as the tap that
 * opens the list.
 */
#include <Arduino.h>
#include <InputManager.h>

#include "machine.h"
#include "display.h"
#include "display_mono.h"
#include "canvas.h"
#include "panel_eink.h"
#include "picture.h"
#include "trackpad.h"
#include "board_eink.h"
#include "chooser.h"
#include "ota_slots.h"
#include "selector.h"
#include "ble_keyboard.h"
#include <BoardConfig.h>

#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_system.h"

static InputManager input;

/* The panel, in pixels. The touch panel reports 0..1 across it. */
static const int PANEL_W = CANVAS_W;
static const int PANEL_H = CANVAS_H;

/* How CrossPlay, in the other app slot of the same device, knows this
 * firmware is here: it looks for this string in the slot's image before
 * offering to boot it, and refuses to self-update over it (CrossPlay's
 * src/util/RetroSlot.h). The two slots' esp_app_desc_t are identical under
 * pioarduino, so nothing else tells them apart. Printed at boot, so the
 * linker keeps it. Change it there too or not at all. */
static const char kAppSlotMarker[] = "RetroComputer-MultiBoard app slot marker v1";

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
static SemaphoreHandle_t i2c_lock;    /* see panel.cpp: touch, power chip, panel reset */
/* What the input task is doing, for `s` when it seems stuck. */
static volatile int input_stage;
static const char *const input_stages[] = { "sleeping", "reading the touch panel", "buttons", "holds", "touch" };
static volatile bool raw_touch_log;   /* `r`: print every touch sample */

/* The trackpad belongs to the input task; the console asks it. */
static volatile int tap_x, tap_y, tap_clicks;

/* --- menus: taps queued for them ------------------------------------ */

typedef struct { int16_t x, y; } Tap;
static QueueHandle_t tap_queue;
static volatile bool ui_mode;
static volatile bool ble_started;

extern "C" void board_ui(int on)
{
    if (on && tap_queue) xQueueReset(tap_queue);
    ui_mode = on != 0;
}

extern "C" int board_take_tap(int *x, int *y, unsigned long wait_ms)
{
    Tap t;
    if (!tap_queue || xQueueReceive(tap_queue, &t, pdMS_TO_TICKS(wait_ms)) != pdTRUE) return 0;
    *x = t.x;
    *y = t.y;
    return 1;
}

/* A touch point, normalised in the panel's own frame, to upright pixels. */
static void upright(float nx, float ny, int *x, int *y)
{
    *x = (int)(nx * PANEL_W);
    *y = (int)(ny * PANEL_H);
#if EINK_UPSIDE_DOWN
    *x = PANEL_W - 1 - *x;
    *y = PANEL_H - 1 - *y;
#endif
}

/* --- the two buttons ----------------------------------------------------- */

static volatile bool restart_to_menu;

static void buttons_service(void)
{
#if !EINK_HAS_BUTTONS
    return;
#endif
    static unsigned long refresh_down_at;
#if EINK_HAS_BUTTONS
    const bool m = digitalRead(BoardConfig::ACTIVE.input.up) == LOW;
    const bool r = digitalRead(BoardConfig::ACTIVE.input.down) == LOW;

    if (m != mouse_btn) {
        mouse_btn = m;
        Serial.printf("button: GPIO%d %s\n", BoardConfig::ACTIVE.input.up, m ? "down" : "up");
        if (machine->pointer) {
            if (tp_ready) trackpad_button(&tp, m);
        } else if (m && !ui_mode) {
            selector_open();
        }
    }
    if (r != refresh_btn) {
        refresh_btn = r;
        Serial.printf("button: GPIO%d %s\n", BoardConfig::ACTIVE.input.down, r ? "down" : "up");
        if (r) refresh_down_at = millis();
        else if (millis() - refresh_down_at < 2000) panel_request_full();
    }
    if (r && refresh_down_at && millis() - refresh_down_at >= 2000) {
        refresh_down_at = 0;
        restart_to_menu = true;
    }
#endif
}

/* A finger held still: 1.5s a full refresh, 5s a restart into the boot
 * menu. The buttons' two jobs, for a board that has none, and on every
 * board because they cost nothing. */
static void hold_service(void)
{
    static bool refreshed, restarted;
    float nx, ny;
    unsigned long held;
    if (!input.isTouchTapCandidate(nx, ny, held)) {
        refreshed = restarted = false;
        return;
    }
    if (!refreshed && held >= 1500) {
        refreshed = true;
        panel_request_full();
        Serial.println("touch: held 1.5s, full refresh");
    }
    if (!restarted && held >= 5000) {
        restarted = true;
        restart_to_menu = true;
    }
}

/* --- touch ------------------------------------------------------------------ */

static void touch_service(void)
{
    float nx, ny;
    if (ui_mode || !machine->pointer) {
        /* a menu, or a machine that has no use for the panel: taps only,
         * and not the lift of a finger held for a refresh (hold_service) */
        if (!input.wasTouchTap(nx, ny)) return;
        if (input.lastTouchHeldMs() >= 1500) return;
        int x, y;
        upright(nx, ny, &x, &y);
        if (ui_mode) {
            Tap t = { (int16_t)x, (int16_t)y };
            if (tap_queue) xQueueSend(tap_queue, &t, 0);
        } else {
            selector_open();
        }
        return;
    }

    const eink_view *v = panel_view();
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

    const bool touching = input.isTouchHeldAt(nx, ny);
    int x = (int)(nx * PANEL_W), y = (int)(ny * PANEL_H);
    if (touching) eink_touch_upright(v, &x, &y);
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
 *   c A MS     clean automatically (a full refresh) once the picture has
 *              been still MS after at least A fast refreshes; c alone
 *              says what it is now
 *   b          the BLE keyboard: connected, reports seen, last length
 *   bl         toggle printing every raw report from the keyboard
 *   r          toggle printing every touch sample, "touch <ms> <x> <y>",
 *              in upright panel pixels: the jitter, measured
 *   t X Y      put the pointer at X,Y (machine pixels) and click there
 *   tt X Y     the same, double-clicking
 *   k U        press and release HID usage U (hex), e.g. k 04 is A
 *   d          the picture, as hex rows (the Mac's own framebuffer, or the
 *              whole panel for the others): tools/fbdump.py makes a PNG of
 *              it, so the screen can be checked with nobody looking
 *   o          open the selector (MSX, Spectrum), as a tap would
 *   anything else goes to the machine's own debug_command
 */

static void dump_picture(void)
{
    static const char hex[] = "0123456789abcdef";
    int w, h;
    const uint8_t *fb = panel_mono_picture(&w, &h);
    char line[CANVAS_W / 4 + 1];
    if (!fb) {
        /* The whole panel as it is being drawn, upright and with a set bit
         * black, the way tools/fbdump.py reads the Mac's. */
        const uint8_t *c = panel_canvas();
        if (!c) { Serial.println("no picture"); return; }
        Serial.printf("FB %d %d\n", PANEL_W, PANEL_H);
        for (int y = 0; y < PANEL_H; y++) {
            for (int x = 0; x < PANEL_W / 8; x++) {
#if EINK_UPSIDE_DOWN
                uint8_t b = eink_reverse8(c[(PANEL_H - 1 - y) * (PANEL_W / 8) + (PANEL_W / 8 - 1 - x)]);
#else
                uint8_t b = c[y * (PANEL_W / 8) + x];
#endif
                b = (uint8_t)~b;
                line[2 * x] = hex[b >> 4];
                line[2 * x + 1] = hex[b & 15];
            }
            line[PANEL_W / 4] = 0;
            Serial.println(line);
        }
        Serial.println("FB END");
        return;
    }
    const int stride = w / 8;
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
    /* A menu takes it as a tap on the panel, in the upright picture's
     * pixels (what `d` dumps); the Mac, as the trackpad's click. */
    if (ui_mode) {
        Tap t = { (int16_t)x, (int16_t)y };
        if (tap_queue) xQueueSend(tap_queue, &t, 0);
        return;
    }
    tap_x = x;
    tap_y = y;
    tap_clicks = clicks;
}

/* Through the BLE transport once it is running: it hands the machine its
 * cached report every 5ms, and a report given any other way is
 * overwritten within one. */
static void key(unsigned usage)
{
    uint8_t r[8] = {0};
    r[2] = (uint8_t)usage;
    if (ble_started) ble_keyboard_inject(r); else machine->hid_report(r);
    delay(80);
    r[2] = 0;
    if (ble_started) ble_keyboard_inject(r); else machine->hid_report(r);
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
        Serial.printf("input: longest gap between samples %lums, now %s\n", input_gap_max,
                      input_stages[input_stage]);
        panel_diag();
        input_gap_max = 0;
        machine->debug_command("s");
    } else if (sscanf(line, "c %d %d", &a, &b) == 2) {
        panel_set_idle_clean(a, (unsigned long)b);
        Serial.printf("clean: a full refresh after %d fast ones and %dms still\n", a, b);
    } else if (!strcmp(line, "c")) {
        int after; unsigned long ms;
        panel_get_idle_clean(&after, &ms);
        Serial.printf("clean: a full refresh after %d fast ones and %lums still\n", after, ms);
    } else if (!strcmp(line, "b")) {
        if (ble_started) ble_keyboard_status(); else Serial.println("BLE: not started yet");
    } else if (!strcmp(line, "bl")) {
        static bool on;
        on = !on;
        ble_keyboard_log_reports(on);
        Serial.printf("BLE: raw reports %s\n", on ? "logged" : "not logged");
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
    } else if (!strcmp(line, "o")) {
        if (!machine->pointer) selector_open();
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
        input_stage = 1;
        xSemaphoreTake(i2c_lock, portMAX_DELAY);
        input.update();
        xSemaphoreGive(i2c_lock);
        input_stage = 2;
        buttons_service();
        input_stage = 3;
        hold_service();
        input_stage = 4;
        touch_service();
        input_stage = 0;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* The pairing code, shown until the keyboard has connected. */
static void ble_service(void)
{
    static unsigned long shown_at, connected_note_at;
    static bool was_connected;
    if (!ble_started) return;
    uint32_t code;
    if (ble_keyboard_take_passkey(&code)) {
        char l2[24];
        snprintf(l2, sizeof l2, "%06lu Enter", (unsigned long)code);
        panel_message("Teclado pedindo codigo: digite no teclado", l2);
        shown_at = millis();
    }
    const bool connected = ble_keyboard_connected() != 0;
    if (connected && !was_connected) {
        panel_message("Teclado conectado", NULL);
        connected_note_at = millis();
        shown_at = 0;
    }
    was_connected = connected;
    if (connected_note_at && millis() - connected_note_at > 3000) {
        connected_note_at = 0;
        panel_message_clear();
    }
    if (shown_at && millis() - shown_at > 120000) {
        shown_at = 0;
        panel_message_clear();
    }
}

static void boardTask(void *arg)
{
    (void)arg;
    for (;;) {
        console_service();
        ble_service();
        if (restart_to_menu) {
            Serial.println("GPIO3 held: restarting into the boot menu");
            delay(50);
            esp_restart();
        }
        if (!panel_service()) vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/* The FT6336 sometimes does not answer its probe after a reset from the
 * serial cable (CrossPlay saw it twice, 2026-09-11), and freeink-sdk then
 * leaves touch off for the whole session: the Mac's pointer stayed dead
 * on 2026-09-28. begin() is safe to call again, so it is, a few times. */
static void input_begin(void)
{
    for (int attempt = 1; attempt <= 3; attempt++) {
        input.begin();
        if (input.hasTouch()) {
            Serial.printf("touch: yes (attempt %d)\n", attempt);
            return;
        }
        Serial.printf("touch: FT6336 did not answer (attempt %d)\n", attempt);
        delay(300);
    }
}

/* What comes up at power-on: which computer, and what it starts with.
 * Untouched for five seconds, the one outlined boots with what it had. */
static void boot_menu(void)
{
    int m = machine_chosen_index();
    /* Chosen from the selector a moment ago: straight into it. */
    if (selector_take_restart_choice()) { machine_choose(m, -1); return; }
    for (;;) {
        const int pick = chooser_pick_machine(0, m, "Sem toque, em 5 s liga o marcado", 5000);
        if (pick == CHOOSER_TIMEOUT) { machine_choose(m, -1); return; }
        if (pick < 0) continue;
        if (machine_list[pick]->entry_count() <= 1) { machine_choose(pick, -1); return; }
        const int e = chooser_pick_entry(pick, 1);
        if (e >= 0) { machine_choose(pick, e); return; }
        m = pick;
    }
}

void setup()
{
    Serial.begin(115200);
    delay(250);

    machine_storage_init();
    machine = machine_list[machine_chosen_index()];
    Serial.printf("\n\n" EINK_BOARD_NAME ": %d computer(s) built in\n", machine_count);
    Serial.printf("%s\n", kAppSlotMarker);
    ota_slots_register();
    Serial.printf("boot: internal %u free, PSRAM %u free\n",
                  heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    i2c_lock = xSemaphoreCreateMutex();
    panel_set_i2c_lock(i2c_lock);
    panel_begin();
    display8_attach(panel_canvas());
    input_begin();
    media_begin();
    tap_queue = xQueueCreate(8, sizeof(Tap));

    /* Input above the board task, so a refresh never delays a sample. */
    xTaskCreatePinnedToCore(inputTask, "input", 6144, NULL, 4, NULL, 0);
    xTaskCreatePinnedToCore(boardTask, "board", 8192, NULL, 3, NULL, 0);

    boot_menu();
    Serial.printf("starting %s\n", machine->name);
    display_fill_panel(0);
    panel_request_full();

    machine->prealloc_video();
    /* 16kB, where the CYD gets 12: nothing here is short of internal RAM,
     * and umac longjmps back into its loop out of bus errors. */
    xTaskCreatePinnedToCore(machineTask, "machine", 16384, NULL, 5, NULL, 1);

    /* The keyboard after the machine has its memory, as on the CYD. */
    for (int i = 0; i < 200 && !machine->ready(); i++) delay(25);
    ble_keyboard_init();
    ble_started = true;
}

void loop()
{
    vTaskDelay(pdMS_TO_TICKS(1000));
}
