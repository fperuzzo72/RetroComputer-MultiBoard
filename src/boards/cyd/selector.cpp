/* selector.cpp - swapping cartridge or tape without rebooting the board.
 *
 * Opened by holding a finger on the screen for about a second. A tap
 * would open it by accident during a game; a hold is deliberate and needs
 * no particular place to aim at, which matters because the picture moves
 * around with the scale.
 *
 * The screens themselves are chooser.cpp, the same ones the boot menu
 * uses. All that is here is the gesture and the handover: the machine is
 * stopped while the chooser runs, which is what makes it safe for the
 * chooser to block.
 */
#include <Arduino.h>

#include "panel.h"
#include "display.h"
#include "selector.h"
#include "chooser.h"
#include "machine.h"

#include "esp_system.h"

static bool sActive;

int selector_active(void) { return sActive ? 1 : 0; }

void selector_open(void) { sActive = true; }

/* A press held for about a second. */
#define HOLD_MS 900

/* How hard counts as a finger. TFT_eSPI's own getTouch() uses 600 as its
 * default and this is the same reading, taken directly. */
#define TOUCH_Z 600

void selector_poll_open(void) {
    static uint32_t downSince;

    if (sActive) return;

    /* Pressure only, never getTouch().
     *
     * This runs once per emulated frame, and getTouch() cost 12.6ms of a
     * 23ms frame - measured, and it was more than half the machine. The
     * cost is TFT_eSPI's debounce: it re-reads pressure until the reading
     * stops rising, with a delay(1) each time round, and an untouched
     * resistive panel gives it noise to chase. Reading Z once is a few
     * microseconds and is all a hold needs, because where the finger is
     * does not matter here - only that it stayed. */
    if (panel_tft().getTouchRawZ() < TOUCH_Z) { downSince = 0; return; }

    if (!downSince) { downSince = millis(); return; }
    if (millis() - downSince >= HOLD_MS) {
        downSince = 0;
        selector_open();
    }
}

/* Called by the machine once it has stopped. Runs the chooser and comes
 * back with an entry, or -1 if nothing was picked.
 *
 * It opens on the groups for the machine that is running, because that is
 * what somebody holding a finger on a game almost always wants. Going
 * back from there does not close it but offers the other machine, and
 * going back from that returns to the game - so it is still two presses
 * to change your mind, and the other machine is no longer a power cycle
 * away. */
int selector_frame(void) {
    if (!sActive) return -1;

    /* The finger that opened this is still down; if we went straight to
     * the first screen it would press whatever is under it. */
    while (panel_tft().getTouchRawZ() >= TOUCH_Z) delay(20);

    int m = machine_chosen_index();

    for (;;) {
        int chosen = chooser_pick_entry(m, 1);

        if (chosen >= 0) {
            sActive = false;
            if (m == machine_chosen_index()) return chosen;

            /* A different machine cannot be swapped in while this one is
             * running: its RAM, its VRAM and its band buffer were cut out
             * of the one big DRAM region at startup and belong to it. So
             * remember the choice and come up as the other machine. */
            machine_choose(m, chosen);
            delay(80);
            esp_restart();
        }

        int pick = chooser_pick_machine(1);
        if (pick < 0) { sActive = false; return -1; }   /* back again: the game */
        m = pick;
    }
}
