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

Two more boards are M5Stack's e-ink devices, the **Paper Mono** (800x480)
and the **PaperS3** (960x540), each carrying the MSX, the Spectrum, a
**Macintosh Plus**, a **Commodore 64** and a **PC with MS-DOS**. See "The
e-ink boards", "The Commodore 64" and "The PC" below.

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

## The e-ink boards: Paper Mono and PaperS3

`pio run -e papermono` and `pio run -e papers3` build one firmware each
with the MSX, the Spectrum and the Macintosh, from one board layer
(`src/boards/eink/`); what differs between the two devices is
`eink_board.h` and nothing else. At power-on a touch menu asks which computer, then what it
starts with; left untouched for five seconds, the outlined one boots with
what it had last time.

The Paper Mono is held with its **buttons along the top**; the picture and
touch are turned to match. The PaperS3 has no button the firmware can
read (its side button only switches it on and off), so the buttons' jobs
are also gestures, on both:

- a finger held still for 1.5 seconds: a full refresh
- held for 5 seconds: restart into the boot menu

| | MSX, Spectrum | Macintosh |
|---|---|---|
| touch | a tap opens the list of cartridges and snapshots | a trackpad (below) |
| button, top right (GPIO2) | opens the same list | the mouse button |
| other button (GPIO3) | full refresh, to clear ghosting | the same |
| other button held 2 s | restart into the boot menu | the same |

(The buttons are the Paper Mono's. On the PaperS3 the Mac needs none: the
trackpad clicks with a tap and holds the button with tap-then-drag.)

**The keyboard** is the same BLE transport as the CYD's, now shared
(`src/boards/common/`), paired the way the PaperS3 MicroBASIC pairs: a
keyboard that asks for a code gets one on the panel, **123456**, to type
on the keyboard itself followed by Enter. The MSX has its US-International
dead keys, and so does the Mac (below).

**The 8-bit pictures** are 256x216 at exactly 2x, 512x432 in the middle
of the panel. **The Mac** is scaled to the panel's height: 1.40x on the
Paper Mono, 1.58x on the PaperS3. The panel has two colours; each machine pixel is four panel
pixels, so five tones, and the tone is how far a colour stands from the
border colour. MSX-BASIC's white on blue comes out black on white, a
Spectrum's ink on paper as it is. The Spectrum's FLASH attribute is held
steady: blinking it would refresh the whole e-ink panel twice a second.
The MSX runs at 50 frames a second by default (see below).

Status: **running on both devices** (Paper Mono since 2026-09-28, PaperS3
since 2026-09-30), the three machines with sound. `make -C tools/eink_test EINK=PAPERS3 -B` builds the
host tools for the PaperS3's layout. `tools/eink_test` has `zx` and `msx`, which run
the real machine code through the real picture path and write what the
panel would show:

```bash
make -C tools/eink_test
./tools/eink_test/msx 0 /tmp/msx 6          # MSX-BASIC at six seconds
./tools/eink_test/zx 8 /tmp/zx 4            # the eighth Spectrum snapshot
./tools/eink_test/screens /tmp/menus        # the menus
```

## e-ink: variants and options

Several choices here were made by measuring and listening on the device,
and the alternatives were kept: some suit one taste or one board better.

**Builds**

