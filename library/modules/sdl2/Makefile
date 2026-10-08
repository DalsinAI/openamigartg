# Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
#
# SDL2.module and SDL 2's link libraries. build.sh prepares the SDL tree
# (SDL=...) and calls this Makefile; SDL itself is Zlib, and its notices stay
# in its files.
#
#   module   SDL2.module: SDL 2 with the Amiga back ends, SDL's dynamic API
#            on (LIBS:OpenGPU/SDL2.module). Built -fbaserel32 (SHARED=1, the
#            default): its code is loaded once for every program, and each
#            program gets its own copy of SDL's data (include/opengpu/
#            module.h, residency step 2). SHARED=0: a copy for each program.
#   stub     libSDL2.a: the link library programs use (stubs/sdl2)
#   static   libSDL2_static.a: all of SDL in the program, no module
#            (and libSDL2_test.a, SDL's test framework)
#   tests    SDL's testsprite2 and testrendercopyex, static and on the module

HERE    := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
ROOT    := $(abspath $(HERE)/../../..)
SDL     ?= $(error set SDL to the prepared SDL 2 tree)
OUT     ?= $(ROOT)/build
WORK    ?= $(OUT)/sdl2-work
STOVE   ?= $(HOME)/AmigaChrome/stoves/os32-gcc16/prefix
CC      := $(STOVE)/bin/m68k-amigaos-gcc
AR      := $(STOVE)/bin/m68k-amigaos-ar
RANLIB  := $(STOVE)/bin/m68k-amigaos-ranlib
SFDC    := $(STOVE)/bin/sfdc
VASM    ?= vasmm68k_mot
CPU     ?= -m68040 -m68881
OPT     ?= -O2 -fomit-frame-pointer -mno-bitfield
SDK_INC ?= $(WORK)/sdk-include
OGPU_SRC := $(ROOT)/library/opengpu
COMMON  := $(ROOT)/library/modules/common
SHARED  ?= 1
# The module's objects: every global through A4 when shared.
MODULE_CFLAGS := $(if $(filter 1,$(SHARED)),-fbaserel32) -I$(COMMON)
MODULE_LDFLAGS := $(if $(filter 1,$(SHARED)),-fbaserel32 -resident32)
OGPU_BUILD3D := $(wildcard $(OGPU_SRC)/ogpu_build3d.c)

# -fno-delete-null-pointer-checks: address 0 is memory on an Amiga; without it
# GCC puts TRAP #7 (Software Failure 80000027) where it proves a pointer null,
# in place of the access.
CFLAGS_BASE = -std=gnu11 $(OPT) $(CPU) -fno-delete-null-pointer-checks -noixemul -Wall -Wno-unused-parameter \
         -Wno-sign-compare -Wno-unused-but-set-variable \
         -I$(SDL)/include -I$(SDL)/src -I$(SDK_INC) -I$(ROOT)/include -I$(ROOT)/stubs/sdl2 \
         -DSDL_VIDEO_RENDER_OPENGPU=1 -D__AMIGAOS3__

