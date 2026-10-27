#!/usr/bin/env python3
"""The Mac's discs, one for each System, each with all the programs.

    python3 tools/make_mac_discs.py OUT_DIR [EXTRA.img ...]

From roms/mac (the owner's discs, not committed):

  papermac.img   System 3.2 (Finder 5.3, 1986) with the games, MacPaint,
                 MacWrite and their documents: Games, Programs, Files
  system608.img  System 6.0.8's disc, the System and TeachText
  system701.img  System 7.0.1's disc, the System and Disk First Aid

it writes three HFS discs of 10MB, each its System Folder (blessed) and
the same Games, Programs and Files folders, plus TeachText:

  "Sistema 3.2.img", "Sistema 6.0.8.img", "Sistema 7.0.1.img"

Each EXTRA disc's folders go into Games on all three, as the Duane Blehm
Collection (archive.org, TheDuaneBlehmCollection: StuntCopter, Zero
Gravity, Cairo ShootOut, PUZZ'L, made free by his family after his death
in 1988).

What only installs a System on another disc (Installer, Installer
Script, Apple HD SC Setup) stays out. The Mac emulation (umac) takes a
disc of any size; 4MB of RAM on the Android board is enough for System 7.
Files go across as MacBinary (hcopy -m), so resource forks, types and
creators are kept. hformat leaves the boot blocks (the first two sectors,
"LK" and the code that loads the System) empty, and such a disc gets the
question mark, so each disc is given its System's, from its source disc.
Needs hfsutils.
"""
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAC = os.path.join(ROOT, "roms", "mac")
SIZE = 10 * 1024 * 1024
LEAVE_OUT = {b"Installer", b"Installer Script", b"Apple HD SC Setup", b"Desktop DB", b"Desktop DF", b"Desktop",
             b"TheVolumeSettingsFolder"}


def run(*a, **k):
    return subprocess.run(a, check=True, capture_output=True, **k).stdout


def walk(path=b":"):
    """Every entry under path on the mounted volume, as (path, is_folder),
    a folder before what is in it. Names are Mac Roman bytes."""
    for line in run("hls", "-1aF", path).split(b"\n"):
        if not line:
            continue
        folder = line.endswith(b":")
        name = line[:-1] if folder or line[-1:] in b"*" else line
        p = path + name if path.endswith(b":") else path + b":" + name
        yield p, folder
        if folder:
            yield from walk(p + b":")


def export(img, store, skip=()):
    """img's files into store as MacBinary; returns [(macpath, local or None)]."""
    run("hmount", img)
    items = []
    try:
        for p, folder in walk():
            if p.split(b":")[1] in skip or LEAVE_OUT & set(p.split(b":")):
                continue
            if folder:
                items.append((p, None))
            else:
                local = os.path.join(store, "%d.bin" % len(os.listdir(store)))
                run("hcopy", "-m", p, local)
                items.append((p, local))
    finally:
        run("humount")
    return items


def build(out, label, boot, parts):
    with open(out, "wb") as f:
        f.truncate(SIZE)
    run("hformat", "-l", label, out)
    with open(boot, "rb") as f, open(out, "r+b") as o:
        o.write(f.read(1024))
    run("hmount", out)
    try:
        for items in parts:
            for p, local in items:
                if local is None:
                    subprocess.run(["hmkdir", p], capture_output=True)
                else:
                    run("hcopy", "-m", local, p)
        run("hattrib", "-b", b":System Folder")
        print(label + ":", run("hvol").decode("mac_roman").strip().splitlines()[-1])
    finally:
        run("humount")


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    store = tempfile.mkdtemp()
    try:
        s32 = export(os.path.join(MAC, "papermac.img"), store)
        extras = [(b":Games" + p, l) for extra in sys.argv[2:] for p, l in export(extra, store)]
        programs = [i for i in s32 if i[0].split(b":")[1] in (b"Games", b"Programs", b"Files")] + extras
        s32 += extras
        s608 = export(os.path.join(MAC, "system608.img"), store)
        teach = [(b":Programs:TeachText", l) for p, l in s608 if p == b":TeachText"]
        s608 = [i for i in s608 if i[0] != b":TeachText"]
        s701 = export(os.path.join(MAC, "system701.img"), store)
        build(os.path.join(out, "Sistema 3.2.img"), "Sistema 3.2", os.path.join(MAC, "papermac.img"),
              [s32, teach])
        build(os.path.join(out, "Sistema 6.0.8.img"), "Sistema 6.0.8", os.path.join(MAC, "system608.img"),
              [s608, programs, teach])
        build(os.path.join(out, "Sistema 7.0.1.img"), "Sistema 7.0.1", os.path.join(MAC, "system701.img"),
              [s701, programs, teach])
    finally:
        shutil.rmtree(store)


if __name__ == "__main__":
    main()
