#!/usr/bin/env python3
"""MS-DOS hard disks with everything the owner has for DOS, one for each PC.

    python3 tools/make_dos_hd.py GP2.img MSDOS.img DOS.zip OUT386.img OUT8086.img

GP2.img is the bootable 256MB MS-DOS 6.22 disk with Grand Prix 2 (16 heads,
63 sectors, as the 8086 PC's disk.c reads it), and the base: it is copied
and the rest goes in. MSDOS.img is the 31MB disk the boards boot (with
WordStar's document drive already patched, see tools/make_pc_floppy.py):
GAMES (ELITE+, Test Drive), WORD (with the owner's documents), WS, TC
(Turbo C 2.0), VC405SW (Volkov Commander). DOS.zip is the iDOS C: from the
owner's iPads: WINDOWS (installed for a Sound Blaster 16), TP (Turbo
Pascal), TCPP (Turbo C++), SB16. Where the two had the same folder they
were compared file by file: the same but for WORD's MW.INI, the owner's
TESTE.DOC and the patched WS.COM, all taken from MSDOS.img.

Two disks, the same configuration on both:

  - OUT386.img, for the PC 386: GP2.img with all of the above.
  - OUT8086.img, for the PC (8086, src/pc): MSDOS.img with TP, TCPP and
    the same configuration, without what only a 386 runs (WINDOWS, GP2,
    SB16). The 8086 PC does not boot GP2.img, nor anything built on it:
    after reading the first 40 sectors of COMMAND.COM it runs off into
    garbage (ENTERs at E8C3:74E2 in PCLOG). The 31MB disk boots.
    Real MS-DOS 6.22 boots on it: CONFIG.SYS and AUTOEXEC.BAT, COUNTRY=055;
    nothing that needs a 286 or 386 is loaded (no HIMEM, no SMARTDRV);
  - the PC 386 (DOSBox Pure, src/pc386) mounts it as C: and does not boot
    it: CONFIG.SYS is not read, and AUTOBOOT.DBP has it run AUTOEXEC.BAT
    in its own shell, which then opens a shell of its own, by %COMSPEC%
    (Z:\\COMMAND.COM there; a bare COMMAND finds MS-DOS 6.22's on C: and
    gets "Incorrect DOS version"), or DOSBox Pure would go back to its
    menu when the batch file ends. PROMPT is set with SET: DOSBox's shell has no
    PROMPT command.

Turbo C 2.0 and Turbo C++ are both TC.EXE: Turbo C++ is on the PATH, Turbo
C 2.0 is TC2. Programs that want to start in their own folder (GP2, the
games, Turbo C 2.0) have a batch file in C:\\BAT, which is on the PATH;
AJUDA lists them all, in Portuguese.

Needs mtools. Generated from the owner's files: the image is not committed.
"""
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile

PART = 63 * 512   # the first partition starts at sector 63

CONFIG_SYS = r"""FILES=40
BUFFERS=20
COUNTRY=055,,C:\DOS\COUNTRY.SYS
SHELL=C:\COMMAND.COM C:\ /E:1024 /P
"""

AUTOEXEC_BAT = r"""@ECHO OFF
SET PROMPT=$P$G
PATH C:\DOS;C:\BAT;C:\WINDOWS;C:\VC405SW;C:\TP;C:\TCPP\BIN;C:\WORD;C:\WS
SET TEMP=C:\WINDOWS\TEMP
SET BLASTER=A220 I7 D1 H5 T6
SET SOUND=C:\SB16
ECHO.
ECHO Digite AJUDA para ver os programas deste disco.
IF "%COMSPEC%"=="Z:\COMMAND.COM" %COMSPEC%
"""

