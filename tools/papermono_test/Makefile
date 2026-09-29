# tools/papermono_test - the Paper Mono's touch and picture path on the
# development machine. See papermono_test.c.
#
#   make -C tools/papermono_test
#   ./tools/papermono_test/papermono_test roms/mac/macplus.rom roms/mac/boot.img /tmp/pm
#
# Same resolution and memory as [env:papermono-mac]; change them together.

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

papermono_test: $(SRCS) $(BOARD)/picture.h $(BOARD)/trackpad.h
	$(CC) $(CFLAGS) $(SRCS) -lz -lm -o $@

clean:
	rm -f papermono_test
