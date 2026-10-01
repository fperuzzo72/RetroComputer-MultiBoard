#ifndef RETRO_C64_DRIVERS_H
#define RETRO_C64_DRIVERS_H

/* The drivers lib/c64 (retroelec's T-HMI-C64 core) is built with here.
 *
 * Its Config.h, under BOARD_RETRO, points every factory at these: a
 * display that hands the VIC's picture to this project's panel code, a
 * keyboard that is a whole key matrix filled from a BLE keyboard's
 * reports (C64Sys::getDC01 reads it through retro_c64_matrix_read), a
 * sound driver that feeds the board's buzzer, and files on the card.
 * The same header serves the development machine (tools/c64host), where
 * files are plain files and the picture is written out as a PNG.
 */
#include <cstdint>
#include <cstdio>
#include <string>

#include "display/DisplayDriver.h"
#include "fs/FileDriver.h"
#include "keyboard/KeyboardDriver.h"
#include "sound/SoundDriver.h"

/* c64_machine.cpp */
void retro_c64_picture(const uint8_t *bitmap, uint8_t border);
void retro_c64_audio(const int16_t *samples, size_t n);

class RetroC64Display : public DisplayDriver {
public:
  void init() override {}
  void drawFrame(uint8_t frameColor) override { border = frameColor & 15; }
  void drawBitmap(const uint8_t *bitmap) override { retro_c64_picture(bitmap, border); }
  void drawBitmap(const uint8_t *bitmap, const uint8_t *vicreg) override {
    retro_c64_picture(bitmap, vicreg ? (vicreg[0x20] & 15) : border);
  }
  void drawBitmap(const uint8_t *bitmap, const uint8_t *vicreg, const uint8_t *ram) override {
    (void)ram;
    retro_c64_picture(bitmap, vicreg ? (vicreg[0x20] & 15) : border);
  }

private:
  uint8_t border = 14;
};

/* The matrix is c64_keys.cpp's; the core only ever asks it through
 * retro_c64_matrix_read, so this is all the interface needs. */
class RetroC64Keyboard : public KeyboardDriver {
public:
  void init() override {}
  void scanKeyboard() override {}
  uint8_t getKBCodeDC01() override { return 0xff; }
  uint8_t getKBCodeDC00() override { return 0xff; }
  uint8_t getShiftctrlcode() override { return 0; }
  uint8_t getKBJoyValue() override;
};

class RetroC64Sound : public SoundDriver {
public:
  void init() override {}
  void playAudio(int16_t *samples, size_t size) override { retro_c64_audio(samples, size); }
};

/* Files: the board's card on the device (c64_files.cpp), stdio on the
 * development machine. */
class RetroC64File : public FileDriver {
public:
  RetroC64File();
  ~RetroC64File() override;
  bool open(const std::string &path, const char *mode) override;
  size_t read(void *buffer, size_t count) override;
  size_t write(const void *buffer, size_t count) override;
  bool seek(long offset, int origin) override;
  long tell() const override;
  bool eof() override;
  int64_t size() override;
  void close() override;
  bool listnextentry(std::string &name, bool start) override;

private:
  void *impl;
};

#endif
