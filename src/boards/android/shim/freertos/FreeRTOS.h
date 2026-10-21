#ifndef RETRO_FREERTOS_H
#define RETRO_FREERTOS_H
#include <stdint.h>
typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;
typedef void *TaskHandle_t;
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define portMAX_DELAY 0xffffffffu
/* Critical sections, as lib/pc8086 takes them around its key buffer: one
 * process-wide lock is enough for what they guard here. */
#include <pthread.h>
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
extern pthread_mutex_t retro_critical;
#define portENTER_CRITICAL(m) ((void)(m), pthread_mutex_lock(&retro_critical))
#define portEXIT_CRITICAL(m) ((void)(m), pthread_mutex_unlock(&retro_critical))
#define taskENTER_CRITICAL(m) portENTER_CRITICAL(m)
#define taskEXIT_CRITICAL(m) portEXIT_CRITICAL(m)
static inline int xPortInIsrContext(void) { return 0; }
#endif
