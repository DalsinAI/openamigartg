# Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
#
# SDL3.module and SDL 3's link libraries, built the same way as SDL2.module
# (library/modules/sdl2/Makefile). build.sh prepares the SDL tree (SDL=...)
# and calls this Makefile; SDL itself is Zlib, and its notices stay in its
# files.
#
#   module   SDL3.module: SDL 3 with the Amiga back ends, SDL's dynamic API
#            on (LIBS:OpenGPU/SDL3.module). Built -fbaserel32 (SHARED=1, the
#            default): its code is loaded once for every program, and each
#            program gets its own copy of SDL's data. SHARED=0: a copy for
#            each program.
#   stub     libSDL3.a: the link library programs use (stubs/sdl3)
#   static   libSDL3_static.a: all of SDL in the program, no module
#            (and libSDL3_test.a, SDL's test framework)
#   tests    SDL's testsprite and testrendercopyex, static and on the module

HERE    := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
ROOT    := $(abspath $(HERE)/../../..)
SDL     ?= $(error set SDL to the prepared SDL 3 tree)
OUT     ?= $(ROOT)/build
WORK    ?= $(OUT)/sdl3-work
STOVE   ?= $(HOME)/AmigaChrome/stoves/os32-gcc16/prefix
CC      := $(STOVE)/bin/m68k-amigaos-gcc
AR      := $(STOVE)/bin/m68k-amigaos-ar
RANLIB  := $(STOVE)/bin/m68k-amigaos-ranlib
OBJCOPY := $(STOVE)/bin/m68k-amigaos-objcopy
SFDC    := $(STOVE)/bin/sfdc
CPU     ?= -m68040 -m68881
OPT     ?= -O2 -fomit-frame-pointer -mno-bitfield
SDK_INC ?= $(WORK)/sdk-include
OGPU_SRC := $(ROOT)/library/opengpu
COMMON  := $(ROOT)/library/modules/common
SHARED  ?= 1
# The module's objects: every global through A4 when shared.
MODULE_CFLAGS := $(if $(filter 1,$(SHARED)),-fbaserel32) -I$(COMMON)
MODULE_LDFLAGS := $(if $(filter 1,$(SHARED)),-fbaserel32 -resident32)

# -fno-delete-null-pointer-checks: address 0 is memory on an Amiga; without it
# GCC puts TRAP #7 (Software Failure 80000027) where it proves a pointer null,
# in place of the access.
CFLAGS_BASE = -std=gnu11 $(OPT) $(CPU) -fno-delete-null-pointer-checks -noixemul -Wall -Wno-unused-parameter \
         -Wno-sign-compare -Wno-unused-but-set-variable \
         -I$(SDL)/include -I$(SDL)/include/build_config -I$(SDL)/src -I$(SDK_INC) -I$(ROOT)/include \
         -I$(ROOT)/stubs/sdl3 -I$(COMMON) -D__AMIGAOS3__

