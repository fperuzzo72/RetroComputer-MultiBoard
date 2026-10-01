#ifndef PCSHIM_SEMPHR_H
#define PCSHIM_SEMPHR_H
#include "freertos/FreeRTOS.h"
typedef void *SemaphoreHandle_t;
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
#define xSemaphoreTake(s, t) ((void)(s), (void)(t), 1)
#define xSemaphoreGive(s) ((void)(s), 1)
#endif
