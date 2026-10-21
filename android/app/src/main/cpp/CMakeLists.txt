# The RetroComputer's native side: the machines (the same sources as the
# boards build) and the Android board, src/boards/android.
cmake_minimum_required(VERSION 3.22.1)
project(retro C CXX)

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
    ${ROOT}/src/machines.c
    ${MSX_GLUE}
    ${MSX_CORE})

target_include_directories(retro PRIVATE
    ${BOARD}/shim ${BOARD} ${ROOT}/src ${ROOT}/src/boards/common ${ROOT}/lib/z80
    ${ROOT}/src/msx ${ROOT}/src/spectrum ${PC} ${FMSX}/fMSX ${FMSX}/EMULib ${FMSX}/video)

# The flags the MSX is built with on every board (BPS16, LSB_FIRST, ESP32
# for the ESPlay-fMSX paths, FMSX for the Z80's fast fetch).
target_compile_definitions(retro PRIVATE HAVE_MACHINE_MSX HAVE_MACHINE_SPECTRUM HAVE_MACHINE_PC
    BPS16 LSB_FIRST ESP32 FMSX PC8086_LEAN)
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
target_link_libraries(retro android log aaudio z)