| env | board | panel | slot |
|---|---|---|---|
| `papermono` | Paper Mono | the SSD1677's own waveform, ~320-400ms a picture | app1, 0x800000 |
| `papers3` | PaperS3 | **driven directly** (`fastepd.c`, after PaperBoy's Modos Smooth Graphics): a changed picture on the glass in tens of ms, ~50 scans a second | app2, 0x8A0000 |
| `papers3-waveform` | PaperS3 | M5GFX's waveform, ~400ms a picture, the way it was until 2026-10-01 | app2, 0x8A0000 |

Flash only the app, into its own slot: `python3 -m esptool --chip esp32s3
--port /dev/cu.usbmodem101 --baud 921600 write_flash <slot> .pio/build/<env>/firmware.bin`.
Never `pio run -t upload`, which writes over the other firmwares' otadata.

**Options on the serial console** (115200), no reflash needed:

| command | what | default |
|---|---|---|
| `snd pcm` | MSX sound: fMSX's whole mix (PSG, SCC, drums) as PWM on the buzzer | yes |
| `snd voz` | MSX sound: only the loudest PSG voice, as a square wave | |
| `snd off` | MSX silent | |
| `hz 50` / `hz 60` | MSX frame rate: 50 (a European MSX, and how the owner's Hotbit played) or 60 (the HB-8000's VDP per msx.org) | 50 PaperS3, 60 Paper Mono |
| `u <pct>` | MSX frames drawn, in percent | 60 on the direct panel, 20 otherwise |
| `v` / `q` | MSX frames in each of the last 60 s / where a frame's time goes | |
| `vel <20-100>` | MSX speed in percent of real time (restarts) | 100 |
| `c <after> <ms> [cap]` | waveform panels: clean after `after` fast refreshes and `ms` still, and every `cap` regardless | 5, 3000, 200 |
| `fe` | direct panel: scans, rows, timing; `fe c` clean, `fe x`/`fe y` flip, `fe s` pause, `fe n` scan zeros only, `fe z <us>` row time, `fe P <n>` task priority | |
| `snd mudo` / `snd som` / `snd dc <pct>` | sound tests: buzzer still, back, held at a fixed duty | |
| `o`, `t x y`, `d` | open the selector, tap a menu at x,y, dump the panel as text (`tools/fbdump.py`) | |
| `off` | PaperS3: switch off (works with USB plugged in) | |

`snd` and `hz` are remembered and restart the board; the rest act at once.
The Spectrum's sound is its own beeper, always: the speaker bit's flips,
to the T-state, through the RMT peripheral.

**What was measured, and why the defaults are what they are**

- *Sound.* The first MSX sound was the loudest voice (`snd voz`): tunes
  recognisable, "meio ruim". The PCM mix is PaperBoy's technique and the
  owner found it far better. It is rendered by its own task at the
  buzzer's rate, so a late frame does not make it stutter.
- *The hiss.* With the PaperS3 panel driven directly the PCM hissed; with
  the waveform panel it never did. Found by ear, one test at a time: the
  ring never ran dry; muted, silence; panel paused, gone; buzzer held at
  a fixed duty with no sound, hiss at 50% and hardly any at 10%. The
  panel's current ripples the supply and the buzzer passes it on in
  proportion to its own mean current. The PWM's centre now follows the
  sound's level (silence draws nothing): "o chiado praticamente sumiu".
  It is not the 50Hz: the hiss stopped with the panel paused at 50Hz.
- *Speed.* Drawing every MSX frame cost more than a frame (19ms of 16.7)
  and held the machine at 32fps. Drawing only rows that changed, and 60%
  of frames on the direct panel, gives a steady 50 (or 60) with ~36
  pictures a second.
- *The Z80.* The MSX's Z80 waits one clock on every opcode fetch; fMSX
  did not count it, so the machine did ~15% too much work a frame and
  Nemesis left its own slowdowns at the wrong moments. Counted now.

- *The Paper Mono and games.* Its SSD1677 controller only refreshes
  through its own waveform, ~320-400ms a picture, and PaperBoy's direct
  drive cannot be done there. An action game is not playable on it: the
  owner tried The Goonies at full speed, at 50% (`vel 50`), with cleans
  every 30 refreshes, and on the build from before any of the sound and
  speed work (`53a5de8`); all "bem ruim". For a moment at half speed it
  seemed "bem jogável", then not: most likely each of those moments came
  just after a restart, whose full refresh leaves the panel clean until
  the fast refreshes darken it again. The Paper Mono's MSX runs at 60Hz
  (no hiss to avoid on this panel) and full speed. It is kept current, for
  MSX-BASIC, slow or strategy games, the music, and the Mac, whose mostly
  still desktop suits a slow panel far better than a game's constant
  motion, however much more the Mac asks of the processor. The PaperS3
  is the one to show.

**Going back.** Each step is one commit on `multi-board`; build any of
them with `git checkout <hash>` and the env above.

| commit | what it brought |
|---|---|
| `974a74e` | MSX sound as the loudest PSG voice |
| `2891316` | MSX sound as PCM; one MSX frame in five drawn |
| `24cfe62` | the PaperS3 panel driven directly (first version) |
| `3c63415` | faster scans; only changed rows redrawn |
| `63e6672` | MSX at 50Hz by default |
| `b64fba4` | the PCM centre follows the sound (the hiss) |
| `6c17bdc` | the MSX's M1 wait state |

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
reads better than anything averaged. `src/boards/eink/picture.h`.

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

`src/boards/eink/trackpad.c` is plain C, and `tools/eink_test`
runs it, with `picture.h`, in front of the real Mac on the development
machine: a simulated finger aims the pointer, double-taps the disc open and
holds the Apple menu down, and every pixel of the scaled, turned panel is
checked against the Mac pixel it should show.

```bash
make -C tools/eink_test
./tools/eink_test/papermono_test roms/mac/macplus.rom roms/mac/boot.img /tmp/pm
```

Under all that, the Mac only ever had a relative mouse, so the position is
written straight into the ROM's cursor globals, the way Mini vMac does it.
Two things about that cost time and are worth knowing: nothing may be
written there until the system is keeping those globals, or the ROM's RAM
test reads it back and stops with a sad Mac (03FFFF); and a button change
waits until the Mac's own `Mouse` global says the cursor has arrived, or a
tap clicks wherever the cursor was before.

**The keyboard types US-International** on the Mac too: ' ` ^ ~ " are
dead keys, and a composed letter goes over as the Mac's own Option
sequence (Option-e then e), so the Mac draws the accent in its own font.
`text Ol'a, voc^e` in machost comes out "Olá, você" in TeachText.

**The discs.** The disc built into the firmware is **Paper Mac**, a 1.44MB
volume made from pico-mac's 32MB PicoMicroMac image: System 3.2 with Finder
5.3, MacWrite 1.6 and MacPaint 1.3, nineteen small games (Lode Runner,
Missile Command, Crystal Raider, Asteroids, Frogger...) and about 400kB
free. `tools/make_mac_disc.py` builds it, or any other selection, with
hfsutils (`brew install hfsutils`):

```bash
python3 tools/make_mac_disc.py ~/Downloads/umac0.img roms/mac/boot.img 1440 "Paper Mac" \
    ":System Folder" ":Programs" ":Files" ":Games:Lode Runner" ...
```

The card holds more: every `.img`, `.dsk` or `.hfv` in a `mac` folder at
its root is a choice in the Mac's menu, read and written in place, so what
is saved stays after a power cycle (confirmed on the Paper Mono,
2026-10-01: a MacWrite document saved, the board switched off and on, and
the document was there). **A card with no disc on it is given a
copy of the built-in one** the first time the Mac starts, and the Mac boots
from that copy: nobody has to take the card out to begin. The choice of
disc is remembered.

**Back to the reader.** The first menu's last choice, when the other app
slot holds a firmware, is "Voltar ao CrossPlay" (CrossPoint on the
PaperS3): it makes that slot the one that boots and restarts.

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

## The Commodore 64, on the e-ink boards

retroelec's **T-HMI-C64** core (`lib/c64/`, GPLv3, vendored at dea1a82; every
local change is marked `NOT UPSTREAM` and listed in `lib/c64/README.md`),
with this project's drivers in `src/c64/`: the VIC's picture through
display8, a BLE keyboard as the whole key matrix, the SID as PCM on the
buzzer, files on the card under `/c64/`. Verified on the PaperS3: BASIC
with sound and keyboard (ESC is RUN/STOP), cartridges, SAVE and LOAD.

