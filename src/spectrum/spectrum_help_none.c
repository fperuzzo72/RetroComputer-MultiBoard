/* spectrum_help_none.c - no keyword crib, for a board that cannot draw one.
 *
 * spectrum_help.cpp draws the crib in the CYD panel's margins with TFT_eSPI
 * directly. A board without that (the Paper Mono) builds this instead, so
 * the machine's calls to it do nothing. */
#include "spectrum.h"

void spectrum_help_toggle(void) {}
int  spectrum_help_active(void) { return 0; }
void spectrum_help_draw(void) {}
void spectrum_help_invalidate(void) {}
