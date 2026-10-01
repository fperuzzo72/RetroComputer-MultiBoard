#ifndef PC_CORE_H
#define PC_CORE_H

/* The PC, as plain C with no board in it (pc_core.c), so tools/pchost can
 * build the same thing on the development machine. The CPU, BIOS and
 * devices are lib/pc8086 (M5PaperDOS's 8086 core, after 8086tiny). */
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Mounts the images (NULL for none), loads the BIOS, reads the boot
 * sector. C: boots when there is one, else A:. False and a reason in
 * pc_core_error() when nothing can boot. */
bool pc_core_init(const char *c_image, const char *a_image);
const char *pc_core_error(void);

/* Runs the machine for about this many microseconds of real time. The
 * 18.2Hz timer interrupt is due by the clock, not by instructions. */
void pc_core_run(uint32_t us);

/* The screen. Text modes: 80 or 40 columns, 25 rows, two bytes a cell
 * (character, attribute) at the returned pointer. graphics is set for
 * the CGA and VGA modes, which this does not draw yet. */
typedef struct {
    const uint8_t *cells;
    uint8_t mode, cols, rows;
    uint8_t cursor_row, cursor_col;
    bool cursor_visible;
    bool graphics;
} pc_screen;
void pc_core_screen(pc_screen *s);

/* A key, as a PC/XT scancode (set 1, without the break bit) and the ASCII
 * the BIOS should queue with it (0 for none). */
void pc_core_key(uint8_t scancode, uint8_t ascii, bool extended, bool pressed);

/* Instructions a second over the last pc_core_run calls, for `s`. */
uint32_t pc_core_ips(void);
void pc_core_profile(char *out, int n);   /* and resets it */

#ifdef __cplusplus
}
#endif

#endif
