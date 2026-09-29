/* media_sd.cpp - disc images on the card, for media.h.
 *
 * freeink-sdk's SDCardManager mounts the card: SDMMC on the Paper Mono
 * (which switches the card's supply through the IOE1 expander itself),
 * SPI on the PaperS3. The card is mounted once at boot, before the menu,
 * so that the Mac's list can include what is on it; after that only the
 * machine's task touches it.
 */
#include "media.h"

#include <Arduino.h>
#include <SDCardManager.h>
#include <string.h>

#include "panel_eink.h"

#define MEDIA_DIR "/mac"
#define MEDIA_MAX 16

static char names[MEDIA_MAX][48];
static char paths[MEDIA_MAX][64];
static int count;
static FsFile file;
static bool mounted;

static bool wanted(const char *n)
{
    const char *dot = strrchr(n, '.');
    if (!dot || n[0] == '.') return false;   /* no extension, or a macOS "._" file */
    return !strcasecmp(dot, ".img") || !strcasecmp(dot, ".dsk") || !strcasecmp(dot, ".hfv");
}

/* Mount and list. Called once from setup(); a missing card or folder is
 * not an error, only an empty list. */
extern "C" void media_begin(void)
{
    SDCardManager &sd = SDCardManager::getInstance();
    if (!sd.begin()) {
        Serial.println("media: no card");
        return;
    }
    mounted = true;
    FsFile dir = sd.open(MEDIA_DIR);
    if (!dir || !dir.isDirectory()) {
        Serial.println("media: card mounted, no " MEDIA_DIR " folder");
        return;
    }
    FsFile f;
    char n[64];
    while (count < MEDIA_MAX && f.openNext(&dir, O_RDONLY)) {
        f.getName(n, sizeof n);
        if (!f.isDirectory() && wanted(n)) {
            snprintf(paths[count], sizeof paths[count], MEDIA_DIR "/%s", n);
            strncpy(names[count], n, sizeof names[count] - 1);
            char *dot = strrchr(names[count], '.');
            if (dot) *dot = 0;
            Serial.printf("media: %s, %lu kB\n", paths[count], (unsigned long)(f.fileSize() / 1024));
            count++;
        }
        f.close();
    }
    dir.close();
}

int media_create(const char *name, const uint8_t *data, uint32_t len)
{
    if (!mounted || count >= MEDIA_MAX) return -1;
    SDCardManager &sd = SDCardManager::getInstance();
    sd.mkdir(MEDIA_DIR);
    char path[64];
    snprintf(path, sizeof path, MEDIA_DIR "/%s.img", name);
    if (sd.exists(path)) return -1;

    panel_message("Copiando o disco para o cartao", "um momento");
    FsFile f = sd.open(path, O_RDWR | O_CREAT | O_EXCL);
    bool ok = (bool)f;
    for (uint32_t done = 0; ok && done < len; ) {
        const uint32_t n = len - done > 32768 ? 32768 : len - done;
        ok = f.write(data + done, n) == n;
        done += n;
    }
    if (f) { f.sync(); f.close(); }
    panel_message_clear();
    if (!ok) {
        Serial.printf("media: could not write %s, removing it\n", path);
        sd.remove(path);
        return -1;
    }
    strncpy(paths[count], path, sizeof paths[count] - 1);
    strncpy(names[count], name, sizeof names[count] - 1);
    Serial.printf("media: %s written, %lu kB\n", path, (unsigned long)(len / 1024));
    return count++;
}

int media_count(void) { return count; }
const char *media_name(int i) { return i >= 0 && i < count ? names[i] : ""; }

void *media_open(int i, uint32_t *size)
{
    if (i < 0 || i >= count) return NULL;
    file = SDCardManager::getInstance().open(paths[i], O_RDWR);
    if (!file) {
        Serial.printf("media: cannot open %s for writing\n", paths[i]);
        return NULL;
    }
    *size = (uint32_t)file.fileSize();
    return &file;
}

int media_read(void *h, uint8_t *data, uint32_t offset, uint32_t len)
{
    FsFile *f = (FsFile *)h;
    if (!f->seekSet(offset)) return -1;
    return f->read(data, len) == (int)len ? 0 : -1;
}

int media_write(void *h, const uint8_t *data, uint32_t offset, uint32_t len)
{
    FsFile *f = (FsFile *)h;
    if (!f->seekSet(offset)) return -1;
    if (f->write(data, len) != len) return -1;
    /* A Mac expects what it wrote to stay written; the board can lose power
     * at any moment, so every write goes all the way to the card. */
    f->sync();
    return 0;
}
