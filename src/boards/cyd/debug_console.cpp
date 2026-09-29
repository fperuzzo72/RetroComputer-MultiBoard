/* debug_console.cpp - serial self-test console. See debug_console.h.
 *
 * Board-level, not machine-level: it knows how to type into a machine and
 * read its screen back, but not what machine it is. Anything only one
 * machine can answer goes through machine->debug_command().
 *
 * Commands (one per line, 115200 8N1):
 *   s            dump the machine's text screen, non-ASCII as {XX}
 *   t <text>     type text as if on the keyboard (\n = Return)
 *   g <code>     draw a character's 8x8 glyph
 *   r <a> [n]    peek the machine's memory
 *   z [1|2]      picture scale, 1:1 or 1.5x
 *   x <n>        panel test pattern
 *   w <0|1>      colour byte swap
 *   n <0|1>      sound on/off
 *   a [hz] [ms]  test tone straight to the DAC
 *   b <0|1>      BLE scan on/off
 *   k <0|1>      dump raw HID reports
 *   m <0|1>      mount/unmount the SD card
 *   h            heap, frame rate, keyboard and audio counters
 *   ?            this list
 */
#include <Arduino.h>
#include "debug_console.h"
#include "board.h"
#include "ble_keyboard.h"
#include "sd_mount.h"
#include "machine.h"
#include "boot_menu.h"
#include "chooser.h"
#include "selector.h"
#include "panel.h"
#include <esp_system.h>

extern "C" {
void display_request_test_pattern(int which);
void display_set_swap_bytes(int on);
void display_set_scale(int scale);
int  display_get_scale(void);
unsigned long display_full_repaints(void);
unsigned long display_blit_us(void);
void display_blit_us_reset(void);
void audio_test_tone(int hz, int ms);
unsigned long audio_samples_written(void);
}

/* A few character codes worth naming when they turn up in a screen dump;
 * everything else prints as its hex code. These are the Hotbit's, and the
 * labels are only a reading aid - docs/KEYBOARD.md has the measured
 * table. */
static const char *charName(uint8_t c) {
    switch (c) {
        case 0x80: return "C,"; case 0x87: return "c,";
        case 0x84: return "A'"; case 0xA0: return "a'";
        case 0x90: return "E'"; case 0x82: return "e'";
        case 0x89: return "I'"; case 0xA1: return "i'";
        case 0x8A: return "O'"; case 0xA2: return "o'";
        case 0x8B: return "U'"; case 0xA3: return "u'";
        case 0x8F: return "A`"; case 0x85: return "a`";
        case 0x97: return "u`";
        case 0xB0: return "A~"; case 0xB1: return "a~";
        case 0xB4: return "O~"; case 0xB5: return "o~";
        case 0x8C: return "A^"; case 0x83: return "a^";
        case 0x8D: return "E^"; case 0x88: return "e^";
        case 0x8E: return "O^"; case 0x93: return "o^";
        case 0x96: return "u^";
        case 0x9A: return "U\""; case 0x81: return "u\"";
        default:   return nullptr;
    }
}

static void dumpScreen() {
    uint8_t row[64];
    Serial.printf("%s\n", machine->screen_mode_name());
    for (int y = 0; y < 24; y++) {
        int cols = machine->screen_row(y, row, sizeof(row));
        if (cols <= 0) { Serial.println("(not a text mode)"); return; }
        Serial.printf("%2d |", y);
        for (int x = 0; x < cols; x++) {
            uint8_t c = row[x];
            if (c >= 0x20 && c < 0x7F) Serial.write(c);
            else if (c == 0x00 || c == 0x20) Serial.write(' ');
            else {
                const char *n = charName(c);
                if (n) Serial.printf("[%s]", n);
                else   Serial.printf("{%02X}", c);
            }
        }
        Serial.println("|");
    }
}

