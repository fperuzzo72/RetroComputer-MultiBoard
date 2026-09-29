# lib/umac - the Macintosh core, vendored

Matt Evans' [umac](https://github.com/evansm7/umac), a Mac 128K/512K/Plus
emulator, at `b62d3c6`, with Karl Stenerud's Musashi 68000 core at the
commit umac pins (`b3144f1`). Licences: `third_party_licenses/umac_and_musashi.txt`.
Most of it is MIT; the disc driver (`disc.c`, `b2_macos_util.h`, the
`sonydrv.h` blob) comes from Basilisk II and `keymap.h` from Mini vMac,
and those are GPLv2.

```
include/   umac's headers
src/       umac: the machine (main.c), ROM patcher, disc, VIA, SCC
musashi/   Musashi, with umac's m68kconf.h in place of Musashi's own
```

Kept as close to upstream as possible. What is different, all of it
marked in the source:

- **`musashi/m68kops.c` and `m68kops.h` are generated, not upstream
  files.** umac's build runs Musashi's `m68kmake` over `m68k_in.c` and then
  `tools/decorate_ops.py` over the result, which marks the 200 hottest
  opcode handlers (`tools/fn_hot200.txt`, profiled by Matt Evans on a
  System 3.2 boot, MacWrite and Missile Command) with `M68K_FAST_FUNC`. The
  output is committed here so the firmware build needs neither step. To
  regenerate: `make prepare` in a umac checkout, and copy the two files.
- **`M68K_FAST_FUNC` and umac's `FAST_FUNC` mean `IRAM_ATTR` on an
  ESP32** (`musashi/m68kconf.h`, `src/main.c`), as they mean
  `__not_in_flash_func` on the Pico: those handlers and the bus functions
  run from internal RAM, not through the flash cache. The firmware's link
  map shows 200 `m68k_op_*` at 0x403..., the other 1,799 in flash.
- **`umac_kbd_pending()`** (`src/main.c`, `include/umac.h`): the emulated
  keyboard holds one event, and a second arriving before the Mac has read
  the first overwrites it. `src/mac/mac_core.c` queues keys and asks this
  before handing over the next.
- **`include/scc.h` is `include/umac_scc.h`** here, and `main.c` and
  `scc.c` include it by that name. fMSX has an `SCC.h` (Konami's sound
  chip), and on a case-insensitive filesystem a build carrying both
  machines found fMSX's when umac asked for its own.
- **The global `overlay` is `umac_overlay`** (`src/main.c`,
  `include/machw.h`): fMSX's video layer defines a global of the same
  name, and the two machines would not link into one firmware.
- `m68kconf.h` lives in `musashi/` so that Musashi's `#include "m68kconf.h"`
  finds umac's without the `MUSASHI_CNF` define umac's Makefile passes.

Not taken: `unix_main.c` and `keymap_sdl.h` (the SDL frontend), and
Musashi's own `m68kconf.h`, `m68kmake.c`, `m68k_in.c` and examples.

The resolution and memory size are compile-time: `DISP_WIDTH`,
`DISP_HEIGHT` and `UMAC_MEMSIZE` (kB), set in `platformio.ini` and in
`tools/machost/Makefile`, which must agree. The X resolution has to be a
multiple of 32 and the framebuffer under 64kB.
