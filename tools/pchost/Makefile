# tools/pchost: the PC on the development machine. See pchost.c.
ROOT := ../..
CORE := $(ROOT)/lib/pc8086/src
SRC := $(wildcard $(CORE)/*.c) $(ROOT)/src/pc/pc_core.c $(ROOT)/src/pc/pc_keys.c $(ROOT)/src/pc/pc_text.c \
       $(ROOT)/src/pc/pc_speaker.c pchost.c
# the full font, when tools/make_pc_font.py has made it
FONT := $(wildcard $(ROOT)/src/pc/pc_font_data.c)
ifneq ($(FONT),)
SRC += $(FONT)
override PCFLAGS += -DHAVE_PC_FONT
endif
CFLAGS := -O2 -g $(PCFLAGS) -I shim -I $(ROOT)/src/pc -Wno-unused-function -w

pchost: $(SRC) $(ROOT)/src/pc/pc_files.cpp $(wildcard $(CORE)/*.h) $(ROOT)/src/pc/*.h
	cc $(CFLAGS) -c $(SRC)
	c++ -O2 -g -I shim -I $(CORE) -I $(ROOT)/src/pc -c $(ROOT)/src/pc/pc_files.cpp
	c++ -o $@ *.o
	rm -f *.o

clean:
	rm -f pchost *.o