SRCS_CORE = \
	SDL.c SDL_assert.c SDL_dataqueue.c SDL_error.c SDL_guid.c SDL_hints.c \
	SDL_list.c SDL_log.c SDL_utils.c \
	atomic/SDL_atomic.c atomic/SDL_spinlock.c \
	cpuinfo/SDL_cpuinfo.c \
	dynapi/SDL_dynapi.c \
	stdlib/SDL_crc16.c stdlib/SDL_crc32.c stdlib/SDL_getenv.c stdlib/SDL_iconv.c \
	stdlib/SDL_malloc.c stdlib/SDL_qsort.c stdlib/SDL_stdlib.c stdlib/SDL_string.c \
	stdlib/SDL_strtokr.c \
	libm/e_atan2.c libm/e_exp.c libm/e_fmod.c libm/e_log.c libm/e_log10.c \
	libm/e_pow.c libm/e_rem_pio2.c libm/e_sqrt.c libm/k_cos.c libm/k_rem_pio2.c \
	libm/k_sin.c libm/k_tan.c libm/s_atan.c libm/s_copysign.c libm/s_cos.c \
	libm/s_fabs.c libm/s_floor.c libm/s_scalbn.c libm/s_sin.c libm/s_tan.c \
	file/SDL_rwops.c \
	events/SDL_clipboardevents.c events/SDL_displayevents.c events/SDL_dropevents.c \
	events/SDL_events.c events/SDL_gesture.c events/SDL_keyboard.c \
	events/SDL_keysym_to_scancode.c events/SDL_mouse.c events/SDL_quit.c \
	events/SDL_scancode_tables.c events/SDL_touch.c events/SDL_windowevents.c \
	events/imKStoUCS.c \
	video/SDL_RLEaccel.c video/SDL_blit.c video/SDL_blit_0.c video/SDL_blit_1.c \
	video/SDL_blit_A.c video/SDL_blit_N.c video/SDL_blit_auto.c video/SDL_blit_copy.c \
	video/SDL_blit_slow.c video/SDL_bmp.c video/SDL_clipboard.c video/SDL_fillrect.c \
	video/SDL_pixels.c video/SDL_rect.c video/SDL_shape.c video/SDL_stretch.c \
	video/SDL_surface.c video/SDL_video.c video/SDL_yuv.c video/yuv2rgb/yuv_rgb_std.c \
	video/dummy/SDL_nullevents.c video/dummy/SDL_nullframebuffer.c video/dummy/SDL_nullvideo.c \
	audio/SDL_audio.c audio/SDL_audiocvt.c audio/SDL_audiodev.c audio/SDL_audiotypecvt.c \
	audio/SDL_mixer.c audio/SDL_wave.c audio/dummy/SDL_dummyaudio.c \
	thread/SDL_thread.c timer/SDL_timer.c \
	joystick/SDL_gamecontroller.c joystick/SDL_joystick.c \
	joystick/SDL_steam_virtual_gamepad.c joystick/controller_type.c \
	haptic/SDL_haptic.c haptic/dummy/SDL_syshaptic.c \
	render/SDL_d3dmath.c render/SDL_render.c render/SDL_yuv_sw.c \
	render/software/SDL_blendfillrect.c render/software/SDL_blendline.c \
	render/software/SDL_blendpoint.c render/software/SDL_drawline.c \
	render/software/SDL_drawpoint.c render/software/SDL_render_sw.c \
	render/software/SDL_rotate.c render/software/SDL_triangle.c \
	loadso/dummy/SDL_sysloadso.c \
	sensor/SDL_sensor.c sensor/dummy/SDL_dummysensor.c \
	power/SDL_power.c locale/SDL_locale.c locale/dummy/SDL_syslocale.c \
	misc/SDL_url.c misc/dummy/SDL_sysurl.c \
	hidapi/SDL_hidapi.c

# The Amiga back ends: libSDL2-amigaos3's (Zlib) with patches/amigaos3, and
# the Team's (src/): AHI, joystick, clipboard, OpenGPU, the opengpu renderer.
SRCS_AMIGA = \
	video/amigaos3/SDL_os3video.c video/amigaos3/SDL_os3window.c \
	video/amigaos3/SDL_os3framebuffer.c video/amigaos3/SDL_os3events.c \
	video/amigaos3/SDL_os3mouse.c video/amigaos3/SDL_os3aga.c \
	video/amigaos3/SDL_os3clipboard.c video/amigaos3/SDL_os3gpu.c video/amigaos3/SDL_os3gl.c \
	audio/amigaos3/SDL_os3audio.c audio/amigaos3/SDL_os3paula.c \
	thread/amigaos3/SDL_systhread.c thread/amigaos3/SDL_sysmutex.c \
	thread/amigaos3/SDL_syscond.c thread/amigaos3/SDL_syssem.c thread/amigaos3/SDL_systls.c \
	timer/amigaos3/SDL_systimer.c \
	joystick/amigaos3/SDL_sysjoystick.c \
	filesystem/amigaos3/SDL_sysfilesystem.c \
	render/opengpu/SDL_render_opengpu.c

