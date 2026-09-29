#!/usr/bin/env python3
"""Turn the Paper Mono console's `d` dump into a PNG.

`d` on the serial console prints the machine's 1-bit picture as
"FB <w> <h>", one line of hex per row, and "FB END". This finds the last
such dump in a captured log and writes it out, so what the machine is
showing can be checked with nobody looking at the panel. A set bit is
black, as it is to the Mac.

  python3 tools/fbdump.py serial.log screen.png
"""
import struct
import sys
import zlib

if len(sys.argv) != 3:
    sys.exit(__doc__)
lines = open(sys.argv[1], errors="replace").read().splitlines()
starts = [i for i, l in enumerate(lines) if l.startswith("FB ") and l != "FB END"]
if not starts:
    sys.exit("no FB dump in %s" % sys.argv[1])
i = starts[-1]
w, h = map(int, lines[i].split()[1:3])
rows = lines[i + 1:i + 1 + h]
if len(rows) < h or any(len(r.strip()) != w // 4 for r in rows):
    sys.exit("dump is cut short or garbled: %d of %d rows" % (len(rows), h))

# PNG greyscale, 1 bit, 0 = black: the Mac's bits inverted.
raw = b"".join(b"\x00" + bytes(255 - b for b in bytes.fromhex(r.strip())) for r in rows)


def chunk(kind, data):
    return (struct.pack(">I", len(data)) + kind + data +
            struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF))


png = (b"\x89PNG\r\n\x1a\n" +
       chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 1, 0, 0, 0, 0)) +
       chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))
open(sys.argv[2], "wb").write(png)
print("%s: %dx%d" % (sys.argv[2], w, h))