SRCS_CORE = \
	SDL.c SDL_assert.c SDL_error.c SDL_guid.c SDL_hashtable.c SDL_hints.c \
	SDL_list.c SDL_log.c SDL_properties.c SDL_utils.c atomic/SDL_atomic.c \
	atomic/SDL_spinlock.c audio/SDL_audio.c audio/SDL_audiocvt.c \
	audio/SDL_audiodev.c audio/SDL_audioqueue.c audio/SDL_audioresample.c \
	audio/SDL_audiotypecvt.c audio/SDL_mixer.c audio/SDL_wave.c \
	camera/SDL_camera.c camera/dummy/SDL_camera_dummy.c core/SDL_core_unsupported.c cpuinfo/SDL_cpuinfo.c dynapi/SDL_dynapi.c hidapi/SDL_hidapi.c \
	dialog/SDL_dialog.c dialog/SDL_dialog_utils.c events/SDL_categories.c \
	events/SDL_clipboardevents.c events/SDL_displayevents.c \
	events/SDL_dropevents.c events/SDL_events.c events/SDL_eventwatch.c \
	events/SDL_keyboard.c events/SDL_keymap.c events/SDL_keysym_to_keycode.c \
	events/SDL_keysym_to_scancode.c events/SDL_mouse.c events/SDL_pen.c \
	events/SDL_quit.c events/SDL_scancode_tables.c events/SDL_touch.c \
	events/SDL_windowevents.c events/imKStoUCS.c gpu/SDL_gpu.c \
	haptic/SDL_haptic.c haptic/dummy/SDL_syshaptic.c io/SDL_asyncio.c \
	io/SDL_iostream.c io/generic/SDL_asyncio_generic.c joystick/SDL_gamepad.c \
	joystick/SDL_joystick.c joystick/SDL_steam_virtual_gamepad.c \
	joystick/controller_type.c libm/e_atan2.c libm/e_exp.c libm/e_fmod.c \
	libm/e_log.c libm/e_log10.c libm/e_pow.c libm/e_rem_pio2.c libm/e_sqrt.c \
	libm/k_cos.c libm/k_rem_pio2.c libm/k_sin.c libm/k_tan.c libm/s_atan.c \
	libm/s_copysign.c libm/s_cos.c libm/s_fabs.c libm/s_floor.c libm/s_isinf.c \
	libm/s_isinff.c libm/s_isnan.c libm/s_isnanf.c libm/s_modf.c libm/s_scalbn.c \
	libm/s_sin.c libm/s_tan.c loadso/dummy/SDL_sysloadso.c locale/SDL_locale.c \
	main/generic/SDL_sysmain_callbacks.c main/SDL_main_callbacks.c \
	main/SDL_runapp.c misc/SDL_libusb.c misc/SDL_url.c misc/dummy/SDL_sysurl.c \
	power/SDL_power.c process/SDL_process.c process/dummy/SDL_dummyprocess.c \
	render/SDL_render.c render/SDL_render_unsupported.c render/SDL_yuv_sw.c \
	render/software/SDL_blendfillrect.c render/software/SDL_blendline.c \
	render/software/SDL_blendpoint.c render/software/SDL_drawline.c \
	render/software/SDL_drawpoint.c render/software/SDL_render_sw.c \
	render/software/SDL_triangle.c sensor/SDL_sensor.c \
	sensor/dummy/SDL_dummysensor.c stdlib/SDL_crc16.c stdlib/SDL_crc32.c \
	stdlib/SDL_getenv.c stdlib/SDL_iconv.c stdlib/SDL_malloc.c \
	stdlib/SDL_memcpy.c stdlib/SDL_memmove.c stdlib/SDL_memset.c \
	stdlib/SDL_mslibc.c stdlib/SDL_murmur3.c stdlib/SDL_qsort.c \
	stdlib/SDL_random.c stdlib/SDL_stdlib.c stdlib/SDL_string.c \
	stdlib/SDL_strtokr.c storage/SDL_storage.c \
	storage/generic/SDL_genericstorage.c thread/SDL_thread.c \
	thread/generic/SDL_syscond.c thread/generic/SDL_sysrwlock.c time/SDL_time.c \
	timer/SDL_timer.c tray/SDL_tray_utils.c tray/dummy/SDL_tray.c \
	video/SDL_RLEaccel.c video/SDL_blit.c video/SDL_blit_0.c video/SDL_blit_1.c \
	video/SDL_blit_A.c video/SDL_blit_N.c video/SDL_blit_auto.c \
	video/SDL_blit_copy.c video/SDL_blit_slow.c video/SDL_bmp.c \
	video/SDL_clipboard.c video/SDL_egl.c video/SDL_fillrect.c \
	video/SDL_pixels.c video/SDL_rect.c video/SDL_rotate.c video/SDL_stb.c \
	video/SDL_stretch.c video/SDL_surface.c video/SDL_video.c \
	video/SDL_video_unsupported.c video/SDL_vulkan_utils.c video/SDL_yuv.c \
	video/yuv2rgb/yuv_rgb_std.c video/dummy/SDL_nullevents.c \
	video/dummy/SDL_nullframebuffer.c video/dummy/SDL_nullvideo.c \
	filesystem/SDL_filesystem.c audio/dummy/SDL_dummyaudio.c

