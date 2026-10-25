/* amiga_machine.c - the Commodore Amiga, as a Machine: PUAE.
 *
 * third_party/libretro-uae is libretro's PUAE, built by the Android app as
 * its own library (libpuae.so) and run by src/libretro/lr_frontend.c, with
 * third_party/capsimg (libcapsimg.so) for the IPF disks the romsets come
 * in. What is particular to it is here:
 *
 *   - the files are the card's amiga/ folder: floppies (.adf .adz .dms
 *     .ipf), hard disks (.hdf .hdz .lha), or a .zip or .m3u of them;
 *     entry 0 is the Amiga with no disk;
 *   - the Kickstart ROMs are the owner's, in amiga/system with the names
 *     PUAE looks for (kick34005.A500, kick40068.A1200...). PUAE picks the
 *     model from the disk; with no Kickstart 1.3 there, it runs AROS, the
 *     free one it carries, which boots far fewer games;
 *   - the keyboard joystick is on from the start: the arrows and Ctrl are
 *     the joystick, F10 gives the arrows back to the keyboard, F9 moves it
 *     to the other RetroPad;
 *   - shown at 4:3, as on the Amiga's monitor.
 *
 * Android only.
 */
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "lr_frontend.h"

static const char *option(const char *k, const char *sysdir)
{
    if (!strcmp(k, "puae_kickstart")) {
        char path[600];
        struct stat st;
        snprintf(path, sizeof path, "%s/kick34005.A500", sysdir);
        return stat(path, &st) == 0 ? NULL : "aros";
    }
    /* The smallest fixed crop: the picture centred with a thin border.
     * Uncropped, PUAE gives the whole PAL overscan and a game sits off to
     * the right with its right edge cut; "auto" follows each screen and
     * changes size, which at 4:3 changes the scale. */
    if (!strcmp(k, "puae_crop")) return "minimum";
    /* PUAE's own default makes right Ctrl swap joystick and mouse; here
     * Ctrl is the fire button, and the swap would come with no warning. */
    if (!strcmp(k, "puae_mapper_mouse_toggle")) return "---";
    /* No floppy drive noise (the owner's choice): "100" is PUAE's
     * "disabled", the value being how much the volume is cut. */
    if (!strcmp(k, "puae_floppy_sound")) return "100";
    return NULL;
}

static const char *const exts[] = {".adf", ".adz", ".dms", ".fdi", ".ipf", ".hdf", ".hdz",
                                   ".lha", ".zip", ".m3u", ".uae", NULL};

static const lr_core amiga = {
    "amiga", "libpuae.so", exts, "Sem disquete", "amigaentry",
    option, NULL, 1, 1,
};

LR_MACHINE(amiga_machine, "Commodore Amiga", amiga);
