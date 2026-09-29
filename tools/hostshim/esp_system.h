#ifndef HOSTSHIM_ESP_SYSTEM_H
#define HOSTSHIM_ESP_SYSTEM_H
#include "hostshim.h"
static inline void esp_restart(void) { exit(3); }
#endif
