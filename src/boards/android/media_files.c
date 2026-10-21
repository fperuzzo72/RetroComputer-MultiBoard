/* media_files.c - media.h for the Android board: the Mac's disc images in
 * <card>/mac, the folder that plays the boards' SD card (RETRO_CARD, set
 * by android_jni.cpp). The same rules as the card's media_sd.cpp: .img,
 * .dsk or .hfv, listed once, read and written in place so that what the
 * Mac saves is there next time, a new image never written over an old. */
#include "media.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define MEDIA_MAX 16

static char names[MEDIA_MAX][48];
static char paths[MEDIA_MAX][512];
static int count = -1;

static int wanted(const char *n)
{
    const char *dot = strrchr(n, '.');
    if (!dot || n[0] == '.') return 0;   /* no extension, or a macOS "._" file */
    return !strcasecmp(dot, ".img") || !strcasecmp(dot, ".dsk") || !strcasecmp(dot, ".hfv");
}

static const char *folder(void)
{
    static char f[480];
    const char *card = getenv("RETRO_CARD");
    snprintf(f, sizeof f, "%s/mac", card ? card : ".");
    return f;
}

static void list(void)
{
    if (count >= 0) return;
    count = 0;
    DIR *d = opendir(folder());
    if (!d) return;
    while (count < MEDIA_MAX) {
        struct dirent *e = readdir(d);
        if (!e) break;
        if (!wanted(e->d_name)) continue;
        snprintf(paths[count], sizeof paths[count], "%s/%s", folder(), e->d_name);
        struct stat st;
        if (stat(paths[count], &st) != 0 || !S_ISREG(st.st_mode)) continue;
        snprintf(names[count], sizeof names[count], "%s", e->d_name);
        char *dot = strrchr(names[count], '.');
        if (dot) *dot = 0;
        printf("media: %s, %lu kB\n", paths[count], (unsigned long)(st.st_size / 1024));
        count++;
    }
    closedir(d);
}

void media_begin(void) { list(); }
int media_count(void) { list(); return count; }
const char *media_name(int i) { list(); return i >= 0 && i < count ? names[i] : ""; }

void *media_open(int i, uint32_t *size)
{
    list();
    if (i < 0 || i >= count) return NULL;
    FILE *f = fopen(paths[i], "r+b");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *size = (uint32_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    return f;
}

int media_create(const char *name, const uint8_t *data, uint32_t len)
{
    list();
    if (count >= MEDIA_MAX) return -1;
    char p[512];
    snprintf(p, sizeof p, "%s/%s.img", folder(), name);
    struct stat st;
    if (stat(p, &st) == 0) return -1;   /* never over an existing one */
    FILE *f = fopen(p, "wb");
    if (!f) return -1;
    const size_t n = fwrite(data, 1, len, f);
    fclose(f);
    if (n != len) { remove(p); return -1; }
    snprintf(paths[count], sizeof paths[count], "%s", p);
    snprintf(names[count], sizeof names[count], "%s", name);
    return count++;
}

int media_read(void *h, uint8_t *data, uint32_t offset, uint32_t len)
{
    FILE *f = (FILE *)h;
    if (!f || fseek(f, (long)offset, SEEK_SET)) return -1;
    return fread(data, 1, len, f) == len ? 0 : -1;
}

int media_write(void *h, const uint8_t *data, uint32_t offset, uint32_t len)
{
    FILE *f = (FILE *)h;
    if (!f || fseek(f, (long)offset, SEEK_SET)) return -1;
    const int ok = fwrite(data, 1, len, f) == len;
    fflush(f);
    return ok ? 0 : -1;
}
