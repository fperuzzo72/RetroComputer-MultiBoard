/* card_catalogue.c - see card_catalogue.h. */
#include "card_catalogue.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "nvs.h"

#define MAX_FILES 4096
#define NVS_NS "cyd"

static const char *card_root(void)
{
    const char *r = getenv("RETRO_CARD");
    return r ? r : ".";
}

static int wanted(const card_catalogue *c, const char *n)
{
    const char *dot = strrchr(n, '.');
    if (!dot || n[0] == '.') return 0;
    for (const char *const *e = c->exts; *e; e++) if (!strcasecmp(dot, *e)) return 1;
    return 0;
}

static int by_name(const void *a, const void *b)
{
    return strcasecmp((const char *)a, (const char *)b);
}

static void list(card_catalogue *c)
{
    if (c->count >= 0) return;
    c->count = 0;
    char dir[512];
    snprintf(dir, sizeof dir, "%s/%s", card_root(), c->folder);
    mkdir(dir, 0775);
    DIR *d = opendir(dir);
    if (!d) return;
    c->files = calloc(MAX_FILES, sizeof c->files[0]);
    c->names = calloc(MAX_FILES, sizeof c->names[0]);
    if (!c->files || !c->names) { closedir(d); return; }
    struct dirent *e;
    while ((e = readdir(d)) && c->count < MAX_FILES) {
        if (!wanted(c, e->d_name) || strlen(e->d_name) >= sizeof c->files[0]) continue;
        strcpy(c->files[c->count++], e->d_name);
    }
    closedir(d);
    qsort(c->files, (size_t)c->count, sizeof c->files[0], by_name);
    for (int i = 0; i < c->count; i++) {
        snprintf(c->names[i], sizeof c->names[0], "%s", c->files[i]);
        char *dot = strrchr(c->names[i], '.');
        if (dot) *dot = 0;
    }
}

int card_count(card_catalogue *c) { list(c); return c->count; }

const char *card_name(card_catalogue *c, int i)
{
    list(c);
    return i >= 0 && i < c->count ? c->names[i] : "";
}

int card_selected(card_catalogue *c)
{
    if (c->selected != -2) return c->selected;
    list(c);
    c->selected = -1;
    nvs_handle_t h;
    char name[128] = "";
    size_t len = sizeof name;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return -1;
    if (nvs_get_str(h, c->nvs_key, name, &len) != ESP_OK) name[0] = 0;
    nvs_close(h);
    for (int i = 0; name[0] && i < c->count; i++)
        if (!strcmp(c->files[i], name)) c->selected = i;
    return c->selected;
}

void card_select(card_catalogue *c, int i)
{
    list(c);
    if (i < -1 || i >= c->count) return;
    c->selected = i;
    free(c->bytes);
    c->bytes = 0;
    c->size = 0;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, c->nvs_key, i >= 0 ? c->files[i] : "");
    nvs_commit(h);
    nvs_close(h);
}

const char *card_path(card_catalogue *c)
{
    static char path[700];
    const int i = card_selected(c);
    if (i < 0) return 0;
    snprintf(path, sizeof path, "%s/%s/%s", card_root(), c->folder, c->files[i]);
    return path;
}

const unsigned char *card_bytes(card_catalogue *c, int *size)
{
    if (!c->bytes) {
        const char *p = card_path(c);
        FILE *f = p ? fopen(p, "rb") : 0;
        if (!f) { if (size) *size = 0; return 0; }
        fseek(f, 0, SEEK_END);
        const long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        c->bytes = n > 0 ? malloc((size_t)n) : 0;
        if (c->bytes && fread(c->bytes, 1, (size_t)n, f) == (size_t)n) {
            c->size = (int)n;
        } else {
            free(c->bytes);
            c->bytes = 0;
        }
        fclose(f);
    }
    if (size) *size = c->size;
    return c->bytes;
}
