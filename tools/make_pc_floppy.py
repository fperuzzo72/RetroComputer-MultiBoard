#!/usr/bin/env python3
"""A bootable 1.44MB MS-DOS floppy image for the PC, from a hard disk image.

    python3 tools/make_pc_floppy.py HD.img OUT.img

Takes the system and a selection of programs off a bootable MS-DOS hard
disk image (its first partition) and builds a floppy small enough to send
to the card over USB (tools/sd_put.py, a couple of minutes, where 31MB is
an hour). Needs mtools. Two things the DOS boot sector insists on, each of
which cost a try here:

  - IO.SYS and MSDOS.SYS are the first two entries of the root directory,
    so the volume label goes on last (mformat -v puts it first);
  - no long-name entries in front of them: mtools writes VFAT entries
    even with MTOOLS_NO_VFAT, so they are stripped from the root after.

The boot code is taken from the hard disk's partition boot sector, keeping
the floppy's own BPB, drive number 0. WordStar's document drive is set to
the current one (WS.COM offset 1E1Eh, see README "The PC").
"""
import os
import struct
import subprocess
import sys
import tempfile

DOS = ["ATTRIB.EXE", "CHKDSK.EXE", "CHOICE.COM", "DEBUG.EXE", "DELTREE.EXE",
       "DOSKEY.COM", "EDIT.COM", "FC.EXE", "FIND.EXE", "FORMAT.COM", "LABEL.EXE",
       "MEM.EXE", "MORE.COM", "MOVE.EXE", "QBASIC.EXE", "SORT.EXE", "SYS.COM",
       "TREE.COM", "XCOPY.EXE"]
WS = ["WS.COM", "WSMSGS.OVR", "WSOVLY1.OVR"]
VC = ["VC.COM", "VC.HLP", "VC.INI", "VC.MNU", "VC.EXT", "VCVIEW.EXT",
      "VCEDIT.EXT", "ARCHIVES.MNU", "FORMAT.MNU"]


def run(*a):
    subprocess.run(a, check=True, env=dict(os.environ, MTOOLS_NO_VFAT="1"))


def main():
    hd, out = sys.argv[1], sys.argv[2]
    mbr = open(hd, "rb").read(512)
    lba = struct.unpack_from("<I", mbr, 446 + 8)[0]
    src = "%s@@%d" % (hd, lba * 512)
    with open(hd, "rb") as f:
        f.seek(lba * 512)
        vbr = f.read(512)
    tmp = tempfile.mkdtemp()
    get = lambda path, name: run("mcopy", "-o", "-i", src, "::" + path, os.path.join(tmp, name))
    for n in ["IO.SYS", "MSDOS.SYS", "COMMAND.COM"]:
        get(n, n)
    for n in DOS:
        get("DOS/" + n, n)
    for n in WS:
        get("WS/" + n, n)
    for n in VC:
        get("VC405SW/" + n, n)
    ws = bytearray(open(os.path.join(tmp, "WS.COM"), "rb").read())
    if ws[0x1E1D:0x1E20] in (b"\xB0\x01\xC3", b"\xB0\x00\xC3"):
        ws[0x1E1E] = 0
        open(os.path.join(tmp, "WS.COM"), "wb").write(ws)
    open(os.path.join(tmp, "CONFIG.SYS"), "wb").write(b"FILES=20\r\nBUFFERS=10\r\n")
    open(os.path.join(tmp, "AUTOEXEC.BAT"), "wb").write(
        b"@ECHO OFF\r\nPATH A:\\DOS;A:\\WS;A:\\VC\r\nPROMPT $P$G\r\n")

    if os.path.exists(out):
        os.remove(out)
    run("mformat", "-f", "1440", "-C", "-i", out, "::")
    d = bytearray(open(out, "rb").read())
    d[0:11] = vbr[0:11]
    d[0x24] = 0
    d[0x3E:0x200] = vbr[0x3E:0x200]
    open(out, "wb").write(d)
    p = lambda *names: [os.path.join(tmp, n) for n in names]
    run("mcopy", "-i", out, *p("IO.SYS", "MSDOS.SYS"), "::/")
    run("mattrib", "-i", out, "+s", "+h", "+r", "::IO.SYS", "::MSDOS.SYS")
    run("mcopy", "-i", out, *p("COMMAND.COM", "CONFIG.SYS", "AUTOEXEC.BAT"), "::/")
    run("mmd", "-i", out, "::DOS", "::WS", "::VC")
    run("mcopy", "-i", out, *p(*DOS), "::DOS/")
    run("mcopy", "-i", out, *p(*WS), "::WS/")
    run("mcopy", "-i", out, *p(*VC), "::VC/")
    run("mlabel", "-i", out, "::PAPERDOS")

    d = bytearray(open(out, "rb").read())
    root, n = 19 * 512, 224
    ents = [bytes(d[root + 32 * i:root + 32 * i + 32]) for i in range(n)]
    keep = [e for e in ents if e[0] not in (0, 0xE5) and e[11] != 0x0F]
    d[root:root + 32 * n] = b"".join(keep) + b"\0" * 32 * (n - len(keep))
    open(out, "wb").write(d)
    if keep[0][:11] != b"IO      SYS" or keep[1][:11] != b"MSDOS   SYS":
        raise SystemExit("IO.SYS and MSDOS.SYS did not end up first")
    print("%s: bootable, %d entries in the root" % (out, len(keep)))


if __name__ == "__main__":
    main()
