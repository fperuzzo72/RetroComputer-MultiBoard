/* c64host - the Commodore 64 without a board.
 *
 * lib/c64 (T-HMI-C64's core) with this project's drivers (src/c64/), on
 * the development machine: the 6502 runs in real time on a thread, as on
 * the device, keys go in through the same HID path the BLE keyboard uses,
 * and the VIC's picture comes out as a PNG.
 *
 *   c64host <out-prefix> <seconds>[,<seconds>...] [<text>]
 *
 * <text> is typed after the first capture (\n is Return). Files for LOAD
 * are read from $C64_ROOT/c64/ (default ./c64/); D64=<name.d64> puts that
 * disc in drive 8 first.
 */
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <zlib.h>

#include "C64Sys.h"
#include "platform/PlatformFactory.h"
#include "platform/PlatformManager.h"
#include "roms/charset.h"
#include "c64_keys.h"

/* selector.h, for c64_keys.cpp's F12 */
extern "C" void selector_open(void) {}

static uint8_t picture[320 * 200];
static uint8_t border_col = 14;
static unsigned long pictures, samples;

void retro_c64_picture(const uint8_t *bitmap, uint8_t border)
{
    memcpy(picture, bitmap, sizeof picture);
    border_col = border;
    pictures++;
}

void retro_c64_audio(const int16_t *s, size_t n) { (void)s; samples += n; }

/* Pepto's palette, the usual one */
static const uint8_t pal[16][3] = {
    {0, 0, 0}, {255, 255, 255}, {104, 55, 43}, {112, 164, 178}, {111, 61, 134}, {88, 141, 67},
    {53, 40, 121}, {184, 199, 111}, {111, 79, 37}, {67, 57, 0}, {154, 103, 89}, {68, 68, 68},
    {108, 108, 108}, {154, 210, 132}, {108, 94, 181}, {149, 149, 149}};

static void put32(unsigned char *p, unsigned v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static void chunk(FILE *f, const char *t, const unsigned char *d, unsigned n)
{
    unsigned char h[8];
    put32(h, n); memcpy(h + 4, t, 4); fwrite(h, 1, 8, f);
    if (n) fwrite(d, 1, n, f);
    unsigned long c = crc32(0, (const unsigned char *)t, 4);
    if (n) c = crc32(c, d, n);
    unsigned char cc[4]; put32(cc, (unsigned)c); fwrite(cc, 1, 4, f);
}

static void png(const char *path)
{
    const int B = 32, W = 320 + 2 * B, H = 200 + 2 * B;
    std::string raw;
    for (int y = 0; y < H; y++) {
        raw.push_back(0);
        for (int x = 0; x < W; x++) {
            const bool in = x >= B && x < B + 320 && y >= B && y < B + 200;
            const uint8_t c = in ? picture[(y - B) * 320 + (x - B)] & 15 : border_col;
            raw.append((const char *)pal[c], 3);
        }
    }
    uLongf zl = compressBound(raw.size());
    std::string z(zl, 0);
    compress((Bytef *)&z[0], &zl, (const Bytef *)raw.data(), raw.size());
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    unsigned char ih[13]; put32(ih, W); put32(ih + 4, H); ih[8] = 8; ih[9] = 2; ih[10] = ih[11] = ih[12] = 0;
    chunk(f, "IHDR", ih, 13);
    chunk(f, "IDAT", (const unsigned char *)z.data(), (unsigned)zl);
    chunk(f, "IEND", nullptr, 0);
    fclose(f);
}

/* One character as a US keyboard types it. */
static void type_char(char ch)
{
    static const char plain[] = "abcdefghijklmnopqrstuvwxyz1234567890\n\x1b\b\t -=[]\\#;'`,./";
    static const char shifted[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*()\n\x1b\b\t _+{}|#:\"~<>?";
    uint8_t r[8] = {0};
    const char *p;
    if ((p = strchr(plain, ch))) r[2] = (uint8_t)(0x04 + (p - plain));
    else if ((p = strchr(shifted, ch))) { r[2] = (uint8_t)(0x04 + (p - shifted)); r[0] = 0x02; }
    else return;
    retro_c64_keys_report(r);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    uint8_t up[8] = {0};
    retro_c64_keys_report(up);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: %s <out-prefix> <seconds>[,...] [text]\n", argv[0]); return 1; }
    PlatformManager::initialize(PlatformNS::create());
    static uint8_t ram[65536];
    static C64Sys cpu;
    cpu.init(ram, charset_rom);
    retro_c64_keys_attach(&cpu);
    if (getenv("D64")) printf("  drive 8: %s %s\n", getenv("D64"), cpu.floppy.attach(getenv("D64")) ? "attached" : "NOT attached");
    PlatformManager::getInstance().startIntervalTimer([&]() { cpu.scanKeyboard(); }, 8000);
    PlatformManager::getInstance().startIntervalTimer([&]() { cpu.cia1.updateTOD(); cpu.cia2.updateTOD(); }, 100000);
    std::thread([&]() { cpu.run(); }).detach();
    std::thread([&]() {
        for (;;) {
            PlatformManager::getInstance().lock();
            cpu.vic.refresh();
            PlatformManager::getInstance().unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }).detach();

    const auto t0 = std::chrono::steady_clock::now();
    char *times = strdup(argv[2]);
    int shot = 0;
    for (char *t = strtok(times, ","); t; t = strtok(nullptr, ","), shot++) {
        const double at = atof(t);
        while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < at)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        char path[512];
        snprintf(path, sizeof path, "%s-%s.png", argv[1], t);
        png(path);
        printf("  %.1fs: %s (%lu pictures, %lu samples)\n", at, path, pictures, samples);
        if (shot == 0 && argc > 3)
            for (const char *s = argv[3]; *s; s++) {
                if (s[0] == '\\' && s[1] == 'n') { type_char('\n'); s++; }
                else type_char(*s);
            }
    }
    fflush(stdout);
    _exit(0);
}
