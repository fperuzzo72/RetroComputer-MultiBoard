#!/bin/sh
# Copies the Linux board onto the VAIO's pendrive, mounted on the Mac:
# build/vaio/out (tools/vaio/build.sh) into retro/bin, the extra Alpine
# packages into retro/apks, retro/start.sh, and the card's folder
# (tools/make_android_card.py's RetroComputer) beside them.
#
#     sh tools/vaio/install.sh /Volumes/RETRO CARD_DIR/RetroComputer
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
USB=$1
CARD=$2
mkdir -p "$USB/retro/bin" "$USB/retro/apks"
rsync -rt --delete "$ROOT/build/vaio/out/" "$USB/retro/bin/"
rsync -rt --delete "$HOME/Library/Caches/retrocomputer/vaio/apks/" "$USB/retro/apks/"
cp "$ROOT/tools/vaio/retro/start.sh" "$USB/retro/start.sh"
[ -n "$CARD" ] && rsync -rt --modify-window=2 "$CARD" "$USB/"
du -sh "$USB/retro" "$USB/RetroComputer" 2>/dev/null