BAT = {
    "AJUDA.BAT": r"""@ECHO OFF
ECHO.
ECHO   VC        Volkov Commander
ECHO   WIN       Windows
ECHO   WORD      Microsoft Word
ECHO   WS        WordStar
ECHO   EDIT      editor do MS-DOS
ECHO   QBASIC    QBasic
ECHO   TURBO     Turbo Pascal
ECHO   TC        Turbo C++
ECHO   TC2       Turbo C 2.0
ECHO.
ECHO   ELITE     Elite+
ECHO   TD        Test Drive
ECHO   GP2       Grand Prix 2
ECHO.
""",
    "TC2.BAT": "@ECHO OFF\r\nC:\r\nCD \\TC\r\nTC %1 %2\r\nCD \\\r\n",
    "ELITE.BAT": "@ECHO OFF\r\nC:\r\nCD \\GAMES\\ELITE\r\nELITE\r\nCD \\\r\n",
    "TD.BAT": "@ECHO OFF\r\nC:\r\nCD \\GAMES\\TD\r\nTD\r\nCD \\\r\n",
    "GP2.BAT": "@ECHO OFF\r\nC:\r\nCD \\GP2\r\nGP2\r\nCD \\\r\n",
}


def crlf(s):
    return s.replace("\r\n", "\n").replace("\n", "\r\n").encode("cp437")


def mtools(*a):
    subprocess.run(a, check=True, env=dict(os.environ, MTOOLS_NO_VFAT="1", MTOOLS_SKIP_CHECK="1"))


def build(base, out, msdos, doszip, from_hd, from_zip):
    """base copied to out, then from_hd's folders from MSDOS.img and
    from_zip's from DOS.zip go in, then the configuration. AJUDA lists only
    what this disk has."""
    shutil.copyfile(base, out)
    disk = f"{out}@@{PART}"
    tmp = tempfile.mkdtemp()
    try:
        hd = os.path.join(tmp, "hd")
        os.mkdir(hd)
        for d in from_hd:
            mtools("mcopy", "-s", "-n", "-i", f"{msdos}@@{PART}", f"::/{d}", hd)
            subprocess.run(["mdeltree", "-i", disk, f"::/{d}"], capture_output=True,
                           env=dict(os.environ, MTOOLS_SKIP_CHECK="1"))
            mtools("mcopy", "-s", "-i", disk, os.path.join(hd, d), "::/")

        z = os.path.join(tmp, "zip")
        with zipfile.ZipFile(doszip) as f:
            f.extractall(z, [n for n in f.namelist() if not n.startswith("__MACOSX")])
        for d in from_zip:
            mtools("mcopy", "-s", "-i", disk, os.path.join(z, d), "::/")

        have = set(from_hd) | set(from_zip) | {d for d in ("GP2", "WINDOWS") if exists(disk, d)}
        b = os.path.join(tmp, "BAT")
        os.mkdir(b)
        for name, text in BAT.items():
            if name == "GP2.BAT" and "GP2" not in have:
                continue
            if name == "AJUDA.BAT":
                text = "".join(l + "\n" for l in text.split("\n")[:-1]
                               if not (("WIN " in l and "WINDOWS" not in have) or ("GP2" in l and "GP2" not in have)))
            open(os.path.join(b, name), "wb").write(crlf(text))
        mtools("mcopy", "-s", "-i", disk, b, "::/")
        for name, text in (("CONFIG.SYS", CONFIG_SYS), ("AUTOEXEC.BAT", AUTOEXEC_BAT),
                           ("AUTOBOOT.DBP", "C:\\AUTOEXEC.BAT\n0\n")):
            p = os.path.join(tmp, name)
            open(p, "wb").write(crlf(text))
            mtools("mcopy", "-o", "-i", disk, p, f"::/{name}")
    finally:
        shutil.rmtree(tmp)
    mtools("mdir", "-i", disk, "::/")


def exists(disk, d):
    return subprocess.run(["mdir", "-i", disk, f"::/{d}"], capture_output=True,
                          env=dict(os.environ, MTOOLS_SKIP_CHECK="1")).returncode == 0


def main():
    if len(sys.argv) != 6:
        sys.exit(__doc__)
    gp2, msdos, doszip, out386, out8086 = sys.argv[1:]
    build(gp2, out386, msdos, doszip, ("GAMES", "WORD", "WS", "TC", "VC405SW"), ("WINDOWS", "TP", "TCPP", "SB16"))
    build(msdos, out8086, msdos, doszip, (), ("TP", "TCPP"))


if __name__ == "__main__":
    main()
