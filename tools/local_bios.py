"""PlatformIO pre-build hook: build in whichever ROMs are present locally.

None of these images are in the repository. They are generated from local
files by tools/embed_rom.py, are gitignored, and exist only on the owner's
machine. Each one that is present defines a flag so the firmware uses it:

  src/msx/hotbit_bios_data.c  HAVE_LOCAL_BIOS   an MSX BIOS that boots BASIC
  src/msx/local_cart_data.c   HAVE_LOCAL_CART   an MSX cartridge
  src/spectrum/spectrum_rom_data.c
                              HAVE_SPECTRUM_ROM the Spectrum 48K ROM
  src/c64/c64_rom_data.c      HAVE_C64_ROMS     the C64's BASIC, KERNAL,
                                                CHARGEN and 1541 DOS
                                                (tools/make_c64_roms.py)
  src/pc/pc_font_data.c       HAVE_PC_FONT      the PC's code pages 437 and 860
                                                font, from EGA.CPI
                                                (tools/make_pc_font.py)

A clone without any of them still builds. The MSX falls back to C-BIOS,
which runs cartridges but not BASIC; the Spectrum has no fallback and says
so.
"""
import os

Import("env")  # noqa: F821  (injected by PlatformIO)

bios_c = os.path.join(env.subst("$PROJECT_SRC_DIR"), "msx", "hotbit_bios_data.c")
if os.path.isfile(bios_c):
    env.Append(CPPDEFINES=["HAVE_LOCAL_BIOS"])
    print("local BIOS: %s found, embedding it (MSX-BASIC available)" % os.path.relpath(bios_c))
else:
    print("local BIOS: not present, falling back to C-BIOS (cartridge-only, no MSX-BASIC)")

cart_c = os.path.join(env.subst("$PROJECT_SRC_DIR"), "msx", "local_cart_data.c")
if os.path.isfile(cart_c):
    env.Append(CPPDEFINES=["HAVE_LOCAL_CART"])
    print("local cartridge: %s found, embedding it" % os.path.relpath(cart_c))
else:
    print("local cartridge: none, the machine boots with an empty slot")

spectrum_c = os.path.join(env.subst("$PROJECT_SRC_DIR"), "spectrum", "spectrum_rom_data.c")
if os.path.isfile(spectrum_c):
    env.Append(CPPDEFINES=["HAVE_SPECTRUM_ROM"])
    print("Spectrum ROM: %s found, embedding it" % os.path.relpath(spectrum_c))
else:
    print("Spectrum ROM: none. That firmware will say so and stop.")

snaps_c = os.path.join(env.subst("$PROJECT_SRC_DIR"), "spectrum", "spectrum_snap_data.c")
if os.path.isfile(snaps_c):
    env.Append(CPPDEFINES=["HAVE_SPECTRUM_SNAPS"])
    print("Spectrum snapshots: %s found, embedding them" % os.path.relpath(snaps_c))
else:
    print("Spectrum snapshots: none")

tapes_c = os.path.join(env.subst("$PROJECT_SRC_DIR"), "spectrum", "spectrum_tape_data.c")
if os.path.isfile(tapes_c):
    env.Append(CPPDEFINES=["HAVE_SPECTRUM_TAPES"])
    print("Spectrum tapes: %s found, embedding them" % os.path.relpath(tapes_c))
else:
    print("Spectrum tapes: none")

c64_c = os.path.join(env.subst("$PROJECT_SRC_DIR"), "c64", "c64_rom_data.c")
if os.path.isfile(c64_c):
    env.Append(CPPDEFINES=["HAVE_C64_ROMS"])
    print("C64 ROMs: %s found, embedding them" % os.path.relpath(c64_c))
else:
    print("C64 ROMs: none (tools/make_c64_roms.py); that machine will say so and stop")

pc_font_c = os.path.join(env.subst("$PROJECT_SRC_DIR"), "pc", "pc_font_data.c")
if os.path.isfile(pc_font_c):
    env.Append(CPPDEFINES=["HAVE_PC_FONT"])
    print("PC font: %s found, code pages 437 and 860" % os.path.relpath(pc_font_c))
else:
    print("PC font: none, the PC draws with lib/pc8086's (no accents, no double frames)")
