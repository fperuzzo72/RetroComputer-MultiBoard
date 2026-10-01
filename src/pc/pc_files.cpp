/* pc_files.cpp - lib/pc8086's pc_file.h: the disk images. On the device
 * the board's card through freeink-sdk's SDCardManager (already mounted
 * by the board); on the development machine (tools/pchost) plain stdio
 * under PC_ROOT, or the working directory. */
#include "../../lib/pc8086/src/pc_file.h"

#ifdef ARDUINO

#include <SDCardManager.h>

struct pc_file { FsFile f; };

extern "C" pc_file *pc_fopen(const char *path, const char *mode)
{
    const bool w = strchr(mode, '+') || mode[0] == 'w';
    pc_file *p = new pc_file;
    p->f = SDCardManager::getInstance().open(path, w ? O_RDWR : O_RDONLY);
    if (!p->f) { delete p; return nullptr; }
    return p;
}
extern "C" int pc_fclose(pc_file *p) { if (!p) return -1; p->f.sync(); p->f.close(); delete p; return 0; }
extern "C" size_t pc_fread(void *b, size_t sz, size_t n, pc_file *p)
{
    const int got = p->f.read(b, sz * n);
    return got > 0 ? (size_t)got / sz : 0;
}
extern "C" size_t pc_fwrite(const void *b, size_t sz, size_t n, pc_file *p) { return p->f.write(b, sz * n) / sz; }
extern "C" int pc_fseek(pc_file *p, long off, int whence)
{
    uint64_t base = whence == SEEK_CUR ? p->f.curPosition() : whence == SEEK_END ? p->f.fileSize() : 0;
    return p->f.seekSet(base + off) ? 0 : -1;
}
extern "C" long pc_ftell(pc_file *p) { return (long)p->f.curPosition(); }
extern "C" int pc_fflush(pc_file *p) { p->f.sync(); return 0; }

#else

#include <cstdlib>
#include <string>

#undef FILE
#undef fopen
#undef fclose
#undef fread
#undef fwrite
#undef fseek
#undef ftell
#undef fflush

struct pc_file { FILE *f; };

extern "C" pc_file *pc_fopen(const char *path, const char *mode)
{
    const char *root = getenv("PC_ROOT");
    std::string full = std::string(root ? root : ".") + path;
    FILE *f = fopen(full.c_str(), mode);
    if (!f) return nullptr;
    return new pc_file{f};
}
extern "C" int pc_fclose(pc_file *p) { int r = fclose(p->f); delete p; return r; }
extern "C" size_t pc_fread(void *b, size_t sz, size_t n, pc_file *p) { return fread(b, sz, n, p->f); }
extern "C" size_t pc_fwrite(const void *b, size_t sz, size_t n, pc_file *p) { return fwrite(b, sz, n, p->f); }
extern "C" int pc_fseek(pc_file *p, long off, int whence) { return fseek(p->f, off, whence); }
extern "C" long pc_ftell(pc_file *p) { return ftell(p->f); }
extern "C" int pc_fflush(pc_file *p) { return fflush(p->f); }

#endif
