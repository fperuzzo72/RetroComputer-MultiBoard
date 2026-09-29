# tools/eink_test - the e-ink boards' touch and picture path on the
# development machine. See eink_test.c. EINK=PAPERS3 builds everything for
# the PaperS3 instead of the Paper Mono (make -B when switching).
#
#   make -C tools/eink_test
#   ./tools/eink_test/eink_test roms/mac/macplus.rom roms/mac/boot.img /tmp/pm
#   ./tools/eink_test/screens /tmp/pm
#
# Same resolution and memory as [env:papermono]; change them together.

ROOT = ../..
UMAC = $(ROOT)/lib/umac
BOARD = $(ROOT)/src/boards/eink

EINK ?= PAPERMONO
MEMSIZE ?= 4096
DISP_WIDTH ?= 512
DISP_HEIGHT ?= 342

SRCS = eink_test.c $(BOARD)/trackpad.c \
       $(ROOT)/src/mac/mac_core.c \
       $(wildcard $(UMAC)/src/*.c) \
       $(UMAC)/musashi/m68kcpu.c $(UMAC)/musashi/m68kops.c \
       $(UMAC)/musashi/m68kdasm.c $(UMAC)/musashi/softfloat/softfloat.c

CFLAGS = -O2 -I$(ROOT)/src/mac -I$(BOARD) -DEINK_BOARD_$(EINK)=1 -I$(UMAC)/include -I$(UMAC)/musashi \
         -I$(UMAC)/musashi/softfloat \
         -DUMAC_MEMSIZE=$(MEMSIZE) -DDISP_WIDTH=$(DISP_WIDTH) -DDISP_HEIGHT=$(DISP_HEIGHT) -w

all: eink_test screens zx msx

eink_test: $(SRCS) $(BOARD)/picture.h $(BOARD)/trackpad.h
	$(CC) $(CFLAGS) $(SRCS) -lz -lm -o $@

# The menus and the 8-bit picture path: ./screens /tmp/pm draws them to PNG.
FONT = $(ROOT)/freeink-sdk/libs/ui/FreeInkUI/include
screens: screens.c $(BOARD)/canvas.cpp $(BOARD)/ui.cpp $(BOARD)/display8.c $(BOARD)/picture.h
	$(CXX) -O1 -w -std=c++17 -I$(BOARD) -DEINK_BOARD_$(EINK)=1 -I$(FONT) -c $(BOARD)/canvas.cpp -o canvas.o
	$(CXX) -O1 -w -std=c++17 -I$(BOARD) -DEINK_BOARD_$(EINK)=1 -c $(BOARD)/ui.cpp -o ui.o
	$(CC) -O1 -w -I$(BOARD) -DEINK_BOARD_$(EINK)=1 -c $(BOARD)/display8.c -o display8.o
	$(CC) -O1 -w -I$(BOARD) -DEINK_BOARD_$(EINK)=1 -c screens.c -o screens.o
	$(CXX) screens.o canvas.o ui.o display8.o -lz -o $@
	rm -f canvas.o ui.o display8.o screens.o

clean:
	rm -f eink_test screens zx msx

# The MSX and the Spectrum through the Paper Mono's picture path, with
# tools/hostshim for the ESP-IDF headers: ./zx 8 /tmp/zx 4 draws the eighth
# built-in snapshot at four seconds. Their ROMs and snapshots come from the
# same gitignored generated files the firmware uses.
SHIM = $(ROOT)/tools/hostshim
HOSTM = -O2 -w -pthread -I$(SHIM) -I$(ROOT)/src -I$(BOARD) -DEINK_BOARD_$(EINK)=1 -I$(ROOT)/lib/z80
SPEC = $(ROOT)/src/spectrum
FMSX = $(ROOT)/lib/fmsx_core

zx: machines.c $(BOARD)/display8.c $(wildcard $(SPEC)/*.c)
	$(CC) $(HOSTM) -DHAVE_MACHINE_SPECTRUM -DLSB_FIRST -DHAVE_SPECTRUM_ROM -DHAVE_SPECTRUM_SNAPS \
	    -DHAVE_SPECTRUM_TAPES -I$(SPEC) machines.c $(ROOT)/src/machines.c $(BOARD)/display8.c \
	    $(filter-out $(SPEC)/spectrum_help.cpp,$(wildcard $(SPEC)/*.c)) -lz -o $@

MSX_CORE = $(FMSX)/fMSX/MSX.c $(FMSX)/fMSX/V9938.c $(FMSX)/fMSX/I8251.c $(FMSX)/fMSX/Patch.c \
           $(FMSX)/EMULib/AY8910.c $(FMSX)/EMULib/SCC.c $(FMSX)/EMULib/YM2413.c $(FMSX)/EMULib/I8255.c \
           $(FMSX)/EMULib/WD1793.c $(FMSX)/EMULib/FDIDisk.c $(FMSX)/EMULib/Floppy.c $(FMSX)/EMULib/Hunt.c \
           $(FMSX)/EMULib/IPS.c $(FMSX)/EMULib/MCF.c $(FMSX)/EMULib/SHA1.c $(FMSX)/EMULib/Sound.c \
           $(FMSX)/video/video_glue.c $(ROOT)/lib/z80/Z80.c
msx: machines.c $(BOARD)/display8.c $(BOARD)/audio.c $(wildcard $(ROOT)/src/msx/*.c)
	$(CC) $(HOSTM) -DHAVE_MACHINE_MSX -DBPS16 -DLSB_FIRST -DESP32 -DFMSX -DHAVE_LOCAL_BIOS -DHAVE_LOCAL_CART \
	    -I$(ROOT)/src/msx -I$(ROOT)/src/boards/common -I$(FMSX)/fMSX -I$(FMSX)/EMULib -I$(FMSX)/video \
	    machines.c $(ROOT)/src/machines.c $(BOARD)/display8.c $(BOARD)/audio.c \
	    $(wildcard $(ROOT)/src/msx/*.c) $(MSX_CORE) -lz -Wl,-dead_strip -o $@
