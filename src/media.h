#ifndef MEDIA_H
#define MEDIA_H
#ifdef __cplusplus
extern "C" {
#endif

/* Disc images on the board's card, for a machine that boots from one.
 *
 * The machine (src/mac/) asks what is there and reads and writes it; the
 * board (src/boards/eink/media_sd.cpp) knows where "there" is and how to
 * get at it. A board without a card, or a card without images, answers 0
 * and the machine carries on with what is built into the firmware.
 *
 * Images live in /mac on the card: raw 400k/800k floppy images or HFS
 * hard-disc images of any size (pico-mac's umac0.img is one), named
 * .img, .dsk or .hfv. What the machine writes goes to the file, so it is
 * there after a power cycle. */

#include <stdint.h>

int         media_count(void);
const char *media_name(int i);

/* An image opened for reading and writing, or NULL. */
void *media_open(int i, uint32_t *size);

/* 0 on success, as umac's disc callbacks want. */
int media_read(void *h, uint8_t *data, uint32_t offset, uint32_t len);
int media_write(void *h, const uint8_t *data, uint32_t offset, uint32_t len);

#ifdef __cplusplus
}
#endif
#endif
