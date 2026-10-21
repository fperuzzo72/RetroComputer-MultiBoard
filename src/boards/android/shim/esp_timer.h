/* Android (and tools/androidhost): the ESP-IDF pieces the machines include,
 * with real time. Unlike tools/hostshim, nothing here is simulated: the
 * machines pace themselves against this clock as they do on the boards. */
#ifndef RETRO_ESP_TIMER_H
#define RETRO_ESP_TIMER_H
#include <stdint.h>
#include <time.h>
static inline int64_t esp_timer_get_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}
#endif
