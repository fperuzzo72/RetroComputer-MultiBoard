/* scummvm_machine.c - ScummVM, as a Machine: the adventure games.
 *
 * third_party/scummvm is libretro's ScummVM, built by
 * tools/build_scummvm_core.sh (its own makefiles, not the app's CMake) into
 * the app as libscummvm.so, with only the engines of the games their makers
 * set free, and run by src/libretro/lr_frontend.c. What is particular to it:
 *
 *   - each game is a folder in the card's scummvm/ with a hook file in it,
 *     <gameid>.scummvm (sky, lure, queen, drascula, dreamweb), which the
 *     core detects the game from; the entry is the folder's name. Entry 0
 *     is ScummVM's own launcher;
 *   - its themes and engine data (scummvm.zip from the build) are in
 *     scummvm/system/scummvm/{theme,extra}, where the core looks;
 *   - software rendering (the frontend gives no OpenGL), the mouse as the
 *     pointer (a finger is the trackpad, as on the other machines).
 *
 * Android only.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "lr_frontend.h"

static const char *option(const char *k, const char *sysdir)
{
    (void)sysdir;
    if (!strcmp(k, "scummvm_video_hw_acceleration")) return "disabled";
    if (!strcmp(k, "scummvm_pointer_device")) return "mouse";
    return NULL;
}

#define MAX_GAMES 64

static int by_name(const void *a, const void *b)
{
    return strcasecmp(*(char *const *)a, *(char *const *)b);
}

/* Each folder of scummvm/ that has a .scummvm hook in it, by the folder's
 * name, in alphabetical order. */
static void games(const char *card, void (*add)(const char *name, const char *path))
{
    static char found[MAX_GAMES][2][512];
    char *order[MAX_GAMES];
    int n = 0;
    char dir[512], sub[768];
    snprintf(dir, sizeof dir, "%s/scummvm", card);
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while (n < MAX_GAMES && (e = readdir(d))) {
        if (e->d_name[0] == '.' || !strcmp(e->d_name, "system") || !strcmp(e->d_name, "saves")) continue;
        snprintf(sub, sizeof sub, "%s/%s", dir, e->d_name);
        DIR *g = opendir(sub);
        if (!g) continue;
        struct dirent *f;
        while ((f = readdir(g))) {
            const char *dot = strrchr(f->d_name, '.');
            if (dot && !strcasecmp(dot, ".scummvm")) {
                snprintf(found[n][0], sizeof found[n][0], "%s", e->d_name);
                snprintf(found[n][1], sizeof found[n][1], "%s/%s", sub, f->d_name);
                order[n] = found[n][0];
                n++;
                break;
            }
        }
        closedir(g);
    }
    closedir(d);
    qsort(order, (size_t)n, sizeof order[0], by_name);
    for (int i = 0; i < n; i++) add(order[i], order[i] + sizeof found[0][0]);
}

static const char *const exts[] = {".scummvm", NULL};

static const lr_core scummvm = {
    "scummvm", "libscummvm.so", exts, "ScummVM", "scummvmentry",
    option, games, 1, 0,
};

LR_MACHINE(scummvm_machine, "ScummVM", scummvm);