C2P_SRC = video/amigaos3/c2p1x1_8_c5_gen.s
SRCS    = $(SRCS_CORE) $(SRCS_AMIGA)

SRCS_TEST = \
	test/SDL_test_assert.c test/SDL_test_common.c test/SDL_test_compare.c \
	test/SDL_test_crc32.c test/SDL_test_font.c test/SDL_test_fuzzer.c \
	test/SDL_test_harness.c test/SDL_test_imageBlit.c test/SDL_test_imageBlitBlend.c \
	test/SDL_test_imageFace.c test/SDL_test_imagePrimitives.c \
	test/SDL_test_imagePrimitivesBlend.c test/SDL_test_log.c test/SDL_test_md5.c \
	test/SDL_test_memory.c test/SDL_test_random.c

STATIC_DIR = $(WORK)/obj-static
CORE_DIR   = $(WORK)/obj-module
STUB_DIR   = $(WORK)/obj-stub
TEST_DIR   = $(OUT)/tests/sdl2

OGPU_OBJS = ogpu_build.o $(if $(OGPU_BUILD3D),ogpu_build3d.o)
STATIC_OBJS = $(addprefix $(STATIC_DIR)/,$(SRCS:.c=.o) c2p.o $(OGPU_OBJS))
CORE_OBJS   = $(addprefix $(CORE_DIR)/,$(SRCS:.c=.o) c2p.o $(OGPU_OBJS))
TEST_OBJS   = $(addprefix $(STATIC_DIR)/,$(SRCS_TEST:.c=.o))
INLINE      = $(SDK_INC)/inline/opengpu.h

.PHONY: all module stub static tests
all: module stub static tests

module: $(OUT)/SDL2.module
stub: $(OUT)/libSDL2.a
static: $(OUT)/libSDL2_static.a $(OUT)/libSDL2_test.a

$(INLINE): $(OGPU_SRC)/opengpu_lib.sfd
	@mkdir -p $(dir $@)
	$(SFDC) --quiet --mode=macros --output=$@ $<

# Every object depends on the configuration (build.sh copies it keeping its
# time, so a change to it rebuilds SDL; SDL's own files keep the tarball's).
CONFIG_H := $(SDL)/include/SDL_config_amigaos.h
$(STATIC_DIR)/%.o: $(SDL)/src/%.c $(INLINE) $(CONFIG_H)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_BASE) -c $< -o $@
$(CORE_DIR)/%.o: $(SDL)/src/%.c $(INLINE) $(CONFIG_H)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_BASE) $(MODULE_CFLAGS) -DSDL_OPEN_LIBRARY_BUILD=1 -c $< -o $@
$(STATIC_DIR)/ogpu_%.o: $(OGPU_SRC)/ogpu_%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_BASE) -c $< -o $@
$(CORE_DIR)/ogpu_%.o: $(OGPU_SRC)/ogpu_%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_BASE) $(MODULE_CFLAGS) -c $< -o $@
$(STATIC_DIR)/c2p.o: $(SDL)/src/$(C2P_SRC)
	@mkdir -p $(dir $@)
	$(VASM) -Fhunk -m68020 -DBPLSIZE=10240 -quiet -o $@ $<
$(CORE_DIR)/c2p.o: $(SDL)/src/$(C2P_SRC)
	@mkdir -p $(dir $@)
	$(VASM) -Fhunk -m68020 -DBPLSIZE=10240 -quiet -o $@ $<

$(OUT)/libSDL2_static.a: $(STATIC_OBJS)
	@rm -f $@; $(AR) rcs $@ $^; $(RANLIB) $@; echo "built $@"
$(OUT)/libSDL2_test.a: $(TEST_OBJS)
	@rm -f $@; $(AR) rcs $@ $^; $(RANLIB) $@; echo "built $@"
$(WORK)/libSDL2core.a: $(CORE_OBJS)
	@rm -f $@; $(AR) rcs $@ $^; $(RANLIB) $@

