/* msx2_machine.c - an MSX2+, as a Machine: blueMSX.
 *
 * third_party/blueMSX-libretro is libretro's blueMSX, built by the Android
 * app as its own library (libbluemsx.so) and run by
 * src/libretro/lr_frontend.c, beside the Hotbit, which stays fMSX's and
 * is not touched by any of this. What is particular to it is here:
 *
 *   - the machine is blueMSX's "MSX2+" (Machines/MSX2+ in its system
 *     folder, the card's msx2/system), whose ROMs are the owner's, from his
 *     RetroArch system folder's Machines/Shared Roms: MSX2P, MSX2PEXT, the
 *     Panasonic disk ROM and the rest. blueMSX takes the machine by the
 *     name of its folder, so any machine there can be asked for;
 *   - the files are the card's msx2/ folder: cartridges (.rom .mx2),
 *     disks (.dsk, or an .m3u of them) and tapes (.cas); entry 0 is the
 *     machine with nothing in it, into its BASIC;
 *   - MSX games read the cursor keys and space themselves, so there is no
 *     keyboard joystick here;
 *   - shown at 4:3, as on a monitor.
 *
 * Android only.
 */
#include <string.h>

#include "lr_frontend.h"

static const char *option(const char *k, const char *sysdir)
{
    (void)sysdir;
    if (!strcmp(k, "bluemsx_msxtype")) return "MSX2+";
    return NULL;
}

static const char *const exts[] = {".rom", ".mx2", ".mx1", ".dsk", ".m3u", ".cas", NULL};

static const lr_core msx2 = {
    "msx2", "libbluemsx.so", exts, "MSX2+ BASIC", "msx2entry",
    option, NULL, 1, 0,
};

LR_MACHINE(msx2_machine, "MSX2+ (blueMSX)", msx2);
