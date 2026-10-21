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
`libretro-common/`, the license files and the `Makefile` (`make` here
builds `dosbox_pure_libretro.dylib` for tools/dosboxhost, into `build/`,
gitignored), not its project files, CI or images. `library.json` keeps
PlatformIO's ESP32 builds out of it.

## Local changes, all marked `NOT UPSTREAM`

- `src/dos/dos_tables.cpp`, `src/dos/dos.cpp`: the country is Brazil, as
  COUNTRY=055 gives it (dates day/month/year, decimal comma, 24 hours;
  INT 21h/38h answers 55). Upstream is the US.
- `src/shell/shell.cpp`, `src/shell/shell_cmds.cpp`: DATE shows and takes
  the day first.
- `src/dos/dos_keyboard_layout.cpp`: the keyboard layout option takes a
  codepage after a space, `ux 850`, as KEYB does. Upstream takes the
  layout's first codepage, 437 for US-International, which has no ã or õ.
  860 is not among the codepages built in; 850 has every Portuguese
  letter.
- `src/hardware/keyboard.cpp`: when the key repeating is let go and
  another is still held, that one repeats again (the last pressed of
  those still down; Shift, Ctrl, Alt and the locks never do). A real
  keyboard never goes back; Test Drive reads the repeats, so holding Up,
  tapping A to change gear stopped the acceleration until Up was pressed
  again.
