#!/usr/bin/env python3
"""Join MAME's two Mac Plus ROM halves into the one image umac wants.

MAME's macplus set stores the v3 ROM as two 64kB chips, one per data-bus
half: 342-0341-c holds the high byte of every word, 342-0342-b the low
byte. Interleaved they are the 128kB ROM whose first four bytes are its
own checksum, 4D1F8172, which this checks.

  python3 tools/make_macplus_rom.py 342-0341-c.u6d 342-0342-b.u8d roms/mac/macplus.rom
"""
import struct
import sys

if len(sys.argv) != 4:
    sys.exit(__doc__)
hi = open(sys.argv[1], "rb").read()
lo = open(sys.argv[2], "rb").read()
if len(hi) != 0x10000 or len(lo) != 0x10000:
    sys.exit("each half should be 65536 bytes")
rom = bytes(b for pair in zip(hi, lo) for b in pair)
stored = struct.unpack(">I", rom[:4])[0]
summed = sum(struct.unpack(">%dH" % ((len(rom) - 4) // 2), rom[4:])) & 0xFFFFFFFF
if stored != 0x4D1F8172 or summed != stored:
    sys.exit("not the v3 ROM: stored %08X, computed %08X (halves swapped?)" % (stored, summed))
open(sys.argv[3], "wb").write(rom)
print("%s: Mac Plus v3 ROM, checksum %08X" % (sys.argv[3], stored))
