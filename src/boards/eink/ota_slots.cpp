/* ota_slots.cpp - see ota_slots.h. */
#include "ota_slots.h"

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

#include "eink_board.h"
#include "esp_ota_ops.h"
#include "esp_rom_crc.h"
#include "spi_flash_mmap.h"

#define NVS_NAMES "ota_names"

static int is_ota_app(const esp_partition_t *p)
{
    return p && p->type == ESP_PARTITION_TYPE_APP
        && p->subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_0
        && p->subtype <= ESP_PARTITION_SUBTYPE_APP_OTA_15;
}

static int slot_of(const esp_partition_t *p) { return p->subtype - ESP_PARTITION_SUBTYPE_APP_OTA_0; }

void ota_slots_register(void)
{
    const esp_partition_t *self = esp_ota_get_running_partition();
    if (!is_ota_app(self)) return;
    char key[8];
    snprintf(key, sizeof key, "ota_%d", slot_of(self));
    Preferences prefs;
    if (!prefs.begin(NVS_NAMES, false)) return;
    if (prefs.getString(key, "") != "RetroComputer") prefs.putString(key, "RetroComputer");
    prefs.end();
}

int ota_slots_list(ota_slot_t *out, int max)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    Preferences prefs;
    const bool named = prefs.begin(NVS_NAMES, true);
    int n = 0;
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, NULL);
    for (; it && n < max; it = esp_partition_next(it)) {
        const esp_partition_t *p = esp_partition_get(it);
        esp_app_desc_t desc;
        if (!is_ota_app(p) || p == running) continue;
        if (esp_ota_get_partition_description(p, &desc) != ESP_OK) continue;   /* empty */
        char key[8];
        snprintf(key, sizeof key, "ota_%d", slot_of(p));
        String nm = named ? prefs.getString(key, "") : String();
        snprintf(out[n].name, sizeof out[n].name, "%s", nm.length() ? nm.c_str() : EINK_OTHER_APP);
        out[n].part = p;
        n++;
    }
    esp_partition_iterator_release(it);
    if (named) prefs.end();
    return n;
}

/* otadata, written by hand as CrossPoint and MicroBASIC write it:
 * esp_ota_set_boot_partition() refuses on the PaperS3 over an efuse
 * block-revision check. Two 4kB sectors, one entry each; the live one is
 * the valid entry with the higher sequence, and (seq - 1) modulo the
 * number of OTA app slots is the slot it boots. Marked valid, not new:
 * nothing here confirms itself, and a "new" slot is rolled back by the
 * next reset. */
typedef struct {
    uint32_t ota_seq;
    uint8_t  seq_label[20];
    uint32_t ota_state;
    uint32_t crc;
} ota_entry_t;

static uint32_t seq_crc(uint32_t seq) { return esp_rom_crc32_le(UINT32_MAX, (const uint8_t *)&seq, 4); }

int ota_slots_select(const esp_partition_t *p)
{
    if (!is_ota_app(p)) return -1;
    const esp_partition_t *od = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);
    if (!od || od->size < 2 * SPI_FLASH_SEC_SIZE) return -1;

    uint32_t slots = 0;
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, NULL);
    for (; it; it = esp_partition_next(it)) if (is_ota_app(esp_partition_get(it))) slots++;
    esp_partition_iterator_release(it);
    if (!slots) return -1;

    ota_entry_t e[2];
    if (esp_partition_read(od, 0, &e[0], sizeof e[0]) != ESP_OK
        || esp_partition_read(od, SPI_FLASH_SEC_SIZE, &e[1], sizeof e[1]) != ESP_OK) return -1;
    int live = -1;
    uint32_t seq = 0;
    for (int i = 0; i < 2; i++) {
        if (e[i].ota_seq == 0xFFFFFFFFu || e[i].crc != seq_crc(e[i].ota_seq)) continue;
        if (live < 0 || e[i].ota_seq > seq) { live = i; seq = e[i].ota_seq; }
    }
    const uint32_t want = (uint32_t)slot_of(p);
    uint32_t next = seq + 1;
    while ((next - 1) % slots != want) next++;

    ota_entry_t w;
    memset(&w, 0xFF, sizeof w);
    w.ota_seq = next;
    w.ota_state = 2;                      /* ESP_OTA_IMG_VALID */
    w.crc = seq_crc(next);
    const size_t off = (live == 0 ? 1 : 0) * SPI_FLASH_SEC_SIZE;
    if (esp_partition_erase_range(od, off, SPI_FLASH_SEC_SIZE) != ESP_OK) return -1;
    if (esp_partition_write(od, off, &w, sizeof w) != ESP_OK) return -1;
    Serial.printf("boot: otadata seq %u -> %s (%u slots)\n", (unsigned)next, p->label, (unsigned)slots);
    return 0;
}
