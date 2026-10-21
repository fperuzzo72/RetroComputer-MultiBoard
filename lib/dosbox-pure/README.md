# lib/dosbox-pure: the PC 386

Bernhard Schelling's [DOSBox Pure](https://codeberg.org/schelling/dosbox-pure)
at `468dad0` (2026-10-19), GPLv2 or later: `LICENSE` here, and
`third_party_licenses/dosbox-pure_gpl2.txt`. The GitHub repository only
says it moved; Codeberg is upstream.

A libretro core (DOSBox with a start menu, zip and disk-image mounting,
386/486 with VGA/SVGA, Sound Blaster, a recompiler for ARM64). The Android
app builds it as its own shared library, `libdosbox_pure.so`, and the PC
386 machine (`src/pc386/`) loads it with dlopen, as a libretro frontend
does: so its several hundred globals stay in their own library, away from
the other cores'. `tools/dosboxhost` does the same on the Mac.

Only the sources came over: `*.cpp`, `*.h`, `src/`, `include/`,
`libretro-common/` and the license files, not its project files, CI or
images. `library.json` keeps PlatformIO's ESP32 builds out of it.

No local changes.
