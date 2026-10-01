#ifndef EINK_BOARD_H
#define EINK_BOARD_H

/* Which e-ink board this is, and the few things that differ between them.
 *
 * src/boards/eink/ drives two M5Stack devices from one tree; platformio.ini
 * picks one with EINK_BOARD_PAPERMONO or EINK_BOARD_PAPERS3, and freeink-sdk
 * does the hardware for both. Everything here is a fact about the device,
 * decided once, and read by everything that needs it: never test the board
 * anywhere else (the CYD port's lesson - conditions written in two places
 * drift apart).
 *
 *   EINK_PANEL_W/H     the panel, in the frame the picture is drawn in
 *   EINK_UPSIDE_DOWN   held turned round from freeink-sdk's frame
 *   EINK_HAS_BUTTONS   two buttons the firmware can read (GPIO2, GPIO3)
 *   EINK_OTHER_APP     what the boot menu's last choice goes back to
 *   EINK_BEEPER_PIN    the passive buzzer's gate (freeink-sdk's profile)
 *   EINK_MSX_HZ        the MSX's frame rate unless `hz` says otherwise
 *   EINK_MSX_SPEED     its pace in percent of real time unless `vel` does
 */

#if defined(EINK_BOARD_PAPERS3)

/* M5Stack PaperS3: 4.7" ED047TC1, 960x540, driven over the S3's parallel
 * bus through LovyanGFX; GT911 touch. No button the firmware can read: the
 * side button only switches it on and, held, off. */
#define EINK_BOARD_NAME   "PaperS3"
#define EINK_PANEL_W      960
#define EINK_PANEL_H      540
#define EINK_UPSIDE_DOWN  0
#define EINK_HAS_BUTTONS  0
/* What lives in the other app slot on the owner's device (the shared
 * table: CrossPoint in app0, this in app1). */
#define EINK_OTHER_APP    "CrossPoint"
#define EINK_BEEPER_PIN   21
/* The panel driven directly keeps up with a game at full speed; 50Hz is
 * how the owner remembers his Hotbit playing them. */
#define EINK_MSX_HZ       50
#define EINK_MSX_SPEED    100

#else

/* M5Stack Paper Mono: 800x480 SSD1677, FT6336 touch, two buttons. Held with
 * the buttons along the top: along the bottom, the hand kept pressing them
 * (the owner's request, 2026-09-28). */
#ifndef EINK_BOARD_PAPERMONO
#define EINK_BOARD_PAPERMONO 1
#endif
#define EINK_BOARD_NAME   "Paper Mono"
#define EINK_PANEL_W      800
#define EINK_PANEL_H      480
#define EINK_UPSIDE_DOWN  1
#define EINK_HAS_BUTTONS  1
#define EINK_OTHER_APP    "CrossPlay"
#define EINK_BEEPER_PIN   42
/* The SSD1677's ~400ms waveform cannot follow a game at full speed: at
 * half speed the owner found it "bem jogável, apesar da lentidão"
 * (2026-10-01), and at 60Hz, with no hiss to avoid on this panel. */
#define EINK_MSX_HZ       60
#define EINK_MSX_SPEED    50

#endif

#endif
