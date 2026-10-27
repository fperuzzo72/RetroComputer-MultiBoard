#!/bin/sh
# The Linux board for the VAIO's pendrive: `retro` and its cores, built for
# x86_64 in an Alpine container (the pendrive's own libc, musl) with clang,
# as the NDK builds them, into build/vaio/out.
#
#     sh tools/vaio/build.sh
#
# The same CMakeLists.txt as the Android app (android/app/src/main/cpp),
# which builds src/boards/linux when it is not the NDK. Then copy
# build/vaio/out into the pendrive's retro/ (tools/vaio/install.sh).
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
# mounted at its own path: src/mac/mac_media.S names the Mac ROM by it
docker run --rm --platform linux/amd64 -v "$ROOT:$ROOT" -e ROOT="$ROOT" alpine:3.24 sh -ec '
# the DNS in the container fails now and then: tried again
for i in 1 2 3 4 5; do
	apk add -q build-base clang cmake samurai alsa-lib-dev zlib-dev linux-headers && break
	sleep 3
done
# ScummVM, when tools/vaio/build_scummvm.sh has built its core
S=$ROOT/build/vaio/scummvm/libscummvm.so
CC=clang CXX=clang++ cmake -S $ROOT/android/app/src/main/cpp -B $ROOT/build/vaio/cmake -G Ninja -DCMAKE_BUILD_TYPE=Release \
	$( [ -f $S ] && echo -DSCUMMVM_LIB=$S ) >/dev/null
cmake --build $ROOT/build/vaio/cmake
mkdir -p $ROOT/build/vaio/out
cp $ROOT/build/vaio/cmake/retro $ROOT/build/vaio/cmake/*.so $ROOT/build/vaio/out/
[ -f $S ] && cp $S $ROOT/build/vaio/out/
ls -la $ROOT/build/vaio/out
'
