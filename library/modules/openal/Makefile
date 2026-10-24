# Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
#
# OpenAL.module and its link libraries, built the same way as SDL3.module
# (library/modules/sdl3/Makefile). build.sh prepares the AHI headers
# (SDK_INC) and calls this Makefile. Everything here is the Team's.
#
#   module   OpenAL.module: OpenAL 1.1 with Mix on AHI (LIBS:OpenGPU/).
#            Built -fbaserel32 (SHARED=1, the default): its code is loaded
#            once for every program, and each program gets its own copy of
#            OpenAL's state. SHARED=0: a copy for each program.
#   stub     libopenal.a: the link library programs use (stubs/openal)
#   static   libopenal_static.a: all of OpenAL in the program, no module
#   tests    tests/openal: nomodule.m; ALTone (altone.m on the module,
#            altone static); and the PC tests' three that need no threads
#            (test_al_api, test_al_3d, test_al_stream) built for the 68k,
#            with Mix's output standing in as on the PC

HERE    := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
ROOT    := $(abspath $(HERE)/../../..)
OUT     ?= $(ROOT)/build
WORK    ?= $(OUT)/openal-work
STOVE   ?= $(HOME)/AmigaChrome/stoves/os32-gcc16/prefix
CC      := $(STOVE)/bin/m68k-amigaos-gcc
AR      := $(STOVE)/bin/m68k-amigaos-ar
RANLIB  := $(STOVE)/bin/m68k-amigaos-ranlib
SFDC    := $(STOVE)/bin/sfdc
CPU     ?= -m68040 -m68881
OPT     ?= -O2 -fomit-frame-pointer -mno-bitfield
SDK_INC ?= $(WORK)/sdk-include
OGPU_SRC := $(ROOT)/library/opengpu
COMMON  := $(ROOT)/library/modules/common
TESTSRC := $(ROOT)/tests/openal
SHARED  ?= 1
# The module's objects: every global through A4 when shared.
MODULE_CFLAGS := $(if $(filter 1,$(SHARED)),-fbaserel32) -DAL_MODULE_BUILD
MODULE_LDFLAGS := $(if $(filter 1,$(SHARED)),-fbaserel32 -resident32)

# -fno-delete-null-pointer-checks: address 0 is memory on an Amiga; without it
# GCC puts TRAP #7 (Software Failure 80000027) where it proves a pointer null,
# in place of the access.
CFLAGS_BASE = -std=gnu11 $(OPT) $(CPU) -fno-delete-null-pointer-checks -noixemul -Wall -Wextra -Werror \
         -Wno-unused-parameter -I$(HERE)include -I$(HERE)src -I$(ROOT)/include -I$(ROOT)/stubs/openal \
         -I$(COMMON) -I$(SDK_INC)

