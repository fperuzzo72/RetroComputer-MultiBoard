#ifndef C64_CART_H
#define C64_CART_H

#include <cstdint>
#include <string>

/* A cartridge in the expansion port, from a .crt file. See c64_cart.cpp.
 * lib/c64's C64Sys reads `roml` at $8000 and `romh` at $A000 when the
 * memory configuration in $01 lets them show, and hands writes to $DExx
 * (IO1, where bank-switching cartridges listen) to retro_cart_io1. */
struct RetroCart {
    volatile bool on;
    const uint8_t *volatile roml;     /* the bank showing at $8000, or null */
    const uint8_t *volatile romh;     /* at $A000 (16K cartridges), or null */
};
extern RetroCart retro_cart;

void retro_cart_io1(uint16_t addr, uint8_t val);

/* Load path (under the C64's folder) into memory and plug it in. Returns
 * an empty string, or why not. */
std::string retro_cart_load(const std::string &path);

#endif