# The Amiga back ends: SDL3_68k's (Zlib) with patches/amigaos3.
SRCS_AMIGA = \
	video/amigaos3/SDL_os3video.c video/amigaos3/SDL_os3window.c \
	video/amigaos3/SDL_os3framebuffer.c video/amigaos3/SDL_os3events.c \
	video/amigaos3/SDL_os3keyboard.c video/amigaos3/SDL_os3modes.c \
	video/amigaos3/SDL_os3mouse.c video/amigaos3/SDL_os3messagebox.c \
	audio/amigaos3/SDL_os3audio.c \
	thread/amigaos3/SDL_systhread.c thread/amigaos3/SDL_sysmutex.c \
	thread/amigaos3/SDL_syssem.c thread/amigaos3/SDL_systls.c \
	timer/amigaos3/SDL_systimer.c time/amigaos3/SDL_systime.c \
	joystick/amigaos3/SDL_sysjoystick.c \
	filesystem/amigaos3/SDL_sysfilesystem.c filesystem/amigaos3/SDL_sysfsops.c \
	locale/amigaos3/SDL_syslocale.c dialog/amigaos3/SDL_amigaos3dialog.c

SRCS    = $(SRCS_CORE) $(SRCS_AMIGA)

SRCS_TEST = \
	test/SDL_test_assert.c test/SDL_test_common.c test/SDL_test_compare.c \
	test/SDL_test_crc32.c test/SDL_test_font.c test/SDL_test_fuzzer.c \
	test/SDL_test_harness.c test/SDL_test_log.c test/SDL_test_md5.c \
	test/SDL_test_memory.c

STATIC_DIR = $(WORK)/obj-static
CORE_DIR   = $(WORK)/obj-module
STUB_DIR   = $(WORK)/obj-stub
TEST_DIR   = $(OUT)/tests/sdl3

STATIC_OBJS = $(addprefix $(STATIC_DIR)/,$(SRCS:.c=.o))
CORE_OBJS   = $(addprefix $(CORE_DIR)/,$(SRCS:.c=.o))
TEST_OBJS   = $(addprefix $(STATIC_DIR)/,$(SRCS_TEST:.c=.o))
INLINE      = $(SDK_INC)/inline/opengpu.h

.PHONY: all module stub static tests
all: module stub static tests

module: $(OUT)/SDL3.module
stub: $(OUT)/libSDL3.a
static: $(OUT)/libSDL3_static.a $(OUT)/libSDL3_test.a

$(INLINE): $(OGPU_SRC)/opengpu_lib.sfd
	@mkdir -p $(dir $@)
	$(SFDC) --quiet --mode=macros --output=$@ $<

# Every object depends on the configuration (build.sh copies it keeping its
# time, so a change to it rebuilds SDL; SDL's own files keep the tarball's).
CONFIG_H := $(SDL)/include/build_config/SDL_build_config_amigaos3.h
$(STATIC_DIR)/%.o: $(SDL)/src/%.c $(INLINE) $(CONFIG_H)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_BASE) -c $< -o $@
$(CORE_DIR)/%.o: $(SDL)/src/%.c $(INLINE) $(CONFIG_H)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_BASE) $(MODULE_CFLAGS) -DSDL_OPEN_LIBRARY_BUILD=1 -c $< -o $@

$(OUT)/libSDL3_static.a: $(STATIC_OBJS)
	@rm -f $@; $(AR) rcs $@ $^; $(RANLIB) $@; echo "built $@"
$(OUT)/libSDL3_test.a: $(TEST_OBJS)
	@rm -f $@; $(AR) rcs $@ $^; $(RANLIB) $@; echo "built $@"
