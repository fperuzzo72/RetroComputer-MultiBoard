#ifndef RETRO_ESP_LOG_H
#define RETRO_ESP_LOG_H
/* Errors only: the cores' info and debug logs are their authors' tracing,
 * compiled out on the boards too (LOG_LOCAL_LEVEL there). */
#include <stdio.h>
#define ESP_LOGE(tag, fmt, ...) fprintf(stderr, "E %s: " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) do {} while (0)
#define ESP_LOGI(tag, fmt, ...) do {} while (0)
#define ESP_LOGD(tag, fmt, ...) do {} while (0)
#define ESP_LOGV(tag, fmt, ...) do {} while (0)
#endif
