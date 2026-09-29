/* chooser.cpp - the three-screen picker described in chooser.h. */
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

#include "panel.h"
#include "display.h"
#include "chooser.h"
#include "machine.h"
#include "boot_menu.h"

static const uint16_t COL_BG    = TFT_BLACK;
static const uint16_t COL_TILE  = 0x18E3;   /* dark slate */
static const uint16_t COL_EDGE  = 0x4A69;   /* its lighter edge */
static const uint16_t COL_PRESS = 0x04BF;   /* the flash when pressed */
static const uint16_t COL_TEXT  = TFT_WHITE;
static const uint16_t COL_DIM   = 0xAD55;   /* grey, for second lines */
static const uint16_t COL_HEAD  = TFT_YELLOW;

/* The panel, divided once. A header to say where you are, a body of
 * targets, and a footer for going back. */
#define HEAD_H   34
#define FOOT_H   40
#define BODY_TOP HEAD_H
#define BODY_H   (DISPLAY_PANEL_H - HEAD_H - FOOT_H)
#define FOOT_TOP (DISPLAY_PANEL_H - FOOT_H)
#define GAP      8

#define GROUPS   8            /* screen 2 is eight tiles, always */

/* ---------------------------------------------------------------- */
/* Touch                                                             */
/* ---------------------------------------------------------------- */

#define NVS_NS  "cyd"
#define NVS_KEY "touchcal"

/* Does a calibration look like one?
 *
 * The XPT2046 reports 12-bit positions, so a real corner sits somewhere
 * in the high hundreds to low thousands, and opposite corners are far
 * apart. A set that fails this came from noise rather than from fingers,
 * which is exactly what happened the first time this ran: an untouched
 * resistive panel produced pressure readings over the threshold, the
 * calibration walked through all four corners on its own, and stored
 * nonsense. */
static int calibrationLooksReal(const uint16_t *cal) {
    for (int i = 0; i < 4; i++)
        if (cal[i] < 100 || cal[i] > 4000) return 0;
    if (cal[1] < cal[0] + 500) return 0;      /* x range */
    if (cal[3] < cal[2] + 500) return 0;      /* y range */
    if (cal[4] > 7) return 0;                 /* rotation bits */
    return 1;
}

void chooser_apply_calibration(void) {
    Preferences prefs;
    uint16_t cal[5];
    if (!prefs.begin(NVS_NS, true)) return;
    if (prefs.getBytesLength(NVS_KEY) == sizeof(cal)) {
        prefs.getBytes(NVS_KEY, cal, sizeof(cal));
        if (calibrationLooksReal(cal)) {
            panel_tft().setTouch(cal);
            Serial.printf("touch: calibration %u %u %u %u %u\n",
                          cal[0], cal[1], cal[2], cal[3], cal[4]);
        } else {
            Serial.printf("touch: stored calibration is nonsense "
                          "(%u %u %u %u %u), ignoring it\n",
                          cal[0], cal[1], cal[2], cal[3], cal[4]);
        }
    }
    prefs.end();
}

void chooser_forget_calibration(void) {
    Preferences prefs;
    if (!prefs.begin(NVS_NS, false)) return;
    prefs.remove(NVS_KEY);
    prefs.end();
    Serial.println("touch: calibration forgotten");
}

int chooser_is_calibrated(void) {
    Preferences prefs;
    uint16_t cal[5];
    int ok = 0;
    if (!prefs.begin(NVS_NS, true)) return 0;
    if (prefs.getBytesLength(NVS_KEY) == sizeof(cal)) {
        prefs.getBytes(NVS_KEY, cal, sizeof(cal));
        ok = calibrationLooksReal(cal);
    }
    prefs.end();
    return ok;
}

/* Four corners, tapped once. Without this TFT_eSPI maps raw readings with
 * defaults that are nobody's panel in particular, and every press lands
 * somewhere near where it was meant to - which is what "the touch does
 * not work well" turned out to mean. */
