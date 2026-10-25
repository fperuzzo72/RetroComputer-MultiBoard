#!/usr/bin/env python3
"""An MSX FAT16 hard disk, rebuilt under 2GB for blueMSX.

    python3 tools/msx_fat16_disk.py SOURCE.dd OUT.dsk

SOURCE is an MSX card's image with an MBR and one FAT16 partition, the
owner's One Chip MSX card (~/Downloads/MSXFAT16.dd, 31GB: Nextor, MSX-DOS 2,
some 6000 files of software). blueMSX keeps a disk's size in a signed int
(Disk.c, fileSize[]), so an image of 2GB or more is read as some other size:
the 31GB card wraps round to 1.15GB and boots, but reads nothing past that;
a 2.4GB cut of it goes negative and does not boot. Its files sit spread over
2.15GB in 64kB clusters, so a cut is not enough either.

So every file is read out by its own short name (the 8.3 name MSX-DOS sees;
long names are ignored, as on the MSX), and a fresh FAT16 is made one sector
under 2GB with 32kB clusters, the files copied in with mtools under those
same names. What a Mac or Windows left on the card (.fseventsd,
.Spotlight-V100, System Volume Information) stays out. Needs mtools.
"""
import os
import shutil
import struct
import subprocess
import sys
import tempfile

PART_LBA = 2048
JUNK = {"FSEVE~22", "SPOTLI~4", "SYSTEM~1"}   # the short names they had on this card


def extract(src, out):
    f = open(src, "rb")
    mbr = f.read(512)
    start = struct.unpack("<I", mbr[454:458])[0] * 512
    f.seek(start)
    b = f.read(512)
    bps, spc, res, nf, roots, _ts16, _media, fsz = struct.unpack("<HBHBHHBH", b[11:24])
    f.seek(start + res * bps)
    fat = struct.unpack("<%dH" % (fsz * bps // 2), f.read(fsz * bps))
    root = start + (res + nf * fsz) * bps
    data = root + roots * 32
    cs = spc * bps

    def chain(c):
        while 2 <= c < 0xFFF8:
            yield c
            c = fat[c]

    def read(c):
        f.seek(data + (c - 2) * cs)
        return f.read(cs)

    def entries(raw):
        for i in range(0, len(raw), 32):
            e = raw[i:i + 32]
            if e[0] == 0:
                return
            if e[0] == 0xE5 or e[11] == 0x0F or e[11] & 0x08:
                continue
            name = e[0:8].decode("latin1").rstrip()
            ext = e[8:11].decode("latin1").rstrip()
            if name in (".", ".."):
                continue
            if e[0] == 0x05:
                name = "\xe5" + name[1:]
            yield name + ("." + ext if ext else ""), e[11], struct.unpack("<H", e[26:28])[0], \
                struct.unpack("<I", e[28:32])[0]

    count = 0

    def walk(raw, path, top):
        nonlocal count
        for name, attr, clus, size in list(entries(raw)):
            if top and name in JUNK:
                continue
            p = os.path.join(path, name)
            if attr & 0x10:
                os.makedirs(p, exist_ok=True)
                walk(b"".join(read(c) for c in chain(clus)), p, False)
            else:
                with open(p, "wb") as o:
                    o.write(b"".join(read(c) for c in chain(clus))[:size])
                count += 1

    f.seek(root)
    walk(f.read(roots * 32), out, True)
    return count


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    src, out = sys.argv[1:]
    tmp = tempfile.mkdtemp()
    try:
        n = extract(src, tmp)
        total = 2 ** 31 - 512
        nsec = total // 512 - PART_LBA
        with open(out, "wb") as o:
            o.truncate(total)
            mbr = bytearray(512)
            mbr[446:462] = bytes([0, 0xFE, 0xFF, 0xFF, 0x0E, 0xFE, 0xFF, 0xFF]) + struct.pack("<II", PART_LBA, nsec)
            mbr[510:512] = b"\x55\xaa"
            o.write(mbr)
        env = dict(os.environ, MTOOLS_SKIP_CHECK="1", MTOOLS_NO_VFAT="1")
        disk = f"{out}@@{PART_LBA * 512}"
        subprocess.run(["mformat", "-i", disk, "-T", str(nsec), "-h", "255", "-s", "63", "-c", "64",
                        "-H", str(PART_LBA), "-v", "MSXFAT16", "::"], check=True, env=env)
        names = sorted(os.listdir(tmp))
        subprocess.run(["mcopy", "-s", "-i", disk] + [os.path.join(tmp, x) for x in names] + ["::/"],
                       check=True, env=env)
        print(f"{n} files into {out}")
        subprocess.run(["mdir", "-i", disk, "::/"], env=env)
    finally:
        shutil.rmtree(tmp)


if __name__ == "__main__":
    main()
