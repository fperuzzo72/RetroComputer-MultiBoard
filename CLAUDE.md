# CLAUDE.md

Context for Claude Code (or any assistant) picking up this project. Read
`README.md` first for user-facing details (build/flash/pairing/SD layout);
this file is about *why* things are built the way they are, and what's
still open.

## What this is

Emulator firmware for the Freenove FNK0103 3.5" ESP32 display board (the
"Cheap Yellow Display": plain ESP32-WROOM-32E, ST7796 SPI TFT, resistive
touch, microSD), with input from a BLE keyboard rather than the
touchscreen. PlatformIO + Arduino.

**Two machines are built from this tree**, one firmware each:

- `pio run -e msx` - MSX1, an Epcom Hotbit HB-8000. **Running on
  hardware**: boots a dumped BIOS into MSX-BASIC with no SD card, sound
  generated, BLE keyboard with US-International accents, cartridges from
  flash. 20-30 fps depending on scale.
- `pio run -e spectrum` - ZX Spectrum 48K. **Compiles; has never run.**
  There is no Spectrum ROM on this machine and the board was unplugged
  when it was written. Do not describe it as working.

The split that makes this possible: `src/boards/cyd/` is the board and
knows nothing about what is emulated, `src/msx/` and `src/spectrum/` are
machines and know nothing about the board, and `src/machine.h` is the only
thing that crosses. (Until 2026-09-28 the board lived in `src/device/` and
`src/main.cpp`; they moved, byte-identical builds, when a second board
arrived. See "The Paper Mono and the Macintosh" below.) `lib/z80/` is
the CPU, shared; `lib/fmsx_core/` is the rest of fMSX and is MSX-only,
excluded from the Spectrum build by `lib_ignore`.

## How it's built (context for future changes)

- **Emulator core** (`lib/fmsx_core/{Z80,fMSX,EMULib}`): vendored, not
  reimplemented, from Marat Fayzullin's fMSX via the `pebri86/esplay-fMSX`
  ESP32 port. Kept as close to upstream as possible - changes are
  localized and commented (search `MSX.c` for "embedded fallback" to find
  the one deliberate patch, which adds a C-BIOS fallback to `LoadROM()`).
  Do not casually "clean up" this code; it's old, terse C on purpose and
  matching upstream matters more than style here.
- **Video** (`lib/fmsx_core/video/AVideo.i` + `msx_display.h` +
  `src/display_bridge.cpp`): AVideo.i is also vendored (from the same
  esplay-fMSX project, MIT-licensed portion by Schuemi), trimmed to
  remove a touch-driven virtual keyboard overlay, and rewritten to render
  one 24-line band at a time instead of keeping a whole frame (see
  docs/MEMORY.md for why). `display_bridge.cpp` is the one file that talks
  to TFT_eSPI, and it owns where the picture lands and at what scale.
  **An earlier note here claimed TFT_eSPI wants normal RGB565 order and
  that the byte swap was dropped. That was wrong and it cost hours**:
  `pushPixels` streams the buffer raw and needs `setSwapBytes(true)`. See
  docs/DISPLAY.md, which collects that and the other two display faults.
- **Keyboard**: split in two on purpose. `src/ble_keyboard.cpp` is only
  transport (NimBLE central, Boot Keyboard Input Report 0x2A22, falling
  back to generic Report 0x2A4D). Everything about what the keys *mean* -
  the US-International layer, the dead keys, this machine's matrix - is in
  `src/msx_keys.c`, which is plain C and knows nothing about BLE. The
  Hotbit's matrix does not match the international MSX layout fMSX
  assumes, so `Keys[]` goes unused and the matrix is rebuilt from scratch
  every frame. **Read docs/KEYBOARD.md before touching any of it**; the
  layout came out of the BIOS ROM's own tables and the dead keys were
  identified on the hardware.
- **The C/C++ wall** (`src/msx_bridge.h`): fMSX's `Z80.h` does
  `typedef unsigned short word` and the ESP32 Arduino core's `Arduino.h`
  does `typedef unsigned int word`. Any translation unit pulling in both
  fails to compile, and letting Arduino's 32-bit `word` win would silently
  change the layout of every fMSX structure. So **no .cpp file may include
  MSX.h or Z80.h** - C++ talks to the core only through `msx_bridge.h`,
  and the implementations live in plain-C files that never see Arduino.h.