# The module: its start code first, then SDL; libnix without its program
# startup (library/modules/common stands in for it). A shared module must
# reach its data only through A4 (tools/baserel_check.py).
$(CORE_DIR)/module_start.o: $(COMMON)/module_start.S
	@mkdir -p $(dir $@)
	$(CC) $(CPU) $(MODULE_CFLAGS) -c $< -o $@
$(CORE_DIR)/module_rt.o: $(COMMON)/module_rt.c $(COMMON)/module_rt.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_BASE) $(MODULE_CFLAGS) -c $< -o $@
$(CORE_DIR)/sdl2_module.o: $(HERE)module/sdl2_module.c $(ROOT)/stubs/sdl2/sdl2_module.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_BASE) $(MODULE_CFLAGS) -I$(ROOT)/stubs/sdl2 -c $< -o $@
$(OUT)/SDL2.module: $(CORE_DIR)/module_start.o $(CORE_DIR)/module_rt.o $(CORE_DIR)/sdl2_module.o $(WORK)/libSDL2core.a
	$(CC) $(CPU) $(MODULE_LDFLAGS) -noixemul -nostartfiles -Wl,-Map,$(WORK)/SDL2.module.map -o $@ \
	    $(CORE_DIR)/module_start.o $(CORE_DIR)/module_rt.o $(CORE_DIR)/sdl2_module.o $(WORK)/libSDL2core.a -lm -lamiga
	$(if $(filter 1,$(SHARED)),python3 $(ROOT)/tools/baserel_check.py $(STOVE)/bin/m68k-amigaos- $@ $(WORK)/SDL2.module.map --allow $(HERE)baserel.allow)
	@echo "built $@ ($$(wc -c < $@) bytes)"

# -ffixed-a4: the stub sets A4 around each call into the module (OGPU_A4).
# The window GL calls (SDL_GL_LoadLibrary to SDL_GL_ResetAttributes) are a
# member of their own, SDL2_gl.o, so only a program that calls them needs
# -lGL: SDL_dynapi_procs.h is split into SDL2_stub_procs.h (without them)
# and SDL2_gl_procs.h (only them; every other line that isn't a function).
GL_PROCS := SDL_GL_(LoadLibrary|GetProcAddress|UnloadLibrary|ExtensionSupported|SetAttribute|GetAttribute|CreateContext|MakeCurrent|GetCurrentWindow|GetCurrentContext|GetDrawableSize|SetSwapInterval|GetSwapInterval|SwapWindow|DeleteContext|ResetAttributes)
GLA_INC := -I$(ROOT)/library/modules/gl/gla
STUB_CFLAGS = $(CFLAGS_BASE) -ffixed-a4 -I$(SDL)/src/dynapi -I$(STUB_DIR)
$(STUB_DIR)/SDL2_stub_procs.h: $(SDL)/src/dynapi/SDL_dynapi_procs.h
	@mkdir -p $(dir $@)
	grep -Ev '^SDL_DYNAPI_PROC\([^,]*,$(GL_PROCS),' $< > $@
$(STUB_DIR)/SDL2_gl_procs.h: $(SDL)/src/dynapi/SDL_dynapi_procs.h
	@mkdir -p $(dir $@)
	grep -E '^SDL_DYNAPI_PROC\([^,]*,$(GL_PROCS),|^#' $< > $@
	test "$$(grep -c '^SDL_DYNAPI_PROC' $@)" = 16
$(STUB_DIR)/SDL2_stub.o: $(ROOT)/stubs/sdl2/SDL2_stub.c $(ROOT)/stubs/sdl2/SDL2_stub.h $(ROOT)/stubs/sdl2/sdl2_module.h $(STUB_DIR)/SDL2_stub_procs.h $(INLINE)
	@mkdir -p $(dir $@)
	$(CC) $(STUB_CFLAGS) -c $< -o $@
$(STUB_DIR)/SDL2_gl.o: $(ROOT)/stubs/sdl2/SDL2_gl.c $(ROOT)/stubs/sdl2/SDL2_stub.h $(ROOT)/stubs/sdl2/sdl2_module.h $(STUB_DIR)/SDL2_gl_procs.h $(INLINE)
	@mkdir -p $(dir $@)
	$(CC) $(STUB_CFLAGS) $(GLA_INC) -c $< -o $@
