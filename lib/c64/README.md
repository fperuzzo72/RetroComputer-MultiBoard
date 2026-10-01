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

- `Config.h`: a `BOARD_RETRO` board (our drivers, PSRAM on the ESP32, the
  card's `/c64/` folder) and the SID at 32750Hz, the buzzer's PCM rate,
  an exact 655 samples a 50Hz frame.
- `display/`, `keyboard/`, `sound/`, `fs/` factories: one branch each for
  our drivers.
- `C64Sys.cpp`, `getDC01`: the keyboard is read as a whole 8x8 matrix
  (`retro_c64_matrix_read`), so several keys can be down at once; upstream
  takes one key at a time from its Android app.

`tools/c64host` builds it on the development machine.
