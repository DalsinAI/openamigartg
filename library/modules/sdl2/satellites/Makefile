# Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
#
# SDL 2's satellite libraries. build.sh unpacks the pinned sources (WORK)
# and calls this Makefile.
#
# SDL2_image and SDL2_mixer are shared, resident modules beside SDL2.module
# (stubs/sdl2/sat_module.h): SDL2_image.module and SDL2_mixer.module for
# LIBS:OpenGPU/, with libSDL2_image.a and libSDL2_mixer.a their link stubs
# (programs link -lSDL2_image -lSDL2). The same code, in the program, is
# libSDL2_image_static.a and libSDL2_mixer_static.a, for programs on
# libSDL2_static.a (or that want no module). Both decode through the
# Team's ladders first (module/ogpu_image.c, module/ogpu_mixer.c: the x86
# or ARM64 cores through media.decode/1, then the CPU, then datatypes).
# SDL2_ttf and SDL2_net stay link libraries, libSDL2_ttf.a and
# libSDL2_net.a, calling whatever SDL the program links.
#
# Their own decoders, on this CPU:
#   SDL2_image  BMP, GIF, JPEG and PNG (stb_image), LBM (IFF ILBM), PCX, PNM,
#               QOI, SVG (nanosvg), TGA, XCF, XPM, XV; saves PNG and JPEG
#   SDL2_mixer  WAV, AIFF, VOC, Ogg Vorbis (stb_vorbis), MP3 (minimp3),
#               FLAC (dr_flac), MIDI (Timidity), and MOD, XM, S3M, IT, MED
#               and the other tracker formats through libxmp (built in)
#   SDL2_ttf    FreeType (built in, without HarfBuzz)
#   SDL2_net    bsdsocket.library, through libnix's socket calls (-lsocket)

STOVE   ?= $(HOME)/AmigaChrome/stoves/os32-gcc16/prefix
CC      := $(STOVE)/bin/m68k-amigaos-gcc
AR      := $(STOVE)/bin/m68k-amigaos-ar
RANLIB  := $(STOVE)/bin/m68k-amigaos-ranlib
CPU     ?= -m68040 -m68881
OPT     ?= -O2 -fomit-frame-pointer -mno-bitfield
HERE    := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
ROOT    := $(abspath $(HERE)../../../..)
COMMON  := $(ROOT)/library/modules/common
MODDIR  := $(HERE)module
OUT     ?= $(error set OUT)
WORK    ?= $(OUT)/sdl2-work
SDLINC  := $(OUT)/include/SDL2
SDLSRC  := $(WORK)/SDL2-2.32.10
SDK_INC ?= $(WORK)/sdk-include

IMG := $(WORK)/SDL2_image-2.8.12
MIX := $(WORK)/SDL2_mixer-2.8.2
TTF := $(WORK)/SDL2_ttf-2.24.0
NET := $(WORK)/SDL2_net-2.4.0
FT  := $(WORK)/freetype-2.14.3
XMP := $(WORK)/libxmp-4.7.3
OBJ := $(WORK)/obj-satellites

# -fno-delete-null-pointer-checks: address 0 is memory on an Amiga; without it
# GCC puts TRAP #7 (Software Failure 80000027) where it proves a pointer null,
# in place of the access.
# -fno-tree-loop-distribute-patterns: GCC 16 turns array shift-up loops into a
# memmove it expands as a forward copy.
CFLAGS = -std=gnu11 $(OPT) $(CPU) -fno-delete-null-pointer-checks -fno-tree-loop-distribute-patterns -noixemul -w \
         -I$(SDLINC) -D__AMIGAOS3__
# The Team's files: warnings on; the ladders' includes.
OWN_CFLAGS = $(filter-out -w,$(CFLAGS)) -Wall -Wno-unused-parameter -Wno-pointer-sign \
         -I$(MODDIR) -I$(HERE)include -I$(ROOT)/include
# In a module: every global through A4 (include/opengpu/module.h).
MODFLAGS = -fbaserel32 -DOGPU_SAT_MODULE -I$(COMMON) -I$(MODDIR) -I$(HERE)include -I$(ROOT)/include
IMG_LADDER = -DOGPU_IMAGE_LADDER -I$(MODDIR) -I$(HERE)include
MIX_LADDER = -DOGPU_MIXER_SERVICE -I$(MODDIR) -I$(HERE)include

