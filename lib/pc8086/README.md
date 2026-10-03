# lib/pc8086: the PC's 8086 core

Vendored from **M5PaperDOS** (omeriko9), through the owner's fork
`fperuzzo72/M5Paper_8086` at `db5b4fd`, including two fixes that were
uncommitted in that checkout (the keyboard controller's A20 gate, command
D1h, and the hard disk geometry flag read without the settings store). Its
CPU and BIOS follow Adrian Cable's **8086tiny**, and the BIOS image
(`embedded_8086tiny_bios.c`) is 8086tiny's. Both are MIT; see
`third_party_licenses/m5paperdos_8086tiny.txt`.

Taken: `main/dos/{cpu8086,memory,disk,bios,video,ports,interrupts,xms}`,
`speaker.h`, `main/emulator/embedded_8086tiny_bios`,
`main/display/font8x16`. Left out: the e-paper driver, Wi-Fi and the web
UI, the Bluetooth keyboard, the settings store, `hdd_image.c` and
`fat16_image.c` (making disks from a folder), `speaker.cpp`.

What this project supplies instead lives in `src/pc/`: the start-up and
run loop (`pc_core.c`), the screen (`pc_text.c`), the keyboard
(`pc_keys.c`), the card (`pc_files.cpp`), a silent speaker
(`pc_speaker.c`). The headers here are kept off the global include path
(see `include/README`).

## Local changes, each marked `NOT UPSTREAM`

- `settings.h`, `config_defs.h`: replaced by fixed defaults.
- `pc_file.h` (new), included by `disk.h`: the disk images' stdio calls go
  to the card through `src/pc/pc_files.cpp`.
- `video.c`: M5PaperDOS's e-paper renderer is `#if 0`; the board draws
  the text page itself.
- `bios.c`: INT 16h function 00h **waits for a key**, by running the INT
  again, as a real BIOS does. Upstream returns AX=0 at once; WordStar 3
  reads a key without asking first, took that 0 for a keystroke and
  filled its prompts with garbage.
- `bios.c`: the tick count starts at the time of day (localtime), as an
  AT's POST does from its clock; upstream started it at 0, and DOS, which
  takes its time from it, booted at midnight every time.
- `cpu8086.c`: **the auxiliary carry (AF)** is set by ADD, ADC, SUB, SBB,
  CMP, CMPS, SCAS, INC, DEC and NEG, 47 places, each marked. Upstream only
  DAA, DAS, AAA, AAS and SAHF touched it, so DAA worked on whatever AF
  was left over: DEBUG's hex dumps came out as "H<<=" for "B001" (its
  nibble-to-ASCII is ADD 90h, DAA, ADC 40h, DAA).
- `cpu8086.c`, `memory.c`: under `PC8086_LEAN` (set in `library.json`),
  the per-instruction trace ring, CPU context and debug checks, and the
  interrupt-table write watch are left out. On the PaperS3 at the DOS
  prompt: 352 thousand instructions a second with them, 562 thousand
  without, 588 thousand with `-O2` on top (also in `library.json`).
  `tools/pchost` builds without the flag when the tracing is wanted.
