#ifndef PCSHIM_TASK_H
#define PCSHIM_TASK_H
#include "freertos/FreeRTOS.h"
static inline void vTaskDelay(TickType_t ms) { usleep((ms ? ms : 1) * 1000); }
static inline void taskYIELD(void) {}
#endif
