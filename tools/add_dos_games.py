#!/usr/bin/env python3
"""Freely distributable DOS games, into a disk from tools/make_dos_hd.py.

    python3 tools/add_dos_games.py DISK.img CACHE_DIR

Each game goes in C:\\GAMES\\<folder>, with a batch file in C:\\BAT and a
line in AJUDA. Only what its makers let anyone copy, each with its own
terms inside (README.TXT, VENDOR.DOC, LICENSE.DOC):

  DOOM      Doom 1.9, shareware episode (id's idstuff on the idgames archive)
  HERETIC   Heretic 1.2, shareware episode (the same)
  WOLF3D    Wolfenstein 3D 1.4, shareware episode (archive.org, w3d-box)
  KEEN1     Commander Keen 1, Marooned on Mars, shareware (archive.org, keen1-sw)

They want a 386 (Keen an EGA card), so they are for the PC 386's disk.
The downloads are kept in CACHE_DIR and fetched only once. Doom's and
Heretic's floppies are one self-extracting zip split in two (DEICE), and
Wolfenstein's is an Apogee archive: 7z opens all three, so nothing of DOS
runs here. Needs mtools and 7z.
"""
import os
import subprocess
import sys
import tempfile
import urllib.request
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_dos_hd  # noqa: E402

IDSTUFF = "https://www.gamers.org/pub/idgames/idstuff"
# folder, batch name, its program, AJUDA's line, download, how to unpack
GAMES = [
    ("DOOM", "DOOM", "DOOM", "Doom", IDSTUFF + "/doom/doom19s.zip", ("DOOMS_19.1", "DOOMS_19.2")),
    ("HERETIC", "HERETIC", "HERETIC", "Heretic", IDSTUFF + "/heretic/htic_v12.zip", ("HTIC_V12.1", "HTIC_V12.2")),
    ("WOLF3D", "WOLF3D", "WOLF3D", "Wolfenstein 3D", "https://archive.org/download/w3d-box/1wolf14.zip",
     ("W3D1_BBS._1",)),
    ("KEEN1", "KEEN1", "KEEN1", "Commander Keen 1", "https://archive.org/download/keen1-sw/keen1.zip", None),
]


def fetch(url, cache):
    path = os.path.join(cache, url.rsplit("/", 1)[-1])
    if not os.path.exists(path):
        with urllib.request.urlopen(url) as r, open(path + ".part", "wb") as o:
            o.write(r.read())
        os.replace(path + ".part", path)
    return path


def unpack(zpath, parts, out):
    tmp = tempfile.mkdtemp()
    with zipfile.ZipFile(zpath) as z:
        z.extractall(out if parts is None else tmp)
    if parts:
        joined = os.path.join(tmp, "joined")
        with open(joined, "wb") as o:
            for p in parts:
                o.write(open(os.path.join(tmp, p), "rb").read())
        subprocess.run(["7z", "x", "-y", "-o" + out, joined], check=True, stdout=subprocess.DEVNULL)
    for f in os.listdir(out):   # Apogee's archive leaves some unreadable
        os.chmod(os.path.join(out, f), 0o644)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    img, cache = sys.argv[1:]
    os.makedirs(cache, exist_ok=True)
    disk = f"{img}@@{make_dos_hd.PART}"
    tmp = tempfile.mkdtemp()
    lines = []
    for folder, bat, exe, title, url, parts in GAMES:
        out = os.path.join(tmp, folder)
        unpack(fetch(url, cache), parts, out)
        subprocess.run(["mdeltree", "-i", disk, f"::/GAMES/{folder}"], capture_output=True,
                       env=dict(os.environ, MTOOLS_SKIP_CHECK="1"))
        make_dos_hd.mtools("mcopy", "-s", "-i", disk, out, "::/GAMES/")
        b = os.path.join(tmp, bat + ".BAT")
        open(b, "wb").write(make_dos_hd.crlf(
            f"@ECHO OFF\nC:\nCD \\GAMES\\{folder}\n{exe} %1 %2\nCD \\\n"))
        make_dos_hd.mtools("mcopy", "-o", "-i", disk, b, f"::/BAT/{bat}.BAT")
        lines.append(f"ECHO   {bat:<9} {title}")
        print(f"{folder}: {len(os.listdir(out))} files")

    # AJUDA: the games after the last game line it already has
    ajuda = os.path.join(tmp, "AJUDA.BAT")
    make_dos_hd.mtools("mcopy", "-n", "-i", disk, "::/BAT/AJUDA.BAT", ajuda)
    have = open(ajuda, "rb").read().decode("cp437").replace("\r\n", "\n").split("\n")
    have = [l for l in have if not any(l.startswith(f"ECHO   {g[1]} ") for g in GAMES)]
    at = max(i for i, l in enumerate(have) if l.startswith("ECHO   ") and l.split()[1] in ("ELITE", "TD", "GP2")) + 1
    open(ajuda, "wb").write(make_dos_hd.crlf("\n".join(have[:at] + lines + have[at:])))
    make_dos_hd.mtools("mcopy", "-o", "-i", disk, ajuda, "::/BAT/AJUDA.BAT")


if __name__ == "__main__":
    main()
