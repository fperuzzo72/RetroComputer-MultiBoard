#ifndef PCSHIM_ESP_LOG_H
#define PCSHIM_ESP_LOG_H
#include <stdio.h>
#include <stdlib.h>
/* PCLOG=1 shows I/W, PCLOG=2 also D; errors always. */
static inline int pcshim_log_level(void) { static int l = -1; if (l < 0) { const char *e = getenv("PCLOG"); l = e ? atoi(e) : 0; } return l; }
#define ESP_LOGE(tag, fmt, ...) fprintf(stderr, "E %s: " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) do { if (pcshim_log_level() >= 1) fprintf(stderr, "W %s: " fmt "\n", tag, ##__VA_ARGS__); } while (0)
#define ESP_LOGI(tag, fmt, ...) do { if (pcshim_log_level() >= 1) fprintf(stderr, "I %s: " fmt "\n", tag, ##__VA_ARGS__); } while (0)
#define ESP_LOGD(tag, fmt, ...) do { if (pcshim_log_level() >= 2) fprintf(stderr, "D %s: " fmt "\n", tag, ##__VA_ARGS__); } while (0)
#endif
