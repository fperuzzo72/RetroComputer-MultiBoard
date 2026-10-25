# The RetroComputer's native side: the machines (the same sources as the
# boards build) and the Android board, src/boards/android.
cmake_minimum_required(VERSION 3.22.1)
project(retro C CXX ASM)

get_filename_component(ROOT ${CMAKE_SOURCE_DIR}/../../../../.. ABSOLUTE)
set(BOARD ${ROOT}/src/boards/android)
set(FMSX ${ROOT}/lib/fmsx_core)

file(GLOB MSX_GLUE ${ROOT}/src/msx/*.c)
# The Spectrum, without the CYD's help overlay (spectrum_help.cpp), as the
# e-ink boards build it.
file(GLOB SPECTRUM ${ROOT}/src/spectrum/*.c)
# The PC: M5PaperDOS's 8086 core and src/pc, with the font
# tools/make_pc_font.py generates from the owner's EGA.CPI when present.
file(GLOB PC8086 ${ROOT}/lib/pc8086/src/*.c)
set(PC ${ROOT}/src/pc)
set(PC_SRC ${PC}/pc_core.c ${PC}/pc_keys.c ${PC}/pc_text.c ${PC}/pc_speaker.c ${PC}/pc_files.cpp ${PC}/pc_machine.cpp)
if(EXISTS ${PC}/pc_font_data.c)
  list(APPEND PC_SRC ${PC}/pc_font_data.c)
endif()
# The Mac: umac and Musashi, src/mac, the e-ink boards' trackpad, and the
# ROM and boot disc that tools/local_mac.py links in with .incbin
# (src/mac/mac_media.S, generated and gitignored; run a board build once to
# make it).
set(UMAC ${ROOT}/lib/umac)
file(GLOB UMAC_SRC ${UMAC}/src/*.c)
set(MAC_SRC ${UMAC_SRC} ${UMAC}/musashi/m68kcpu.c ${UMAC}/musashi/m68kops.c ${UMAC}/musashi/m68kdasm.c
    ${UMAC}/musashi/softfloat/softfloat.c ${ROOT}/src/mac/mac_core.c ${ROOT}/src/mac/mac_machine.c
    ${BOARD}/media_files.c ${ROOT}/src/boards/eink/trackpad.c)
if(EXISTS ${ROOT}/src/mac/mac_media.S)
  list(APPEND MAC_SRC ${ROOT}/src/mac/mac_media.S)
  set(HAVE_MAC_MEDIA 1)
endif()
set(MSX_CORE
    ${FMSX}/fMSX/MSX.c ${FMSX}/fMSX/V9938.c ${FMSX}/fMSX/I8251.c ${FMSX}/fMSX/Patch.c
    ${FMSX}/EMULib/AY8910.c ${FMSX}/EMULib/SCC.c ${FMSX}/EMULib/YM2413.c ${FMSX}/EMULib/I8255.c
    ${FMSX}/EMULib/WD1793.c ${FMSX}/EMULib/FDIDisk.c ${FMSX}/EMULib/Floppy.c ${FMSX}/EMULib/Hunt.c
    ${FMSX}/EMULib/IPS.c ${FMSX}/EMULib/MCF.c ${FMSX}/EMULib/SHA1.c ${FMSX}/EMULib/Sound.c
    ${FMSX}/video/video_glue.c ${ROOT}/lib/z80/Z80.c)

add_library(retro SHARED
    ${BOARD}/android_jni.cpp
    ${BOARD}/android_audio.c
    ${BOARD}/retro_board.c
    ${BOARD}/nvs_file.c
    ${BOARD}/beeper.c
    ${SPECTRUM}
    ${PC8086}
    ${PC_SRC}
    ${MAC_SRC}
    ${ROOT}/src/machines.c
    ${MSX_GLUE}
    ${MSX_CORE})

target_include_directories(retro PRIVATE
    ${BOARD}/shim ${BOARD} ${ROOT}/src ${ROOT}/src/boards/common ${ROOT}/lib/z80
    ${ROOT}/src/msx ${ROOT}/src/spectrum ${PC} ${ROOT}/src/mac
    ${UMAC}/include ${UMAC}/musashi ${UMAC}/musashi/softfloat ${FMSX}/fMSX ${FMSX}/EMULib ${FMSX}/video)

# The flags the MSX is built with on every board (BPS16, LSB_FIRST, ESP32
# for the ESPlay-fMSX paths, FMSX for the Z80's fast fetch).
target_compile_definitions(retro PRIVATE HAVE_MACHINE_MSX HAVE_MACHINE_SPECTRUM HAVE_MACHINE_PC
    HAVE_MACHINE_MAC UMAC_MEMSIZE=4096 DISP_WIDTH=512 DISP_HEIGHT=342
    BPS16 LSB_FIRST ESP32 FMSX PC8086_LEAN)
if(HAVE_MAC_MEDIA)
  target_compile_definitions(retro PRIVATE HAVE_MAC_MEDIA)
endif()
if(EXISTS ${PC}/pc_font_data.c)
  target_compile_definitions(retro PRIVATE HAVE_PC_FONT)
endif()

# The owner's own ROMs, generated and gitignored, built in when present, as
# tools/local_bios.py does for the boards.
if(EXISTS ${ROOT}/src/msx/hotbit_bios_data.c)
  target_compile_definitions(retro PRIVATE HAVE_LOCAL_BIOS)
  message(STATUS "local BIOS: found, MSX-BASIC available")
endif()
if(EXISTS ${ROOT}/src/msx/local_cart_data.c)
  target_compile_definitions(retro PRIVATE HAVE_LOCAL_CART)
endif()
if(EXISTS ${ROOT}/src/spectrum/spectrum_rom_data.c)
  target_compile_definitions(retro PRIVATE HAVE_SPECTRUM_ROM)
endif()
if(EXISTS ${ROOT}/src/spectrum/spectrum_snap_data.c)
  target_compile_definitions(retro PRIVATE HAVE_SPECTRUM_SNAPS)
endif()
if(EXISTS ${ROOT}/src/spectrum/spectrum_tape_data.c)
  target_compile_definitions(retro PRIVATE HAVE_SPECTRUM_TAPES)
endif()

# The Commodore 64: retroelec's T-HMI-C64 (lib/c64, GPLv3) on its Linux
# platform (pthreads), with src/c64's drivers, as tools/c64host builds it.
# A library of its own so that lib/c64's include path (Config.h, VIC.h,
# SID.h...) stays out of the other machines' way. ROMs from
# tools/make_c64_roms.py, generated and gitignored.
file(GLOB C64_LIB ${ROOT}/lib/c64/src/*.cpp)
add_library(c64 STATIC ${C64_LIB}
    ${ROOT}/lib/c64/src/platform/PlatformFactory.cpp ${ROOT}/lib/c64/src/joystick/JoystickFactory.cpp
    ${ROOT}/src/c64/c64_keys.cpp ${ROOT}/src/c64/c64_files.cpp ${ROOT}/src/c64/c64_cart.cpp
    ${ROOT}/src/c64/c64_machine.cpp)
target_include_directories(c64 PRIVATE ${ROOT}/lib/c64/src ${ROOT}/src/c64 ${BOARD}/shim ${BOARD} ${ROOT}/src)
target_compile_definitions(c64 PRIVATE PLATFORM_LINUX BOARD_RETRO RETRO_BOARD_ANDROID)
target_compile_options(c64 PRIVATE -w -O2)
set_target_properties(c64 PROPERTIES CXX_VISIBILITY_PRESET hidden POSITION_INDEPENDENT_CODE ON)
if(EXISTS ${ROOT}/src/c64/c64_rom_data.c)
  target_sources(c64 PRIVATE ${ROOT}/src/c64/c64_rom_data.c)
  target_compile_definitions(c64 PRIVATE HAVE_C64_ROMS)
endif()
target_compile_definitions(retro PRIVATE HAVE_MACHINE_C64)

# The PC 386: DOSBox Pure (lib/dosbox-pure, GPLv2+), a libretro core, as a
# library of its own that src/pc386/pc386_machine.c loads with dlopen: its
# hundreds of globals (mem_readb, CPU_*, PIC_*...) stay out of the other
# cores' way. Built as its jni/Android.mk builds it: C++11, exceptions and
# RTTI, -D__LIBRETRO__, the ARM64 recompiler chosen by include/config.h.
set(DBP ${ROOT}/lib/dosbox-pure)
file(GLOB DBP_SRC ${DBP}/*.cpp ${DBP}/src/*.cpp ${DBP}/src/*/*.cpp ${DBP}/src/*/*/*.cpp)
add_library(dosbox_pure SHARED ${DBP_SRC})
target_include_directories(dosbox_pure PRIVATE ${DBP}/include)
target_compile_definitions(dosbox_pure PRIVATE __LIBRETRO__ NDEBUG)
target_compile_options(dosbox_pure PRIVATE -std=c++11 -O2 -fomit-frame-pointer -fexceptions -frtti
    -fvisibility=hidden -ffunction-sections -fdata-sections -w)
