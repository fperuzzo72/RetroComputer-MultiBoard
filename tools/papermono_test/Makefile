# tools/papermono_test - the Paper Mono's touch and picture path on the
# development machine. See papermono_test.c.
#
#   make -C tools/papermono_test
#   ./tools/papermono_test/papermono_test roms/mac/macplus.rom roms/mac/boot.img /tmp/pm
#   ./tools/papermono_test/screens /tmp/pm
#
# Same resolution and memory as [env:papermono]; change them together.

ROOT = ../..
UMAC = $(ROOT)/lib/umac
BOARD = $(ROOT)/src/boards/papermono

MEMSIZE ?= 4096
DISP_WIDTH ?= 512
DISP_HEIGHT ?= 342

SRCS = papermono_test.c $(BOARD)/trackpad.c \
       $(ROOT)/src/mac/mac_core.c \
       $(wildcard $(UMAC)/src/*.c) \
       $(UMAC)/musashi/m68kcpu.c $(UMAC)/musashi/m68kops.c \
       $(UMAC)/musashi/m68kdasm.c $(UMAC)/musashi/softfloat/softfloat.c

CFLAGS = -O2 -I$(ROOT)/src/mac -I$(BOARD) -I$(UMAC)/include -I$(UMAC)/musashi \
         -I$(UMAC)/musashi/softfloat \
         -DUMAC_MEMSIZE=$(MEMSIZE) -DDISP_WIDTH=$(DISP_WIDTH) -DDISP_HEIGHT=$(DISP_HEIGHT) -w

all: papermono_test screens

papermono_test: $(SRCS) $(BOARD)/picture.h $(BOARD)/trackpad.h
	$(CC) $(CFLAGS) $(SRCS) -lz -lm -o $@

# The menus and the 8-bit picture path: ./screens /tmp/pm draws them to PNG.
FONT = $(ROOT)/freeink-sdk/libs/ui/FreeInkUI/include
screens: screens.c $(BOARD)/canvas.cpp $(BOARD)/ui.cpp $(BOARD)/display8.c $(BOARD)/picture.h
	$(CXX) -O1 -w -std=c++17 -I$(BOARD) -I$(FONT) -c $(BOARD)/canvas.cpp -o canvas.o
	$(CXX) -O1 -w -std=c++17 -I$(BOARD) -c $(BOARD)/ui.cpp -o ui.o
	$(CC) -O1 -w -I$(BOARD) -c $(BOARD)/display8.c -o display8.o
	$(CC) -O1 -w -I$(BOARD) -c screens.c -o screens.o
	$(CXX) screens.o canvas.o ui.o display8.o -lz -o $@
	rm -f canvas.o ui.o display8.o screens.o

clean:
	rm -f papermono_test screens
