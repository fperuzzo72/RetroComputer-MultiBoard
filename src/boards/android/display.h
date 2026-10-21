#ifndef DISPLAY_H
#define DISPLAY_H
/* The Android board's display.h: the same calls the 8-bit machines make on
 * the e-ink boards (src/boards/eink/display.h), kept in colour. The picture
 * is held as RGB565 at the machine's own size (up to 320x216) with its
 * border colour, and presented by the platform side, scaled to the panel
 * (retro_board.c, android_jni.cpp). */
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>

#define DISPLAY_PICTURE_W 256
#define DISPLAY_PICTURE_W_MAX 320
#define DISPLAY_PICTURE_H 216
/* The machines draw every frame here: the CPU has the time. */
#define DISPLAY_FRAME_PERCENT 100
/* But the Spectrum's FLASH attribute stays still, as on the e-ink boards:
 * on an e-ink panel every blink would be a refresh, three a second. */
#define DISPLAY_STEADY 1

void display_bridge_init(void);
void display_write_picture(short srcX, short srcY, short width, short height,
                           const uint8_t *buffer, uint16_t bgColor,
                           const uint16_t *palette);
void display_write_frame_msx(short left, short top, short width, short height,
                             const uint8_t *buffer, uint16_t bgColor,
                             const uint16_t *palette);
void display_fill_panel(uint16_t color);
/* Android board only: a whole picture already in RGB565, w*h, row-major,
 * up to 1024x768 (the PC 386). */
void display_write_rgb565(const uint16_t *px, int w, int h);
/* Android board only: show the picture at 4:3 whatever its size, as a PC
 * monitor did, instead of with square pixels. */
void display_set_four3(int on);
unsigned long display_full_repaints(void);
void display_service(void);
int  display_take_repaint(void);
void display_set_scale(int scale);
int  display_get_scale(void);
unsigned long display_blit_us(void);
void display_blit_us_reset(void);
void display_set_swap_bytes(int on);
int  display_get_swap_bytes(void);
void display_request_test_pattern(int which);
void display8_attach(uint8_t *canvas);
void display8_request_repaint(void);

#ifdef __cplusplus
}
#endif
#endif
