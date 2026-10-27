/* machines.c - which machines this firmware carries, and which was chosen.
 *
 * A build can carry one machine or several; platformio.ini decides by which
 * machine directories go into it, and the HAVE_* flags below follow. With
 * both present the boot menu picks, and the choice is remembered in NVS
 * so the board comes back up as whatever it was last. It is remembered by
 * name ("machinename"), so the order can change; the index it was kept as
 * before ("machine") is still read when there is no name.
 */
#include "machine.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <stdio.h>
#include <string.h>

#ifdef HAVE_MACHINE_MSX
extern const Machine msx_machine;
#endif
#ifdef HAVE_MACHINE_SPECTRUM
extern const Machine spectrum_machine;
#endif
#ifdef HAVE_MACHINE_MAC
extern const Machine mac_machine;
#endif
#ifdef HAVE_MACHINE_C64
extern const Machine c64_machine;
#endif
#ifdef HAVE_MACHINE_PC
extern const Machine pc_machine;
#endif
#ifdef HAVE_MACHINE_PC386
extern const Machine pc386_machine;
#endif
#ifdef HAVE_MACHINE_AMIGA
extern const Machine amiga_machine;
#endif
#ifdef HAVE_MACHINE_MSX2
extern const Machine msx2_machine;
#endif
#ifdef HAVE_MACHINE_SCUMMVM
extern const Machine scummvm_machine;
#endif

const Machine *const machine_list[] = {
#ifdef HAVE_MACHINE_MSX
    &msx_machine,
#endif
#ifdef HAVE_MACHINE_MSX2
    &msx2_machine,
#endif
#ifdef HAVE_MACHINE_SPECTRUM
    &spectrum_machine,
#endif
#ifdef HAVE_MACHINE_MAC
    &mac_machine,
#endif
#ifdef HAVE_MACHINE_C64
    &c64_machine,
#endif
#ifdef HAVE_MACHINE_PC
    &pc_machine,
#endif
#ifdef HAVE_MACHINE_PC386
    &pc386_machine,
#endif
#ifdef HAVE_MACHINE_AMIGA
    &amiga_machine,
#endif
#ifdef HAVE_MACHINE_SCUMMVM
    &scummvm_machine,
#endif
};

const int machine_count = (int)(sizeof(machine_list) / sizeof(machine_list[0]));

/* The order before the choice was kept by name (2026-10-27), with the
 * MSX2+ last: what an index kept then means. */
static const Machine *const index_order[] = {
#ifdef HAVE_MACHINE_MSX
    &msx_machine,
#endif
#ifdef HAVE_MACHINE_SPECTRUM
    &spectrum_machine,
#endif
#ifdef HAVE_MACHINE_MAC
    &mac_machine,
#endif
#ifdef HAVE_MACHINE_C64
    &c64_machine,
#endif
#ifdef HAVE_MACHINE_PC
    &pc_machine,
#endif
#ifdef HAVE_MACHINE_PC386
    &pc386_machine,
#endif
#ifdef HAVE_MACHINE_AMIGA
    &amiga_machine,
#endif
#ifdef HAVE_MACHINE_MSX2
    &msx2_machine,
#endif
    0
};

/* Set during boot, before anything else touches it. */
const Machine *machine = 0;

/* NVS has to be up before anything reads a choice out of it.
 *
 * The Arduino core brings it up on its way to Bluetooth, which is much
 * later than setup() runs - so the first read returned the default, the
 * boot menu then wrote that default back over the real choice, and
 * selecting anything from the serial console appeared not to stick. */
void machine_storage_init(void) {
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        e = nvs_flash_init();
    }
    if (e != ESP_OK) printf("machine: NVS unavailable (%d), choices will not stick\n", (int)e);
}

#define NVS_NAMESPACE "cyd"
#define NVS_KEY       "machine"
#define NVS_KEY_NAME  "machinename"

int machine_chosen_index(void) {
    nvs_handle_t h;
    int32_t v = 0;
    char name[40] = "";
    size_t len = sizeof name;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_str(h, NVS_KEY_NAME, name, &len) != ESP_OK) name[0] = 0;
        if (nvs_get_i32(h, NVS_KEY, &v) != ESP_OK) v = 0;
        nvs_close(h);
    }
    if (!name[0] && v >= 0 && v < (int)(sizeof index_order / sizeof index_order[0]) - 1)
        snprintf(name, sizeof name, "%s", index_order[v]->name);
    for (int i = 0; name[0] && i < machine_count; i++)
        if (!strcmp(machine_list[i]->name, name)) return i;
    return 0;
}

void machine_choose(int index, int entry) {
    nvs_handle_t h;
    esp_err_t e;
    if (index < 0 || index >= machine_count) index = 0;
    machine = machine_list[index];
    if (entry >= 0) machine->select_entry(entry);
    e = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (e != ESP_OK) { printf("machine: nvs_open failed (%d)\n", (int)e); return; }
    e = nvs_set_i32(h, NVS_KEY, (int32_t)index);
    if (e == ESP_OK) e = nvs_set_str(h, NVS_KEY_NAME, machine->name);
    if (e != ESP_OK) printf("machine: nvs_set failed (%d)\n", (int)e);
    e = nvs_commit(h);
    if (e != ESP_OK) printf("machine: nvs_commit failed (%d)\n", (int)e);
    nvs_close(h);

}
