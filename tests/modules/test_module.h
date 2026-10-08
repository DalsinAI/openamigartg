/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * Test.module's table (tests/modules/test_module.c). Calls through it run
 * with A4 = head.a4 (include/opengpu/module.h's OGPU_A4). */
#ifndef TEST_MODULE_H
#define TEST_MODULE_H

#include <opengpu/module.h>

#define TEST_MODULE_VERSION 2

struct TestModuleTable {
    struct OGPUModuleTable head;
    void (*close)(void);
    long (*add)(long a, long b);
    long (*bump)(void);
    int (*ctor_ran)(void);
    const char *(*getenv)(const char *name);
    int (*say)(const char *what);
    struct Library *(*opengpu)(void);
    /* version 2 */
    int (*shared)(void);                        /* 1: built -fbaserel32 (code once, data per program) */
    long (*call_back)(long (*fn)(long x), long x);  /* fn(x) as a callback into the program, then counter */
    long (*thread_bump)(void);                  /* a thread of the module's own bumps the counter: counter after */
    APTR (*data)(void);                         /* where this copy's counter is */
};

#endif