target_link_options(dosbox_pure PRIVATE -Wl,--gc-sections)
target_link_libraries(dosbox_pure log)
target_sources(retro PRIVATE ${ROOT}/src/libretro/lr_frontend.c ${ROOT}/src/pc386/pc386_machine.c)
target_include_directories(retro PRIVATE ${DBP}/libretro-common/include ${ROOT}/src/libretro)
target_compile_definitions(retro PRIVATE HAVE_MACHINE_PC386)

# The Amiga: libretro's PUAE (third_party/libretro-uae, a submodule, GPLv2),
# a library of its own as DOSBox Pure is, run by src/libretro/lr_frontend.c
# through src/amiga/amiga_machine.c. Built as its jni/Android.mk builds it;
# the sources and flags come from its Makefile.common through
# tools/puae_sources.py. The 68000 core alone is most of 41MB of C.
set(PUAE ${ROOT}/third_party/libretro-uae)
include(${CMAKE_SOURCE_DIR}/puae_sources.cmake)
# PUAE opens IPF disks through CAPSImg, by dlopen("capsimg.so"); an APK
# only carries libraries named lib*.so, so a copy of caps.c asks for
# libcapsimg.so instead, and the submodule stays as upstream left it.
file(READ ${PUAE}/sources/src/caps/caps.c CAPS_C)
string(REPLACE "\"capsimg.so\"" "\"libcapsimg.so\"" CAPS_C "${CAPS_C}")
file(WRITE ${CMAKE_BINARY_DIR}/puae_caps.c "${CAPS_C}")
list(REMOVE_ITEM PUAE_SRC ${PUAE}/sources/src/caps/caps.c)
add_library(puae SHARED ${PUAE_SRC} ${CMAKE_BINARY_DIR}/puae_caps.c)
target_include_directories(puae PRIVATE ${PUAE_INC} ${PUAE}/sources/src/caps ${PUAE}/jni/include)
target_compile_definitions(puae PRIVATE ${PUAE_DEFS} ANDROID __LIBRETRO__ USE_LIBRETRO_VFS
    GIT_VERSION="7387937")