The menu lists BASIC plus every `/c64/*.prg`, `*.d64` and `*.crt` on the
card, names cleaned of " (USA, Europe)" and the like. A `.prg` is loaded
and run by typing `LOAD"name",8,1` and `RUN`; a `.d64` is attached and
its first program run the same way. Programs with their own fast loader
(Maniac Mansion) do not run: the 1541 is emulated at the KERNAL's level,
not as a drive with its own 6502.

**Cartridges** (`src/c64/c64_cart.cpp`) are read into PSRAM: plain 8K and
16K (type 0), Ocean (5) and Magic Desk (19). Magic Desk is what
**OneLoad64** uses for its single-file conversions of disk and tape games,
so Boulder Dash, Bubble Bobble, Commando, Ghosts'n Goblins, Giana Sisters,
IK+, Impossible Mission, Paradroid, Uridium and Wizball all boot on the
host. On the card since 2026-10-01: those ten, plus Pac-Man, Donkey Kong,
Pitfall II, H.E.R.O., River Raid, Lode Runner, Jumpman Junior, Choplifter,
Ghostbusters and Gridrunner II.

Keys: F10 turns the cursor keys and right Ctrl into a joystick, F9 picks
its port, PgUp is RESTORE, F12 the selector.

The ROMs (BASIC, KERNAL, character set, 1541) come from `roms/c64/` and are
built in by `tools/make_c64_roms.py`, which checks their SHA-1 and patches
the three KERNAL bytes the 1541 hooks need. Gitignored, like every ROM.