$(OUT)/libSDL2.a: $(STUB_DIR)/SDL2_stub.o $(STUB_DIR)/SDL2_gl.o
	@rm -f $@; $(AR) rcs $@ $^; $(RANLIB) $@; echo "built $@"

# SDL's tests, unchanged, under tests/sdl2/sdltest_run.c (their log on
# standard output, and an end after SDLTEST_SECONDS), and the Team's own
# (tests/sdl2): rendercompare checks the opengpu renderer's pictures against
# the software renderer's, sdlcheck the back ends. NAME is linked with
# libSDL2_static.a, NAME.m with libSDL2.a (SDL2.module).
TESTS = testsprite2 testrendercopyex
OWN_TESTS = rendercompare sdlcheck
STATIC_TESTS = $(addprefix $(TEST_DIR)/,$(TESTS) $(OWN_TESTS))
MODULE_TESTS = $(addsuffix .m,$(STATIC_TESTS))
tests: $(STATIC_TESTS) $(MODULE_TESTS)

SDLTEST_OBJS = $(addprefix $(TEST_DIR)/obj/,$(addsuffix .o,$(TESTS)))
OWNTEST_OBJS = $(addprefix $(TEST_DIR)/obj/,$(addsuffix .o,$(OWN_TESTS)))
TEST_RUN = $(TEST_DIR)/obj/testutils.o $(TEST_DIR)/obj/sdltest_run.o
# On the module, SDL's test framework brings in libSDL2.a's SDL_GL_ calls;
# with no libGL.a here, tests/sdl2/nogl.c answers for GL.
NOGL = $(TEST_DIR)/obj/nogl.o

$(SDLTEST_OBJS): $(TEST_DIR)/obj/%.o: $(SDL)/test/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_BASE) -Dmain=sdltest_main -c $< -o $@
$(OWNTEST_OBJS): $(TEST_DIR)/obj/%.o: $(ROOT)/tests/sdl2/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_BASE) -c $< -o $@
$(TEST_DIR)/obj/testutils.o: $(SDL)/test/testutils.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_BASE) -c $< -o $@
$(NOGL): $(ROOT)/tests/sdl2/nogl.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_BASE) -c $< -o $@
$(TEST_DIR)/obj/sdltest_run.o: $(ROOT)/tests/sdl2/sdltest_run.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_BASE) -c $< -o $@

$(addprefix $(TEST_DIR)/,$(TESTS)): $(TEST_DIR)/%: $(TEST_DIR)/obj/%.o $(TEST_RUN) $(OUT)/libSDL2_static.a $(OUT)/libSDL2_test.a
	$(CC) $(CPU) -noixemul -o $@ $< $(TEST_RUN) -L$(OUT) -lSDL2_test -lSDL2_static -lm -lamiga
$(addsuffix .m,$(addprefix $(TEST_DIR)/,$(TESTS))): $(TEST_DIR)/%.m: $(TEST_DIR)/obj/%.o $(TEST_RUN) $(NOGL) $(OUT)/libSDL2.a $(OUT)/libSDL2_test.a
	$(CC) $(CPU) -noixemul -o $@ $< $(TEST_RUN) -L$(OUT) -lSDL2_test -lSDL2 $(NOGL) -lm -lamiga
$(addprefix $(TEST_DIR)/,$(OWN_TESTS)): $(TEST_DIR)/%: $(TEST_DIR)/obj/%.o $(OUT)/libSDL2_static.a
	$(CC) $(CPU) -noixemul -o $@ $< -L$(OUT) -lSDL2_static -lm -lamiga
$(addsuffix .m,$(addprefix $(TEST_DIR)/,$(OWN_TESTS))): $(TEST_DIR)/%.m: $(TEST_DIR)/obj/%.o $(OUT)/libSDL2.a
	$(CC) $(CPU) -noixemul -o $@ $< -L$(OUT) -lSDL2 -lm -lamiga
