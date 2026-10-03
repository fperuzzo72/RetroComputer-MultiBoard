/* NOT UPSTREAM: the Mac Plus's real-time clock chip, see src/rtc.c. */
#ifndef UMAC_RTC_H
#define UMAC_RTC_H

#include <stdint.h>

/* VIA port B as the Mac drives it: bit 0 data, bit 1 clock, bit 2 enable
 * (low = selected). Called on every change. */
void umac_rtc_port(uint8_t portb);

/* The data bit the chip drives back, for port B bit 0 while it is sending. */
uint8_t umac_rtc_data(void);

/* The 20 bytes of parameter RAM (sound volume, mouse speed, start-up disc,
 * the alarm...), for a board that wants to keep them across power cycles,
 * and a call when the Mac has written to them. */
uint8_t *umac_rtc_pram(void);
extern void (*umac_rtc_pram_written)(void);

#endif