- **Memory**: the single hardest constraint, and the reason for several
  things that look odd (the BIOS executing from flash, the video band
  buffer, EmptyRAM being a static array, the allocation ordering in
  `setup()`). **docs/MEMORY.md has the measured numbers**; do not undo any
  of it without reading that first.
- **Serial console** (`src/debug_console.cpp`): types into the emulated
  machine and reads its screen back, so the firmware can be verified over
  the USB cable with nobody watching the panel. This is how the keyboard
  layout was worked out and how it should be re-checked after changes.
- **BIOS**: the owner's own dumped Hotbit HB-8000 BIOS, embedded in flash
  by `tools/embed_rom.py` into `src/hotbit_bios_data.c`. **That file and
  the ROM it comes from are gitignored and must never be committed or
  published.** A clone without them still builds: `tools/local_bios.py`
  (a PlatformIO pre-build hook) only defines `HAVE_LOCAL_BIOS` when the
  generated file is present, and otherwise the C-BIOS in `src/cbios_data.c`
  is used - which boots cartridges but not MSX-BASIC. An `MSX.ROM` on an
  SD card still overrides both.
- **SD card** (`src/sd_mount.cpp`): via ESP-IDF's
  `esp_vfs_fat_sdspi_mount` (not Arduino's `SD.h`) so the vendored core's
  plain `fopen()`/`fread()` on hardcoded `/sdcard/msx/...` paths work
  unmodified. **Not mounted at boot** - see open item 3. Two things here
  are load-bearing: the host must be set to `SPI3_HOST` explicitly,
  because the default is the display's bus and taking it blanks the panel
  with no error at all, and `max_files` must stay small, because FATFS
  keeps a 4kB cache per open file.
- **CI** (`.github/workflows/build.yml`): builds the `fnk0103` PlatformIO
  env on every push/PR and uploads `firmware.bin` +
  `bootloader.bin`/`partitions.bin` as an artifact, so a full ESP32
  toolchain is never required locally - flash the artifact with `esptool`
  alone. This exists because this project's firmware was never actually
  compiled in the sandbox that originally wrote it (no toolchain
  download there) - GitHub Actions is the first real compile pass, so
  treat early CI failures as genuinely new information, not noise.

## Known open items / likely next requests

1. **Speed.** Largely solved, and not where anyone was looking.

   **Measure before touching this**: `q` on the serial console splits a
   frame into its parts on both machines. The emulated Z80 runs at 9.1MHz,
   260% of a real Spectrum, and never was the bottleneck. What was: a
   `getTouch()` call once per frame, costing **12.6ms of a 23ms frame**,
   because TFT_eSPI debounces pressure in a loop with a `delay(1)` in it
   and an untouched resistive panel is noisy. The hold gesture now reads
   `getTouchRawZ()` instead, at 45us. Spectrum went 43 -> 98 fps, MSX
   15.3 -> 21.8 at 1.5x and 31.7 at 1:1.

   What is left is the panel. The MSX blit is 13ms at 1:1 against 9.8ms of
   unavoidable wire time (786kbit at 80MHz). **At 1.5x, 60fps is
   arithmetically impossible on this bus**: 110,592 pixels 60 times a
   second is 106Mbit/s and the bus carries 80.

   **Two things past this were tried and measured slower. Do not redo
   them without reading this.**

   - *DMA per scanline*, two line buffers and `pushPixelsDMA`: blit
     13.3ms -> 20ms. A line is 512-768 bytes and TFT_eSPI's per-transfer
     setup costs more than the conversion it overlaps. (An earlier attempt
     also put a flashing white screen up. That was a buffer-reuse bug; the
     slowness is real and survives fixing it.)
   - *The blit on its own task*, pinned to core 0, with two 12-line bands
     costing the same RAM as one 24-line band: 31.7 fps -> 26.8, and the
     blit itself 13.2ms -> 32.1ms. Two cores running flat out contend for
     flash cache and DRAM by more than the overlap wins, and the MSX was
     left with 300 bytes of heap. The deadlock it started with is worth
     remembering too: draining the queue by taking every band permit can
     never succeed, because the renderer always holds one.

   What did work: pushing `BLIT_ROWS` rows per `pushPixels` call rather
   than one. Two is the sweet spot - four gains 0.4 fps more and costs 2kB
   that the BLE keyboard needs.

   **And then `-D FMSX`, which was the real one.** `lib/z80/Z80.c` has a
   fast inline `OpZ80` for fMSX that reads the `RAM[]` page table directly
   instead of calling `RdZ80` for every instruction byte, and this project
   had never defined the flag that enables it. MSX-BASIC at 1:1 went from
   28.4 to 36.6 fps, the Z80's share of a frame from 15.2ms to 10.9ms.
   The Spectrum's copy of the core opts out in `z80_names.h`: it has no
   `RAM[]` and must not link against fMSX's.

   **Measure on a fixed workload.** How long a frame takes depends on what
   the emulated program executes, not only on its T-states - Nemesis reads
   30 to 42 fps depending on whether it is on its title screen or running
   its attract mode, and comparing across those is how a change gets
   credited with a win it did not earn. MSX-BASIC at its prompt is the
   bench used here.

   Two more things tried after that, both dead:

   - *The Z80 loop in IRAM* (`-D Z80_IN_IRAM`, an attribute on RunZ80):
     the linker comes out **5752 bytes over**. The loop is too big.
   - *BLE scanning as a suspect*: turning the scan off changes nothing,
     measured. It is not stealing time from the emulation.

   Where the MSX time still goes, per frame at 1:1: blit 13.0ms against
   9.8ms of unavoidable wire, Z80 10.9ms, everything else about 3.4ms.
   Both machines' Z80 copies now run at about the same rate, so the core
   itself is not the outlier it was.
2. **Nobody has heard the sound.** `InitSound()` reports 22050Hz,
   `PlayAllSound()` feeds `RenderAndPlayAudio()` into the I2S built-in DAC
   (`src/audio_glue.c`), and `PLAY` runs without stalling the frame rate,
   but no sound has been confirmed coming out of the speaker. Freenove's
   own MP3 example for this board uses `AudioOutputI2S(0, 1)`, the
   internal-DAC mode, which is what that file drives.
3. **The SD card is not mounted at boot** (see the note in `setup()`).
   Nothing needs it - the BIOS is in flash - and mounting costs ~45kB,
   which with a card in the slot left the emulated VRAM 12 bytes short of
   fitting. `m 1` on the serial console mounts it on demand. Loading ROMs
   and anything MSX-DOS-shaped will have to mount it and decide what to
   give up for the memory; that is the next real design question here.
4. **No cartridge loading yet.** `ROMName[0]` still points at a fixed
   `/sdcard/msx/games/game.rom`. A ROM browser is the natural next step
   and needs item 3 settled first.
5. **No floppy, no MSX-DOS.** Would need a DISK.ROM and the card mounted
   for the life of the session.
6. **No joystick emulation** - `Joystick()`/`Mouse()` in
   `src/platform_glue.c` return 0 unconditionally. Cursor-key-as-joystick
   games still work via the keyboard.
7. **PSRAM**: settled, there is none. `esptool flash-id` reports an
   ESP32-D0WD-V3 with no embedded PSRAM and 4MB of flash. The whole memory
   budget in docs/MEMORY.md is built on that.

## The Spectrum tape: two mechanisms, and why both are needed

`src/spectrum/spectrum_tape.c` plays a `.tap` **as a signal** - bit 6 of
port 0xFE driven from a generated pulse train, standard timings - and also
**traps the ROM's LD-BYTES at 0x0556** and hands over whole blocks when the
tape is sitting at the start of one. Neither alone is enough:

- The trap is instant but only serves loaders that call the ROM. Nebulus
  brought its own and stopped at block 4 forever.
- The signal serves any loader, correctly, but at tape speed - and tape
  speed is real: Halls of the Things is 148 seconds of tape and Nebulus
  273, measured off the `.tap`. This board runs the Z80 at about 0.9 of a
  real Spectrum, so a signal-only load of Nebulus is five minutes with a
  blank screen. **That looks exactly like a hang and is not one**; it cost
  a day here before `spectrum_tape_progress()` existed to tell them apart.

So the trap takes the blocks the ROM asks for and the signal takes the
rest. The trap is disarmed for good the moment a loader is caught reading
mid-block, because from then on it is in charge.

The trap plants an opcode, which needs the ROM in RAM: `spectrum.c` copies
it there at startup (16kB) and `spectrum_rom_writable()` hands the copy
over. Without the copy everything still works, only slowly. The copy is
also about 7% faster than executing from flash, which was measured and is
not why it is there.

### tools/tapebench - test this without the board

The whole thing compiles on the host against the real `spectrum_tape.c`,
the real `lib/z80`, and a real ROM. It runs a three-minute tape in under a
second, which is the only reason any of this got debugged.

```bash
make -C tools/tapebench
./tools/tapebench/loader 48.rom game.tap   # will the ROM loader accept the signal
./tools/tapebench/load   48.rom game.tap   # boot, type LOAD "", does the game come up
TRAP=0 ./tools/tapebench/load 48.rom game.tap   # signal only, no shortcut
```

Measured over the 30 tapes: **28 load with both mechanisms, 26 with the
signal alone.** Avalon and Thrust load with neither. Run this before
believing anything about tape changes - reasoning about pulse timings on
the device is how a week disappears.

### Which is why the firmware ships snapshots, not tapes

`tools/tapes_to_snaps.sh` runs that same bench over a directory of tapes,
snapshots each game once it has loaded, and verifies the snapshot by
restoring it into a fresh machine. The firmware carries the results: 28
snapshots, plus the two tapes that would not convert. **Do not build a
game in twice** - a tape and a snapshot of the same game is a megabyte of
flash for nothing, and flash is 84% full.

The tape code stays because the conversion needs it, because Avalon and
Thrust still ride on it, and because it is the only honest way to load a
tape a user supplies later.

## The Paper Mono and the Macintosh

A second board, `src/boards/papermono/`: M5Stack Paper Mono, ESP32-S3,
8MB PSRAM, 800x480 1-bit SSD1677 e-ink, FT6336 touch. Different chip and
toolchain from the CYD (pioarduino, Arduino core 3.3), with the hardware
driven by **freeink-sdk, a git submodule pinned to `6dfe245`, the commit
CrossPlay runs on this same device**. Do not move it to a newer SDK
without a reason; that commit is the one known to light this panel.

The first machine on it is a **Macintosh Plus** (`pio run -e
papermono`, `src/mac/`, `lib/umac/`). Status as of 2026-09-28:
**runs on the device** (flashed to app1 beside CrossPlay in app0; otadata
selects which). Measured there: 175-181% of a Mac Plus, fast refresh
~320-400ms, Finder reached and a serial-console double-click opens the
disc, and **the owner double-clicked the disc icon with a finger** on the
panel, so the picture is the right way round and touch drives the pointer.

- `src/mac/mac_core.c` is plain C with no board in it; `mac_machine.c` is
  the ESP32 task loop and the Machine table. The split exists so
  `tools/machost` can build the same core on the Mac. **Use machost before
  reasoning about anything the Mac does**: 30 emulated seconds of boot
  take under a second there, and it takes PNG screenshots.
- The boot is **~30 emulated seconds**, most of it the ROM testing 4MB of
  RAM. A screenshot at 20s shows a grey screen and looks like a hang. It
  is not one; this cost an hour on the first run.
- Touch is an absolute mouse written into the ROM's cursor globals
  (MTemp, RawMouse, CrsrNew), and **a button change waits until the
  `Mouse` global has caught up**. Deliver both at once and the click lands
  where the cursor was. Verified on the host: menus open, icons
  double-click.
- `display_mono.h` is the 1-bit framebuffer handoff: the machine attaches
  its framebuffer once and never waits; the board compares and refreshes
  on its own schedule (`panel.cpp`). The Paper Mono driver has no partial
  window, so every refresh is the whole panel; the waveform, not the 48kB
  of SPI, is what takes the time. `s` on the console reports refresh times
  and emulation speed.
- **The device is held upside down**, buttons along the top, because at
  the bottom the hand kept pressing them (owner's request, 2026-09-28).
- **The Mac is 512x342, scaled 1.40x** (nearest-pixel, 719x480 centred).
  It started at the panel's native 800x480; the owner found it too small
  to read. `src/boards/papermono/picture.h` holds the scale and the
  upside-down flag, and both the picture and touch go through it.
- **Touch is a trackpad** (`trackpad.c`, plain C): drag pushes the
  pointer with acceleration, tap clicks, tap-tap double-clicks,
  tap-then-drag holds the button. GPIO2 is the mouse button, GPIO3 a full
  refresh, read straight off the pins (InputManager's two-button logic
  reports presses only on release). Replaced pointing straight at things
  because a finger is too big for a Mac close box. Three bugs were found
  by `tools/papermono_test` before the device saw any of it: the slop
  catch-up getting fast-end acceleration (every push overshot), a
  double tap holding the button 20ms (the Mac saw one click), and the
  sad Mac below.
- **Input has its own task (10ms, core 0, above the board task).** The
  first trackpad build on the device read touch from the same loop as the
  panel refresh, which blocks ~400ms, and pointer movement keeps the panel
  refreshing back to back: touch was sampled once in ~450ms. The owner
  saw a jumping pointer, lost taps, a double-click arriving seconds late
  and buttons that never registered. The host test could not see any of
  it, because it has no refresh. `s` now prints the longest gap between
  input samples; it should stay near 10ms.
- **Only `FULL_REFRESH` clears ghosting on this panel.** The Paper Mono
  driver treats `RefreshMode::Full` as its one corrective mode;
  `HALF_REFRESH` is an ordinary update. The first builds used HALF for
  "clean" and the owner watched the ghosting pile up with the button doing
  nothing. With FULL the screen came out clean. But FULL here is not a
  flashing clean either: this driver has none, and it drives every pixel
  through the ordinary waveform (~395ms, same as fast), which under the
  desktop pattern reads as the whole screen blinking. Cleaning after 1.5s
  idle / 10 fast / cap 60 blinked every ~20s and was too much; now 4s idle
  after 30 fast, cap 200. `s` prints why each clean ran.
- **Pointer movement: the first trackpad's is the one to keep.** Held
  still until the 14px tap slop, then caught up and followed. Smallest
  nudge ~6 Mac pixels, which the owner found fine ("anda bem o
  suficiente"). Two finer versions (following from the first pixel;
  following after a 6px slop without catch-up) felt erratic on the device
  and were undone. Raw touch samples (`r`) showed the FT6336 is clean: a
  still finger reports the same point every sample, a moving one 1-6px
  steps every ~11ms. So jitter was not it; do not re-add filtering for it.
- **Window refreshes were tried and removed.** Measured: 2.3s per window
  refresh against ~400ms whole-panel, input blocked up to 2s behind them,
  the pointer leaping and the screen darkening.
- **The controller sleeps after 1s without a refresh** (`controllerIdle`).
  Left awake it holds the drive rails up and a still screen darkened, with
  the refresh count not moving. `s` counts the sleeps.
- **The text caret is not shown blinking.** TeachText's caret is one
  1-pixel column 16 tall, blinking about twice a second (measured with
  machost); every blink was a refresh and an editor never let the panel
  rest. `panel.cpp` skips a change that is one bit in one byte column
  under 24 rows. A typed letter is 6+ pixels wide and goes out at once.
- Measured on the device (2026-09-28, three minutes of use): input sampled
  every 11-13ms, fast refresh 398-407ms, emulation 154-206% of a Mac Plus.
  Tap-then-drag on a menu works and the owner is happy with it; the
  pointer speed is "good enough".
- **Never write the cursor globals during boot.** The ROM's RAM test
  reads its patterns back and a pointer write in the middle is a sad Mac,
  03FFFF. `mac_core.c` waits for CrsrCouple == 0xFF and Mouse on screen.
- **Run `tools/papermono_test` after touching picture.h, trackpad.c or
  the pointer code.** Its probes were wrong three times before they were
  right (a desktop pattern looks like a title bar; the cursor sprite
  moving is not the screen changing), so read the PNGs it writes, not
  only its verdicts.
- ROM and disc come from `roms/mac/` (gitignored) through
  `tools/local_mac.py`, which checks for the v3 ROM (4D1F8172) and links
  both in with `.incbin`. Never commit them.
- Partition table `partitions-papermono.csv` is CrossPlay's, byte for byte.
  The Mac goes into **app1 (0x800000)**; CrossPlay stays in app0. Which one
  boots is otadata at 0xe000: sequence 2 in the second entry picks app1,
  and the CRC is `zlib.crc32(seq_le32, 0xffffffff)`. The original otadata,
  the table and a full app0 image are in
  `~/github/_backups/papermono-2026-09-28/`; writing back `pm_ota.bin`
  returns to CrossPlay. Never `pio run -t upload` here: it writes app0.

## Licensing (do not relax this casually)

`LICENSE` at the repo root is MIT but explicitly scoped (see its "NOTE ON
SCOPE" section) to only the original glue code - not the vendored
`lib/fmsx_core/` core, which is under Marat Fayzullin's fMSX/EMULib
license (personal-use-only, not commercial-redistribution-friendly). Full
texts for every vendored/adapted piece are in `third_party_licenses/`.
`lib/umac/` is MIT except its disc driver (Basilisk II) and key code
table (Mini vMac), which are GPLv2; see
`third_party_licenses/umac_and_musashi.txt`.
Keep this distinction intact in any future README/LICENSE edits.
