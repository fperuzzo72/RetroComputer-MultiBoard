#ifndef EINK_CLOCK_H
#define EINK_CLOCK_H

/* The board's real-time clock as the system's: read once at boot into the
 * ESP32's time of day, which is what the machines ask (the PC's BIOS through
 * time() and localtime(), INT 1Ah). clock.cpp. */
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

void clock_begin(void);

/* Console: `rtc` reports, `rtc YYYY-MM-DD HH:MM:SS` (local time) sets the
 * clock and the system time. Takes the I2C lock: the touch panel is on the same bus. */
void clock_command(const char *args, SemaphoreHandle_t i2c_lock);

#endif
