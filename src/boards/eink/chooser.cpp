/* chooser.cpp - picking a computer and a game by touch. See chooser.h. */
#include "chooser.h"

#include <Arduino.h>

#include "board_eink.h"
#include "canvas.h"
#include "machine.h"
#include "panel_eink.h"
#include "ui.h"

/* A menu takes the whole panel, and what was there before - a game, the
 * Mac's desktop - would ghost through a fast refresh of it. */
static void show(void) { panel_request_full(); }

int chooser_pick_machine(int allow_cancel, int mark, const char *note, unsigned long timeout_ms,
                         const char *extra)
{
    const char *names[9];
    const int machines = machine_count < 8 ? machine_count : 8;
    int n = machines;
    for (int i = 0; i < machines; i++) names[i] = machine_list[i]->name;
    if (extra) names[n++] = extra;

    uint8_t *c = panel_canvas();
    panel_canvas_lock();
    ui_draw_machines(c, names, n, mark, allow_cancel, note);
    panel_canvas_unlock();
    show();
    board_ui(1);
    const unsigned long start = millis();
    int result = -1;
    for (;;) {
        unsigned long wait = 200;
        if (timeout_ms) {
            const unsigned long gone = millis() - start;
            if (gone >= timeout_ms) { result = CHOOSER_TIMEOUT; break; }
            if (timeout_ms - gone < wait) wait = timeout_ms - gone;
        }
        int x, y;
        if (!board_take_tap(&x, &y, wait)) continue;
        /* one touch and the countdown is off: somebody is choosing */
        timeout_ms = 0;
        const int hit = ui_hit_machines(x, y, n, allow_cancel);
        if (hit >= machines) { result = CHOOSER_EXTRA; break; }
        if (hit >= 0) { result = hit; break; }
        if (hit == UI_BACK) { result = -1; break; }
    }
    board_ui(0);
    return result;
}

static const Machine *sNaming;
static const char *entry_name(int i) { return sNaming->entry_name(i); }

int chooser_pick_entry(int machine_index, int allow_cancel)
{
    sNaming = machine_list[machine_index];
    const int count = sNaming->entry_count();
    int page = 0;
    uint8_t *c = panel_canvas();
    board_ui(1);
    int result = -1;
    for (;;) {
        panel_canvas_lock();
        ui_draw_entries(c, sNaming->name, entry_name, count, page, allow_cancel);
        panel_canvas_unlock();
        show();
        int x, y, hit = UI_NONE;
        while (hit == UI_NONE) {
            if (board_take_tap(&x, &y, 1000)) hit = ui_hit_entries(x, y, count, page, allow_cancel);
        }
        if (hit >= 0) { result = hit; break; }
        if (hit == UI_BACK) { result = -1; break; }
        if (hit == UI_PREV) page--;
        if (hit == UI_NEXT) page++;
    }
    board_ui(0);
    return result;
}