**Putting files on the card over the cable**, without taking it out:

```bash
python3 tools/sd_put.py --dest /c64/ *.crt
```

2kB blocks, each acknowledged, and a CRC at the end (about 9kB/s). The
board writes `name.part` and renames it only when all of it arrived, so a
failed transfer leaves the old file alone; still, do not replace the disk
image of the machine that is running (it restarted the Paper Mono once). `ls
/c64` and `rm /c64/name.crt` on the serial console look and tidy up.

`tools/c64host` runs the same core on the Mac, with PNG screenshots:
`C64_ROOT=dir CRT=game.crt ./tools/c64host/c64host out 15` (or `D64=`).

## The PC (MS-DOS), on the e-ink boards

An IBM PC-compatible with an 8086, **text mode only so far**: M5PaperDOS's
8086 core (`lib/pc8086/`, MIT, after 8086tiny; local changes in its
README) with this project's glue in `src/pc/`. Verified on the PaperS3
on 2026-10-01: MS-DOS 6.22 boots from the card, `dir` and `ver` answer,
WordStar 3 opens, and on the host it saves a document.

It boots a **disk image from the card**: every `/pc/*.img` and the
`/msdos.img` M5PaperDOS kept at the card's root (left where it is, so
that firmware still finds it). A hard disk image boots as C:; one of
floppy size (2.88MB or less) as A:, with the first hard disk beside it as
C:. Writes go into the image, so what is saved stays. With several, the
selector lists them.

The screen is the VGA's own text, 80x25 cells of 8x16 (640x400), handed
to the board as a 1-bit picture and scaled to the panel like the Mac's.
Colour becomes black and white by contrast: the brighter of a cell's two
colours is paper, so DOS's grey on black and EDIT's white on blue both
come out black on white, and a highlighted menu bar white on black. The
cursor is a steady underline. CGA and VGA graphics are not drawn yet.

Speed: **about 590 thousand instructions a second** at the DOS prompt and
750 thousand in a tight loop, about twice an IBM PC XT (4.77MHz, roughly
0.33 MIPS). The core is a C interpreter, measured to spend all its time
in the CPU; see `lib/pc8086/README.md` for what was taken out to get here.

**The font** is the VGA's code page 437, taken at build time from the
EGA.CPI of your own MS-DOS (in C:\DOS), like the ROMs: never in the
repository. Without it the PC draws with lib/pc8086's font, which has no
accented letters and no double frames:

```bash
mkdir -p roms/pc && mcopy -i "msdos.img@@32256" ::DOS/EGA.CPI roms/pc/
python3 tools/make_pc_font.py
```

