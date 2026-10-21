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
CFLAGS = -O2 -w -pthread -I$(BOARD)/shim -I$(BOARD) -I$(ROOT)/src -I$(ROOT)/src/boards/common -I$(ROOT)/lib/z80 \
         -DHAVE_MACHINE_MSX -DHAVE_MACHINE_SPECTRUM -DHAVE_SPECTRUM_ROM -DHAVE_SPECTRUM_SNAPS -I$(SPEC) -DBPS16 -DLSB_FIRST -DESP32 -DFMSX -DHAVE_LOCAL_BIOS -DHAVE_LOCAL_CART \
         -I$(ROOT)/src/msx -I$(FMSX)/fMSX -I$(FMSX)/EMULib -I$(FMSX)/video

androidhost: androidhost.c $(BOARD_SRC) $(wildcard $(BOARD)/*.h $(BOARD)/shim/*.h)
	$(CC) $(CFLAGS) androidhost.c $(BOARD_SRC) $(ROOT)/src/machines.c \
	    $(wildcard $(ROOT)/src/msx/*.c) $(MSX_CORE) $(SPEC_SRC) -lz -Wl,-dead_strip -o $@

clean:
	rm -f androidhost
