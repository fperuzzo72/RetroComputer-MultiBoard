#!/bin/sh
# The VAIO's retro (build/vaio/out) on the Mac, in the x86_64 container:
# the screen a file (RETRO_FB), keys from a script (RETRO_KEYS), no sound
# card. The screen at the end is written out as a PNG.
#
#     sh tools/vaio/run.sh CARD_DIR NVS_DIR SECONDS OUT.png ["2:88 3:108 ..."]
#
# Keys are "seconds:evdev code" (88 F12, 103 Up, 108 Down, 28 Enter,
# 1 Esc). CARD_DIR and NVS_DIR must be under the home folder (colima
# mounts only that).
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
CARD=$(cd "$1" && pwd)
NVS=$(cd "$2" && pwd)
SECS=$3
OUT=$(cd "$(dirname "$4")" && pwd)/$(basename "$4")
KEYS=$5
FB=$ROOT/build/vaio/fb.raw
rm -f "$FB"
docker run --rm --platform linux/amd64 -v "$ROOT:$ROOT" -v "$CARD:$CARD" -v "$NVS:$NVS" \
    -e RETRO_FB="$FB" -e RETRO_KEYS="$KEYS" alpine:3.24 sh -c "
apk add -q alsa-lib libstdc++ zlib >/dev/null 2>&1
cd $ROOT/build/vaio/out
LD_LIBRARY_PATH=. timeout $SECS ./retro '$CARD' '$NVS'
rc=\$?
[ \$rc = 143 ] && echo 'retro: still running at the end' || echo \"retro: ended (\$rc)\"
" || true
python3 - "$FB" "$OUT" <<'EOF'
import sys, zlib, struct
raw = open(sys.argv[1], 'rb').read()
w, h = 1280, 800
rows = b''.join(b'\0' + bytes(c for i in range(y * w * 4, (y + 1) * w * 4, 4) for c in (raw[i + 2], raw[i + 1], raw[i])) for y in range(h))
def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d))
open(sys.argv[2], 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(rows, 6)) + chunk(b'IEND', b''))
EOF
