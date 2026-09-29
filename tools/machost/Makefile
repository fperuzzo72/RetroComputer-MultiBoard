# tools/machost - the Macintosh on the development machine. See machost.c.
#
#   make -C tools/machost
#   ./tools/machost/machost macplus.rom system.img "run 25; shot boot.png"
#
# Same resolution and memory as the Paper Mono build (platformio.ini,
# [env:papermono-mac]); change both together.

ROOT = ../..
UMAC = $(ROOT)/lib/umac

MEMSIZE ?= 4096
DISP_WIDTH ?= 512
DISP_HEIGHT ?= 342

SRCS = machost.c \
       $(ROOT)/src/mac/mac_core.c \
       $(wildcard $(UMAC)/src/*.c) \
       $(UMAC)/musashi/m68kcpu.c \
       $(UMAC)/musashi/m68kops.c \
       $(UMAC)/musashi/m68kdasm.c \
       $(UMAC)/musashi/softfloat/softfloat.c

CFLAGS = -O2 -I$(ROOT)/src/mac -I$(UMAC)/include -I$(UMAC)/musashi \
         -I$(UMAC)/musashi/softfloat \
         -DUMAC_MEMSIZE=$(MEMSIZE) -DDISP_WIDTH=$(DISP_WIDTH) -DDISP_HEIGHT=$(DISP_HEIGHT) \
         -w

machost: $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -lz -lm -o $@

clean:
	rm -f machost
