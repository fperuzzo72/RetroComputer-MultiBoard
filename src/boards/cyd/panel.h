#ifndef PANEL_H
#define PANEL_H

/* The one TFT_eSPI instance. C++ only, and only for code that has to draw
 * on the panel directly rather than through display.h - which today means
 * the boot menu. Two instances would fight over one SPI bus. */
#include <TFT_eSPI.h>
TFT_eSPI &panel_tft();

#endif