static void handleLine(char *line) {
    switch (line[0]) {
        case 's':
            dumpScreen();
            break;

        case 't': {
            /* "\n" in the text is a real Return, so a whole line can be
             * typed and entered in one command. */
            char *text = line[1] == ' ' ? line + 2 : line + 1;
            char *r = text, *w = text;
            while (*r) {
                if (r[0] == '\\' && r[1] == 'n') { *w++ = '\n'; r += 2; }
                else *w++ = *r++;
            }
            *w = 0;
            Serial.printf("typing %d chars\n", machine->type(text));
            break;
        }

        case 'g': {
            int code = 0;
            if (sscanf(line + 1, "%i", &code) == 1) {
                uint8_t rows[8];
                if (!machine->char_pattern(code, rows)) { Serial.println("no pattern table"); break; }
                Serial.printf("char 0x%02X:\n", code);
                for (int y = 0; y < 8; y++) {
                    char out[10];
                    for (int x = 0; x < 8; x++) out[x] = (rows[y] & (0x80 >> x)) ? '#' : '.';
                    out[8] = 0;
                    Serial.printf("  %s\n", out);
                }
            }
            break;
        }

        case 'r': {
            int addr = 0, len = 8;
            if (sscanf(line + 1, "%i %i", &addr, &len) >= 1) {
                if (len < 1) len = 1;
                if (len > 64) len = 64;
                Serial.printf("%04X:", addr);
                for (int i = 0; i < len; i++) Serial.printf(" %02X", machine->peek(addr + i));
                Serial.println();
            }
            break;
        }

        case 'z': {
            int scale = 0;
            if (sscanf(line + 1, "%d", &scale) != 1 || (scale != 1 && scale != 2))
                scale = display_get_scale() == 1 ? 2 : 1;   /* bare 'z' toggles */
            display_set_scale(scale);
            boot_remember_scale(machine_chosen_index(), scale);
            Serial.printf("picture scale %s, remembered for %s\n",
                          scale == 1 ? "1:1 (crisp, small)" : "1.5x (nearly full screen)",
                          machine->name);
            break;
        }

        case 'x': {
            int which = 0;
            sscanf(line + 1, "%d", &which);
            display_request_test_pattern(which);
            Serial.printf("test pattern %d (0 black, 1 red, 2 green, 3 blue, "
                          "4 white block, 5 the same block through the machine's path)\n", which);
            break;
        }

        case 'w': {
            int on = 1;
            sscanf(line + 1, "%d", &on);
            display_set_swap_bytes(on);
            Serial.printf("colour byte swap %s\n", on ? "on" : "off");
            break;
        }

        case 'n': {
            int on = 1;
            sscanf(line + 1, "%d", &on);
            machine->set_sound(on);
            Serial.printf("sound %s\n", on ? "on" : "off");
            break;
        }

        case 'a': {
            int hz = 440, ms = 600;
            sscanf(line + 1, "%d %d", &hz, &ms);
            Serial.printf("test tone %d Hz for %d ms (samples sent so far: %lu)\n",
                          hz, ms, audio_samples_written());
            audio_test_tone(hz, ms);
            Serial.println("tone done");
            break;
        }

        case 'b': {
            int on = 1;
            sscanf(line + 1, "%d", &on);
            ble_keyboard_scan(on);
            Serial.printf("BLE scan %s (%lu adverts seen)\n",
                          on ? "on" : "off", ble_keyboard_adverts_seen());
            break;
        }

        case 'k': {
            int on = 1;
            sscanf(line + 1, "%d", &on);
            ble_keyboard_log_reports(on);
            Serial.printf("HID report dump %s, %lu report(s) received so far\n",
                          on ? "on" : "off", ble_keyboard_report_count());
            break;
        }

        case 'm': {
            int on = 1;
            sscanf(line + 1, "%d", &on);
            if (on) Serial.printf("mounting the card: %s\n", sd_mount_init() ? "ok" : "failed");
            else    sd_unmount();
            Serial.printf("free heap %u\n", board_free_heap());
            break;
        }

        case 'j': {
            /* Hand the machine a keyboard report we made up.
             *
             * "This key does nothing" has two causes that look identical
             * from the outside: the keyboard never sent it, or the
             * firmware dropped it. This settles which, without needing
             * the keyboard that cannot send it. Modifiers are the byte
             * before the keys: 01 LCtrl, 02 LShift, 04 LAlt, 08 LGUI,
             * 10 RCtrl, 20 RShift, 40 AltGr, 80 RGUI.
             *
             *   j 4          hold Left Alt for a moment
             *   j 5 4e       hold Left Ctrl + Left Alt and PageDown
             */
            unsigned mods = 0, k1 = 0, k2 = 0;
            int n = sscanf(line + 1, "%x %x %x", &mods, &k1, &k2);
            if (n < 1) { Serial.println("j <mods> [key] [key], all hex"); break; }
            uint8_t rep[8] = { (uint8_t)mods, 0, (uint8_t)k1, (uint8_t)k2, 0, 0, 0, 0 };
            uint8_t up[8]  = { 0, 0, 0, 0, 0, 0, 0, 0 };
            ble_keyboard_inject(rep);
            Serial.printf("holding mods %02X key %02X %02X\n", mods, k1, k2);
            delay(120);
            /* Read the matrix back while it is still held, so this says
             * what the machine was told rather than what the screen did
             * about it. A zero bit is a key down. */
            if (machine->matrix_row) {
                Serial.print("matrix:");
                for (int r = 0; r < 11; r++)
                    Serial.printf(" %d=%02X", r, machine->matrix_row(r));
                Serial.println();
            }
            delay(400);
            ble_keyboard_inject(up);
            Serial.println("released");
            break;
        }

        case 'o':
            selector_open();
            Serial.println("selector open on the panel - tap a row, or anywhere else to cancel");
            break;

        case 'u': {
            /* Touch pressure, once every 100ms for three seconds.
             *
             * Worth having: the hold-to-open gesture now reads pressure
             * directly rather than calling getTouch(), because getTouch()
             * cost half of every frame. Anything at or above 600 counts as
             * a finger, which is the threshold TFT_eSPI itself uses. Press
             * the glass while this runs and watch the number. */
            if (line[1] == ' ' && line[2] == 'c') {
                chooser_calibrate();
                break;
            }
            if (line[1] == ' ' && line[2] == 'x') {
                chooser_forget_calibration();
                break;
            }
            Serial.println("touch pressure for 3s - press the screen (600+ is a finger)");
            for (int i = 0; i < 30; i++) {
                uint16_t z = panel_tft().getTouchRawZ();
                Serial.printf("  z %4u%s\n", z, z >= 600 ? "   <- finger" : "");
                delay(100);
            }
            break;
        }

        case 'e': {
            /* Same choice the boot menu offers, for when there is a cable
             * in hand and no finger on the glass. */
            int mi = -1, en = 0;
            if (sscanf(line + 1, "%d %d", &mi, &en) >= 1 && mi >= 0) {
                machine_choose(mi, en);
                Serial.printf("chose %s / %s - rebooting\n",
                              machine->name, machine->entry_name(en));
                delay(100);
                esp_restart();
            }
            for (int m = 0; m < machine_count; m++) {
                for (int i = 0; i < machine_list[m]->entry_count(); i++)
                    Serial.printf(" %c e %d %d   %s / %s\n",
                                  (machine_list[m] == machine &&
                                   i == machine->selected_entry()) ? '*' : ' ',
                                  m, i, machine_list[m]->name,
                                  machine_list[m]->entry_name(i));
            }
            break;
        }
        case 'h': {
            static unsigned long lastFrames = 0, lastFramesBlit = 0;
            static unsigned long lastMs = 0;
            unsigned long frames = machine->frames();
            unsigned long now = millis();
            float fps = (lastMs && now > lastMs)
                        ? (frames - lastFrames) * 1000.0f / (now - lastMs) : 0.0f;
            unsigned long blitUs = display_blit_us();
            display_blit_us_reset();
            lastFrames = frames; lastMs = now;

            Serial.printf("%s: free heap %u, keyboard %s, %.1f fps, typing %d\n",
                          machine->name, board_free_heap(),
                          ble_keyboard_connected() ? "connected" : "not connected",
                          fps, machine->typing());
            Serial.printf("HID reports received: %lu, audio samples sent: %lu, sound %s\n",
                          ble_keyboard_report_count(), audio_samples_written(),
                          machine->sound_on() ? "on" : "off");
            Serial.printf("full-panel repaints: %lu\n", display_full_repaints());
            if (fps > 0.0f && frames > lastFramesBlit) {
                float perFrameUs = (float)blitUs / (float)(frames - lastFramesBlit);
                Serial.printf("blit %.1f ms/frame, %.0f%% of a %.1f ms frame\n",
                              perFrameUs / 1000.0f,
                              100.0f * perFrameUs / (1000000.0f / fps),
                              1000.0f / fps);
            }
            lastFramesBlit = frames;
            break;
        }

        default:
            if (machine->debug_command(line)) break;
            /* fall through to help */
        case '?':
            Serial.println("s=screen  t <text>=type (\\n = Return)  g <code>=glyph  r <addr> [n]=peek");
            Serial.println("z [1|2]=scale  x <n>=test pattern  w <0|1>=byte swap");
            Serial.println("n <0|1>=sound  a [hz] [ms]=test tone  b <0|1>=BLE scan  k <0|1>=HID dump");
            Serial.println("m <0|1>=SD card  e [machine entry]=boot choice  o=selector  h=status");
            Serial.println("u=touch pressure  u c=calibrate  u x=forget the calibration");
            Serial.println("j <mods> [key] [key]=inject a keyboard report, all hex");
            if (machine->debug_help()[0]) Serial.println(machine->debug_help());
            break;
    }
}

static void consoleTask(void *arg) {
    (void)arg;
    char line[160];
    size_t len = 0;
    for (;;) {
        while (Serial.available()) {
            char c = (char)Serial.read();
            if (c == '\r') continue;
            if (c == '\n') {
                line[len] = 0;
                if (len) handleLine(line);
                len = 0;
            } else if (len < sizeof(line) - 1) {
                line[len++] = c;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void debug_console_init(void) {
    /* Core 0: core 1 belongs to the machine and its rendering, both at
     * priority 5, and a console pinned there would rarely get to run. */
    xTaskCreatePinnedToCore(consoleTask, "console", 4096, NULL, 2, NULL, 0);
}
