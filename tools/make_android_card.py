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
  pc386     pc386/     the 386 disk (tools/make_dos_hd.py's), with the shareware
                       games of tools/add_dos_games.py
  scummvm   scummvm/   the adventures their makers set free, floppy versions,
                       from scummvm.org (SHA-256 checked), a folder each with
                       its <gameid>.scummvm hook, and the data files of
                       tools/build_scummvm_core.sh's build in system/scummvm
  mac       mac/       one disc for each System, all the programs on each, and
                       the Duane Blehm Collection (tools/make_mac_discs.py)

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
CACHE = os.path.join(HOME, "Library/Caches/retrocomputer")   # the free downloads, fetched once
BLEHM = "https://archive.org/download/TheDuaneBlehmCollection/Blehm_Collection.img"
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
    disk = os.path.join(card, "pc386", "DOS completo.img")
    shutil.copy(os.path.join(DOS, "DOS completo 386.img"), disk)
    subprocess.run([sys.executable, os.path.join(ROOT, "tools/add_dos_games.py"), disk, CACHE],
                   check=True, stdout=subprocess.DEVNULL)
    return 1


def mac(card):
    os.makedirs(CACHE, exist_ok=True)
    blehm = os.path.join(CACHE, "Blehm_Collection.img")
    if not os.path.exists(blehm):
        with urllib.request.urlopen(BLEHM) as r, open(blehm, "wb") as o:
            o.write(r.read())
    subprocess.run([sys.executable, os.path.join(ROOT, "tools/make_mac_discs.py"), os.path.join(card, "mac"), blehm],
                   check=True, stdout=subprocess.DEVNULL)
    return 3


SCUMMVM_EXTRAS = "https://downloads.scummvm.org/frs/extras/"
# folder on the card, game id (the hook), the download
SCUMMVM_GAMES = [
    ("Beneath a Steel Sky", "sky", "Beneath a Steel Sky/BASS-Floppy-1.3.zip"),
    ("Lure of the Temptress", "lure", "Lure of the Temptress/lure-1.1.zip"),
    ("Flight of the Amazon Queen", "queen", "Flight of the Amazon Queen/FOTAQ_Floppy.zip"),
    ("Drascula", "drascula", "Drascula_ The Vampire Strikes Back/drascula-1.0.zip"),
    ("Dreamweb", "dreamweb", "Dreamweb/dreamweb-uk-1.1.zip"),
]
# what the five engines and the launcher use of the build's data files
SCUMMVM_DATA = ("sky.cpt", "lure.dat", "queen.tbl", "drascula.dat", "fonts.dat", "encoding.dat",
                "helpdialog.zip", "vkeybd_default.zip")


def scummvm(card):
    import hashlib
    import urllib.parse
    out = os.path.join(card, "scummvm")
    extra = os.path.join(out, "system", "scummvm", "extra")
    os.makedirs(extra, exist_ok=True)
    os.makedirs(os.path.join(out, "system", "scummvm", "theme"), exist_ok=True)
    os.makedirs(CACHE, exist_ok=True)
    for folder, gameid, rel in SCUMMVM_GAMES:
        url = SCUMMVM_EXTRAS + urllib.parse.quote(rel)
        zpath = os.path.join(CACHE, rel.rsplit("/", 1)[-1])
        if not os.path.exists(zpath):
            with urllib.request.urlopen(url) as r, open(zpath, "wb") as o:
                o.write(r.read())
        with urllib.request.urlopen(url + ".sha256") as r:
            want = r.read().decode().split()[0].lower()
        if hashlib.sha256(open(zpath, "rb").read()).hexdigest() != want:
            sys.exit(f"{zpath}: SHA-256 does not match scummvm.org's")
        game = os.path.join(out, folder)
        os.makedirs(game, exist_ok=True)
        with zipfile.ZipFile(zpath) as z:
            for i in z.infolist():
                if i.is_dir() or i.filename.startswith("__MACOSX"):
                    continue
                with open(os.path.join(game, os.path.basename(i.filename)), "wb") as o:
                    o.write(z.read(i))
        open(os.path.join(game, gameid + ".scummvm"), "w").write(gameid)
    with zipfile.ZipFile(os.path.join(ROOT, "build/scummvm/scummvm.zip")) as z:
        for name in SCUMMVM_DATA:
            if "scummvm/extra/" + name in z.namelist():
                with open(os.path.join(extra, name), "wb") as o:
                    o.write(z.read("scummvm/extra/" + name))
    return len(SCUMMVM_GAMES)


MACHINES = {"msx": msx, "spectrum": spectrum, "c64": c64, "amiga": amiga, "msx2": msx2, "pc": pc,
            "pc386": pc386, "mac": mac, "scummvm": scummvm}


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    card = os.path.join(sys.argv[1], "RetroComputer")
    for name in sys.argv[2:] or MACHINES:
        print(f"{name}: {MACHINES[name](card)} files", flush=True)


if __name__ == "__main__":
    main()
