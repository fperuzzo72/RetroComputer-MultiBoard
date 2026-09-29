#ifndef MAC_H
#define MAC_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

/* The Macintosh, on Matt Evans' umac (lib/umac): a Mac Plus ROM driving
 * whatever resolution and memory size the build asks for. Here that is
 * the Paper Mono's own 800x480 and 4MB, which boots System 6 and 7.
 *
 * mac_machine.c is the Machine table. The two functions below are what it
 * runs, split out so tools/machost can run the very same code on the
 * development machine, with no board at all. */

/* Bring the machine up from a ROM and a disc image. The ROM is copied and
 * patched (resolution, disc driver); the disc is copied so the Mac can
 * write to it for as long as it runs. Returns 0 on success. */
int  mac_start(const uint8_t *rom, size_t rom_len,
               const uint8_t *disc, size_t disc_len);

/* The same, with the disc read and written through callbacks instead of
 * copied into memory: a disc on the card, of any size, whose writes stay.
 * The callbacks return 0 on success; a NULL write makes it read-only. */
typedef int (*mac_disc_read)(void *ctx, uint8_t *data, unsigned offset, unsigned len);
typedef int (*mac_disc_write)(void *ctx, uint8_t *data, unsigned offset, unsigned len);
int  mac_start_ops(const uint8_t *rom, size_t rom_len, void *ctx,
                   mac_disc_read read, mac_disc_write write, size_t disc_len);

/* One slice of emulation: 5ms of emulated time, then the timers, the
 * pointer and the keyboard. `now_us` is wall-clock time, which is what
 * the 60Hz and 1Hz interrupts follow. */
void mac_step(uint64_t now_us);

/* The emulated RAM, for the host tool and the serial console. */
uint8_t *mac_ram(void);
unsigned mac_ram_size(void);

/* The picture: 1-bit, 1 = black, DISP_WIDTH x DISP_HEIGHT. */
const uint8_t *mac_framebuffer(void);

/* Absolute pointer, in screen pixels. Safe to call from another task. */
void mac_pointer(int x, int y, int button);

/* A HID boot keyboard report: byte 0 modifiers, bytes 2..7 usages. */
void mac_hid_report(const uint8_t report[8]);

/* Where the cursor actually is, read back from the Mac's own globals. */
void mac_cursor(int *x, int *y);

#ifdef __cplusplus
}
#endif
#endif
