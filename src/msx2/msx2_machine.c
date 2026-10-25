/* msx2_machine.c - an MSX2+, as a Machine: blueMSX.
 *
 * third_party/blueMSX-libretro is libretro's blueMSX, built by the Android
 * app as its own library (libbluemsx.so) and run by
 * src/libretro/lr_frontend.c, beside the Hotbit, which stays fMSX's and
 * is not touched by any of this. What is particular to it is here:
 *
 *   - the machine is blueMSX's "MSX2+ - European" (Machines/ in its
 *     system folder, the card's msx2/system), with the international
 *     keyboard; the Japanese "MSX2+" put half the symbols elsewhere. Its
 *     ROMs come with blueMSX's repository, the rest (MOONSOUND...) are the
 *     owner's Shared Roms. blueMSX takes the machine by the name of its
 *     folder, so any machine there can be asked for;
 *   - a file over 2MB is a hard disk, on a Sunrise IDE controller whose ROM
 *     on the card is Nextor's (Nextor-2.1.4.SunriseIDE.blueMSX.ROM, as
 *     Shared Roms/SUNRISEIDE.rom; the original is SUNRISEIDE-original.rom
 *     beside it): FAT16, which the owner's One Chip MSX disk is. blueMSX
 *     keeps a disk's size in a signed int, so a hard disk must be under
 *     2GB (tools/msx_fat16_disk.py);
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
#include <sys/stat.h>

#include "lr_frontend.h"

static const char *option(const char *k, const char *sysdir)
{
    (void)sysdir;
    if (!strcmp(k, "bluemsx_msxtype")) return "MSX2+ - European";
    if (!strcmp(k, "bluemsx_sunriseide_enable")) {
        struct stat st;
        const char *f = lr_content_path();
        return f && stat(f, &st) == 0 && st.st_size > 2 * 1024 * 1024 ? "enabled" : "disabled";
    }
    return NULL;
}

static const char *const exts[] = {".rom", ".mx2", ".mx1", ".dsk", ".m3u", ".cas", NULL};

static const lr_core msx2 = {
    "msx2", "libbluemsx.so", exts, "MSX2+ BASIC", "msx2entry",
    option, NULL, 1, 0,
};

LR_MACHINE(msx2_machine, "MSX2+ (blueMSX)", msx2);
