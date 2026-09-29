# RetroComputer-MultiBoard

[![Build firmware](https://github.com/fperuzzo72/RetroComputer-MultiBoard/actions/workflows/build.yml/badge.svg)](https://github.com/fperuzzo72/RetroComputer-MultiBoard/actions/workflows/build.yml)

Old computers on small ESP32 boards: several machines, several boards,
one tree. It started as an MSX on a Cheap Yellow Display, which is why the
CYD comes first below.

The first board is the Freenove FNK0103 3.5" ESP32 display board, the
"Cheap Yellow Display" with the ST7796 panel, driven by a Bluetooth Low
Energy keyboard rather than the touchscreen. On it, **two machines**:

| | |
|---|---|
| `pio run -e cyd` | both, chosen from a menu when the board powers up |
| `pio run -e msx` | MSX1, an Epcom **Hotbit HB-8000**, on its own |
| `pio run -e spectrum` | **ZX Spectrum 48K**, on its own |

Both run on the hardware. The boot menu picks the machine *and* what it
starts with - a cartridge, a snapshot, a tape - and everything is in
flash, so there is no SD card in any of this.

And a second board on the way: the **M5Stack Paper Mono**, an ESP32-S3
with an 800x480 e-ink panel, carrying a **Macintosh Plus**. See "The
Macintosh, on the Paper Mono" below.

## Status, in detail

**MSX:** boots a dumped Hotbit HB-8000 BIOS into MSX-BASIC. Sound is
generated and reaches the amplifier. A BLE keyboard pairs and types, with
US-International dead keys composing á ã â é ê ó ô ú ç and the rest.
Twenty-one cartridges are built in, MegaROMs included, with the Konami
mapper. Speed is 20-30 fps depending on the picture scale and what is
running, against the 60 a real MSX manages; see "Speed" below.

**Spectrum:** memory map, ULA video with the interleaved display file and
attribute colours, the 8x5 keyboard matrix through port 0xFE, a 50Hz IM1
frame, `.tap` tapes that load, and twenty-eight games built in as
snapshots so they start instantly. No contention, and the beeper is read
but not sounded.

**Both:** a picture scale toggle (1:1 or 1.5x), and a selector you reach
by holding a finger on the screen, to change cartridge or tape without
rebooting.

## Tapes: loaded once, here, not on the board

A `.tap` is played **as a signal**: a pulse train on bit 6 of port 0xFE,
standard timings, which is what a game's own loader expects. It is also
handed over **whole** whenever the ROM's LD-BYTES asks for a block, which
is instant. Both are needed - the shortcut alone leaves Nebulus stuck at
block four forever, and the signal alone takes as long as the tape did,
which for Nebulus is 273 seconds.

So the tapes are loaded **on your own machine instead**, once, and what
gets built into the firmware is the loaded game:

```bash
tools/tapes_to_snaps.sh 48.rom roms/spectrum roms/spectrum-snaps
```

That boots a Spectrum on the host, types `LOAD ""`, waits for the load,
writes the memory out as a `.sna` and then puts the snapshot back into a
fresh machine to check the game really is in it. Twenty-eight of the
thirty tapes here convert; Avalon and Thrust do not, so they stay as
tapes. Nothing is carried twice.

It is worth saying what this buys: Nebulus went from five minutes of blank
screen to appearing at once. The tape code is still there and still
correct, and `y` on the serial console reports how far a tape has got -
because a tape loading and a machine hung look identical, which cost a day
here.

`tools/tapebench` is the same machinery on its own, for debugging tape
changes without a board. It gets through a three-minute tape in under a
second:

```bash
make -C tools/tapebench
./tools/tapebench/load   48.rom game.tap    # boot, LOAD "", did it come up
./tools/tapebench/loader 48.rom game.tap    # will the ROM accept the signal
TRAP=0 ./tools/tapebench/load 48.rom game.tap   # signal only, no shortcut
```

## The Macintosh, on the Paper Mono

`pio run -e papermono` builds, among others, a Macintosh Plus for the M5Stack Paper
Mono: Matt Evans' [umac](https://github.com/evansm7/umac) on the Musashi
68000 core (`lib/umac/`, see its README), a Mac Plus ROM, **4MB** of RAM
in PSRAM, System 6 or 7.

**The Mac runs at its own 512x342, scaled 1.40x** to fill the panel's
height (719x480, centred). umac can patch the ROM to the panel's own
800x480, and that boots fine, but the panel is about 220 pixels to the
inch against the Mac's 72, and a Mac pixel for a panel pixel was too small
to read. The scaling is nearest-pixel, so some Mac pixels come out two
panel pixels wide and some one; for 1-bit text on a 1-bit panel that
reads better than anything averaged. `src/boards/papermono/picture.h`.

**The device is held with its buttons along the top**, and the picture
and touch are turned 180 degrees to match: along the bottom, the hand
holding it kept pressing them.

**Status: it runs on the device.** First flashed 2026-09-28: System 6.0.8
boots to the Finder in well under 30 seconds, because the emulated Mac runs
at **175-181% of a real Mac Plus**. A fast panel refresh measures
**~320-400ms**. The scaled picture and the trackpad below are tested on
the host and not yet on the device.

**The panel is a trackpad**, because a finger is far too big for a Mac's
close box. The finger pushes the pointer rather than standing on it:

| | |
|---|---|
| drag anywhere | moves the pointer, finer when slow, further when quick |
| tap | click |
| tap, tap | double-click |
| tap, then touch and drag | holds the button down (menus, windows, selecting text) |
| button on GPIO2 (top right, held buttons-up) | the mouse button, held for as long as it is |
| button on GPIO3 | full refresh, to clear the ghosts fast refreshes leave |

`src/boards/papermono/trackpad.c` is plain C, and `tools/papermono_test`
runs it, with `picture.h`, in front of the real Mac on the development
machine: a simulated finger aims the pointer, double-taps the disc open and
holds the Apple menu down, and every pixel of the scaled, turned panel is
checked against the Mac pixel it should show.

```bash
make -C tools/papermono_test
./tools/papermono_test/papermono_test roms/mac/macplus.rom roms/mac/boot.img /tmp/pm
```

Under all that, the Mac only ever had a relative mouse, so the position is
written straight into the ROM's cursor globals, the way Mini vMac does it.
Two things about that cost time and are worth knowing: nothing may be
written there until the system is keeping those globals, or the ROM's RAM
test reads it back and stops with a sad Mac (03FFFF); and a button change
waits until the Mac's own `Mouse` global says the cursor has arrived, or a
tap clicks wherever the cursor was before.

The ROM and the boot disc are not in the repository. Put them in
`roms/mac/`, which git ignores:

```bash
# MAME's macplus set has the ROM as two halves; this joins and checks them
python3 tools/make_macplus_rom.py 342-0341-c.u6d 342-0342-b.u8d roms/mac/macplus.rom
cp "System 6.0.8.img" roms/mac/boot.img
```

`tools/local_mac.py` checks the ROM (it must be the v3 ROM, checksum
4D1F8172, the only one umac can patch) and links both into flash as they
are. Without them the firmware still builds and says what is missing.

`tools/machost` is the same Mac on the development machine: the same
`src/mac/mac_core.c` and `lib/umac`, a real ROM and disc, emulated time,
a script of pointer moves, clicks and keys, and PNG screenshots. Thirty
emulated seconds of boot take under a second:

```bash
make -C tools/machost
./tools/machost/machost roms/mac/macplus.rom roms/mac/boot.img \
    "run 30; shot finder.png; click 471 42; click 471 42; run 2; shot disc.png"
```

On the device, `d` on the serial console prints the Mac's screen and
`tools/fbdump.py` turns a captured log into a PNG, so what the machine is
showing can be checked over the cable. `s` reports the speed as a
percentage of a real Mac Plus, and the panel's refresh times.

## The layout of this repo

```
src/boards/cyd/        the CYD: panel, amplifier, BLE keyboard host, card,
                       serial console. Knows nothing about what is emulated.
src/boards/papermono/  the Paper Mono: e-ink panel, touch, serial console
src/machine.h          the only thing that crosses between boards and machines
src/display_mono.h     how a 1-bit machine hands its framebuffer to a board
src/msx/               the MSX1, on the vendored fMSX core
src/spectrum/          the ZX Spectrum 48K
src/mac/               the Macintosh Plus
lib/z80/               Marat Fayzullin's Z80, shared by MSX and Spectrum
lib/fmsx_core/         the rest of fMSX: VDP, PSG, mappers. MSX only.
lib/umac/              umac and Musashi, the Macintosh core
freeink-sdk/           the Paper Mono's hardware library (git submodule)
```

The split is worth keeping. Everything painful about the CYD - one DRAM
region big enough for the machine's RAM, a panel that shares a bus with
the card, an amplifier behind an enable pin - lives in `src/boards/cyd/`
and is solved once for both machines, and the Paper Mono came in as a
second directory beside it without either machine noticing.

## ROMs: none are in this repository

No BIOS, no cartridge, no Spectrum ROM. They are other people's, and they
stay out of the tree and out of git. Each is built into the firmware from
a local file, and `.gitignore` covers the generated C:

```bash
# an MSX BIOS of your own - this is what makes MSX-BASIC work
python3 tools/embed_rom.py your-msx.rom src/msx/hotbit_bios_data.c

# an MSX cartridge, 8kB to 32kB, plain ROM
python3 tools/embed_rom.py game.rom src/msx/local_cart_data.c local_cart_rom

# the Spectrum 48K ROM
python3 tools/embed_rom.py 48.rom src/spectrum/spectrum_rom_data.c spectrum_rom
```

The build says which it found. Without an MSX BIOS the MSX falls back to
**C-BIOS**, which is open source and ships here, and which runs cartridges
but **not** MSX-BASIC. The Spectrum has no fallback: with no ROM it says
so and stops.

They go into flash rather than into RAM, and are executed from there. That
is not tidiness - a 32kB image copied into RAM does not fit next to 64kB
of emulated RAM on this board, and the machine simply fails to start. Same
for cartridges.

## Building and flashing

```bash
pio run -e cyd -t upload       # both machines, menu at boot
pio run -e msx -t upload       # the MSX on its own
pio run -e spectrum -t upload  # the Spectrum on its own
pio device monitor             # 115200
```

**Use 460800 for uploads, not 921600.** At 921600 esptool reads the MAC
and then times out on the stub loader every time on this board. The
`platformio.ini` here already does this.

GitHub Actions builds both on every push and uploads `firmware.bin`,
`bootloader.bin` and `partitions.bin` as artifacts, so you can flash with
`esptool` alone and never install a toolchain. Those artifacts have no
ROMs built in, for the reasons above.

## Hardware reference

Pin assignments are Freenove's own, from the setup files and schematic
they ship for this board (FNK0103N and FNK0114N are the same design under
different product codes).

| | |
|---|---|
| TFT (ST7796, **HSPI**) | MISO 12, MOSI 13, SCLK 14, CS 15, DC 2, RST tied to EN, BL 27 |
| Touch (XPT2046) | CS 33, unused here |
| microSD (**VSPI**) | SCK 18, MISO 19, MOSI 23, CS 5 |
| Audio | GPIO26 into an SC8002B, enable on **GPIO4 active LOW**, out to an SP+/SP- header |

Three things on that table have bitten this project and are worth stating
plainly:

- **The card must be told to use VSPI.** `SDSPI_HOST_DEFAULT()` returns
  `SPI2_HOST` on the ESP32, and SPI2 *is* HSPI, the panel's bus. Mounting
  a card on the default host reassigns the pin matrix and the display goes
  dark with no error anywhere.
- **GPIO4 must be pulled low** or the amplifier stays in shutdown and the
  DAC plays to nobody.
- **There is no speaker on the board.** SP+/SP- is a header. Connect a
  small 8-ohm speaker or you will hear nothing however correct the rest
  is.

Chip: **ESP32-D0WD-V3**, no PSRAM, 4MB flash, confirmed with
`esptool flash-id` rather than assumed. Everything in `docs/MEMORY.md`
follows from that.

## The keyboard

BLE HID, Bluetooth Low Energy only - a Bluetooth Classic keyboard will not
appear. The firmware subscribes to **both** the boot keyboard report and
the generic report characteristics, because plenty of keyboards expose the
former and only ever notify on the latter, which gives a connection that
is up, subscribed and permanently silent.

On the MSX it behaves as **US-International**: the dead keys compose, `'`
then `c` is ç, and a dead key then Space is the accent alone. Case follows
this machine's own rule, CAPS *or* Shift, which was measured rather than
assumed. Esc is ESC, PageDown is STOP so **Ctrl+PageDown breaks** a
running program, PageUp is SELECT.

`docs/KEYBOARD.md` has the whole story, including the accented character
codes, which had to be measured on the machine: the lower-case half is the
standard MSX international set but the capitals are a Brazilian invention
laid over codes the standard set uses for something else.

## The serial console

The firmware can be driven and read back over the USB cable, which is how
almost everything here was verified without anyone watching the panel.
115200 8N1:

```
s            dump the machine's screen, read out of its own video memory
t <text>     type as if on the keyboard (\n is Return)
g <code>     draw a character's 8x8 glyph
r <a> [n]    peek the machine's memory
z [1|2]      picture scale, 1:1 or 1.5x
h            heap, frame rate, how much of a frame is spent blitting
a [hz] [ms]  test tone straight to the DAC, machine bypassed
n / b / k / m / w / x    sound, BLE scan, HID dump, SD card, byte swap,
                         panel test patterns
```

It is worth saying why this exists. A machine can be perfectly alive in
its own video memory while nothing reaches the glass, and this project
spent a day proving exactly that. Read the screen *and* look at the panel.

## Speed

Measured on the device with `q` on the serial console, which is worth
reading before optimising anything here, because the answer was not what
anyone expected.

**The emulated Z80 was never the problem.** It runs at **9.1MHz**, which
is 260% of a real Spectrum. What cost half of every frame was reading the
touchscreen: TFT_eSPI's `getTouch()` debounces by re-reading pressure
until it stops rising, with a `delay(1)` each time round, and an untouched
resistive panel gives it noise to chase. Once per frame, that was **12.6ms
of a 23ms frame**. Reading the pressure register directly costs 45µs and
is all a hold gesture needs.

| Spectrum, per frame | before | after |
|---|---|---|
| Z80 | 7.7 ms | 7.7 ms |
| drawing (2.4 ms of it the blit) | 2.7 ms | 2.6 ms |
| touchscreen | **12.6 ms** | **0.045 ms** |
| whole frame | 23.2 ms, 43 fps | 10.2 ms, **98 fps** |

The Spectrum is now paced down to 50fps with the machine half idle.

The MSX gained twice. The touchscreen fix took it from 22.5 to 33 fps at
1:1, and then one missing compiler define took it to **36.6**: `lib/z80`
carries a fast inline opcode fetch for fMSX, guarded by `-D FMSX`, that
this project had never turned on. Without it every instruction byte went
through `RdZ80`, which has to test for the slot register and the floppy
controller on every read - and an opcode fetch can be neither.

**Measure on a fixed workload or not at all.** How long a frame takes
depends on what the emulated program is executing, not only on its
T-states: Nemesis reads anywhere between 30 and 42 fps depending on
whether it is sitting on its title screen or running its attract mode.
The numbers here are MSX-BASIC at its prompt, which does the same thing
every frame.

| MSX-BASIC at 1:1 | before `-D FMSX` | after |
|---|---|---|
| frame rate | 28.4 fps | **36.6 fps** |
| whole frame | 35.2 ms | 27.3 ms |
| the Z80's share | 15.2 ms | 10.9 ms |
| the blit | 13.2 ms | 13.0 ms |

What limits the MSX now is the panel, not the emulator:

| MSX at 1:1, per frame | |
|---|---|
| blit (pushing pixels over SPI) | 13.0 ms |
| drawing the scanlines | 1.4 ms |
| sound | 0.6 ms |
| Z80 and the rest of fMSX | ~15 ms |

13ms is close to what the wire costs: 256x192 pixels at 16 bits is
786kbit and the bus runs at 80MHz, so 9.8ms of it cannot be avoided. At
1.5x the picture is 110,592 pixels, which needs 106Mbit/s, so **60fps at
1.5x is arithmetically impossible on this bus** whatever the emulator
does.

Two things were tried past this point and **both made it slower**, which
is worth knowing before trying them again:

- **DMA per scanline** (two line buffers, `pushPixelsDMA`): the MSX blit
  went from 13.3ms to 20ms. A line is 512 to 768 bytes and TFT_eSPI's
  per-transfer setup costs more than the conversion it saves.
- **The blit on its own task**, overlapping the emulation, with two
  12-line bands costing the same RAM as one 24-line band: 31.7 fps became
  26.8, the blit itself went from 13.2ms to 32.1ms, and free heap fell to
  300 bytes. Two cores running flat out contend for flash and DRAM by
  more than the overlap wins.

What did help was pushing two rows per SPI call instead of one, which
spreads the per-call cost over twice the wire for 960 bytes of buffer.

## Documents worth reading before changing things

- `docs/MEMORY.md` - why the ROMs run from flash, why the video layer
  keeps a 24-line band instead of a frame, why the allocation order in
  `setup()` matters, and why the SD card is not mounted at boot.
- `docs/DISPLAY.md` - the three display faults that all looked identical
  from the outside, and the instruments that tell them apart.
- `docs/KEYBOARD.md` - the Hotbit layout, read out of the BIOS ROM's own
  tables, and the dead keys, identified on the hardware.
- `CLAUDE.md` - why things are built the way they are, and what is open.

## Known limitations

- **Tape loading on the board is slow** when the game brings its own
  loader, because it is reading a tape. Convert it instead; see above.
- **Speed**, above.
- **No speaker on the board**, so sound is untested by ear.
- **The SD card is not mounted at boot.** It costs about 45kB, which with
  a card in the slot left the emulated VRAM twelve bytes short of fitting.
  `m 1` on the console mounts it. Loading ROMs from the card, and anything
  MSX-DOS shaped, has to solve that properly first.
- **Avalon and Thrust do not load** at all, and it is not known why.
- **No joystick, no floppy**, and no Spectrum beeper.

## Licensing (this matters if you do anything beyond personal hobby use)

The `LICENSE` file (MIT) at the repo root covers only the original code
written for this project - it is **not** a blanket license for everything
in the repo. This project vendors and adapts several other people's code.
See `third_party_licenses/` for full texts:

- `lib/z80/` and `lib/fmsx_core/{fMSX,EMULib}`: Marat Fayzullin's
  fMSX/EMULib - **free for personal use, NOT for commercial
  redistribution** (his license predates OSI-style open source licenses;
  see `fmsx_and_emulib.txt`).
- `lib/fmsx_core/video/AVideo.i`: Schuemi's fMSX-go / ESPlay-fMSX video
  glue, MIT licensed.
- C-BIOS binaries (`src/msx/cbios_data.c`): BSD-style license, freely
  redistributable.
- `src/boards/cyd/ble_keyboard.cpp` connect pattern: adapted from esp32beans'
  BLE_HID_Client, MIT licensed.

Given the fMSX/EMULib non-commercial restriction, this project as a whole
is fine for your own personal device but shouldn't be sold or commercially
redistributed without sorting that out with Marat Fayzullin first.

No ROM images are distributed here, and none should be added.
