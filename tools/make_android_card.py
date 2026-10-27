#!/usr/bin/env python3
"""The Android app's card folder, gathered from the owner's own files.

    python3 tools/make_android_card.py OUT_DIR [machine ...]

Writes OUT_DIR/RetroComputer, laid out as the app reads it (README,
"Android"), for the machines named (all of them when none is):

  msx       msx/       the No-Intro MSX1 set (iCloud 00 ROMS Emuladores)
  spectrum  spectrum/  roms/spectrum-snaps and roms/spectrum-tapes-kept
  c64       c64/       OneLoad64's root .crt files and the US romset
  amiga     amiga/     the US romset (IPF, one zip a game), Kickstarts in system/
  msx2      msx2/      the No-Intro MSX2 set, blueMSX's system folder with the
                       Shared Roms, Nextor as the Sunrise IDE ROM, and the One
                       Chip MSX card rebuilt under 2GB (tools/msx_fat16_disk.py)
  pc        pc/, msdos.img  the MS-DOS floppy (tools/make_pc_floppy.py) and the
                       8086 disk (tools/make_dos_hd.py's)
  pc386     pc386/     the 386 disk (tools/make_dos_hd.py's)
  mac       mac/       one disc for each System, all the programs on each
                       (tools/make_mac_discs.py)

Copy it with `adb push --sync OUT_DIR/RetroComputer /sdcard/`, which
resumes where a dropped cable left it. Nothing here is committed: these are
the owner's files. Needs 7z and mtools.
"""
import io
import os
import shutil
import subprocess
import sys
import urllib.request
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOME = os.path.expanduser("~")
ICLOUD = os.path.join(HOME, "Library/Mobile Documents/com~apple~CloudDocs")
ROMSETS = os.path.join(ICLOUD, "Downloads/00 ROMS Emuladores")
EMUS = os.path.join(ICLOUD, "01 Arquivos/Emuladores")
RA_SYSTEM = os.path.join(EMUS, "EMULATOR GAMES/Retroarch data/System")
DOS = os.path.join(HOME, "Downloads/DOS files")
NEXTOR = "https://github.com/Konamiman/Nextor/releases/download/v2.1.4/Nextor-2.1.4.SunriseIDE.blueMSX.ROM"


def from_zip_of_zips(src, out, exts):
    """A No-Intro style set: a zip holding a zip per game."""
    n = 0
    with zipfile.ZipFile(src) as z:
        for i in z.infolist():
            if not i.filename.lower().endswith(".zip") or "[BIOS]" in i.filename:
                continue
            with zipfile.ZipFile(io.BytesIO(z.read(i))) as inner:
                for j in inner.infolist():
                    if j.filename.lower().rsplit(".", 1)[-1] in exts:
                        with open(os.path.join(out, os.path.basename(j.filename)), "wb") as o:
                            o.write(inner.read(j))
                        n += 1
    return n


def msx(card):
    out = os.path.join(card, "msx")
    os.makedirs(out, exist_ok=True)
    return from_zip_of_zips(os.path.join(ROMSETS, "microsoft-msx_msx2_romset/Microsoft - MSX.zip"), out,
                            ("rom", "mx1"))


def spectrum(card):
    out = os.path.join(card, "spectrum")
    os.makedirs(out, exist_ok=True)
    n = 0
    for d, ext in (("spectrum-snaps", ".sna"), ("spectrum-tapes-kept", ".tap")):
        for f in os.listdir(os.path.join(ROOT, "roms", d)):
            if f.endswith(ext):
                shutil.copy(os.path.join(ROOT, "roms", d, f), out)
                n += 1
    return n


def c64(card):
    out = os.path.join(card, "c64")
    os.makedirs(out, exist_ok=True)
    n = from_zip_of_zips(os.path.join(EMUS, "commodore-64-romset-us.zip"), out, ("crt", "prg", "d64"))
    subprocess.run(["7z", "e", "-y", "-r-", "-o" + out, os.path.join(HOME, "Downloads/OneLoad64-Games-Collection-v5.7z"),
                    "*.crt"], check=True, stdout=subprocess.DEVNULL)
    return n + sum(1 for f in os.listdir(out) if f.endswith(".crt"))


def amiga(card):
    out = os.path.join(card, "amiga")
    os.makedirs(os.path.join(out, "system"), exist_ok=True)
    n = 0
    with zipfile.ZipFile(os.path.join(EMUS, "commodore-amiga-romset-us.zip")) as z:
        for i in z.infolist():
            b = i.filename.split("/")[-1]
            if b.lower().endswith(".zip"):
                with open(os.path.join(out, b), "wb") as o:
                    o.write(z.read(i))
                n += 1
    for f in os.listdir(RA_SYSTEM):
        if f.startswith("kick"):
            shutil.copy(os.path.join(RA_SYSTEM, f), os.path.join(out, "system"))
    return n


def msx2(card):
    out = os.path.join(card, "msx2")
    system = os.path.join(out, "system")
    os.makedirs(out, exist_ok=True)
    n = from_zip_of_zips(os.path.join(ROMSETS, "microsoft-msx_msx2_romset/Microsoft - MSX2.zip"), out,
                         ("rom", "mx2", "dsk"))
    # blueMSX's Machines and Databases at the top of its system folder, not
    # under bluemsx/ as its repository ships them
    shutil.copytree(os.path.join(ROOT, "third_party/blueMSX-libretro/system/bluemsx"), system, dirs_exist_ok=True)
    shared = os.path.join(system, "Machines", "Shared Roms")
    shutil.copytree(os.path.join(RA_SYSTEM, "Machines", "Shared Roms"), shared, dirs_exist_ok=True)
    os.replace(os.path.join(shared, "SUNRISEIDE.rom"), os.path.join(shared, "SUNRISEIDE-original.rom"))
    with urllib.request.urlopen(NEXTOR) as r, open(os.path.join(shared, "SUNRISEIDE.rom"), "wb") as o:
        o.write(r.read())
    subprocess.run([sys.executable, os.path.join(ROOT, "tools/msx_fat16_disk.py"),
                    os.path.join(HOME, "Downloads/MSXFAT16.dd"), os.path.join(out, "OCM MSXFAT16.dsk")],
                   check=True, stdout=subprocess.DEVNULL)
    return n + 1


def pc(card):
    os.makedirs(os.path.join(card, "pc"), exist_ok=True)
    subprocess.run([sys.executable, os.path.join(ROOT, "tools/make_pc_floppy.py"), os.path.join(DOS, "msdos.img"),
                    os.path.join(card, "pc", "MS-DOS 6.22.img")], check=True, stdout=subprocess.DEVNULL)
    shutil.copy(os.path.join(DOS, "DOS completo 8086.img"), os.path.join(card, "msdos.img"))
    return 2


def pc386(card):
    os.makedirs(os.path.join(card, "pc386"), exist_ok=True)
    shutil.copy(os.path.join(DOS, "DOS completo 386.img"), os.path.join(card, "pc386", "DOS completo.img"))
    return 1


def mac(card):
    subprocess.run([sys.executable, os.path.join(ROOT, "tools/make_mac_discs.py"), os.path.join(card, "mac")],
                   check=True, stdout=subprocess.DEVNULL)
    return 3


MACHINES = {"msx": msx, "spectrum": spectrum, "c64": c64, "amiga": amiga, "msx2": msx2, "pc": pc,
            "pc386": pc386, "mac": mac}


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    card = os.path.join(sys.argv[1], "RetroComputer")
    for name in sys.argv[2:] or MACHINES:
        print(f"{name}: {MACHINES[name](card)} files", flush=True)


if __name__ == "__main__":
    main()
