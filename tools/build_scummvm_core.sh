#!/bin/sh
# The ScummVM core for the Android app: libretro's ScummVM
# (third_party/scummvm, a submodule) built with the NDK into
# android/app/src/main/jniLibs/arm64-v8a/libscummvm.so, and its data files
# (themes, the engines' .dat files) into build/scummvm/scummvm.zip.
#
#     sh tools/build_scummvm_core.sh
#
# Only the engines of the games their makers set free are built (LITE=1 with
# our list): sky (Beneath a Steel Sky), lure (Lure of the Temptress), queen
# (Flight of the Amazon Queen), drascula and dreamweb. The core is built with
# its own makefiles, not the app's CMake: they generate the engine tables
# first. USE_LIBRETRO_SAF=0: the app reads the card as plain files (it has
# all-files access), and SAF would want JNI from the core. USE_MAD= : no MP3
# (the floppy versions have none), and libmad's file "version" stands in
# for C++'s <version> header on the include path, which breaks the build.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SDK=$(sed -n 's/^sdk.dir=//p' "$ROOT/android/local.properties")
NDK=$(ls -d "$SDK"/ndk/* | tail -1)
CORE="$ROOT/third_party/scummvm/backends/platform/libretro"
ENGINES="sky lure queen drascula dreamweb"

cp "$CORE/lite_engines.list" "$CORE/lite_engines.list.orig"
trap 'mv "$CORE/lite_engines.list.orig" "$CORE/lite_engines.list"' EXIT
printf '%s\n' $ENGINES > "$CORE/lite_engines.list"

"$NDK/ndk-build" -C "$CORE/jni" APP_ABI=arm64-v8a LITE=1 USE_LIBRETRO_SAF=0 USE_MAD= -j"$(sysctl -n hw.ncpu)"
mkdir -p "$ROOT/android/app/src/main/jniLibs/arm64-v8a"
cp "$CORE/libs/arm64-v8a/libretro.so" "$ROOT/android/app/src/main/jniLibs/arm64-v8a/libscummvm.so"

make -C "$CORE" datafiles LITE=1
mkdir -p "$ROOT/build/scummvm"
cp "$CORE/scummvm.zip" "$ROOT/build/scummvm/"
ls -la "$ROOT/android/app/src/main/jniLibs/arm64-v8a/libscummvm.so" "$ROOT/build/scummvm/scummvm.zip"
