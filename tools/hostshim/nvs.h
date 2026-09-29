#ifndef HOSTSHIM_NVS_H
#define HOSTSHIM_NVS_H
#include "hostshim.h"
typedef int esp_err_t;
typedef uint32_t nvs_handle_t;
typedef enum { NVS_READONLY, NVS_READWRITE } nvs_open_mode_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NVS_NO_FREE_PAGES 0x1100
#define ESP_ERR_NVS_NEW_VERSION_FOUND 0x1101
static inline esp_err_t nvs_open(const char *ns, nvs_open_mode_t m, nvs_handle_t *h) { (void)ns; (void)m; (void)h; return ESP_FAIL; }
static inline esp_err_t nvs_get_i32(nvs_handle_t h, const char *k, int32_t *v) { (void)h; (void)k; (void)v; return ESP_FAIL; }
static inline esp_err_t nvs_set_i32(nvs_handle_t h, const char *k, int32_t v) { (void)h; (void)k; (void)v; return ESP_FAIL; }
static inline esp_err_t nvs_get_u8(nvs_handle_t h, const char *k, uint8_t *v) { (void)h; (void)k; (void)v; return ESP_FAIL; }
static inline esp_err_t nvs_set_u8(nvs_handle_t h, const char *k, uint8_t v) { (void)h; (void)k; (void)v; return ESP_FAIL; }
static inline esp_err_t nvs_get_u16(nvs_handle_t h, const char *k, uint16_t *v) { (void)h; (void)k; (void)v; return ESP_FAIL; }
static inline esp_err_t nvs_set_u16(nvs_handle_t h, const char *k, uint16_t v) { (void)h; (void)k; (void)v; return ESP_FAIL; }
static inline esp_err_t nvs_commit(nvs_handle_t h) { (void)h; return ESP_OK; }
static inline void nvs_close(nvs_handle_t h) { (void)h; }
#endif
