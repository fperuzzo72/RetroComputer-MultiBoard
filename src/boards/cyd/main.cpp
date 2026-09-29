/* main.cpp - CYD emulator firmware, entry point.
 *
 * This file is board-level and machine-agnostic: it brings the panel up,
 * hands the machine first claim on the heap, starts it, and only then
 * lets the BLE stack allocate. Which machine gets built in is decided by
 * platformio.ini - src/msx/ or src/spectrum/ - and everything either of
 * them has to provide is in src/machine.h.
 *
 * Boot order matters more than it looks. The framebuffer is claimed
 * before anything else because it is the biggest single block and the
 * chip has exactly one DRAM region large enough; BLE waits until the
 * machine has its memory, because otherwise its allocations land in the
 * middle of that region and the machine's RAM stops fitting. See
 * docs/MEMORY.md.
 *
 * The SD card is deliberately NOT mounted at boot. Nothing needs it - the
 * ROMs are in flash - and mounting costs about 45kB, which with a card in
 * the slot left the emulated VRAM twelve bytes short of fitting. `m 1` on
 * the serial console mounts it on demand. The card is fine; this is a
 * memory decision.
 */
#include <Arduino.h>

#include "board.h"
#include "sd_mount.h"
#include "ble_keyboard.h"
#include "machine.h"
#include "debug_console.h"
#include "display.h"
#include "boot_menu.h"

static void machineTask(void *arg) {
    (void)arg;
    Serial.printf("%s: starting, free heap %u, largest block %u\n",
                  machine->name, board_free_heap(), board_largest_block());
    board_heap_report();
    machine->run();
    Serial.println("machine: exited");
    vTaskDelete(NULL);
}

void setup() {
    Serial.begin(115200);
    delay(300);

    machine_storage_init();

    /* Whatever this board was last told to be. With one machine built in
     * there is nothing to choose; with both, the boot menu can still
     * change it below. */
    machine = machine_list[machine_chosen_index()];

    Serial.printf("\n\nCYD emulator: %s\n", machine->name);
    Serial.printf("boot: free heap %u, largest block %u\n",
                  board_free_heap(), board_largest_block());

    display_bridge_init();

    /* Choose before anything allocates. The menu draws on the panel and
     * reads the touchscreen, so it needs neither the keyboard nor any
     * memory worth speaking of. */
    boot_menu_run();

    /* First claim on the heap goes to the framebuffer. */
    if (!machine->prealloc_video())
        Serial.println("video: framebuffer allocation FAILED");

    debug_console_init();


    /* 12KB: the CPU cores and the renderers here are iterative, not
     * recursive, and on a board with no PSRAM 20KB of unused stack is
     * 20KB the emulated machine does not get. */
    xTaskCreatePinnedToCore(machineTask, "machine", 12288, NULL, 5, NULL, 1);

    for (int i = 0; i < 200 && !machine->ready(); i++) delay(25);
    Serial.printf("boot: machine has its memory, free %u, largest block %u\n",
                  board_free_heap(), board_largest_block());

    ble_keyboard_init();
}

void loop() {
    /* Everything runs on its own task: the machine on core 1, NimBLE and
     * the serial console on core 0. */
    delay(1000);
}
