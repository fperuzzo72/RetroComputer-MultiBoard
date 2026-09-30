#ifndef OTA_SLOTS_H
#define OTA_SLOTS_H

/* The other firmwares in this device's app slots, and switching to them.
 *
 * The Paper Mono has two slots: CrossPlay and this. The PaperS3 has three:
 * the CrossPoint reader, MicroBASIC and this. Every app slot other than the
 * running one that holds a firmware is offered, by the name that firmware
 * wrote into NVS for its siblings (namespace "ota_names", key "ota_<slot>",
 * the scheme CrossPoint and MicroBASIC share; see their OtaApps / ota_apps),
 * and this firmware writes its own there, so CrossPoint's Home lists it.
 * A slot with no name in NVS goes by EINK_OTHER_APP.
 */
#include "esp_partition.h"

#define OTA_SLOTS_MAX 4

typedef struct {
    const esp_partition_t *part;
    char name[32];
} ota_slot_t;

/* Writes "RetroComputer" for the running slot. Once, at boot. */
void ota_slots_register(void);

/* The other slots holding a firmware, in table order. */
int ota_slots_list(ota_slot_t *out, int max);

/* Boots `p` from the next reset. 0 on success. */
int ota_slots_select(const esp_partition_t *p);

#endif
