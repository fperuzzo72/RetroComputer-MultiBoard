/* Stand-ins for the ESP-IDF pieces lib/pc8086 includes, for tools/pchost.
 * Unlike tools/hostshim, time here is real: the PC's timer tick and the
 * BIOS's clock read esp_timer_get_time and DOS should see a real 18.2Hz. */
#ifndef PCSHIM_H
#define PCSHIM_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
static inline int64_t esp_timer_get_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}
#endif
