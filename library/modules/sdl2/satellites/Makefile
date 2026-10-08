# Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
#
# SDL 2's satellite libraries as link libraries: libSDL2_image.a,
# libSDL2_mixer.a, libSDL2_ttf.a and libSDL2_net.a. Each calls SDL through
# whatever SDL the program links (libSDL2.a, the module's stub, or
# libSDL2_static.a). build.sh unpacks the pinned sources (WORK) and calls
# this Makefile. Their own decoders are used where they carry one:
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
OUT     ?= $(error set OUT)
WORK    ?= $(OUT)/sdl2-work
SDLINC  := $(OUT)/include/SDL2

IMG := $(WORK)/SDL2_image-2.8.12
MIX := $(WORK)/SDL2_mixer-2.8.2
TTF := $(WORK)/SDL2_ttf-2.24.0
NET := $(WORK)/SDL2_net-2.4.0
FT  := $(WORK)/freetype-2.14.3
XMP := $(WORK)/libxmp-4.7.3
OBJ := $(WORK)/obj-satellites

CFLAGS = -std=gnu11 $(OPT) $(CPU) -noixemul -w -I$(SDLINC) -D__AMIGAOS3__

# --- SDL2_image ---
IMG_SRCS = IMG.c IMG_bmp.c IMG_gif.c IMG_jpg.c IMG_lbm.c IMG_pcx.c IMG_png.c IMG_pnm.c \
           IMG_qoi.c IMG_stb.c IMG_svg.c IMG_tga.c IMG_xcf.c IMG_xpm.c IMG_xv.c IMG_xxx.c \
           IMG_avif.c IMG_jxl.c IMG_tif.c IMG_webp.c
IMG_DEFS = -DUSE_STBIMAGE -DLOAD_BMP -DLOAD_GIF -DLOAD_JPG -DLOAD_LBM -DLOAD_PCX -DLOAD_PNG \
           -DLOAD_PNM -DLOAD_QOI -DLOAD_SVG -DLOAD_TGA -DLOAD_XCF -DLOAD_XPM -DLOAD_XV \
           -DSDL_IMAGE_SAVE_PNG=1 -DSDL_IMAGE_SAVE_JPG=1
IMG_OBJS = $(addprefix $(OBJ)/img/,$(IMG_SRCS:.c=.o))
$(OBJ)/img/%.o: $(IMG)/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(IMG_DEFS) -I$(IMG)/include -c $< -o $@
# The GCC 16 stove's -m68040 float-to-int can save FPCR in the register a
# store indexes with (fpcr_check.py). In IMG_jpg.c (tiny_jpeg, SDL_image's
# JPEG writer) that wrote every coefficient to one place, so its JPEGs were
# flat grey; in Timidity's mix.c and playmidi.c it hits envelopes and
# pitch. Those three are built at -O0 until the stove is fixed; the build
# runs fpcr_check.py over every library.
$(OBJ)/img/IMG_jpg.o: $(IMG)/src/IMG_jpg.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -O0 $(IMG_DEFS) -I$(IMG)/include -c $< -o $@

# --- libxmp (for SDL2_mixer) ---
include $(XMP)/src/Makefile
include $(XMP)/src/loaders/Makefile
include $(XMP)/src/loaders/prowizard/Makefile
include $(XMP)/src/depackers/Makefile
include $(XMP)/src/depackers/lhasa/Makefile
XMP_OBJS = $(addprefix $(OBJ)/xmp/,$(OBJS) $(PROWIZARD_OBJS) $(DEPACKER_OBJS))
$(OBJ)/xmp/%.o: $(XMP)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DLIBXMP_STATIC -DHAVE_POPEN=0 -I$(XMP)/include -I$(XMP)/src -c $< -o $@

