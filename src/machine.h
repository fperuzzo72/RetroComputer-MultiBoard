#ifndef MACHINE_H
#define MACHINE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* What a machine has to provide to the board.
 *
 * Everything under src/boards/<board>/ is one board: its panel, its
 * keyboard host, its card, its serial console, its boot menu. It knows
 * nothing about MSX, Spectrum or Macintosh. Everything under src/msx/,
 * src/spectrum/ and src/mac/ is a machine, and knows nothing about any
 * board.
 *
 * A table of function pointers rather than a set of link-time symbols,
 * because one firmware carries both machines and picks at boot. The cost
 * is one indirection on calls that happen at most once a frame; nothing
 * in an inner loop goes through here. */

typedef struct Machine {
    /* For the boot menu and the banner, e.g. "MSX1 (Hotbit HB-8000)". */
    const char *name;

    /* Picture scale this machine comes up at: 1 for one panel pixel per
     * machine pixel, 2 for one and a half. Not a preference but a
     * measurement - the Spectrum runs at twice the speed it needs and can
     * spend it on a bigger picture, the MSX cannot (see README, "Speed").
     * Whatever the user last chose with `z` wins over this. */
    int default_scale;

    /* Claim the framebuffer before anything else fragments the one big
     * DRAM region this chip has. Called before run(). */
    int (*prealloc_video)(void);

    /* Bring the machine up and run it. Never returns. */
    void (*run)(void);

    /* Non-zero once it has claimed all the memory it needs, so the BLE
     * stack knows when it is safe to allocate. */
    int (*ready)(void);

    unsigned long (*frames)(void);

    /* --- keyboard: the board carries the keys, the machine means them --- */
    void (*hid_report)(const uint8_t report[8]);
    int  (*type)(const char *text);   /* synthetic typing, for the console */
    int  (*typing)(void);

    /* --- reading the machine back over the serial console -------------- */
    int  (*screen_row)(int row, uint8_t *out, int max);
    const char *(*screen_mode_name)(void);
    int  (*char_pattern)(int code, uint8_t *rows8);
    int  (*peek)(int addr);

    /* One row of the emulated key matrix, or NULL where a machine has
     * none to show. For checking what a key mapping actually does. */
    int  (*matrix_row)(int row);

    /* --- sound --------------------------------------------------------- */
    void (*set_sound)(int on);
    int  (*sound_on)(void);

    /* --- what this machine can boot into -------------------------------
     * Entry 0 is always the machine on its own - BASIC, an empty slot.
     * The rest are whatever ROMs are built into this firmware. */
    int         (*entry_count)(void);
    const char *(*entry_name)(int i);
    void        (*select_entry)(int i);
    int         (*selected_entry)(void);

    /* Swap to another entry while running, without rebooting the board:
     * insert a different cartridge, put in a different tape. Resets the
     * emulated machine, which is what inserting either would do. */
    void        (*switch_to)(int entry);

    /* --- console commands only this machine has ------------------------ */
    int         (*debug_command)(const char *line);
    const char *(*debug_help)(void);

    /* --- a pointing device, for a machine that has one ------------------
     * Absolute position in machine pixels and the button, from a touch
     * panel. NULL for a machine without a mouse; the 8-bit machines leave
     * it out of their initialisers and get that for free. */
    void        (*pointer)(int x, int y, int button);
} Machine;

/* Every machine in this firmware, and the one that was chosen. */
extern const Machine *const machine_list[];
extern const int machine_count;
extern const Machine *machine;

/* Bring the choice storage up. Must be called before anything reads a
 * remembered choice. */
void machine_storage_init(void);

/* Pick one, by index into machine_list. Remembered in NVS. */
void machine_choose(int index, int entry);
int  machine_chosen_index(void);

#ifdef __cplusplus
}
#endif
#endif
