/* boot_menu.cpp - what comes up at power-on. See boot_menu.h.
 *
 * Almost nothing is left in here: the picking moved to chooser.cpp, which
 * both this and the in-game selector use. What remains is the splash that
 * starts the remembered choice on its own after a few seconds, the touch
 * calibration, and the picture scale.
 */
#include <Arduino.h>
#include <Preferences.h>

#include "panel.h"
#include "display.h"
#include "boot_menu.h"
#include "chooser.h"
#include "machine.h"

#define MENU_SECONDS 5      /* before it boots the remembered choice */

static const uint16_t COL_BG   = TFT_BLACK;
static const uint16_t COL_TEXT = TFT_WHITE;
static const uint16_t COL_DIM  = 0xAD55;
static const uint16_t COL_HEAD = TFT_YELLOW;

/* The picture scale is per machine, and remembered.
 *
 * It has to be: the Spectrum runs at twice the speed it needs and can
 * spend it on a picture that fills the panel, while on the MSX the same
 * setting costs ten frames a second it does not have. Each machine's
 * table says what it comes up at; this only remembers a change. */
#define NVS_NS "cyd"

static void scaleKey(char *out, int machineIndex) {
    snprintf(out, 12, "scale%d", machineIndex);
}

int boot_scale_for_machine(int machineIndex) {
    Preferences prefs;
    char key[12];
    int v = machine_list[machineIndex]->default_scale;
    scaleKey(key, machineIndex);
    if (prefs.begin(NVS_NS, true)) {
        v = prefs.getInt(key, v);
        prefs.end();
    }
    return (v == 1) ? 1 : 2;
}

void boot_remember_scale(int machineIndex, int scale) {
    Preferences prefs;
    char key[12];
    scaleKey(key, machineIndex);
    if (!prefs.begin(NVS_NS, false)) return;
    prefs.putInt(key, scale);
    prefs.end();
}

static int totalEntries(void) {
    int n = 0;
    for (int m = 0; m < machine_count; m++) n += machine_list[m]->entry_count();
    return n;
}

/* Ask before taking over the screen for four corner taps.
 *
 * It has to be asked rather than assumed: calibrateTouch() waits for four
 * presses and has no way out, so a board that powers up with nobody in
 * front of it would sit there for good. Ten seconds of nothing and it
 * carries on with TFT_eSPI's rough defaults, which is how this shipped
 * before and is merely inaccurate rather than stuck. */
static void offerCalibration(TFT_eSPI &tft) {
    Serial.println("menu: this board has no touch calibration");
    for (int left = 10; left > 0; left--) {
        tft.fillScreen(COL_BG);
        tft.setTextDatum(TC_DATUM);
        tft.setTextColor(COL_HEAD, COL_BG);
        tft.drawString("The screen is not calibrated",
                       DISPLAY_PANEL_W / 2, 90, 4);
        tft.setTextColor(COL_TEXT, COL_BG);
        tft.drawString("touch it now to fix that",
                       DISPLAY_PANEL_W / 2, 140, 2);
        tft.setTextColor(COL_DIM, COL_BG);
        char buf[48];
        snprintf(buf, sizeof(buf), "carrying on without it in %d", left);
        tft.drawString(buf, DISPLAY_PANEL_W / 2, 190, 2);
        tft.setTextDatum(TL_DATUM);

        uint32_t end = millis() + 1000;
        while ((int32_t)(end - millis()) > 0) {
            /* Held, not glimpsed. A single reading over the threshold is
              * something a floating resistive panel does on its own, and
              * that is enough to walk the whole calibration through four
              * corners nobody touched. */
            if (tft.getTouchRawZ() >= 600) {
                int held = 0;
                while (held < 20 && tft.getTouchRawZ() >= 600) { held++; delay(15); }
                if (held >= 20) {
                    while (tft.getTouchRawZ() >= 600) delay(20);
                    chooser_calibrate();
                    return;
                }
            }
            delay(20);
        }
    }
    Serial.println("menu: carrying on uncalibrated - 'u c' on the console to do it later");
}

static void splash(TFT_eSPI &tft, int secondsLeft) {
    const Machine *m = machine_list[machine_chosen_index()];
    tft.fillScreen(COL_BG);

    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(COL_DIM, COL_BG);
    tft.drawString("starting", DISPLAY_PANEL_W / 2, 58, 2);

    tft.setTextColor(COL_HEAD, COL_BG);
    tft.drawString(m->name, DISPLAY_PANEL_W / 2, 92, 4);

    tft.setTextColor(COL_TEXT, COL_BG);
    tft.drawString(m->entry_name(m->selected_entry()), DISPLAY_PANEL_W / 2, 140, 4);

    tft.setTextColor(COL_DIM, COL_BG);
    tft.drawString("touch the screen to choose something else",
                   DISPLAY_PANEL_W / 2, 210, 2);

    char buf[24];
    snprintf(buf, sizeof(buf), "%d", secondsLeft);
    tft.setTextColor(COL_DIM, COL_BG);
    tft.drawString(buf, DISPLAY_PANEL_W / 2, 250, 4);
    tft.setTextDatum(TL_DATUM);
}

void boot_menu_run(void) {
    TFT_eSPI &tft = panel_tft();

    /* Before anything asks where a finger landed. A panel with no
     * calibration maps presses with TFT_eSPI's defaults, which are
     * nobody's panel in particular. */
    chooser_apply_calibration();
    if (!chooser_is_calibrated()) offerCalibration(tft);

    /* Nothing to choose between: don't make anyone look at a menu. */
    if (totalEntries() <= 1) {
        display_set_scale(boot_scale_for_machine(machine_chosen_index()));
        return;
    }

    int chosenMachine = machine_chosen_index();
    splash(tft, MENU_SECONDS);

    uint32_t deadline = millis() + MENU_SECONDS * 1000;
    int shown = MENU_SECONDS;
    bool interrupted = false;

    while ((int32_t)(deadline - millis()) > 0) {
        uint16_t tx, ty;
        if (tft.getTouch(&tx, &ty)) { interrupted = true; break; }
        int left = (int)((deadline - millis()) / 1000) + 1;
        if (left != shown) { shown = left; splash(tft, left); }
        delay(20);
    }

    if (interrupted) {
        /* Let go before the first screen appears, or the press that opened
         * the chooser also picks something on it. */
        while (tft.getTouchRawZ() >= 600) delay(20);

        for (;;) {
            int m = chooser_pick_machine(0);
            if (m < 0) break;                    /* timed out: keep what we had */
            int e = chooser_pick_entry(m, 1);
            if (e < 0) continue;                 /* back: which machine again */
            machine_choose(m, e);
            chosenMachine = m;
            break;
        }
    }

    display_set_scale(boot_scale_for_machine(chosenMachine));
    Serial.printf("menu: starting %s / %s, picture %s\n",
                  machine->name, machine->entry_name(machine->selected_entry()),
                  display_get_scale() == 1 ? "1:1" : "1.5x");

    tft.fillScreen(COL_BG);
}