# --- SDL2_image ---
IMG_SRCS = IMG.c IMG_bmp.c IMG_gif.c IMG_jpg.c IMG_lbm.c IMG_pcx.c IMG_png.c IMG_pnm.c \
           IMG_qoi.c IMG_stb.c IMG_svg.c IMG_tga.c IMG_xcf.c IMG_xpm.c IMG_xv.c IMG_xxx.c \
           IMG_avif.c IMG_jxl.c IMG_tif.c IMG_webp.c
IMG_DEFS = -DUSE_STBIMAGE -DLOAD_BMP -DLOAD_GIF -DLOAD_JPG -DLOAD_LBM -DLOAD_PCX -DLOAD_PNG \
           -DLOAD_PNM -DLOAD_QOI -DLOAD_SVG -DLOAD_TGA -DLOAD_XCF -DLOAD_XPM -DLOAD_XV \
           -DSDL_IMAGE_SAVE_PNG=1 -DSDL_IMAGE_SAVE_JPG=1
IMG_OBJS = $(addprefix $(OBJ)/img/,$(IMG_SRCS:.c=.o)) $(OBJ)/img/ogpu_image.o $(OBJ)/img/sat_service.o
IMG_MOBJS = $(addprefix $(OBJ)/mimg/,$(IMG_SRCS:.c=.o)) $(OBJ)/mimg/ogpu_image.o
$(OBJ)/img/%.o: $(IMG)/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(IMG_DEFS) $(IMG_LADDER) -I$(IMG)/include -c $< -o $@
$(OBJ)/mimg/%.o: $(IMG)/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(MODFLAGS) $(IMG_DEFS) $(IMG_LADDER) -I$(IMG)/include -c $< -o $@
$(OBJ)/img/%.o: $(MODDIR)/%.c $(MODDIR)/ogpu_image.h $(MODDIR)/sat_service.h
	@mkdir -p $(dir $@)
	$(CC) $(OWN_CFLAGS) -c $< -o $@
$(OBJ)/mimg/%.o: $(MODDIR)/%.c $(MODDIR)/ogpu_image.h $(MODDIR)/sat_service.h
	@mkdir -p $(dir $@)
	$(CC) $(OWN_CFLAGS) $(MODFLAGS) -c $< -o $@
# The GCC 16 stove's -m68040 float-to-int can save FPCR in the register a
# store indexes with (fpcr_check.py). In IMG_jpg.c (tiny_jpeg, SDL_image's
# JPEG writer) that wrote every coefficient to one place, so its JPEGs were
# flat grey; in Timidity's mix.c and playmidi.c it hits envelopes and
# pitch. Those three are built at -O0 until the stove is fixed; the build
# runs fpcr_check.py over every library.
$(OBJ)/img/IMG_jpg.o: $(IMG)/src/IMG_jpg.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -O0 $(IMG_DEFS) -I$(IMG)/include -c $< -o $@
$(OBJ)/mimg/IMG_jpg.o: $(IMG)/src/IMG_jpg.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(MODFLAGS) -O0 $(IMG_DEFS) -I$(IMG)/include -c $< -o $@

# --- libxmp (for SDL2_mixer) ---
include $(XMP)/src/Makefile
include $(XMP)/src/loaders/Makefile
include $(XMP)/src/loaders/prowizard/Makefile
include $(XMP)/src/depackers/Makefile
include $(XMP)/src/depackers/lhasa/Makefile
XMP_OBJS = $(addprefix $(OBJ)/xmp/,$(OBJS) $(PROWIZARD_OBJS) $(DEPACKER_OBJS))
XMP_MOBJS = $(addprefix $(OBJ)/mxmp/,$(OBJS) $(PROWIZARD_OBJS) $(DEPACKER_OBJS))
$(OBJ)/xmp/%.o: $(XMP)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DLIBXMP_STATIC -DHAVE_POPEN=0 -I$(XMP)/include -I$(XMP)/src -c $< -o $@
$(OBJ)/mxmp/%.o: $(XMP)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(MODFLAGS) -DLIBXMP_STATIC -DHAVE_POPEN=0 -I$(XMP)/include -I$(XMP)/src -c $< -o $@

