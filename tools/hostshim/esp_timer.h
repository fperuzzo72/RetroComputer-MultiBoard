#ifndef HOSTSHIM_ESP_TIMER_H
#define HOSTSHIM_ESP_TIMER_H
#include "hostshim.h"
static inline int64_t esp_timer_get_time(void) { return hostshim_now_us; }
#endif