# --- SDL2_mixer ---
MIX_SRCS = src/mixer.c src/music.c src/utils.c src/effects_internal.c src/effect_position.c \
           src/effect_stereoreverse.c src/codecs/load_aiff.c src/codecs/load_voc.c \
           src/codecs/mp3utils.c src/codecs/music_wav.c src/codecs/music_ogg_stb.c \
           src/codecs/music_drflac.c src/codecs/music_minimp3.c src/codecs/music_xmp.c \
           src/codecs/music_timidity.c src/codecs/remap_channels.c \
           $(patsubst $(MIX)/%,%,$(wildcard $(MIX)/src/codecs/timidity/*.c))
MIX_DEFS = -DMUSIC_WAV -DMUSIC_OGG -DOGG_USE_STB -DMUSIC_FLAC_DRFLAC -DMUSIC_MP3_MINIMP3 \
           -DMUSIC_MOD_XMP -DLIBXMP_STATIC -DMUSIC_MID_TIMIDITY
MIX_OBJS = $(addprefix $(OBJ)/mix/,$(MIX_SRCS:.c=.o))
$(OBJ)/mix/%.o: $(MIX)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(MIX_DEFS) -I$(MIX)/include -I$(MIX)/src -I$(MIX)/src/codecs -I$(XMP)/include -c $< -o $@
$(OBJ)/mix/src/codecs/timidity/mix.o $(OBJ)/mix/src/codecs/timidity/playmidi.o: $(OBJ)/mix/%.o: $(MIX)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -O0 $(MIX_DEFS) -I$(MIX)/include -I$(MIX)/src -I$(MIX)/src/codecs -I$(XMP)/include -c $< -o $@

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
$(OBJ)/net/%.o: $(NET)/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(NET)/include -c $< -o $@

.PHONY: satellites headers
satellites: headers $(OUT)/libSDL2_image.a $(OUT)/libSDL2_mixer.a $(OUT)/libSDL2_ttf.a $(OUT)/libSDL2_net.a \
            $(OUT)/tests/sdl2/satcheck $(OUT)/tests/sdl2/satcheck.m

# Their headers beside SDL's, as programs include them: <SDL2/SDL_image.h>
headers:
	@mkdir -p $(SDLINC)
	cp $(IMG)/include/SDL_image.h $(MIX)/include/SDL_mixer.h $(TTF)/SDL_ttf.h $(NET)/include/SDL_net.h $(SDLINC)/

# tests/sdl2/satcheck.c: each library once (NAME static, NAME.m on SDL2.module)
SAT_LIBS = -lSDL2_image -lSDL2_mixer -lSDL2_ttf -lSDL2_net
$(OBJ)/satcheck.o: $(HERE)../../../../tests/sdl2/satcheck.c | headers
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/tests/sdl2/satcheck: $(OBJ)/satcheck.o $(OUT)/libSDL2_image.a $(OUT)/libSDL2_mixer.a $(OUT)/libSDL2_ttf.a $(OUT)/libSDL2_net.a
	@mkdir -p $(dir $@)
	$(CC) $(CPU) -noixemul -o $@ $< -L$(OUT) $(SAT_LIBS) -lSDL2_static -lsocket -lm -lamiga
$(OUT)/tests/sdl2/satcheck.m: $(OBJ)/satcheck.o $(OUT)/libSDL2_image.a $(OUT)/libSDL2_mixer.a $(OUT)/libSDL2_ttf.a $(OUT)/libSDL2_net.a
	@mkdir -p $(dir $@)
	$(CC) $(CPU) -noixemul -o $@ $< -L$(OUT) $(SAT_LIBS) -lSDL2 -lsocket -lm -lamiga

$(OUT)/libSDL2_image.a: $(IMG_OBJS)
	@rm -f $@; $(AR) rcs $@ $^; $(RANLIB) $@; echo "built $@"
$(OUT)/libSDL2_mixer.a: $(MIX_OBJS) $(XMP_OBJS)
	@rm -f $@; $(AR) rcs $@ $^; $(RANLIB) $@; echo "built $@"
$(OUT)/libSDL2_ttf.a: $(TTF_OBJS) $(FT_OBJS)
	@rm -f $@; $(AR) rcs $@ $^; $(RANLIB) $@; echo "built $@"
$(OUT)/libSDL2_net.a: $(NET_OBJS)
	@rm -f $@; $(AR) rcs $@ $^; $(RANLIB) $@; echo "built $@"