# --- SDL2_mixer ---
MIX_SRCS = src/mixer.c src/music.c src/utils.c src/effects_internal.c src/effect_position.c \
           src/effect_stereoreverse.c src/codecs/load_aiff.c src/codecs/load_voc.c \
           src/codecs/mp3utils.c src/codecs/music_wav.c src/codecs/music_ogg_stb.c \
           src/codecs/music_drflac.c src/codecs/music_minimp3.c src/codecs/music_xmp.c \
           src/codecs/music_timidity.c src/codecs/remap_channels.c \
           $(patsubst $(MIX)/%,%,$(wildcard $(MIX)/src/codecs/timidity/*.c))
MIX_DEFS = -DMUSIC_WAV -DMUSIC_OGG -DOGG_USE_STB -DMUSIC_FLAC_DRFLAC -DMUSIC_MP3_MINIMP3 \
           -DMUSIC_MOD_XMP -DLIBXMP_STATIC -DMUSIC_MID_TIMIDITY
MIX_INC = -I$(MIX)/include -I$(MIX)/src -I$(MIX)/src/codecs -I$(XMP)/include
MIX_OBJS = $(addprefix $(OBJ)/mix/,$(MIX_SRCS:.c=.o)) $(OBJ)/mix/ogpu_mixer.o $(OBJ)/mix/sat_service.o
MIX_MOBJS = $(addprefix $(OBJ)/mmix/,$(MIX_SRCS:.c=.o)) $(OBJ)/mmix/ogpu_mixer.o
$(OBJ)/mix/%.o: $(MIX)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(MIX_DEFS) $(MIX_LADDER) $(MIX_INC) -c $< -o $@
$(OBJ)/mmix/%.o: $(MIX)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(MODFLAGS) $(MIX_DEFS) $(MIX_LADDER) $(MIX_INC) -c $< -o $@
$(OBJ)/mix/%.o: $(MODDIR)/%.c $(MODDIR)/sat_service.h
	@mkdir -p $(dir $@)
	$(CC) $(OWN_CFLAGS) $(MIX_INC) -c $< -o $@
$(OBJ)/mmix/%.o: $(MODDIR)/%.c $(MODDIR)/sat_service.h
	@mkdir -p $(dir $@)
	$(CC) $(OWN_CFLAGS) $(MODFLAGS) $(MIX_INC) -c $< -o $@
$(OBJ)/mix/src/codecs/timidity/mix.o $(OBJ)/mix/src/codecs/timidity/playmidi.o: $(OBJ)/mix/%.o: $(MIX)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -O0 $(MIX_DEFS) $(MIX_INC) -c $< -o $@
$(OBJ)/mmix/src/codecs/timidity/mix.o $(OBJ)/mmix/src/codecs/timidity/playmidi.o: $(OBJ)/mmix/%.o: $(MIX)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(MODFLAGS) -O0 $(MIX_DEFS) $(MIX_INC) -c $< -o $@

# --- FreeType (for SDL2_ttf) ---
FT_SRCS = src/base/ftsystem.c src/base/ftinit.c src/base/ftdebug.c src/base/ftbase.c \
          src/base/ftbbox.c src/base/ftbitmap.c src/base/ftglyph.c src/base/ftstroke.c \
          src/base/ftsynth.c src/base/ftmm.c \
          src/autofit/autofit.c src/truetype/truetype.c src/type1/type1.c src/cff/cff.c \
          src/cid/type1cid.c src/psaux/psaux.c src/psnames/psnames.c src/pshinter/pshinter.c \
          src/sfnt/sfnt.c src/smooth/smooth.c src/raster/raster.c src/gzip/ftgzip.c
FT_DEFS = -DFT2_BUILD_LIBRARY '-DFT_CONFIG_MODULES_H=<ftmodule.h>'
FT_OBJS = $(addprefix $(OBJ)/ft/,$(FT_SRCS:.c=.o))
$(OBJ)/ft/%.o: $(FT)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(FT_DEFS) -I$(HERE) -I$(FT)/include -c $< -o $@

# --- SDL2_ttf ---
TTF_OBJS = $(OBJ)/ttf/SDL_ttf.o
$(OBJ)/ttf/SDL_ttf.o: $(TTF)/SDL_ttf.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DTTF_USE_HARFBUZZ=0 -I$(TTF) -I$(FT)/include -c $< -o $@

# --- SDL2_net ---
NET_OBJS = $(addprefix $(OBJ)/net/,SDLnet.o SDLnetTCP.o SDLnetUDP.o SDLnetselect.o) $(OBJ)/net/net_shim.o
$(OBJ)/net/net_shim.o: $(HERE)net_shim.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
# libnix's select() never sees a socket that has data (net_shim.c): SDL2_net
# calls net_shim.c's ogpu_net_select instead.
$(OBJ)/net/%.o: $(NET)/src/%.c $(HERE)Makefile
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Dselect=ogpu_net_select -I$(NET)/include -c $< -o $@

.PHONY: satellites headers abi
satellites: headers abi $(OUT)/SDL2_image.module $(OUT)/SDL2_mixer.module \
            $(OUT)/libSDL2_image.a $(OUT)/libSDL2_mixer.a \
            $(OUT)/libSDL2_image_static.a $(OUT)/libSDL2_mixer_static.a $(OUT)/libSDL2_ttf.a $(OUT)/libSDL2_net.a \
            $(OUT)/tests/sdl2/satcheck $(OUT)/tests/sdl2/satcheck.m $(OUT)/tests/sdl2/satladder.m

# Their headers beside SDL's, as programs include them: <SDL2/SDL_image.h>
headers:
	@mkdir -p $(SDLINC)
	cp $(IMG)/include/SDL_image.h $(MIX)/include/SDL_mixer.h $(TTF)/SDL_ttf.h $(NET)/include/SDL_net.h $(SDLINC)/

# The modules' ABI (abi/*.h) must have every function the headers declare,
# in the order the modules have always filled their stubs' tables.
abi:
	python3 $(HERE)gen_sat_procs.py $(IMG)/include/SDL_image.h $(HERE)abi/SDL_image_procs.h
	python3 $(HERE)gen_sat_procs.py $(MIX)/include/SDL_mixer.h $(HERE)abi/SDL_mixer_procs.h

# --- The modules: library/modules/common's start, the satellite's SDL calls
# (module/sat_sdl.c, through the program's SDL), the library. A shared
# module reaches its data only through A4 (tools/baserel_check.py).
MCOMMON = $(OBJ)/mcommon/module_start.o $(OBJ)/mcommon/module_rt.o $(OBJ)/mcommon/sat_sdl.o \
          $(OBJ)/mcommon/sat_entry.o $(OBJ)/mcommon/sat_service.o
$(OBJ)/mcommon/module_start.o: $(COMMON)/module_start.S
	@mkdir -p $(dir $@)
	$(CC) $(CPU) $(MODFLAGS) -c $< -o $@
$(OBJ)/mcommon/sat_entry.o: $(MODDIR)/sat_entry.S
	@mkdir -p $(dir $@)
	$(CC) $(CPU) $(MODFLAGS) -c $< -o $@
$(OBJ)/mcommon/module_rt.o: $(COMMON)/module_rt.c $(COMMON)/module_rt.h
	@mkdir -p $(dir $@)
	$(CC) $(OWN_CFLAGS) $(MODFLAGS) -c $< -o $@
$(OBJ)/mcommon/sat_sdl.o: $(MODDIR)/sat_sdl.c $(MODDIR)/sat_rt.h
	@mkdir -p $(dir $@)
	$(CC) $(OWN_CFLAGS) $(MODFLAGS) -I$(SDLSRC)/src/dynapi -c $< -o $@
$(OBJ)/mcommon/sat_service.o: $(MODDIR)/sat_service.c $(MODDIR)/sat_service.h
	@mkdir -p $(dir $@)
	$(CC) $(OWN_CFLAGS) $(MODFLAGS) -c $< -o $@
$(OBJ)/mimg/sdl2_image_module.o: $(MODDIR)/sdl2_image_module.c $(MODDIR)/sat_module_body.h $(HERE)abi/SDL_image_procs.h
	@mkdir -p $(dir $@)
	$(CC) $(OWN_CFLAGS) $(MODFLAGS) -I$(ROOT)/stubs/sdl2 -I$(HERE)abi -c $< -o $@
$(OBJ)/mmix/sdl2_mixer_module.o: $(MODDIR)/sdl2_mixer_module.c $(MODDIR)/sat_module_body.h $(HERE)abi/SDL_mixer_procs.h
	@mkdir -p $(dir $@)
	$(CC) $(OWN_CFLAGS) $(MODFLAGS) -I$(ROOT)/stubs/sdl2 -I$(HERE)abi -c $< -o $@

define SAT_MODULE_LINK
	$(CC) $(CPU) -fbaserel32 -resident32 -noixemul -nostartfiles -Wl,-Map,$(WORK)/$(notdir $@).map -o $@ $^ -lm -lamiga
	python3 $(ROOT)/tools/baserel_check.py $(STOVE)/bin/m68k-amigaos- $@ $(WORK)/$(notdir $@).map --allow $(HERE)baserel.allow
	@echo "built $@ ($$(wc -c < $@) bytes)"
endef
$(OUT)/SDL2_image.module: $(MCOMMON) $(OBJ)/mimg/sdl2_image_module.o $(IMG_MOBJS)
	$(SAT_MODULE_LINK)
$(OUT)/SDL2_mixer.module: $(MCOMMON) $(OBJ)/mmix/sdl2_mixer_module.o $(MIX_MOBJS) $(XMP_MOBJS)
	$(SAT_MODULE_LINK)

# --- The stubs (stubs/sdl2/sat_stub.h), built -ffixed-a4 as libSDL2.a is.
STUB_CFLAGS = -std=gnu11 $(OPT) $(CPU) -fno-delete-null-pointer-checks -noixemul -Wall -Wno-unused-parameter \
              -ffixed-a4 -I$(SDLINC) -I$(SDLSRC)/src/dynapi -I$(SDK_INC) -I$(ROOT)/include -I$(ROOT)/stubs/sdl2 -I$(HERE)abi
$(OBJ)/stub/%_stub.o: $(ROOT)/stubs/sdl2/%_stub.c $(ROOT)/stubs/sdl2/sat_stub.h $(ROOT)/stubs/sdl2/sat_module.h | headers
	@mkdir -p $(dir $@)
	$(CC) $(STUB_CFLAGS) -c $< -o $@
$(OUT)/libSDL2_image.a: $(OBJ)/stub/SDL2_image_stub.o
	@rm -f $@; $(AR) rcs $@ $^; $(RANLIB) $@; echo "built $@"
$(OUT)/libSDL2_mixer.a: $(OBJ)/stub/SDL2_mixer_stub.o
	@rm -f $@; $(AR) rcs $@ $^; $(RANLIB) $@; echo "built $@"

# tests/sdl2/satcheck.c: each library once (satcheck all in the program on
# libSDL2_static.a; satcheck.m on the modules). satladder.c: who decodes
# what, and how long it takes (on the modules).
SAT_STATIC = -lSDL2_image_static -lSDL2_mixer_static -lSDL2_ttf -lSDL2_net
SAT_MODULES = -lSDL2_image -lSDL2_mixer -lSDL2_ttf -lSDL2_net
TESTSRC = $(ROOT)/tests/sdl2
$(OBJ)/%.o: $(TESTSRC)/%.c | headers
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/tests/sdl2/satcheck: $(OBJ)/satcheck.o $(OUT)/libSDL2_image_static.a $(OUT)/libSDL2_mixer_static.a $(OUT)/libSDL2_ttf.a $(OUT)/libSDL2_net.a
	@mkdir -p $(dir $@)
	$(CC) $(CPU) -noixemul -o $@ $< -L$(OUT) $(SAT_STATIC) -lSDL2_static -lsocket -lm -lamiga
$(OUT)/tests/sdl2/satcheck.m: $(OBJ)/satcheck.o $(OUT)/libSDL2_image.a $(OUT)/libSDL2_mixer.a $(OUT)/libSDL2_ttf.a $(OUT)/libSDL2_net.a
	@mkdir -p $(dir $@)
	$(CC) $(CPU) -noixemul -o $@ $< -L$(OUT) $(SAT_MODULES) -lSDL2 -lsocket -lm -lamiga
$(OUT)/tests/sdl2/satladder.m: $(OBJ)/satladder.o $(OUT)/libSDL2_image.a $(OUT)/libSDL2_mixer.a
	@mkdir -p $(dir $@)
	$(CC) $(CPU) -noixemul -o $@ $< -L$(OUT) -lSDL2_image -lSDL2_mixer -lSDL2 -lm -lamiga

$(OUT)/libSDL2_image_static.a: $(IMG_OBJS)
	@rm -f $@; $(AR) rcs $@ $^; $(RANLIB) $@; echo "built $@"
$(OUT)/libSDL2_mixer_static.a: $(MIX_OBJS) $(XMP_OBJS)
	@rm -f $@; $(AR) rcs $@ $^; $(RANLIB) $@; echo "built $@"
$(OUT)/libSDL2_ttf.a: $(TTF_OBJS) $(FT_OBJS)
	@rm -f $@; $(AR) rcs $@ $^; $(RANLIB) $@; echo "built $@"
$(OUT)/libSDL2_net.a: $(NET_OBJS)
	@rm -f $@; $(AR) rcs $@ $^; $(RANLIB) $@; echo "built $@"