void chooser_calibrate(void) {
    TFT_eSPI &tft = panel_tft();
    uint16_t cal[5];

    tft.fillScreen(COL_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(COL_TEXT, COL_BG);
    tft.drawString("Touch the corners", DISPLAY_PANEL_W / 2, DISPLAY_PANEL_H / 2 - 20, 4);
    tft.setTextColor(COL_DIM, COL_BG);
    tft.drawString("press each arrow as it appears",
                   DISPLAY_PANEL_W / 2, DISPLAY_PANEL_H / 2 + 14, 2);
    delay(1400);

    tft.fillScreen(COL_BG);
    tft.calibrateTouch(cal, COL_HEAD, COL_BG, 15);

    if (!calibrationLooksReal(cal)) {
        Serial.printf("touch: that came out as %u %u %u %u %u, which is not a "
                      "panel - not storing it\n",
                      cal[0], cal[1], cal[2], cal[3], cal[4]);
        tft.fillScreen(COL_BG);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(COL_TEXT, COL_BG);
        tft.drawString("That did not work", DISPLAY_PANEL_W / 2,
                       DISPLAY_PANEL_H / 2, 4);
        tft.setTextDatum(TL_DATUM);
        delay(1500);
        return;
    }

    tft.setTouch(cal);
    Preferences prefs;
    if (prefs.begin(NVS_NS, false)) {
        prefs.putBytes(NVS_KEY, cal, sizeof(cal));
        prefs.end();
    }
    Serial.printf("touch: calibrated %u %u %u %u %u\n",
                  cal[0], cal[1], cal[2], cal[3], cal[4]);
}

/* Wait for a press, report where it landed, and wait for the finger to
 * come off again. Returning only on release is what makes one press one
 * choice: the old code acted on the first reading and then slept for
 * 200ms hoping the finger had gone. */
static bool waitPress(int *x, int *y, uint32_t timeoutMs) {
    TFT_eSPI &tft = panel_tft();
    uint16_t tx = 0, ty = 0;
    uint32_t end = millis() + timeoutMs;
    bool got = false;

    while ((int32_t)(end - millis()) > 0) {
        bool down = tft.getTouch(&tx, &ty);
        if (down) { got = true; break; }
        delay(15);
    }
    if (!got) return false;

    *x = tx; *y = ty;

    /* Off again, with a little patience for the noise a resistive panel
     * makes as the pressure drops. */
    int quiet = 0;
    while (quiet < 3) {
        uint16_t z = tft.getTouchRawZ();
        quiet = (z < 600) ? quiet + 1 : 0;
        delay(15);
    }
    return true;
}

/* ---------------------------------------------------------------- */
/* Drawing                                                           */
/* ---------------------------------------------------------------- */

static void header(const char *text) {
    TFT_eSPI &tft = panel_tft();
    tft.fillRect(0, 0, DISPLAY_PANEL_W, HEAD_H, COL_BG);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(COL_HEAD, COL_BG);
    tft.drawString(text, GAP + 2, 7, 4);
}

/* The picture scale, reachable without a cable.
 *
 * It belongs on these screens rather than in a settings menu somewhere,
 * because it is the one thing worth changing on a machine that is running
 * slowly, and because the difference is visible the moment you go back. */
#define SCALE_W 150
#define SCALE_X (DISPLAY_PANEL_W - GAP - SCALE_W)

static void drawScale(int machineIndex) {
    TFT_eSPI &tft = panel_tft();
    tft.fillRoundRect(SCALE_X, FOOT_TOP + 3, SCALE_W, FOOT_H - 8, 5, COL_TILE);
    tft.drawRoundRect(SCALE_X, FOOT_TOP + 3, SCALE_W, FOOT_H - 8, 5, COL_EDGE);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(COL_TEXT, COL_TILE);
    tft.drawString(boot_scale_for_machine(machineIndex) == 1 ? "picture  1:1"
                                                            : "picture  1.5x",
                   SCALE_X + SCALE_W / 2, FOOT_TOP + FOOT_H / 2 - 2, 2);
    tft.setTextDatum(TL_DATUM);
}

static void footer(const char *left, int machineIndex) {
    TFT_eSPI &tft = panel_tft();
    tft.fillRect(0, FOOT_TOP, DISPLAY_PANEL_W, FOOT_H, COL_BG);
    tft.setTextDatum(ML_DATUM);
    if (left) {
        tft.fillRoundRect(GAP, FOOT_TOP + 3, 120, FOOT_H - 8, 5, COL_TILE);
        tft.drawRoundRect(GAP, FOOT_TOP + 3, 120, FOOT_H - 8, 5, COL_EDGE);
        tft.setTextColor(COL_TEXT, COL_TILE);
        tft.drawString(left, GAP + 14, FOOT_TOP + FOOT_H / 2 - 2, 2);
    }
    if (machineIndex >= 0) drawScale(machineIndex);
    tft.setTextDatum(TL_DATUM);
}

static bool inFooterBack(int x, int y) {
    return y >= FOOT_TOP && x >= GAP && x < GAP + 120;
}

static bool inFooterScale(int x, int y) {
    return y >= FOOT_TOP && x >= SCALE_X;
}

/* Toggle it, remember it for this machine, and show the new state. The
 * machine picks it up on its next frame. */
static void toggleScale(int machineIndex) {
    int next = boot_scale_for_machine(machineIndex) == 1 ? 2 : 1;
    boot_remember_scale(machineIndex, next);
    display_set_scale(next);
    drawScale(machineIndex);
    delay(120);
}

/* A group tile: the names it holds, one to a line. The whole point of
 * the middle screen is to see where a game is without opening anything,
 * so it lists them rather than giving a range. */
static void listTile(const Machine *m, int x, int y, int w, int h,
                     int first, int count, bool pressed);

/* A target. Two lines, because some of them need a subtitle. */
static void tile(int x, int y, int w, int h, const char *line1,
                 const char *line2, bool pressed) {
    TFT_eSPI &tft = panel_tft();
    uint16_t bg = pressed ? COL_PRESS : COL_TILE;
    tft.fillRoundRect(x, y, w, h, 6, bg);
    tft.drawRoundRect(x, y, w, h, 6, COL_EDGE);

    tft.setTextDatum(MC_DATUM);
    if (line2 && line2[0]) {
        tft.setTextColor(COL_TEXT, bg);
        tft.drawString(line1, x + w / 2, y + h / 2 - 10, 2);
        tft.setTextColor(COL_DIM, bg);
        tft.drawString(line2, x + w / 2, y + h / 2 + 12, 2);
    } else {
        tft.setTextColor(COL_TEXT, bg);
        tft.drawString(line1, x + w / 2, y + h / 2, 2);
    }
    tft.setTextDatum(TL_DATUM);
}

/* Enough of a name to recognise it, in the width there is.
 *
 * Measured rather than estimated: the fonts here are proportional, so
 * "Yie Ar Kung-Fu" and "MMMMMMMMMMMMMM" are not the same width, and
 * assuming eight pixels a character threw away characters that fitted. */
static void fit(char *out, int outSize, const char *in, int pixels, int font) {
    TFT_eSPI &tft = panel_tft();
    int n = (int)strlen(in);
    if (n > outSize - 1) n = outSize - 1;
    memcpy(out, in, n);
    out[n] = 0;
    if (tft.textWidth(out, font) <= pixels) return;

    /* Shorten a character at a time, ending in a full stop so the name
     * reads as cut short rather than as a different name. */
    while (n > 1) {
        n--;
        out[n - 1] = '.';
        out[n] = 0;
        if (tft.textWidth(out, font) <= pixels) return;
        out[n - 1] = in[n - 1];     /* put the real character back */
    }
}

static const char *orderedName(const Machine *m, int slot);

/* The names in a group, one to a line. With eight groups a tile holds
 * three or four of them, which fits and means the middle screen answers
 * "where is Nemesis" without anyone opening anything. If a group ever
 * held more than the tile has lines for, the rest are summed up. */
static void listTile(const Machine *m, int x, int y, int w, int h,
                     int first, int count, bool pressed) {
    TFT_eSPI &tft = panel_tft();
    uint16_t bg = pressed ? COL_PRESS : COL_TILE;
    const int line = 19;
    int rows = (h - 12) / line;
    if (rows < 1) rows = 1;

    tft.fillRoundRect(x, y, w, h, 6, bg);
    tft.drawRoundRect(x, y, w, h, 6, COL_EDGE);

    int shown = count <= rows ? count : rows - 1;
    int top = y + (h - (count <= rows ? count : rows) * line) / 2;

    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(COL_TEXT, bg);
    for (int i = 0; i < shown; i++) {
        char name[40];
        fit(name, sizeof(name), orderedName(m, first + i), w - 8, 2);
        tft.drawString(name, x + w / 2, top + i * line, 2);
    }
    if (shown < count) {
        char more[24];
        snprintf(more, sizeof(more), "and %d more", count - shown);
        tft.setTextColor(COL_DIM, bg);
        tft.drawString(more, x + w / 2, top + shown * line, 2);
    }
    tft.setTextDatum(TL_DATUM);
}

/* Flash the tile that was pressed, so a press is visibly a press. */
static void flash(int x, int y, int w, int h, const char *l1, const char *l2) {
    tile(x, y, w, h, l1, l2, true);
    delay(90);
}

/* ---------------------------------------------------------------- */
/* Screen 1: which machine                                           */
/* ---------------------------------------------------------------- */

int chooser_pick_machine(int allowCancel) {
    TFT_eSPI &tft = panel_tft();
    int n = machine_count;
    if (n <= 1) return 0;

    int w = (DISPLAY_PANEL_W - GAP * (n + 1)) / n;
    int h = BODY_H - GAP * 2;
    int y = BODY_TOP + GAP;

    for (;;) {
        tft.fillScreen(COL_BG);
        header("Which machine");
        for (int i = 0; i < n; i++) {
            char sub[28];
            /* Reached from a running machine, this screen is mostly asked
             * for in order to leave it, so say which one that is. */
            if (allowCancel && i == machine_chosen_index())
                snprintf(sub, sizeof(sub), "running now");
            else
                snprintf(sub, sizeof(sub), "%d to choose from",
                         machine_list[i]->entry_count());
            tile(GAP + i * (w + GAP), y, w, h, machine_list[i]->name, sub, false);
        }
        footer(allowCancel ? "back" : 0, -1);

        int tx, ty;
        if (!waitPress(&tx, &ty, 60UL * 1000)) return -1;

        if (allowCancel && inFooterBack(tx, ty)) return -1;

        if (ty >= y && ty < y + h) {
            for (int i = 0; i < n; i++) {
                int x = GAP + i * (w + GAP);
                if (tx >= x && tx < x + w) {
                    flash(x, y, w, h, machine_list[i]->name, "");
                    return i;
                }
            }
        }
    }
}

/* ---------------------------------------------------------------- */
/* Screens 2 and 3: which group, then which game                     */
/* ---------------------------------------------------------------- */

/* The catalogues are not in alphabetical order - the MSX one is whatever
 * order the cartridges were embedded in - and a tile reading "Zaxxon to
 * Antarctic Adventure" tells nobody anything. So the picker works on a
 * sorted view: sOrder[] holds entry numbers sorted by name, with entry 0
 * pinned to the front because that one is the machine on its own rather
 * than a game. */
#define MAX_ENTRIES 128
static int sOrder[MAX_ENTRIES];
static int sOrderCount;
static int sMachineIndex;    /* whose list is on screen, for the footer */

static void buildOrder(const Machine *m) {
    int n = m->entry_count();
    if (n > MAX_ENTRIES) n = MAX_ENTRIES;
    sOrderCount = n;
    for (int i = 0; i < n; i++) sOrder[i] = i;

    /* Insertion sort over the tail, leaving sOrder[0] where it is. A
     * hundred-odd names once per screen; nothing here needs better. */
    for (int i = 2; i < n; i++) {
        int v = sOrder[i];
        int j = i - 1;
        while (j >= 1 && strcasecmp(m->entry_name(sOrder[j]), m->entry_name(v)) > 0) {
            sOrder[j + 1] = sOrder[j];
            j--;
        }
        sOrder[j + 1] = v;
    }
}

/* Both screens index that sorted view, never the machine's own numbering;
 * only the answer is translated back. */
static const char *orderedName(const Machine *m, int slot) {
    return m->entry_name(sOrder[slot]);
}

/* The list cut into eight as evenly as it goes, so the first groups take
 * the remainder rather than the last one being nearly empty. */
static void groupRange(int total, int g, int *first, int *count) {
    int base = total / GROUPS;
    int extra = total % GROUPS;
    int start = g * base + (g < extra ? g : extra);
    int n = base + (g < extra ? 1 : 0);
    *first = start;
    *count = n;
}

static int pickFromGroup(const Machine *m, int first, int count) {
    TFT_eSPI &tft = panel_tft();
    int h = (BODY_H - GAP * (count + 1)) / count;
    if (h > 64) h = 64;
    int w = DISPLAY_PANEL_W - GAP * 2;
    int top = BODY_TOP + GAP;

    for (;;) {
        tft.fillScreen(COL_BG);
        header(m->name);
        for (int i = 0; i < count; i++) {
            char name[40];
            fit(name, sizeof(name), orderedName(m, first + i), w - 20, 4);
            tile(GAP, top + i * (h + GAP), w, h, name, 0, false);
        }
        footer("back", sMachineIndex);

        int tx, ty;
        if (!waitPress(&tx, &ty, 60UL * 1000)) return -1;
        if (inFooterBack(tx, ty)) return -1;
        if (inFooterScale(tx, ty)) { toggleScale(sMachineIndex); continue; }

        for (int i = 0; i < count; i++) {
            int y = top + i * (h + GAP);
            if (ty >= y && ty < y + h) {
                char name[40];
                fit(name, sizeof(name), orderedName(m, first + i), w - 20, 4);
                flash(GAP, y, w, h, name, 0);
                return sOrder[first + i];
            }
        }
    }
}

int chooser_pick_entry(int machineIndex, int allowCancel) {
    TFT_eSPI &tft = panel_tft();
    const Machine *m = machine_list[machineIndex];
    sMachineIndex = machineIndex;
    buildOrder(m);
    int total = sOrderCount;

    if (total <= 0) return -1;

    /* Few enough to show at once: the middle screen would be eight tiles
     * of one game each, which is a screen that asks a question it has
     * already answered. */
    if (total <= 6) return pickFromGroup(m, 0, total);

    int w = (DISPLAY_PANEL_W - GAP * 5) / 4;
    int h = (BODY_H - GAP * 3) / 2;

    for (;;) {
        tft.fillScreen(COL_BG);
        header(m->name);
        for (int g = 0; g < GROUPS; g++) {
            int first, count;
            groupRange(total, g, &first, &count);
            int x = GAP + (g % 4) * (w + GAP);
            int y = BODY_TOP + GAP + (g / 4) * (h + GAP);
            if (count <= 0) { tile(x, y, w, h, "", 0, false); continue; }
            listTile(m, x, y, w, h, first, count, false);
        }
        footer(allowCancel ? "back" : 0, sMachineIndex);

        int tx, ty;
        if (!waitPress(&tx, &ty, 60UL * 1000)) return -1;
        if (allowCancel && inFooterBack(tx, ty)) return -1;
        if (inFooterScale(tx, ty)) { toggleScale(sMachineIndex); continue; }

        for (int g = 0; g < GROUPS; g++) {
            int first, count;
            groupRange(total, g, &first, &count);
            if (count <= 0) continue;
            int x = GAP + (g % 4) * (w + GAP);
            int y = BODY_TOP + GAP + (g / 4) * (h + GAP);
            if (tx >= x && tx < x + w && ty >= y && ty < y + h) {
                listTile(m, x, y, w, h, first, count, true);
                delay(90);
                int chosen = pickFromGroup(m, first, count);
                if (chosen >= 0) return chosen;
                break;          /* back: redraw the groups */
            }
        }
    }
}
