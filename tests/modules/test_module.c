/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Test.module: a small module for ModuleCheck (tests/modules/module_check.c).
 * It shows what a module built on library/modules/common gets: libnix
 * started (malloc, printf to the caller's Output, a constructor run),
 * getenv through GetVar, and globals of its own in each copy. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "module_rt.h"
#include "test_module.h"

static int ctor_ran;
static long counter;                        /* one per copy */
static char *heap;

static void __attribute__((constructor)) test_ctor(void) { ctor_ran = 1; }

static long t_add(long a, long b) { return a + b; }
static long t_bump(void) { return ++counter; }
static int t_ctor_ran(void) { return ctor_ran; }
static const char *t_getenv(const char *name) { return getenv(name); }
static int t_say(const char *what) { return printf("Test.module says: %s\n", what); }
static struct Library *t_opengpu(void) { return ogpu_module_OpenGPUBase; }

static void t_close(void)
{
    free(heap);
    heap = NULL;
    ogpu_module_end();
}

static struct TestModuleTable table = {
    { TEST_MODULE_VERSION }, t_close, t_add, t_bump, t_ctor_ran, t_getenv, t_say, t_opengpu,
};

struct OGPUModuleTable *ogpu_module_entry(struct OGPUModuleArgs *args)
{
    if (!ogpu_module_begin(args, TEST_MODULE_VERSION)) return NULL;
    /* memory from libnix's pools, given back by its exit list */
    if (!(heap = malloc(100000))) {
        ogpu_module_end();
        return NULL;
    }
    memset(heap, 0x55, 100000);
    return &table.head;
}
