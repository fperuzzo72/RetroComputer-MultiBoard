/* pc_speaker.c - lib/pc8086's speaker.h. Silent for now: the PC speaker
 * is PIT channel 2 gated by port 0x61, and will go to the board's buzzer
 * the way the Spectrum's beeper does. */
#include <stdbool.h>
#include <stdint.h>
#include "pc8086.h"

void speaker_init(void) {}
void speaker_set_pit_control(uint8_t mode) { (void)mode; }
void speaker_set_counter(uint16_t reload_value, uint8_t mode) { (void)reload_value; (void)mode; }
void speaker_update_port61(uint8_t v) { (void)v; }