$(WORK)/libSDL3core.a: $(CORE_OBJS)
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
$(CORE_DIR)/sdl3_module.o: $(HERE)module/sdl3_module.c $(ROOT)/stubs/sdl3/sdl3_module.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_BASE) $(MODULE_CFLAGS) -c $< -o $@
$(OUT)/SDL3.module: $(CORE_DIR)/module_start.o $(CORE_DIR)/module_rt.o $(CORE_DIR)/sdl3_module.o $(WORK)/libSDL3core.a
	$(CC) $(CPU) $(MODULE_LDFLAGS) -noixemul -nostartfiles -Wl,-Map,$(WORK)/SDL3.module.map -o $@ \
	    $(CORE_DIR)/module_start.o $(CORE_DIR)/module_rt.o $(CORE_DIR)/sdl3_module.o $(WORK)/libSDL3core.a -lm -lamiga
	$(if $(filter 1,$(SHARED)),python3 $(ROOT)/tools/baserel_check.py $(STOVE)/bin/m68k-amigaos- $@ $(WORK)/SDL3.module.map --allow $(HERE)baserel.allow)
	@echo "built $@ ($$(wc -c < $@) bytes)"

# -ffixed-a4: the stub sets A4 around each call into the module (OGPU_A4).
STUB_CFLAGS = $(CFLAGS_BASE) -ffixed-a4 -I$(SDL)/src/dynapi -I$(SDL)/src
$(STUB_DIR)/SDL3_stub.o: $(ROOT)/stubs/sdl3/SDL3_stub.c $(ROOT)/stubs/sdl3/SDL3_stub.h $(ROOT)/stubs/sdl3/sdl3_module.h $(INLINE)
	@mkdir -p $(dir $@)
	$(CC) $(STUB_CFLAGS) -c $< -o $@
$(OUT)/libSDL3.a: $(STUB_DIR)/SDL3_stub.o
	@rm -f $@; $(AR) rcs $@ $^; $(RANLIB) $@; echo "built $@"

# SDL's tests, unchanged, under tests/sdl3/sdltest_run.c (their log on
# standard output, and an end after SDLTEST_SECONDS). NAME is linked with
# libSDL3_static.a, NAME.m with libSDL3.a (SDL3.module).
TESTS = testsprite testrendercopyex
STATIC_TESTS = $(addprefix $(TEST_DIR)/,$(TESTS))
MODULE_TESTS = $(addsuffix .m,$(STATIC_TESTS))
tests: $(STATIC_TESTS) $(MODULE_TESTS)

SDLTEST_OBJS = $(addprefix $(TEST_DIR)/obj/,$(addsuffix .o,$(TESTS)))
TEST_RUN = $(TEST_DIR)/obj/testutils.o $(TEST_DIR)/obj/sdltest_run.o

# The test's main is renamed sdltest_main in its object: SDL_main.h writes the
# main of a test on SDL's main callbacks (testsprite), after #undef main.
$(SDLTEST_OBJS): $(TEST_DIR)/obj/%.o: $(SDL)/test/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_BASE) -c $< -o $@
	$(OBJCOPY) --redefine-sym _main=_sdltest_main $@
$(TEST_DIR)/obj/testutils.o: $(SDL)/test/testutils.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_BASE) -c $< -o $@
$(TEST_DIR)/obj/sdltest_run.o: $(ROOT)/tests/sdl3/sdltest_run.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_BASE) -c $< -o $@

$(STATIC_TESTS): $(TEST_DIR)/%: $(TEST_DIR)/obj/%.o $(TEST_RUN) $(OUT)/libSDL3_static.a $(OUT)/libSDL3_test.a
	$(CC) $(CPU) -noixemul -o $@ $< $(TEST_RUN) -L$(OUT) -lSDL3_test -lSDL3_static -lm -lamiga
$(MODULE_TESTS): $(TEST_DIR)/%.m: $(TEST_DIR)/obj/%.o $(TEST_RUN) $(OUT)/libSDL3.a $(OUT)/libSDL3_test.a
	$(CC) $(CPU) -noixemul -o $@ $< $(TEST_RUN) -L$(OUT) -lSDL3_test -lSDL3 -lm -lamiga
