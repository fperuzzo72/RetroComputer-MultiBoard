/* nvs_file.c - NVS as files, for the Android board (shim/nvs.h).
 *
 * One small text file per key, <storage>/<namespace>.<key>, holding the
 * number or the string. Blobs are binary. There is no handle state beyond
 * the namespace, and every write goes straight to its file, so a commit is
 * nothing and an app killed mid-game keeps what it had written. */
#include "nvs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char root[512] = ".";
#define MAX_NS 16
static char spaces[MAX_NS][16];
static int nspaces;

void retro_board_set_storage(const char *dir)
{
    snprintf(root, sizeof root, "%s", dir);
}

esp_err_t nvs_open(const char *ns, nvs_open_mode_t m, nvs_handle_t *h)
{
    (void)m;
    for (int i = 0; i < nspaces; i++)
        if (!strcmp(spaces[i], ns)) { *h = (nvs_handle_t)i; return ESP_OK; }
    if (nspaces >= MAX_NS) return ESP_FAIL;
    snprintf(spaces[nspaces], sizeof spaces[0], "%s", ns);
    *h = (nvs_handle_t)nspaces++;
    return ESP_OK;
}

static void path_of(nvs_handle_t h, const char *k, char *out, size_t n)
{
    snprintf(out, n, "%s/%s.%s", root, h < (nvs_handle_t)nspaces ? spaces[h] : "x", k);
}

static esp_err_t read_bytes(nvs_handle_t h, const char *k, void *out, size_t *len)
{
    char p[640];
    path_of(h, k, p, sizeof p);
    FILE *f = fopen(p, "rb");
    if (!f) return ESP_ERR_NVS_NOT_FOUND;
    const size_t got = fread(out, 1, *len, f);
    fclose(f);
    *len = got;
    return ESP_OK;
}

static esp_err_t write_bytes(nvs_handle_t h, const char *k, const void *v, size_t len)
{
    char p[640];
    path_of(h, k, p, sizeof p);
    FILE *f = fopen(p, "wb");
    if (!f) return ESP_FAIL;
    fwrite(v, 1, len, f);
    fclose(f);
    return ESP_OK;
}

static esp_err_t get_num(nvs_handle_t h, const char *k, long long *v)
{
    char buf[32] = {0};
    size_t len = sizeof buf - 1;
    if (read_bytes(h, k, buf, &len) != ESP_OK || !len) return ESP_ERR_NVS_NOT_FOUND;
    buf[len] = 0;
    *v = strtoll(buf, NULL, 10);
    return ESP_OK;
}

static esp_err_t set_num(nvs_handle_t h, const char *k, long long v)
{
    char buf[32];
    const int n = snprintf(buf, sizeof buf, "%lld", v);
    return write_bytes(h, k, buf, (size_t)n);
}

esp_err_t nvs_get_i32(nvs_handle_t h, const char *k, int32_t *v) { long long x; esp_err_t e = get_num(h, k, &x); if (!e) *v = (int32_t)x; return e; }
esp_err_t nvs_set_i32(nvs_handle_t h, const char *k, int32_t v) { return set_num(h, k, v); }
esp_err_t nvs_get_u8(nvs_handle_t h, const char *k, uint8_t *v) { long long x; esp_err_t e = get_num(h, k, &x); if (!e) *v = (uint8_t)x; return e; }
esp_err_t nvs_set_u8(nvs_handle_t h, const char *k, uint8_t v) { return set_num(h, k, v); }
esp_err_t nvs_get_u16(nvs_handle_t h, const char *k, uint16_t *v) { long long x; esp_err_t e = get_num(h, k, &x); if (!e) *v = (uint16_t)x; return e; }
esp_err_t nvs_set_u16(nvs_handle_t h, const char *k, uint16_t v) { return set_num(h, k, v); }

esp_err_t nvs_get_str(nvs_handle_t h, const char *k, char *out, size_t *len)
{
    if (!out) return ESP_FAIL;
    size_t n = *len ? *len - 1 : 0;
    esp_err_t e = read_bytes(h, k, out, &n);
    if (e) return e;
    out[n] = 0;
    *len = n + 1;
    return ESP_OK;
}

esp_err_t nvs_set_str(nvs_handle_t h, const char *k, const char *v) { return write_bytes(h, k, v, strlen(v)); }
esp_err_t nvs_get_blob(nvs_handle_t h, const char *k, void *out, size_t *len) { return read_bytes(h, k, out, len); }
esp_err_t nvs_set_blob(nvs_handle_t h, const char *k, const void *v, size_t len) { return write_bytes(h, k, v, len); }
esp_err_t nvs_commit(nvs_handle_t h) { (void)h; return ESP_OK; }
void nvs_close(nvs_handle_t h) { (void)h; }
