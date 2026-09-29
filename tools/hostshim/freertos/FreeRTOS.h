#ifndef HOSTSHIM_FREERTOS_H
#define HOSTSHIM_FREERTOS_H
#include "hostshim.h"
typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;
typedef void *TaskHandle_t;
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY 0xffffffffu
#endif
