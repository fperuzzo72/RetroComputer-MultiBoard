#ifndef LR_FRONTEND_H
#define LR_FRONTEND_H
/* lr_frontend - a libretro core as a Machine, for the Android board.
 *
 * A machine that is a libretro core (the PC 386, DOSBox Pure; the Amiga,
 * PUAE) is a table of what differs - which library, which folder on the
 * card, which files, which options - and lr_frontend.c does the rest:
 * loads the library with dlopen, runs it a frame at a time, hands its
 * picture, sound, keys and mouse to the board, lists its entries and
 * remembers the choice. LR_MACHINE() makes the Machine table.
 *
 * Only one core runs in a process (another computer is a start over), so
 * the frontend's state is plain statics. Android only.
 */
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include "machine.h"

typedef struct lr_core {
    const char *tag;             /* the log's prefix and the card's folder: "pc386" */
    const char *lib;             /* the core: "libdosbox_pure.so" */
    const char *const *exts;     /* the files it takes, NULL-terminated: ".zip", ... */
    const char *entry0;          /* entry 0, the machine with no file */
    const char *nvs_key;         /* where the choice is kept (15 characters at most) */
    /* its options, or NULL for the core's default; sysdir is where its
     * ROMs are (<card>/<tag>/system) */
    const char *(*option)(const char *key, const char *sysdir);
    /* entries from elsewhere on the card, after its own folder's */
    void (*extra)(const char *card, void (*add)(const char *name, const char *path));
    int four3;                   /* shown at 4:3 whatever the size */
    int kbjoy;                   /* arrows and Ctrl are a joystick on RetroPad 0, F10 toggles */
    /* a file of its folder listed right after entry 0, under a name of its
     * own in the menus (kept by its file name all the same), or NULL */
    const char *first_file, *first_label;
} lr_core;

void lr_run(const lr_core *c);
/* The file the running core was started with, or NULL (for options that
 * depend on it). */
const char *lr_content_path(void);
int lr_ready(void);
unsigned long lr_frames(void);
void lr_hid(const lr_core *c, const uint8_t report[8]);
void lr_pointer(int x, int y, int button);
int lr_entry_count(const lr_core *c);
const char *lr_entry_name(const lr_core *c, int i);
void lr_select_entry(const lr_core *c, int i);
int lr_selected_entry(const lr_core *c);
void lr_switch_to(const lr_core *c, int entry);
int lr_one(void);
int lr_zero(void);
int lr_no_type(const char *text);
int lr_screen_row(int row, uint8_t *out, int max);
int lr_char_pattern(int code, uint8_t *rows8);
int lr_peek(int addr);
void lr_set_sound(int on);
int lr_debug_command(const char *line);
const char *lr_debug_help(void);

/* The Machine for a core: NAME is its title in the menus, CORE an lr_core. */
#define LR_MACHINE(sym, NAME, CORE)                                                          \
    static void sym##_run(void) { lr_run(&CORE); }                                           \
    static void sym##_hid(const uint8_t r[8]) { lr_hid(&CORE, r); }                          \
    static int sym##_count(void) { return lr_entry_count(&CORE); }                           \
    static const char *sym##_name(int i) { return lr_entry_name(&CORE, i); }                 \
    static void sym##_select(int i) { lr_select_entry(&CORE, i); }                           \
    static int sym##_selected(void) { return lr_selected_entry(&CORE); }                     \
    static void sym##_switch(int i) { lr_switch_to(&CORE, i); }                              \
    static const char *sym##_mode(void) { return CORE.tag; }                                 \
    const Machine sym = {                                                                    \
        NAME, 1,                                                                             \
        lr_one, sym##_run, lr_ready, lr_frames,                                              \
        sym##_hid, lr_no_type, lr_zero,                                                      \
        lr_screen_row, sym##_mode, lr_char_pattern, lr_peek, 0,                              \
        lr_set_sound, lr_one,                                                                \
        sym##_count, sym##_name, sym##_select, sym##_selected, sym##_switch,                 \
        lr_debug_command, lr_debug_help,                                                     \
        lr_pointer,                                                                          \
    }

#ifdef __cplusplus
}
#endif
#endif