**A floppy to start with.** A 31MB hard disk image takes an hour over the
cable; `tools/make_pc_floppy.py HD.img OUT.img` makes a bootable 1.44MB
one from it (MS-DOS 6.22, EDIT and QBASIC, DEBUG and the usual tools,
WordStar, Volkov Commander) that goes over in two and a half minutes. On
the Paper Mono since 2026-10-02 as `/pc/MS-DOS 6.22.img`: it boots as A:,
measured there at 455 thousand instructions a second.

**Keys are US-International** (`src/pc/pc_keys.c`), as on the other
machines: ' ` ^ ~ " are dead keys, the letter after one gets the accent, a
space after one is the accent itself, ' then c is ç. The accented letter
is a byte of **code page 860, Portuguese**, which has every letter
Portuguese needs (ã õ Á Ê Ç ...) and the same frames as 437; `cp 437` on
the console switches screen and keyboard to the US code page, which has no
ã or õ. The floppy's CONFIG.SYS says `COUNTRY=055`: Brazilian dates
(dd/mm/yy) and decimal comma. Messages stay in English: the owner's MS-DOS
6.22 is the US one. F12 or a tap opens the selector. Console: `w dir` types `dir` and Enter (`|` for a space, `wn`
without the Enter), `s` gives the speed. `tools/pchost` runs the same PC on
the Mac and prints the screen:

```bash
make -C tools/pchost PCFLAGS=-DPC8086_LEAN
PC_ROOT=dir ./tools/pchost/pchost /pc/c.img 10 $'ver\r'
```

(`PBM=file.pbm` also writes the picture the board would get; `HID="..."`
types as the BLE keyboard does, dead keys and all, with a key table of its
own, which is how a lost character in pc_keys.c's table was caught: every
key after the backslash typed its neighbour on the device, and typing
through the same table both ways had hidden it.)

**WordStar on the owner's image** was installed to open documents on A:
(the routine at WS.COM offset 1E1Dh returned drive 1). On 2026-10-01,
with the owner's go-ahead, that byte was changed to 0, the current drive,
on the card itself, with DEBUG inside the emulated PC (`e 1f1e 0`, `w`).
WordStar now opens documents on C:; on the host it also saves them.

The PC has no clock of its own on the board yet: files saved there are
dated 1980.

## The layout of this repo

```
src/boards/cyd/        the CYD: panel, amplifier, BLE keyboard host, card,
                       serial console. Knows nothing about what is emulated.
src/boards/eink/  the Paper Mono: e-ink panel, touch, serial console
src/machine.h          the only thing that crosses between boards and machines
src/display_mono.h     how a 1-bit machine hands its framebuffer to a board
src/msx/               the MSX1, on the vendored fMSX core
src/spectrum/          the ZX Spectrum 48K
src/mac/               the Macintosh Plus
src/c64/               the Commodore 64's drivers, cartridges, keys
src/pc/                the PC: start-up, text screen, keys, card files
lib/z80/               Marat Fayzullin's Z80, shared by MSX and Spectrum
lib/fmsx_core/         the rest of fMSX: VDP, PSG, mappers. MSX only.
lib/umac/              umac and Musashi, the Macintosh core
lib/c64/               T-HMI-C64, the Commodore 64 core (GPLv3)
lib/pc8086/            M5PaperDOS's 8086 core, after 8086tiny (MIT)
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
- `lib/pc8086/`: M5PaperDOS's 8086 core and 8086tiny's BIOS, MIT
  (`m5paperdos_8086tiny.txt`).
- `lib/c64/`: retroelec's T-HMI-C64, **GPLv3**
  (`t-hmi-c64_gpl3.txt`). See the note in `LICENSE` about what that means
  next to fMSX in one firmware.
- C-BIOS binaries (`src/msx/cbios_data.c`): BSD-style license, freely
  redistributable.
- `src/boards/cyd/ble_keyboard.cpp` connect pattern: adapted from esp32beans'
  BLE_HID_Client, MIT licensed.

Given the fMSX/EMULib non-commercial restriction, this project as a whole
is fine for your own personal device but shouldn't be sold or commercially
redistributed without sorting that out with Marat Fayzullin first.

No ROM images are distributed here, and none should be added.
