/* NVS as files: one file per key, under the folder the board names
 * (retro_board_set_storage). What the machines remember (the machine and
 * entry last chosen, the MSX's 50/60Hz, speed) survives the app closing,
 * as it survives a power cycle on the boards. nvs_file.c. */
#ifndef RETRO_NVS_H
#define RETRO_NVS_H
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef uint32_t nvs_handle_t;
typedef enum { NVS_READONLY, NVS_READWRITE } nvs_open_mode_t;
esp_err_t nvs_open(const char *ns, nvs_open_mode_t m, nvs_handle_t *h);
esp_err_t nvs_get_i32(nvs_handle_t h, const char *k, int32_t *v);
esp_err_t nvs_set_i32(nvs_handle_t h, const char *k, int32_t v);
esp_err_t nvs_get_u8(nvs_handle_t h, const char *k, uint8_t *v);
esp_err_t nvs_set_u8(nvs_handle_t h, const char *k, uint8_t v);
esp_err_t nvs_get_u16(nvs_handle_t h, const char *k, uint16_t *v);
esp_err_t nvs_set_u16(nvs_handle_t h, const char *k, uint16_t v);
esp_err_t nvs_get_str(nvs_handle_t h, const char *k, char *out, size_t *len);
esp_err_t nvs_set_str(nvs_handle_t h, const char *k, const char *v);
esp_err_t nvs_get_blob(nvs_handle_t h, const char *k, void *out, size_t *len);
esp_err_t nvs_set_blob(nvs_handle_t h, const char *k, const void *v, size_t len);
esp_err_t nvs_commit(nvs_handle_t h);
void nvs_close(nvs_handle_t h);
#ifdef __cplusplus
}
#endif
#endif
