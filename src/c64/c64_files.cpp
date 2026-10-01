/* c64_files.cpp - lib/c64's FileDriver on this project's storage.
 *
 * The core opens .prg and .d64 files under Config::PATH ("/c64/"), writes
 * a .prg when BASIC SAVEs, and lists that folder. On the device that is
 * the board's card, through freeink-sdk's SDCardManager, already mounted
 * by the board (media_sd.cpp); on the development machine (tools/c64host)
 * it is a folder of plain files, C64_ROOT or the working directory.
 */
#include "retro_c64_drivers.h"
#include "Config.h"

#ifdef ARDUINO

#include <SDCardManager.h>

struct Impl {
    FsFile f;
    FsFile dir;
};

RetroC64File::RetroC64File() : impl(new Impl) {}
RetroC64File::~RetroC64File() { close(); delete (Impl *)impl; }

bool RetroC64File::open(const std::string &path, const char *mode)
{
    Impl *p = (Impl *)impl;
    close();
    const bool w = mode && (mode[0] == 'w' || mode[0] == 'a');
    oflag_t fl = w ? (O_RDWR | O_CREAT | (mode[0] == 'a' ? O_APPEND : O_TRUNC)) : O_RDONLY;
    if (w) SDCardManager::getInstance().mkdir(Config::PATH);
    p->f = SDCardManager::getInstance().open(path.c_str(), fl);
    return (bool)p->f;
}

size_t RetroC64File::read(void *b, size_t n)
{
    Impl *p = (Impl *)impl;
    if (!p->f) return 0;
    const int got = p->f.read(b, n);
    return got > 0 ? (size_t)got : 0;
}

size_t RetroC64File::write(const void *b, size_t n)
{
    Impl *p = (Impl *)impl;
    return p->f ? p->f.write(b, n) : 0;
}

bool RetroC64File::seek(long off, int origin)
{
    Impl *p = (Impl *)impl;
    if (!p->f) return false;
    uint64_t base = origin == SEEK_CUR ? p->f.curPosition() : origin == SEEK_END ? p->f.fileSize() : 0;
    return p->f.seekSet(base + off);
}

long RetroC64File::tell() const
{
    const Impl *p = (const Impl *)impl;
    return p->f ? (long)const_cast<FsFile &>(p->f).curPosition() : -1;
}

bool RetroC64File::eof()
{
    Impl *p = (Impl *)impl;
    return !p->f || p->f.available() <= 0;
}

int64_t RetroC64File::size()
{
    Impl *p = (Impl *)impl;
    return p->f ? (int64_t)p->f.fileSize() : -1;
}

void RetroC64File::close()
{
    Impl *p = (Impl *)impl;
    if (p->f) { p->f.sync(); p->f.close(); }
}

bool RetroC64File::listnextentry(std::string &name, bool start)
{
    Impl *p = (Impl *)impl;
    if (start) {
        if (p->dir) p->dir.close();
        p->dir = SDCardManager::getInstance().open(Config::PATH);
        if (!p->dir || !p->dir.isDirectory()) return false;
    }
    if (!p->dir) return false;
    FsFile e;
    char n[64];
    while (e.openNext(&p->dir, O_RDONLY)) {
        e.getName(n, sizeof n);
        const bool skip = e.isDirectory() || n[0] == '.';
        e.close();
        if (skip) continue;
        name = n;
        return true;
    }
    p->dir.close();
    return false;
}

#else  /* the development machine */

#include <cstdlib>
#include <dirent.h>

struct Impl {
    FILE *f = nullptr;
    DIR *dir = nullptr;
};

static std::string host_path(const std::string &path)
{
    const char *root = getenv("C64_ROOT");
    return std::string(root ? root : ".") + path;
}

RetroC64File::RetroC64File() : impl(new Impl) {}
RetroC64File::~RetroC64File() { close(); delete (Impl *)impl; }

bool RetroC64File::open(const std::string &path, const char *mode)
{
    Impl *p = (Impl *)impl;
    close();
    p->f = fopen(host_path(path).c_str(), mode);
    return p->f != nullptr;
}

size_t RetroC64File::read(void *b, size_t n) { Impl *p = (Impl *)impl; return p->f ? fread(b, 1, n, p->f) : 0; }
size_t RetroC64File::write(const void *b, size_t n) { Impl *p = (Impl *)impl; return p->f ? fwrite(b, 1, n, p->f) : 0; }
bool RetroC64File::seek(long off, int origin) { Impl *p = (Impl *)impl; return p->f && fseek(p->f, off, origin) == 0; }
long RetroC64File::tell() const { const Impl *p = (const Impl *)impl; return p->f ? ftell(p->f) : -1; }
bool RetroC64File::eof() { Impl *p = (Impl *)impl; return !p->f || feof(p->f); }

int64_t RetroC64File::size()
{
    Impl *p = (Impl *)impl;
    if (!p->f) return -1;
    const long at = ftell(p->f);
    fseek(p->f, 0, SEEK_END);
    const long n = ftell(p->f);
    fseek(p->f, at, SEEK_SET);
    return n;
}

void RetroC64File::close() { Impl *p = (Impl *)impl; if (p->f) { fclose(p->f); p->f = nullptr; } }

bool RetroC64File::listnextentry(std::string &name, bool start)
{
    Impl *p = (Impl *)impl;
    if (start) {
        if (p->dir) closedir(p->dir);
        p->dir = opendir(host_path(Config::PATH).c_str());
    }
    if (!p->dir) return false;
    while (struct dirent *e = readdir(p->dir)) {
        if (e->d_name[0] == '.' || e->d_type == DT_DIR) continue;
        name = e->d_name;
        return true;
    }
    closedir(p->dir);
    p->dir = nullptr;
    return false;
}

#endif
