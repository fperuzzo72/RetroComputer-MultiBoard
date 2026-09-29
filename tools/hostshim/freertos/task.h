#ifndef HOSTSHIM_TASK_H
#define HOSTSHIM_TASK_H
#include "freertos/FreeRTOS.h"
#ifdef __cplusplus
extern "C" {
#endif
static inline void vTaskDelay(TickType_t ms) { hostshim_now_us += (int64_t)(ms ? ms : 1) * 1000; }
static inline void taskYIELD(void) {}
#ifdef __cplusplus
}
#endif
#endif
