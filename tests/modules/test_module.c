/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Test.module: a small module for ModuleCheck (tests/modules/module_check.c).
 * It shows what a module built on library/modules/common gets: libnix
 * started (malloc, printf to the caller's Output, a constructor run),
 * getenv through GetVar, and globals of its own in each copy. Built twice
 * (tests/modules/build.sh): Test.module shared (-fbaserel32: code once, data
 * per program) and TestEach.module, a copy for each program. Version 2 adds
 * the A4 checks: a callback into the program with the program's A4, and a
 * thread of the module's own working on the copy that started it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <exec/tasks.h>
#include <dos/dostags.h>
#include <proto/exec.h>
#include <proto/dos.h>

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
static int t_shared(void) { return OGPU_MODULE_SHARED; }
static APTR t_data(void) { return &counter; }

/* The program's fn, with the program's A4; afterwards this copy's counter,
 * read through the module's own A4 again. */
static long t_call_back(long (*fn)(long x), long x)
{
    long r = ogpu_module_callout((const void *)fn, x, 0, 0, 0);
    return r + counter;
}

/* A process of the module's own: it starts with the A4 of the copy that
 * made it, bumps that copy's counter and signals. */
struct bump_job {
    struct Task *parent;
    BYTE sig;
};

static void bump_thread(void)
{
    struct bump_job *j = FindTask(NULL)->tc_UserData;
    counter += 100;
    Signal(j->parent, 1UL << j->sig);
}
static OGPU_MODULE_THREAD_ENTRY(bump_entry, bump_thread)

static long t_thread_bump(void)
{
    struct bump_job j;
    struct Process *p;
    if ((j.sig = AllocSignal(-1)) < 0) return -1;
    j.parent = FindTask(NULL);
    Forbid();                               /* tc_UserData and A4 are set before the child runs */
    p = CreateNewProcTags(NP_Entry, (ULONG)bump_entry, NP_Name, (ULONG) "Test.module thread",
                          NP_StackSize, 8192, TAG_DONE);
    if (p) {
        p->pr_Task.tc_UserData = &j;
        ogpu_module_thread_a4(&p->pr_Task);
    }
    Permit();
    if (p) Wait(1UL << j.sig);
    FreeSignal(j.sig);
    return p ? counter : -1;
}

static void t_close(void)
{
    free(heap);
    heap = NULL;
    ogpu_module_end();
}

static struct TestModuleTable table = {
    { TEST_MODULE_VERSION, 0, 0 }, t_close, t_add, t_bump, t_ctor_ran, t_getenv, t_say, t_opengpu,
    t_shared, t_call_back, t_thread_bump, t_data,
};

struct OGPUModuleTable *ogpu_module_entry(struct OGPUModuleArgs *args)
{
    if (!ogpu_module_begin(args, TEST_MODULE_VERSION, &table.head)) return NULL;
    /* memory from libnix's pools, given back by its exit list */
    if (!(heap = malloc(100000))) {
        ogpu_module_end();
        return NULL;
    }
    memset(heap, 0x55, 100000);
    return &table.head;
}
