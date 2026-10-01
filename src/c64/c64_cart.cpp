/* c64_cart.cpp - cartridges, from .crt files.
 *
 * A .crt is a 64-byte header (hardware type, the EXROM and GAME lines)
 * and CHIP packets of ROM, each with a bank number and the address it
 * answers at. The owner's set (No-Intro's, 243 of them) is mostly the
 * plain kind, and that is what this covers:
 *
 *   type 0, EXROM low, GAME high   8K at $8000          (39 in the set)
 *   type 0, EXROM low, GAME low    16K at $8000-$BFFF   (152)
 *   type 5, Ocean                  8K banks at $8000, chosen by writing
 *                                  the bank to $DE00    (10)
 *   type 19, Magic Desk            the same, bit 7 unplugs it (6)
 *
 * Not Ultimax (EXROM high, GAME low: the MAX machine's 11), nor the
 * freezers and utilities' own schemes. The C64 is reset with the
 * cartridge in, so the KERNAL finds its CBM80 signature and starts it.
 */
#include "c64_cart.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "Config.h"
#include "retro_c64_drivers.h"

#ifdef ARDUINO
#include "esp_heap_caps.h"
static uint8_t *big(size_t n) { return (uint8_t *)heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
#else
static uint8_t *big(size_t n) { return (uint8_t *)malloc(n); }
#endif

RetroCart retro_cart;

struct Bank { const uint8_t *roml = nullptr, *romh = nullptr; };
static std::vector<Bank> banks;
static int type = -1;

static void show(int b)
{
    if (b < 0 || b >= (int)banks.size()) return;
    retro_cart.roml = banks[b].roml;
    retro_cart.romh = banks[b].romh;
}

void retro_cart_io1(uint16_t addr, uint8_t val)
{
    (void)addr;
    if (type == 5) show(val & 0x3f);                         /* Ocean */
    else if (type == 19) {                                   /* Magic Desk */
        retro_cart.on = !(val & 0x80);
        show(val & 0x7f);
    }
}

static unsigned be16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }
static unsigned long be32(const uint8_t *p) { return (unsigned long)be16(p) << 16 | be16(p + 2); }

std::string retro_cart_load(const std::string &path)
{
    RetroC64File f;
    if (!f.open(Config::PATH + path, "rb")) return "cannot open " + path;
    const int64_t size = f.size();
    if (size < 0x40 || size > 2 * 1024 * 1024) return "not a cartridge (size)";
    uint8_t *data = big((size_t)size);
    if (!data) return "no memory for it";
    if ((int64_t)f.read(data, (size_t)size) != size) return "could not read it";
    f.close();
    if (memcmp(data, "C64 CARTRIDGE   ", 16)) return "not a .crt";
    const unsigned long hdr = be32(data + 0x10);
    type = (int)be16(data + 0x16);
    const int exrom = data[0x18], game = data[0x19];
    if (type != 0 && type != 5 && type != 19) return "hardware type " + std::to_string(type) + " not supported";
    if (exrom) return "Ultimax cartridges not supported";

    banks.clear();
    for (unsigned long at = hdr; at + 0x10 <= (unsigned long)size;) {
        const uint8_t *c = data + at;
        if (memcmp(c, "CHIP", 4)) break;
        const unsigned long len = be32(c + 4);
        const unsigned bank = be16(c + 0x0a), load = be16(c + 0x0c), rom = be16(c + 0x0e);
        const uint8_t *rd = c + 0x10;
        if (bank >= 64 || len < 0x10 || at + len > (unsigned long)size) break;
        if (banks.size() <= bank) banks.resize(bank + 1);
        if (load == 0x8000) {
            banks[bank].roml = rd;
            if (rom > 0x2000) banks[bank].romh = rd + 0x2000;   /* one 16K chip */
        } else if (load == 0xa000 || load == 0xe000) {
            banks[bank].romh = rd;
        }
        at += len;
    }
    if (banks.empty() || !banks[0].roml) return "no ROM at $8000";
    if (game) for (auto &b : banks) b.romh = nullptr;         /* 8K mode */
    show(0);
    retro_cart.on = true;
    printf("c64: cartridge type %d, %s, %u bank(s)\n", type, game ? "8K" : "16K", (unsigned)banks.size());
    return "";
}
