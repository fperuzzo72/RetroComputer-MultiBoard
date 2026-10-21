# tools/dosboxhost: DOSBox Pure (libretro) on the Mac. See dosboxhost.c.
# DBP is a checkout of https://codeberg.org/schelling/dosbox-pure; `make`
# there builds dosbox_pure_libretro.dylib, which dosboxhost loads.
DBP ?= $(HOME)/github/dosbox-pure

dosboxhost: dosboxhost.c
	$(CC) -O2 -Wall -I$(DBP)/libretro-common/include dosboxhost.c -lz -o $@

clean:
	rm -f dosboxhost
