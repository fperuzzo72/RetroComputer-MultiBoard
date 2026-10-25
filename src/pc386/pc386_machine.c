/* pc386_machine.c - a 386/486 PC with VGA, as a Machine: DOSBox Pure.
 *
 * lib/dosbox-pure is a libretro core, built by the Android app as its own
 * library (libdosbox_pure.so) and run by src/libretro/lr_frontend.c. What
 * is particular to it is here:
 *
 *   - entry 0 is the command line with no disk, the rest the card's pc386/
 *     folder (.zip, .img, .ima, .vhd, .iso, .cue, .conf) and the 8086 PC's
 *     /msdos.img. DOSBox Pure's own start menu then lists the programs on
 *     it, or AUTOBOOT.DBP runs one (tools/make_dos_hd.py);
 *   - DOS's own US-International keyboard, "ux" on codepage 850, which has
 *     every letter Portuguese needs (437 has no a or o with a tilde);
 *   - shown at 4:3 whatever the mode, as on a PC's monitor.
 *
 * Android only: there is no 386 on the ESP32 boards.
 */
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "lr_frontend.h"

static const char *option(const char *k, const char *sysdir)
{
    (void)sysdir;
    if (!strcmp(k, "dosbox_pure_keyboard_layout")) return "ux 850";   /* lib/dosbox-pure, NOT UPSTREAM */
    if (!strcmp(k, "dosbox_pure_on_screen_keyboard")) return "false";   /* there is a keyboard */
    if (!strcmp(k, "dosbox_pure_mouse_input")) return "virtual";        /* the trackpad moves it */
    return NULL;
}

/* the 8086 PC's hard disk, for the same programs at a 386's speed */
static void extra(const char *card, void (*add)(const char *name, const char *path))
{
    char path[512];
    struct stat st;
    snprintf(path, sizeof path, "%s/msdos.img", card);
    if (stat(path, &st) == 0) add("msdos.img (o C: do PC)", path);
}

static const char *const exts[] = {".zip", ".img", ".ima", ".vhd", ".iso", ".cue", ".conf", ".dosz", NULL};

static const lr_core pc386 = {
    "pc386", "libdosbox_pure.so", exts, "Linha de comando", "pc386entry",
    option, extra, 1, 0,
};

LR_MACHINE(pc386_machine, "PC 386 (DOSBox)", pc386);
