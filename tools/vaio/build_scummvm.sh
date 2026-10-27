#!/bin/sh
# The ScummVM core for the VAIO's pendrive: tools/build_scummvm_core.sh's
# core (libretro's ScummVM, the same five engines, no MP3, no SAF), built
# for x86_64 in the Alpine container instead of with the NDK, into
# build/vaio/scummvm/libscummvm.so. tools/vaio/build.sh picks it up when it
# is there. Slow (C++ under Rosetta): run once, again only when the core
# changes.
#
#     sh tools/vaio/build_scummvm.sh
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
CORE="$ROOT/third_party/scummvm/backends/platform/libretro"
ENGINES="sky lure queen drascula dreamweb"

cp "$CORE/lite_engines.list" "$CORE/lite_engines.list.orig"
trap 'mv "$CORE/lite_engines.list.orig" "$CORE/lite_engines.list"' EXIT
printf '%s\n' $ENGINES > "$CORE/lite_engines.list"

# Its makefiles take the engines' list in twice, and the link then has every
# detection.o two times: the list given once, on the command line, wins.
DETECT=$(for e in $ENGINES; do printf 'engines/%s/detection.o ' $e; done)

docker run --rm --platform linux/amd64 -v "$ROOT:$ROOT" -e ROOT="$ROOT" -e CORE="$CORE" -e DETECT="$DETECT" alpine:3.24 sh -ec '
for i in 1 2 3 4 5; do
	apk add -q build-base clang zlib-dev linux-headers git bash && break
	sleep 3
done
make -C $CORE scummvm_libretro.so LITE=1 USE_LIBRETRO_SAF=0 USE_MAD= DETECTION_STATIC=1 "DETECT_OBJS=$DETECT" CC=clang CXX=clang++ -j$(nproc) >$ROOT/build/vaio/scummvm.log 2>&1 || { tail -30 $ROOT/build/vaio/scummvm.log; exit 1; }
mkdir -p $ROOT/build/vaio/scummvm
cp $CORE/scummvm_libretro.so $ROOT/build/vaio/scummvm/libscummvm.so
strip $ROOT/build/vaio/scummvm/libscummvm.so
ls -la $ROOT/build/vaio/scummvm/libscummvm.so
'
