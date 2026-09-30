/* selector.cpp - changing cartridge, snapshot or computer without
 * reflashing, on the Paper Mono.
 *
 * The 8-bit machines use nothing but the keyboard, so the panel is free:
 * a tap anywhere on it, or the mouse button (GPIO2), opens the list of
 * what this machine can start with. main.cpp's input task calls
 * selector_open(); the machine notices at its next frame and hands over
 * to selector_frame(), standing still until it returns.
 *
 * Back from the list offers the other computers, and back from that
 * returns to the game. Another computer cannot be swapped in while this
 * one runs - its memory was claimed at boot - so choosing one remembers
 * the choice and restarts the board as that computer, as on the CYD.
 */
#include <Arduino.h>

#include "chooser.h"
#include "display.h"
#include "machine.h"
#include "panel_eink.h"
#include "selector.h"

#include "esp_system.h"
#include "esp_attr.h"

static volatile bool sActive;

/* Set just before the restart that changes computer, read once by the
 * boot menu: the choice was made here, so the board comes up as that
 * computer instead of asking again. RTC memory survives esp_restart()
 * and not a power cut, which is what a restart into the menu needs. */
#define CHOSEN_MAGIC 0x52455452u   /* "RETR" */
static RTC_NOINIT_ATTR uint32_t sChosenOnRestart;

int selector_take_restart_choice(void)
{
    const int was = sChosenOnRestart == CHOSEN_MAGIC;
    sChosenOnRestart = 0;
    return was;
}

int  selector_active(void) { return sActive ? 1 : 0; }
void selector_open(void) { sActive = true; }
void selector_poll_open(void) {}   /* main.cpp's input task opens it */

int selector_frame(void)
{
    if (!sActive) return -1;
    int m = machine_chosen_index();
    int chosen = -1;
    for (;;) {
        const int e = chooser_pick_entry(m, 1);
        if (e >= 0) {
            if (m == machine_chosen_index()) { chosen = e; break; }
            machine_choose(m, e);
            sChosenOnRestart = CHOSEN_MAGIC;
            delay(80);
            esp_restart();
        }
        const int pick = chooser_pick_machine(1, machine_chosen_index(), NULL, 0);
        if (pick < 0) break;          /* back again: the game */
        m = pick;
    }
    sActive = false;

    /* the menu had the panel: the machine redraws all of its picture */
    display_fill_panel(0);
    display8_request_repaint();
    panel_request_full();
    return chosen;
}
