# The Commodore 64 on the development machine: lib/c64 with src/c64's
# drivers. Needs src/c64/c64_rom_data.c (tools/make_c64_roms.py).
#
#   make -C tools/c64host
#   ./tools/c64host/c64host /tmp/c64 3          # READY. at three seconds
ROOT := ../..
LIB  := $(ROOT)/lib/c64/src
CXXFLAGS := -std=c++17 -O2 -DPLATFORM_LINUX -DBOARD_RETRO -I$(LIB) -I$(ROOT)/src/c64 -I$(ROOT)/src/boards/eink -w
SRC := c64host.cpp $(wildcard $(LIB)/*.cpp) $(LIB)/platform/PlatformFactory.cpp \
       $(LIB)/joystick/JoystickFactory.cpp $(ROOT)/src/c64/c64_keys.cpp $(ROOT)/src/c64/c64_files.cpp $(ROOT)/src/c64/c64_cart.cpp

c64host: $(SRC) $(ROOT)/src/c64/c64_rom_data.c
	$(CC) -c -O2 -o c64_rom_data.o $(ROOT)/src/c64/c64_rom_data.c
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) c64_rom_data.o -lz -lpthread

clean:
	rm -f c64host
