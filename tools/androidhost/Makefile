# tools/androidhost: the Android board on the Mac. See androidhost.c.
ROOT = ../..
BOARD = $(ROOT)/src/boards/android
FMSX = $(ROOT)/lib/fmsx_core
MSX_CORE = $(FMSX)/fMSX/MSX.c $(FMSX)/fMSX/V9938.c $(FMSX)/fMSX/I8251.c $(FMSX)/fMSX/Patch.c \
           $(FMSX)/EMULib/AY8910.c $(FMSX)/EMULib/SCC.c $(FMSX)/EMULib/YM2413.c $(FMSX)/EMULib/I8255.c \
           $(FMSX)/EMULib/WD1793.c $(FMSX)/EMULib/FDIDisk.c $(FMSX)/EMULib/Floppy.c $(FMSX)/EMULib/Hunt.c \
           $(FMSX)/EMULib/IPS.c $(FMSX)/EMULib/MCF.c $(FMSX)/EMULib/SHA1.c $(FMSX)/EMULib/Sound.c \
           $(FMSX)/video/video_glue.c $(ROOT)/lib/z80/Z80.c
BOARD_SRC = $(BOARD)/retro_board.c $(BOARD)/nvs_file.c $(BOARD)/audio_none.c $(BOARD)/beeper.c
SPEC = $(ROOT)/src/spectrum
SPEC_SRC = $(filter-out $(SPEC)/spectrum_help.cpp,$(wildcard $(SPEC)/*.c))
# The PC: lib/pc8086 and src/pc, the font if tools/make_pc_font.py made it.
PC = $(ROOT)/src/pc
PC_C = $(wildcard $(ROOT)/lib/pc8086/src/*.c) $(PC)/pc_core.c $(PC)/pc_keys.c $(PC)/pc_text.c \
       $(PC)/pc_speaker.c $(wildcard $(PC)/pc_font_data.c)
PC_CXX = $(PC)/pc_files.cpp $(PC)/pc_machine.cpp
C64L = $(ROOT)/lib/c64/src
C64_CXX = $(wildcard $(C64L)/*.cpp) $(C64L)/platform/PlatformFactory.cpp $(C64L)/joystick/JoystickFactory.cpp \
          $(ROOT)/src/c64/c64_keys.cpp $(ROOT)/src/c64/c64_files.cpp $(ROOT)/src/c64/c64_cart.cpp \
          $(ROOT)/src/c64/c64_machine.cpp
PC_FLAGS = -DHAVE_MACHINE_C64 -DHAVE_MACHINE_PC -DPC8086_LEAN $(if $(wildcard $(PC)/pc_font_data.c),-DHAVE_PC_FONT) -I$(PC)
CFLAGS = -O2 -w -pthread -I$(BOARD)/shim -I$(BOARD) -I$(ROOT)/src -I$(ROOT)/src/boards/common -I$(ROOT)/lib/z80 \
         -DHAVE_MACHINE_MSX -DHAVE_MACHINE_SPECTRUM -DHAVE_SPECTRUM_ROM -DHAVE_SPECTRUM_SNAPS -I$(SPEC) -DBPS16 -DLSB_FIRST -DESP32 -DFMSX -DHAVE_LOCAL_BIOS -DHAVE_LOCAL_CART \
         -I$(ROOT)/src/msx -I$(FMSX)/fMSX -I$(FMSX)/EMULib -I$(FMSX)/video

androidhost: androidhost.c $(BOARD_SRC) $(wildcard $(BOARD)/*.h $(BOARD)/shim/*.h)
	rm -rf obj && mkdir -p obj
	cd obj && $(CXX) -std=c++17 -O2 -w -I../$(BOARD)/shim -I../$(BOARD) -I../$(ROOT)/src -I../$(PC) $(PC_FLAGS) \
	    -c $(addprefix ../,$(PC_CXX))
	cd obj && $(CXX) -std=c++17 -O2 -w -DPLATFORM_LINUX -DBOARD_RETRO -DRETRO_BOARD_ANDROID -DHAVE_C64_ROMS \
	    -I../$(C64L) -I../$(ROOT)/src/c64 -I../$(BOARD)/shim -I../$(BOARD) -I../$(ROOT)/src -c $(addprefix ../,$(C64_CXX))
	cd obj && $(CC) -O2 -w -c ../$(ROOT)/src/c64/c64_rom_data.c
	$(CC) $(CFLAGS) $(PC_FLAGS) androidhost.c $(BOARD_SRC) $(ROOT)/src/machines.c \
	    $(wildcard $(ROOT)/src/msx/*.c) $(MSX_CORE) $(SPEC_SRC) $(PC_C) obj/*.o -lz -lc++ -Wl,-dead_strip -o $@
	rm -rf obj

clean:
	rm -f androidhost
