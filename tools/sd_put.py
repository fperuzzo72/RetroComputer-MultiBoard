#!/usr/bin/env python3
"""Send files to the e-ink board's SD card over the USB serial line.

    python3 tools/sd_put.py [--port /dev/cu.usbmodem101] [--dest /c64/] FILE...

Uses the firmware's `put <path> <size>` console command (main.cpp): the
board answers "ready", takes the bytes and answers "ok <size> <crc32>",
checked here. Each 2kB block is answered with '+' once it is on the card.
Spaces in names travel as '|'. Best done from the boot menu
or an 8-bit machine, not while the Mac is reading its disc.
"""
import argparse
import os
import sys
import time
import zlib

import serial


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/cu.usbmodem101")
    ap.add_argument("--dest", default="/c64/")
    ap.add_argument("files", nargs="+")
    a = ap.parse_args()
    s = serial.Serial(a.port, 115200, timeout=0.2)
    time.sleep(0.3)
    s.read(1 << 20)
    total = 0
    t0 = time.time()
    for path in a.files:
        data = open(path, "rb").read()
        name = os.path.basename(path).replace(" ", "|")
        s.write(("put %s%s %d\n" % (a.dest, name, len(data))).encode())
        reply = b""
        deadline = time.time() + 5
        while b"ready" not in reply and b"error" not in reply and time.time() < deadline:
            reply += s.read(256)
        if b"ready" not in reply:
            sys.exit("%s: board said %r" % (path, reply[-120:]))
        reply = b""
        for i in range(0, len(data), 2048):
            s.write(data[i:i + 2048])
            # the board answers '+' once each block is on the card
            deadline = time.time() + 5
            while reply.count(b"+") < i // 2048 + 1 and b"error" not in reply and time.time() < deadline:
                reply += s.read(64)
            if reply.count(b"+") < i // 2048 + 1:
                sys.exit("%s: no answer after %d bytes (%r)" % (path, i + 2048, reply[-80:]))
        reply = reply.replace(b"+", b"")
        deadline = time.time() + 20
        while b"\nok " not in b"\n" + reply and b"error" not in reply and time.time() < deadline:
            reply += s.read(256).replace(b"+", b"")
        line = [l for l in reply.decode("utf-8", "replace").splitlines() if l.startswith(("ok", "error"))]
        want = "ok %d %08x" % (len(data), zlib.crc32(data) & 0xffffffff)
        if not line or line[0].strip() != want:
            sys.exit("%s: expected %r, board said %r" % (path, want, line))
        total += len(data)
        print("%-60s %7d bytes ok" % (os.path.basename(path)[:60], len(data)))
    dt = time.time() - t0
    print("%d file(s), %d bytes in %.1fs (%.0f kB/s)" % (len(a.files), total, dt, total / 1024 / max(dt, 0.001)))


if __name__ == "__main__":
    main()