target_compile_options(puae PRIVATE $<$<COMPILE_LANGUAGE:C>:-std=gnu99> -O2 -w)
target_link_options(puae PRIVATE -Wl,--version-script=${PUAE}/libretro/link.T)
target_link_libraries(puae z log)

# CAPSImg (third_party/capsimg, a submodule; the SPS licence, non-commercial):
# the IPF decoder, built as its CAPSImg/Makefile.in lists it.
set(CAPS ${ROOT}/third_party/capsimg)
add_library(capsimg SHARED
    ${CAPS}/Codec/CTRawCodec.cpp ${CAPS}/Codec/CTRawCodecDecompressor.cpp ${CAPS}/Codec/DiskEncoding.cpp
    ${CAPS}/Core/BaseFile.cpp ${CAPS}/Core/BitBuffer.cpp ${CAPS}/Core/CRC.cpp ${CAPS}/Core/DiskFile.cpp
    ${CAPS}/Core/MemoryFile.cpp ${CAPS}/CAPSImg/CapsAPI.cpp ${CAPS}/CAPSImg/CapsFDCEmulator.cpp
    ${CAPS}/CAPSImg/CapsFile.cpp ${CAPS}/CAPSImg/CapsFormatMFM.cpp ${CAPS}/CAPSImg/CapsImage.cpp
    ${CAPS}/CAPSImg/CapsImageStd.cpp ${CAPS}/CAPSImg/CapsLoader.cpp ${CAPS}/CAPSImg/DiskImage.cpp
    ${CAPS}/CAPSImg/DiskImageFactory.cpp ${CAPS}/CAPSImg/stdafx.cpp ${CAPS}/CAPSImg/StreamCueImage.cpp
    ${CAPS}/CAPSImg/StreamImage.cpp)
target_include_directories(capsimg PRIVATE ${CMAKE_SOURCE_DIR}/capsimg ${CAPS}/LibIPF ${CAPS}/Codec
    ${CAPS}/Core ${CAPS}/Device ${CAPS}/CAPSImg)
target_compile_options(capsimg PRIVATE -O2 -w)

target_sources(retro PRIVATE ${ROOT}/src/amiga/amiga_machine.c)
target_compile_definitions(retro PRIVATE HAVE_MACHINE_AMIGA)
add_dependencies(retro puae capsimg)
add_dependencies(retro dosbox_pure)
target_link_libraries(retro dl)

# Old C, kept as upstream wrote it: its warnings are not ours to fix.
# And -UANDROID for it: the NDK defines ANDROID, and to EMULib that means
# Marat Fayzullin's own Android port (LibAndroid.h, his JNI). The machines
# here take the path they take on the boards and on the Mac, with none of
# EMULib's platforms defined.
target_compile_options(retro PRIVATE $<$<COMPILE_LANGUAGE:C>:-w -UANDROID> -O2)
# EMULib.c is left out on purpose, here and on the boards, but ImageMux.h
# compiles a GetColor() into video_glue.c that names its VideoImg. Nobody
# calls it; in a shared library, though, every global is exported and so
# kept, and the NDK's --no-undefined fails on it. Hidden visibility (only
# the JNIEXPORT entry points are exported) plus --gc-sections drops it, as
# the boards' static links always did.
set_target_properties(retro PROPERTIES C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden)
target_link_options(retro PRIVATE -Wl,--gc-sections)
target_link_libraries(retro c64 android log aaudio z)