CORE    = alc al_state al_buffer al_source al_spatial al_mix
SRCDIR  = $(HERE)src
CORE_DIR   = $(WORK)/obj-module
STATIC_DIR = $(WORK)/obj-static
STUB_DIR   = $(WORK)/obj-stub
TEST_DIR   = $(OUT)/tests/openal
HEADERS = $(wildcard $(HERE)include/AL/*.h) $(SRCDIR)/al_internal.h $(SRCDIR)/al_attr.h $(ROOT)/stubs/openal/openal_procs.h

CORE_OBJS   = $(addprefix $(CORE_DIR)/,$(addsuffix .o,$(CORE) al_ahi))
STATIC_OBJS = $(addprefix $(STATIC_DIR)/,$(addsuffix .o,$(CORE) al_ahi))
INLINE      = $(SDK_INC)/inline/opengpu.h

.PHONY: all module stub static tests
all: module stub static tests

module: $(OUT)/OpenAL.module
stub: $(OUT)/libopenal.a
static: $(OUT)/libopenal_static.a

$(INLINE): $(OGPU_SRC)/opengpu_lib.sfd
	@mkdir -p $(dir $@)
	$(SFDC) --quiet --mode=macros --output=$@ $<

$(CORE_DIR)/%.o: $(SRCDIR)/%.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_BASE) $(MODULE_CFLAGS) -c $< -o $@
$(STATIC_DIR)/%.o: $(SRCDIR)/%.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_BASE) -DAL_STATIC_BUILD -c $< -o $@

$(OUT)/libopenal_static.a: $(STATIC_OBJS)
	@rm -f $@; $(AR) rcs $@ $^; $(RANLIB) $@; echo "built $@"

# The module: its start code first, then OpenAL; libnix without its program
# startup (library/modules/common stands in for it). A shared module must
# reach its data only through A4 (tools/baserel_check.py).
$(CORE_DIR)/module_start.o: $(COMMON)/module_start.S
	@mkdir -p $(dir $@)
	$(CC) $(CPU) $(MODULE_CFLAGS) -c $< -o $@
$(CORE_DIR)/module_rt.o: $(COMMON)/module_rt.c $(COMMON)/module_rt.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_BASE) $(MODULE_CFLAGS) -c $< -o $@
$(CORE_DIR)/openal_module.o: $(HERE)module/openal_module.c $(ROOT)/stubs/openal/openal_module.h $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_BASE) $(MODULE_CFLAGS) -c $< -o $@
$(OUT)/OpenAL.module: $(CORE_DIR)/module_start.o $(CORE_DIR)/module_rt.o $(CORE_DIR)/openal_module.o $(CORE_OBJS)
	$(CC) $(CPU) $(MODULE_LDFLAGS) -noixemul -nostartfiles -Wl,-Map,$(WORK)/OpenAL.module.map -o $@ $^ -lm -lamiga
	$(if $(filter 1,$(SHARED)),python3 $(ROOT)/tools/baserel_check.py $(STOVE)/bin/m68k-amigaos- $@ $(WORK)/OpenAL.module.map $(if $(wildcard $(HERE)baserel.allow),--allow $(HERE)baserel.allow))
	@echo "built $@ ($$(wc -c < $@) bytes)"

# -ffixed-a4: the stub sets A4 around each call into the module (OGPU_A4).
$(STUB_DIR)/openal_stub.o: $(ROOT)/stubs/openal/openal_stub.c $(ROOT)/stubs/openal/openal_module.h $(HEADERS) $(INLINE)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_BASE) -ffixed-a4 -c $< -o $@
$(OUT)/libopenal.a: $(STUB_DIR)/openal_stub.o
	@rm -f $@; $(AR) rcs $@ $^; $(RANLIB) $@; echo "built $@"

# The tests (tests/openal).
TEST_CFLAGS = $(CFLAGS_BASE) -I$(TESTSRC)
tests: $(TEST_DIR)/nomodule.m $(TEST_DIR)/altone.m $(TEST_DIR)/altone
$(TEST_DIR)/nomodule.m: $(TESTSRC)/nomodule.c $(OUT)/libopenal.a
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CFLAGS) -o $@ $< -L$(OUT) -lopenal -lm -lamiga
$(TEST_DIR)/altone.m: $(TESTSRC)/altone.c $(OUT)/libopenal.a
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CFLAGS) -o $@ $< -L$(OUT) -lopenal -lm -lamiga
$(TEST_DIR)/altone: $(TESTSRC)/altone.c $(OUT)/libopenal_static.a
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CFLAGS) -o $@ $< -L$(OUT) -lopenal_static -lm -lamiga

# The PC tests on the 68k: the core with Mix's stand-in output (no AHI).
PCTESTS = test_al_api test_al_3d test_al_stream
PCTEST_CORE = $(addprefix $(WORK)/obj-pctest/,$(addsuffix .o,$(CORE) host_out))
tests: $(addprefix $(TEST_DIR)/,$(PCTESTS))
$(WORK)/obj-pctest/%.o: $(SRCDIR)/%.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CFLAGS) -DAL_STATIC_BUILD -c $< -o $@
$(WORK)/obj-pctest/host_out.o: $(TESTSRC)/host_out.c $(TESTSRC)/host_out.h $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CFLAGS) -c $< -o $@
$(addprefix $(TEST_DIR)/,$(PCTESTS)): $(TEST_DIR)/%: $(TESTSRC)/%.c $(TESTSRC)/check.h $(PCTEST_CORE)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CFLAGS) -o $@ $< $(PCTEST_CORE) -lm -lamiga
