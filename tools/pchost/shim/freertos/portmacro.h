#ifndef PCSHIM_FREERTOS_H
#define PCSHIM_FREERTOS_H
#include "pcshim.h"
typedef uint32_t TickType_t;
typedef int BaseType_t;
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define pdTRUE 1
#define portMAX_DELAY 0xffffffffu
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(m) ((void)(m))
#define portEXIT_CRITICAL(m) ((void)(m))
#define taskENTER_CRITICAL(m) ((void)(m))
#define taskEXIT_CRITICAL(m) ((void)(m))
static inline int xPortInIsrContext(void) { return 0; }
#endif
