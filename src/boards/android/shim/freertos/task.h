#ifndef RETRO_TASK_H
#define RETRO_TASK_H
#include <unistd.h>
#include "freertos/FreeRTOS.h"
static inline void vTaskDelay(TickType_t ms) { usleep((useconds_t)(ms ? ms : 1) * 1000); }
static inline void taskYIELD(void) { usleep(0); }
#endif
