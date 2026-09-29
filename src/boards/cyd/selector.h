#ifndef SELECTOR_H
#define SELECTOR_H
#ifdef __cplusplus
extern "C" {
#endif

/* Change what the machine is running, without rebooting the board.
 *
 * The boot menu decides what starts; this decides what to swap to while
 * it is running - another cartridge, another tape. Drawn over the picture
 * and driven by the touchscreen, the same as the boot menu, because that
 * is the input this board has that needs nothing paired.
 *
 * Opened by holding a finger on the screen for a second, by F12 on either
 * machine, or by the `o` command on the serial console. The touch is what
 * matters: plenty of keyboards have no function keys, and this board's
 * own screen is the one input that is always there. The machine calls selector_frame() once a frame while it is
 * open, and stops emulating until it closes: nobody expects a game to
 * keep running underneath a menu. */

void selector_open(void);

/* Watch for the press-and-hold that opens it. Called once a frame by the
 * machine, from the machine's own task - the panel and the touch share an
 * SPI bus and only one task may drive it. */
void selector_poll_open(void);
int  selector_active(void);

/* Draw and handle touch for one frame. Returns the entry that was chosen,
 * or -1 while it is still open or if it was dismissed. */
int  selector_frame(void);

#ifdef __cplusplus
}
#endif
#endif
