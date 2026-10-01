# lib/c64: the Commodore 64

retroelec's [T-HMI-C64](https://github.com/retroelec/T-HMI-C64) core at
`dea1a82` (2026-09-05), GPLv3: `third_party_licenses/t-hmi-c64_gpl3.txt`.
Only the emulator itself came over (6502, VIC, CIAs, SID, the 1541, the
platform and driver interfaces); not its boards, displays, Wi-Fi, OTA or
keyboards, and not the Commodore ROMs it embeds, which come from local
files through `tools/make_c64_roms.py`.

The drivers it is built with are this project's, in `src/c64/`
(`retro_c64_drivers.h`), chosen by `BOARD_RETRO`.

## Local changes, all marked `NOT UPSTREAM`

- `Floppy.h` is `C64Floppy.h`: fMSX has a `Floppy.h` too, and PlatformIO
  puts every library's include folder on everyone's path.

- `Config.h`: a `BOARD_RETRO` board (our drivers, PSRAM on the ESP32, the
  card's `/c64/` folder) and the SID at 32750Hz, the buzzer's PCM rate,
  an exact 655 samples a 50Hz frame.
- `display/`, `keyboard/`, `sound/`, `fs/` factories: one branch each for
  our drivers.
- `C64Sys.cpp`, `getDC01`: the keyboard is read as a whole 8x8 matrix
  (`retro_c64_matrix_read`), so several keys can be down at once; upstream
  takes one key at a time from its Android app.

- `C64Sys.cpp`, `run`: yields its core for a tick once a frame. Upstream
  paces by spinning, and on a shared core that starved the BLE keyboard.
- `Floppy.cpp`, `Floppy.h`, `Hooks.cpp`: BASIC's `SAVE"NAME",8` writes
  `/c64/name.prg` (upstream saves only through its Android app). The
  IECOUT hook tells data from bus commands by the caller's return address
  on the 6502 stack (`$ED1B`, `$EDE9`: data), since it skips the code
  that drives ATN. Its per-byte log line is gone.

The KERNAL needs three bytes changed to reach the 1541 at all: the SEI at
`$EE13`, `$ED40` and `$EDCC` becomes a BRK that `Hooks.cpp` catches.
Upstream's embedded KERNAL has them; `tools/make_c64_roms.py` applies them
to the owner's clean dump (they are the only difference between the two).

`tools/c64host` builds it on the development machine. A disc whose loader
sends its own code into the drive (Maniac Mansion) does not load: this
1541 answers DOS commands, it does not run drive code.
