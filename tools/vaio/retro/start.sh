#!/bin/sh
# Run by /etc/retro/start on tty1 at every boot, from the pendrive's
# retro/ folder: the libraries Alpine's ISO lacks (apks/), the sound
# unmuted, then the RetroComputer (bin/, copied to memory first: running
# off FAT is slow, and the pendrive stays free to be written).
R="$RETRO_USB/retro"
[ -f "$R/log.txt" ] && mv "$R/log.txt" "$R/log-anterior.txt"
exec >"$R/log.txt" 2>&1
echo "== $(date)"
# diskless Alpine has no package cache and refuses a loose .apk without
# --force-non-repository ("it would be lost on next reboot": it is meant to)
apk add --quiet --no-network --allow-untrusted --force-non-repository "$R"/apks/*.apk
for c in Master PCM Speaker Headphone Front; do amixer -q sset "$c" 85% unmute 2>/dev/null; done
rm -rf /tmp/retro && cp -r "$R/bin" /tmp/retro && chmod +x /tmp/retro/retro
export LD_LIBRARY_PATH=/tmp/retro
cd /tmp/retro
./retro "$RETRO_USB/RetroComputer" "$R/nvs"
echo "== retro ended ($?)"
sync
