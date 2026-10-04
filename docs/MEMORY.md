# Where the RAM goes

This board is a plain ESP32-WROOM-32E: **no PSRAM**, 4MB flash. `esptool
flash-id` on the unit confirms it (`ESP32-D0WD-V3`, features list has no
embedded PSRAM, 4MB flash). So everything the emulated machine needs has
to come out of the ESP32's own 320kB of DRAM, and that is the constraint
that shaped most of the decisions in this firmware.

## The numbers, measured on the device

Printed by the firmware itself at boot (`msx_heap_report()` in
`src/msx_bridge.c`, and the `h` command in the serial console):

| | bytes |
|---|---|
| static (.data/.bss, includes the 16kB EmptyRAM) | ~99,700 |
| heap free at boot | ~149,000 |
| largest single free block at boot | ~110,600 |
| heap free once the machine is up and BLE is scanning | ~13,400 |

The second number is the one that matters. ESP32 DRAM is not one pool:
it is a handful of fixed regions, and only **one of them is about 110kB**.
Everything large has to come from that one region, in the right order.

What the emulated machine claims:

| | bytes | why |
|---|---|---|
| emulated RAM | 65,536 | 64kB, what a Hotbit HB-8000 has |
| VRAM | 16,384 | 16kB, what a TMS9918 has |
| video band buffer | 6,144 | 24 lines of 256 pixels, 8bpp |
| EmptyRAM | 16,384 | static array, see below |
| BIOS | 0 | read straight out of flash |

## The four things that made it fit

**1. The BIOS is executed from flash, not copied to RAM.** The dumped
32kB BIOS is a `const` array, so it is already mapped and readable. fMSX's
`LoadROM()` hands back a pointer to it instead of allocating and copying.
The one thing that stops this working is fMSX patching the cassette BIOS
entry points (`00E1`..`00F3`) with its own trap opcode - so when the BIOS
is in flash those patches are skipped. There is no cassette on this board,
so nothing is lost. **32kB saved.**

**2. The video layer keeps one band, not a frame.** Upstream rendered a
whole 256x216 8bpp frame (55kB) and handed it to a second task to blit.
This build renders 24 lines at a time and pushes each band to the panel as
soon as it is complete. Blitting moved onto the emulation task, which is
the cost: the measured frame rate is **~42 fps at 1:1**, about 70% of MSX
speed, and ~25-33 fps at the 1.5x scale. (A 59-61 fps figure recorded
earlier was measured while the display's SPI bus was broken and nothing
was being drawn - see docs/DISPLAY.md.) **49kB saved**, and it is the
single change that made 64kB of emulated RAM possible at all.

**3. EmptyRAM is a static array.** It is a fixed 16kB block that never
changes size. As a heap allocation it landed *inside* the 110kB region
before the big blocks did, and 16kB in the middle of that region is the
difference between the 64kB allocation succeeding and failing. As a static
array it is out of the way.

**4. The framebuffer is claimed before anything else.** `PreallocVideo()`
runs from `setup()`, before the emulator starts and before BLE comes up.
Order matters more than totals here: the same set of allocations succeeds
or fails depending on which one gets first pick of the big region.

BLE is also started *after* the emulator has its memory (`main.cpp` waits
for `msx_memory_claimed()`), for the same reason.

## The margin, and what to do if it runs out

**Measured 2026-10-03** (MSX with a 128kB cartridge from flash): 26kB free
before BLE, 10.6kB after `NimBLEDevice::init()` (NimBLE takes 15kB), 8.3kB
with a keyboard connected and paired by passkey. A 4kB stack for a BLE
service task no longer fits there: it was never created, and a keyboard
that was found was never connected. The CYD now services the keyboard from
its console task (`BLE_KEYBOARD_NO_TASK`). The scan also keeps no results
(`setMaxResults(0)`): NimBLE's default keeps every device it hears, and a
busy room filled the heap and aborted the BLE task.

About **13kB of heap is left** once the machine is running and NimBLE is
scanning. Connecting a keyboard costs a few kB more on top of that
(the client object plus the discovered GATT attributes).

If a keyboard fails to connect and the serial console's `h` command shows
the heap near zero, the one-line fallback is in `src/msx_bridge.c`:

```c
RAMPages = 2;   /* 32kB instead of 64kB */
```

That gives 32kB back. It costs nothing for MSX-BASIC, which reports the
same `Mem. livre 28815` either way - BASIC only ever sees the RAM in pages
2 and 3. What it costs is machine-code software and games that use the
whole 64kB, which is why 64kB is the default.
