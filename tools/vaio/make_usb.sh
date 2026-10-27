#!/bin/sh
# The VAIO's pendrive: Alpine Linux, diskless, booting straight into
# retro/start.sh, on one FAT32 partition the size of the whole pendrive
# (it stays readable on the Mac, where the card's folder is copied in).
#
#     sh tools/vaio/make_usb.sh ALPINE.iso DISK_BYTES OUT.img
#
# ALPINE.iso is alpine-standard for x86_64 (the VAIO VGN-SZ330P's Core 2 Duo
# is 64-bit; its BIOS has no UEFI, so syslinux boots it). DISK_BYTES is the
# pendrive's size (diskutil info), so the partition fills it. Built in an
# x86_64 Alpine container (docker, colima with Rosetta on the Mac).
#
# OUT.img is only the start of the disk: the MBR, the FAT and the files,
# which a fresh FAT puts first; the rest of the partition is free clusters
# whose contents do not matter. Written with
#
#     sudo dd if=OUT.img of=/dev/rdiskN bs=4m
#
# the pendrive then has the whole partition. tools/vaio/overlay is the
# apkovl (Alpine's saved /etc): tty1 runs /etc/retro/start, which runs the
# pendrive's retro/start.sh; tools/vaio/retro is that folder.
set -e
ISO=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
BYTES=$2
OUT=$(cd "$(dirname "$3")" && pwd)/$(basename "$3")
HERE=$(cd "$(dirname "$0")" && pwd)

docker run --rm --platform linux/amd64 -v "$ISO:/alpine.iso:ro" -v "$HERE:/vaio:ro" \
    -v "$(dirname "$OUT"):/out" -e BYTES="$BYTES" -e OUT="$(basename "$OUT")" alpine:3.24 sh -ec '
apk add -q syslinux dosfstools mtools sfdisk xorriso tar
mkdir /iso && xorriso -osirrox on -indev /alpine.iso -extract / /iso 2>/dev/null
img=/tmp/usb.img
truncate -s "$BYTES" $img
echo "2048,,c,*" | sfdisk -q $img
off=$((2048 * 512))
sectors=$(( BYTES / 512 - 2048 ))
mkfs.vfat -F 32 -n RETRO --offset 2048 $img $((sectors / 2)) >/dev/null
export MTOOLS_SKIP_CHECK=1
d="$img@@$off"
mcopy -s -i "$d" /iso/boot /iso/apks /iso/.alpine-release ::/
# the apkovl: /etc as the pendrive boots it
cp -a /vaio/overlay /tmp/ovl
tar -C /tmp/ovl -czf /tmp/localhost.apkovl.tar.gz --owner=0 --group=0 etc
mcopy -i "$d" /tmp/localhost.apkovl.tar.gz ::/
mcopy -s -i "$d" /vaio/retro ::/
syslinux --offset $off --install --directory /boot/syslinux $img
dd if=/usr/share/syslinux/mbr.bin of=$img bs=440 count=1 conv=notrunc 2>/dev/null
# cut at 512MB past the partition start, then checked: padded back to the
# whole size (the cut part reads as zeros, as free clusters may), every
# file must read the same as in the full image
end=$(( off + 512 * 1024 * 1024 ))
head -c $end $img > /out/$OUT
cp /out/$OUT /tmp/check.img && truncate -s "$BYTES" /tmp/check.img
mkdir /tmp/a /tmp/b
mcopy -s -n -i "$d" "::/*" /tmp/a/ && mcopy -s -n -i "/tmp/check.img@@$off" "::/*" /tmp/b/
diff -r /tmp/a /tmp/b
mdir -i "$d" ::/
echo "image: $end bytes of $BYTES, the files whole"
'
