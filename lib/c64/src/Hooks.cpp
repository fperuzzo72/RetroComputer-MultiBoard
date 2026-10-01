/*
 Copyright (C) 2024-2026 retroelec <retroelec42@gmail.com>

 This program is free software; you can redistribute it and/or modify it
 under the terms of the GNU General Public License as published by the
 Free Software Foundation; either version 3 of the License, or (at your
 option) any later version.

 This program is distributed in the hope that it will be useful, but
 WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 for more details.

 For the complete text of the GNU General Public License see
 http://www.gnu.org/licenses/.
*/
#include "Hooks.h"

#include "C64Sys.h"
#include "platform/PlatformManager.h"
#include <cstdint>

static const char *TAG = "Hooks";

static const uint16_t IECINHOOK = 0xee13;
static const uint16_t IECOUTHOOK = 0xed40;
static const uint16_t IECWAIT4CLKHOOK = 0xedcc;

void Hooks::init(uint8_t *ram, C64Sys *cpu) {
  this->ram = ram;
  this->cpu = cpu;
}

bool Hooks::handlehooks(uint16_t pc) {
  if (pc == IECINHOOK + 1) {
    uint8_t a = cpu->floppy.iecin();
    // PlatformManager::getInstance().log(LOG_INFO, TAG, "iecin hook: %x", a);
    cpu->setA(a);
    ram[0xa4] = a;
    ram[0xa5] = 0;
    ram[0x90] = cpu->floppy.lastStatus;
    cpu->setPC(0xee82);
    return true;
  } else if (pc == IECOUTHOOK + 1) {
    uint8_t a = ram[0x95];
    /* NOT UPSTREAM: tell a SAVE's data from bus commands, so the drive can
     * write it (Floppy.cpp, "saving"). $DD00 cannot say: this hook skips
     * the code that drives ATN. Where the send routine was called from
     * can. The KERNAL reaches $ED40 by JSR only to send data, from CIOUT
     * ($EDE7) and when it flushes the last buffered byte ahead of a
     * command ($ED19); command bytes fall through into it from $ED36. So
     * a return address of $ED1B or $EDE9 on the stack is data. The
     * per-byte log line is gone with it: a SAVE sends thousands. */
    const uint8_t sp = cpu->getSP();
    const uint16_t ret = ram[0x100 + (uint8_t)(sp + 1)] |
                         (ram[0x100 + (uint8_t)(sp + 2)] << 8);
    const bool data = ret == 0xed1b || ret == 0xede9;
    cpu->floppy.iecout(a, !data);
    ram[0xa5] = 0;
    ram[0x90] = cpu->floppy.lastStatus;
    cpu->setPC(0xee82);
    return true;
  } else if (pc == IECWAIT4CLKHOOK + 1) {
    PlatformManager::getInstance().log(LOG_INFO, TAG, "wait4clk hook");
    cpu->setPC(0xeddb);
    return true;
  }
  return false;
}
